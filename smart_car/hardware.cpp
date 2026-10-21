#include "hardware.h"
#include "ui_hardware.h"
#include "hal.h"
#include "uiadapter.h"
#include "commonhelper.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QPainter>
#include <QUrl>

Hardware::Hardware(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::Hardware)
{
    ui->setupUi(this);

    /*
     * 把 menubar / statusbar 收起来 —— 这是本页"最下面一行字被裁掉"的根因。
     *
     * hardware.ui 的根是 **QMainWindow**，Qt Designer 默认给了 menubar 和
     * statusbar。QMainWindow 会把窗口高度分给它们，**centralwidget 只剩中间
     * 一块**，而这一页的内容是按整屏 600（缩放后 720）摆的，于是 statusbar
     * 占掉的那 ~30px 里的内容就被裁掉了。
     * 实测（应用自己 grab() 出来的图 + 逐行量像素）：y > 690 的内容全部不可见，
     * 正好是 statusbar 的上沿；"后台保持近距离警告"下面那行提示因此只剩半截。
     *
     * 这个界面是自己画的，不需要菜单栏/状态栏，隐藏掉把空间还给 centralwidget。
     * 注意：**不要**顺手把 menubar 也一起藏了 —— 那会让所有子控件整体上移，
     * 改动面变大；只藏 statusbar 就能补回底部这段高度。
     */
    if (statusBar())
        statusBar()->hide();

    /*
     * MQ-135 的"状态 + 真实电压"副标签。
     *
     * 为什么用代码加、而不是改 .ui：这一页的布局是按 1024x600 画布手工摆的，
     * 重新跑 Designer 很容易把别的控件挪位。加一个只读 QLabel 到空气质量卡片
     * （widget_5）里，既不动现有字段，也不会影响其它页面的交互。
     */
    if (ui->widget_5) {
        m_labelMqSub = new QLabel(ui->widget_5);
        m_labelMqSub->setGeometry(QRect(104, 148, 230, 26));
        m_labelMqSub->setStyleSheet(QStringLiteral("color: rgb(150, 210, 255);"
                                                  " font: 11pt \"Agency FB\";"
                                                  " border: none; background: transparent;"));
        m_labelMqSub->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_labelMqSub->setText(QStringLiteral("--"));
        m_labelMqSub->show();
    }

    // 适配 RK3506B 5.5 寸屏（1280x720 横屏），满屏显示
    UiAdapter::fit(this);
    // 返回键放大（原本 .ui 里只有 31x28，手指点不到）
    CommonHelper::makeBackButtonBig(ui->back1, 64, 64);

    dht11 = new DHT11(this);
    sr04 = new Sr04(this);
    buzzer = new Buzzer(this);
    led = new Led(this);
    mq135 = new Mq135(this);

    // “滴滴滴”间歇报警用的定时器（250ms 翻转一次）
    m_beepTimer = new QTimer(this);
    m_beepTimer->setInterval(250);
    connect(m_beepTimer, &QTimer::timeout, this, &Hardware::onBeepTick);

    /* ---------- 后台保持近距离警告 ---------- */
    // 后台测距轮询：200ms 一次，只在退出本页且开关打开时工作
    m_bgTimer = new QTimer(this);
    m_bgTimer->setInterval(200);
    connect(m_bgTimer, &QTimer::timeout, this, &Hardware::onBgAlarmTick);

    // 状态保存防抖：开关变动后 800ms 再写文件，避免连续点击频繁写 flash
    m_bgSaveTimer = new QTimer(this);
    m_bgSaveTimer->setSingleShot(true);
    connect(m_bgSaveTimer, &QTimer::timeout, this, &Hardware::onBgAlarmSaveTimer);

    // 连接界面上的开关
    if (ui->bgAlarmCheck) {
        connect(ui->bgAlarmCheck, &QCheckBox::toggled,
                this, &Hardware::onBgAlarmToggled);
    }

    // 读取上次保存的开关状态
    loadBackgroundAlarmSetting();

    // 开机自检：短响一声，用来确认蜂鸣器接线是否真的接通。
    // 响 = 接线正确；不响 = 信号线没接到 A12。可用 SMARTCAR_BUZZER_SELFTEST=0 关掉。
    if (qEnvironmentVariable("SMARTCAR_BUZZER_SELFTEST", "1") != "0") {
        buzzer->buzzer_write(0);                       // 响
        QTimer::singleShot(200, this, [this]() {
            if (!m_manualAlarm)
                buzzer->buzzer_write(1);               // 立刻静音
            qInfo() << "蜂鸣器开机自检完成（听到一声=接线正常）";
        });
    }

    qInfo().noquote() << "Hardware 外设状态:" << Hal::instance().describe();
}

Hardware::~Hardware()
{
    delete ui;
}

// 手动报警器：开 -> 滴滴滴间歇叫；关 -> 恢复静音
void Hardware::setManualAlarm(bool on)
{
    if (m_manualAlarm == on)
        return;
    m_manualAlarm = on;
    if (on) {
        m_beepOn = true;
        buzzer->buzzer_write(0);      // 立刻响第一声
        m_beepTimer->start();
        qInfo() << "报警器：已启动（滴滴滴）";
    } else {
        m_beepTimer->stop();
        m_beepOn = false;
        buzzer->buzzer_write(1);      // 静音
        qInfo() << "报警器：已停止（恢复正常）";
    }
}

void Hardware::onBeepTick()
{
    m_beepOn = !m_beepOn;
    buzzer->buzzer_write(m_beepOn ? 0 : 1);
}

// 自动报警（超声波 <30cm）：也走"滴滴滴"间歇，而不是一直长鸣。
// 统一用 m_beepTimer 驱动，避免两套逻辑互相抢蜂鸣器。
void Hardware::setAutoAlarm(bool on)
{
    if (m_autoAlarm == on)
        return;
    m_autoAlarm = on;
    if (on) {
        m_beepOn = true;
        buzzer->buzzer_write(0);      // 立刻响第一声
        m_beepTimer->start();
        qInfo() << "距离过近：开始滴滴滴报警";
    } else {
        // 手动报警器还开着的话，别把它关掉
        if (m_manualAlarm)
            return;
        m_beepTimer->stop();
        m_beepOn = false;
        buzzer->buzzer_write(1);      // 静音
        qInfo() << "距离恢复正常：停止报警";
    }
}

// 报警距离阈值（厘米），可用环境变量覆盖，默认 30
static double alarmThresholdCm()
{
    bool ok = false;
    const double v = qEnvironmentVariable("SMARTCAR_ALARM_CM", "30").toDouble(&ok);
    return (ok && v > 0) ? v : 30.0;
}

// 后台保持近距离警告开关被切换
void Hardware::onBgAlarmToggled(bool on)
{
    m_bgAlarm = on;
    qInfo().noquote() << "后台保持近距离警告:" << (on ? "已开启" : "已关闭");

    if (m_bgSaveTimer)
        m_bgSaveTimer->start(800);      // 延迟保存，避免频繁写 flash

    if (on) {
        // 立即开始后台测距（无论当前在不在本页）
        if (m_bgTimer && !m_bgTimer->isActive())
            m_bgTimer->start();
    } else {
        if (m_bgTimer)
            m_bgTimer->stop();
        // 关掉后台模式：只有本页可见时才继续报警，否则停止
        if (!isVisible())
            setAutoAlarm(false);
    }

    emit backgroundAlarmChanged(on);
}

// 后台测距轮询：只在"本页不可见 + 后台开关打开"时工作
void Hardware::onBgAlarmTick()
{
    if (!m_bgAlarm || isVisible()) {
        // 开关关了，或本页可见（由 sr04_handler 负责），后台不再插手
        m_bgTimer->stop();
        return;
    }

    int us = 0;
    sr04->sr04_read(&us);
    const bool valid = (us > 0);
    const double cm = us / 58.0;

    // 后台模式：<30cm 就报警
    setAutoAlarm(valid && cm > 0.0 && cm < alarmThresholdCm());
}

// 延迟保存后台开关状态
void Hardware::onBgAlarmSaveTimer()
{
    if (m_bgSaveTimer)
        m_bgSaveTimer->stop();

    const QString path = QCoreApplication::applicationDirPath() + "/bg_alarm.conf";
    QFile f(path);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        f.write(m_bgAlarm ? "1\n" : "0\n");
        f.close();
        qInfo().noquote() << "后台警告设置已保存:" << (m_bgAlarm ? "开" : "关");
    }
}

// 启动时读回后台开关状态
void Hardware::loadBackgroundAlarmSetting()
{
    const QString path = QCoreApplication::applicationDirPath() + "/bg_alarm.conf";
    QFile f(path);
    bool on = false;
    if (f.open(QIODevice::ReadOnly)) {
        on = (f.readAll().trimmed() == "1");
        f.close();
    }
    m_bgAlarm = on;

    if (ui->bgAlarmCheck) {
        // 阻塞信号，避免触发一次多余的保存
        const bool blocked = ui->bgAlarmCheck->blockSignals(true);
        ui->bgAlarmCheck->setChecked(on);
        ui->bgAlarmCheck->blockSignals(blocked);
    }

    if (on && m_bgTimer && !m_bgTimer->isActive())
        m_bgTimer->start();

    qInfo().noquote() << "后台保持近距离警告（启动读取）:" << (on ? "开启" : "关闭");
    emit backgroundAlarmChanged(on);
}

void Hardware::dht11_handler()
{
    char data[4] = {};
    dht11->dht11_read(data);
    dht11Valid = Hal::instance().dht11Available() || data[0] || data[2];
    if (dht11Valid) {
        ui->Temp_label->setText(QString::number((int)data[2]));
        ui->Humi_label->setText(QString::number((int)data[0]));
    } else {
        ui->Temp_label->setText("--");
        ui->Humi_label->setText("--");
    }
    update();
}

void Hardware::sr04_handler()
{
    int us = 0;
    sr04->sr04_read(&us);

    // us -> cm（声速 340m/s：距离(cm) = 回波时间(us) / 58）
    // 注意：传感器没接/读到 0 时 us 也是 0，不能当成"距离 0cm"去报警！
    const double cm = us / 58.0;
    sr04Valid = (us > 0);
    QString strValue = sr04Valid ? QString::number(cm, 'f', 2) : QString("--");
    ui->Sr04_label->setText(strValue);

    // 判定"是否该报警"：
    //   本页可见           → 只要 <30cm 就报警（无论后台开关是否打开）
    //   本页不可见         → 只有"后台保持近距离警告"打开才报警
    //
    // 注意：Sr04 的轮询定时器是全局的（不管页面可不可见都在跑），
    // 所以这里是判断"要不要报警"的唯一入口，必须带上可见性条件，
    // 否则退出本页后仍会因为 <30cm 而一直响。
    // 阈值可用 SMARTCAR_ALARM_CM 调整（默认 30）：
    //   echo 'export SMARTCAR_ALARM_CM=300' >> /etc/smartcar.conf
    const bool autoAlarm = sr04Valid && (cm > 0.0) && (cm < alarmThresholdCm())
                           && (isVisible() || m_bgAlarm);
    setAutoAlarm(autoAlarm);
    const bool alarm = sr04Valid && (cm > 0.0) && (cm < alarmThresholdCm());

    if (alarm) {
        QPixmap pixmap(":/images/car-chose.png");
        pixmap = pixmap.scaled(QSize(91, 111), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        ui->label_3->setPixmap(pixmap);
        ui->Sr04labal->setStyleSheet("color: rgb(255, 110, 110);");   // 报警：亮红（暗底下更醒目）

        QPixmap pixmap2(":/images/buzzer-chose.png");
        pixmap2 = pixmap2.scaled(QSize(120, 120), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        ui->buzzer->setPixmap(pixmap2);
    } else {
        QPixmap pixmap(":/images/car.png");
        pixmap = pixmap.scaled(QSize(91, 111), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        ui->label_3->setPixmap(pixmap);
        ui->Sr04labal->setStyleSheet("color: rgb(120, 200, 255);");   // 正常：青蓝

        QPixmap pixmap2(":/images/buzzer.png");
        pixmap2 = pixmap2.scaled(QSize(120, 120), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        ui->buzzer->setPixmap(pixmap2);
    }

    // 蜂鸣器由 m_beepTimer 统一驱动（手动/自动报警共用），这里不再直接写电平，
    // 否则会跟定时器抢，又变成长鸣。
    update();
}

void Hardware::mq135_handler()
{
    /*
     * MQ-135 真实读数：Hal::mq135Sample() 一次抓 20 个 ADC 原始码值，给出
     *   原始值 / ADC 电压 / 换算回 AO 的电压 / 相对清洁空气基线的比值 / 状态。
     *
     * 这里**不再**算 raw/量程*100 那个"空气质量百分比"：
     *   1) 那个量没有物理意义（MQ-135 的有效信息是 Rs/R0 这种**比值**）；
     *   2) 悬空 ADC 恰好停在中点，于是没接模块也会显示 ~50% —— 这就是以前
     *      被当成"假数据"的那一条链路，现在整条删掉了。
     * 没接模块时状态就是"未接"，数字显示 "--"，绝不会再出现一个像真数据的数。
     */
    const Hal::Mq135Sample mq = Hal::instance().mq135Sample();
    const bool haveReading = mq.ok && mq.connected;

    mq135Valid = haveReading;
    if (mq.ok)
        mq135Last = mq.raw;

    QString strValue = QStringLiteral("--");
    if (haveReading)
        strValue = QString::number(mq.ratio, 'f', 2);   // 相对基线倍数，1.00 = 基线
    ui->fumes_label->setText(strValue);

    /* 状态 + 真实电压。用程序里加的副标签显示，不改 .ui、不动页面其它字段 */
    QString sub;
    if (!mq.ok) {
        sub = QStringLiteral("ADC 读取失败 --");
    } else if (!mq.connected) {
        sub = QStringLiteral("未接 --");
    } else {
        /* 设了外部分压比就显示换算回 AO 的电压（那才是模块输出电压），
         * 没设就显示 ADC 引脚上的电压 —— 两个数都明确标出来源，不含糊 */
        const double shownV = (Hal::instance().mq135Divider() > 1) ? mq.vAo : mq.vAdc;
        sub = QStringLiteral("%1  %2 V").arg(mq.statusCn).arg(shownV, 0, 'f', 3);
    }
    if (m_labelMqSub)
        m_labelMqSub->setText(sub);

    /* 卡面上的小标题**保持原样不改字**（.ui 里是"空气质量"）。
     *
     * 为什么不在标题上写"(未接)/(比值)"：这个 QLabel 在 .ui 里只有 81px 宽
     * （真机截图实测"空气质量"4 个字约占 46px 设计宽度），
     * 换成"空气质量 (未接)"要占约 160px，会直接压到旁边 x=104 起的副标签上。
     * 状态信息统一由副标签给出（未接 / 预热中 / 正常 / 偏高 / 超阈值 + 电压），
     * 这样"未接"一样看得很清楚，又不会把标题和副标签叠在一起。 */

    /* 报警：优先用"相对基线倍数"（MQ-135 的物理特性），没有基线时才退回
     * 按量程百分比的兜底阈值 */
    const bool alarm = haveReading && mq.alarm;
    if (alarm) {
        QPixmap pixmap(":/images/air-chose.png");
        pixmap = pixmap.scaled(QSize(90, 110), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        ui->label_5->setPixmap(pixmap);
        ui->Humilabel_3->setStyleSheet("color: rgb(255, 110, 110);");   // 空气差：亮红
        led->led_write(QByteArray(2, '\0').data()); // 报警时点亮板载 LED
        QPixmap pixmap1(":/images/led-chose.png");
        pixmap1 = pixmap1.scaled(QSize(120, 120), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        ui->led->setPixmap(pixmap1);
    } else {
        QPixmap pixmap(":/images/air.png");
        pixmap = pixmap.scaled(QSize(90, 110), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        ui->label_5->setPixmap(pixmap);
        ui->Humilabel_3->setStyleSheet("color: rgb(120, 230, 130);");   // 空气好：柔和绿
        char off[2] = {0, 1};
        led->led_write(off);
        QPixmap pixmap1(":/images/led.png");
        pixmap1 = pixmap1.scaled(QSize(120, 120), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        ui->led->setPixmap(pixmap1);
    }

    /* 日志：
     *   - 状态变化 -> 立刻打
     *   - 状态没变 -> 每 2 次（约 4 秒）打一行，方便"对着传感器呼气"这种
     *     短时间对比实验取证（以前限流到 30 秒一行，太稀，做实验看不到变化） */
    static QString s_lastSub;
    static int s_sameCount = 0;
    if (sub != s_lastSub) {
        s_lastSub = sub;
        s_sameCount = 0;
    }
    if (++s_sameCount <= 2 || (s_sameCount % 2) == 0) {
        qInfo().noquote()
            << QString("MQ135: raw=%1  电压=%2mV(%3V)  AO电压=%4V  基线=%5  比值=%6  状态=%7  %8")
                   .arg(mq.raw)
                   .arg((int)(mq.vAdc * 1000.0 + 0.5))
                   .arg(mq.vAdc, 0, 'f', 4)
                   .arg(mq.vAo, 0, 'f', 4)
                   .arg(mq.baselineRaw, 0, 'f', 1)
                   .arg(mq.ratio, 0, 'f', 3)
                   .arg(mq.statusCn)
                   .arg(mq.detail);
    }
    update();
}

void Hardware::hardware_working()
{
    connect(dht11, &DHT11::readyto_read_dht11data, this, &Hardware::dht11_handler);
    connect(sr04, &Sr04::readyto_read_sr04data, this, &Hardware::sr04_handler);
    connect(mq135, &Mq135::readyto_read_mq135data, this, &Hardware::mq135_handler);
}

// 外设未接时在右上角给出提示，避免误以为程序坏了
void Hardware::paintEvent(QPaintEvent *event)
{
    QMainWindow::paintEvent(event);
    QStringList missing;
    if (!dht11Valid)
        missing << "DHT11";
    if (!sr04Valid)
        missing << "SR04";
    if (!mq135Valid)
        missing << "MQ135";

    QPainter p(this);
    QFont f = p.font();
    f.setPixelSize(qMax(12, height() / 40));
    p.setFont(f);
    p.setPen(QColor(255, 210, 60));
    if (!missing.isEmpty()) {
        p.drawText(QRect(0, 4, width() - 8, height() / 20), Qt::AlignRight | Qt::AlignTop,
                   QString("未检测到: %1").arg(missing.join('/')));
    }
    p.setPen(QColor(140, 220, 255));
    p.drawText(QRect(8, 4, width() / 2, height() / 20), Qt::AlignLeft | Qt::AlignTop,
               Hal::instance().describe());
}

// 页面显示：停掉后台定时器（改由 sr04_handler 负责，它还要刷新界面）
void Hardware::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    if (m_bgTimer)
        m_bgTimer->stop();
    qInfo() << "环境检测页：显示（前台测距接管，<阈值即报警）";
}

// 页面隐藏：如果后台开关打开，则启动后台测距继续报警
void Hardware::hideEvent(QHideEvent *event)
{
    QMainWindow::hideEvent(event);

    if (m_bgAlarm) {
        if (m_bgTimer && !m_bgTimer->isActive())
            m_bgTimer->start();
        qInfo() << "环境检测页：隐藏（后台警告开启，继续测距）";
    } else {
        // 后台开关关闭：退出页面即停止报警
        setAutoAlarm(false);
        qInfo() << "环境检测页：隐藏（后台警告关闭，停止报警）";
    }
}

void Hardware::on_back1_clicked()
{
    emit back1();
}
