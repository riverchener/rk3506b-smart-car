#include "otapage.h"
#include "ota_version.h"

#include <QApplication>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QTextCursor>
#include <QTextEdit>
#include <QTimer>
#include <QVBoxLayout>
#include <QFileInfo>

namespace {
/* 和 incarcontrol / sysmon 一套配色，保证进这个页面不像"另一个 App" */
const char *kC_CYAN  = "#00E5FF";
const char *kC_GREEN = "#00E676";
const char *kC_AMBER = "#FFC400";

/* 板端工具的两个候选位置：
 *   ① 正式部署位置（安装脚本会把它放到 /usr/bin）
 *   ② 源码/调试位置（adb push 过来的，方便改完立刻试）
 * 两边都不在时页面会明确报出来，而不是静默失败。 */
const char *kToolInstalled = "/usr/bin/smartcar-ota";
const char *kToolLocal     = "/userdata/smart_car/smartcar-ota";
const char *kToolLog       = "/var/log/smartcar-ota.log";

QString cardQss()
{
    return QStringLiteral(
        "QFrame { background: rgba(14,22,40,235); border: 1px solid #243656;"
        "         border-radius: 10px; }");
}

QString btnQss(const QString &accent, int px, bool primary)
{
    return QStringLiteral(
               "QPushButton {"
               "  background: %1; color: #EEF4FF; border: 1px solid %1;"
               "  border-radius: 8px; font-size: %2px; font-weight: bold; }"
               "QPushButton:pressed { background: #0C1220; }"
               "QPushButton:disabled { color: #4E5A70; border-color: #1A253A;"
               "  background: #0C1220; }")
        .arg(primary ? accent : QStringLiteral("#111A2E"))
        .arg(px);
}
} // namespace

OtaPage::OtaPage(QWidget *parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("系统升级"));
    /* 本页不用 UiAdapter::fit()：那一套是按 1024x600 设计画布 ×1.2 缩放，
     * 而这里直接用 1280x720 的真实坐标（和 MainWindow::buildDesktop 的做法一致），
     * 少一层缩放就少一类"显示不全"的坑。 */
    setFixedSize(1280, 720);
    setAutoFillBackground(true);
    setStyleSheet(QStringLiteral(
        "OtaPage { background: #060A14; }"
        "QLabel { color: #C7D3E8; }"
        "QTextEdit { background: #070C17; color: #9FE8C0;"
        "  border: 1px solid #1C2A44; border-radius: 8px;"
        "  font-family: monospace; font-size: 15px; }"));

    buildUi();
    layoutUi();

    m_tick = new QTimer(this);
    m_tick->setInterval(1000);
    connect(m_tick, &QTimer::timeout, this, &OtaPage::onTick);

    refreshStatus();
}

void OtaPage::buildUi()
{
    /* ---------------- 顶栏 ---------------- */
    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("otaBar"));
    bar->setStyleSheet(QStringLiteral(
        "#otaBar { background: rgba(10,16,30,240);"
        "         border-bottom: 1px solid #1C2A44; }"));

    m_back = new QPushButton(QStringLiteral("← 返回"), bar);
    m_back->setStyleSheet(btnQss(kC_CYAN, 18, false));
    m_back->setFocusPolicy(Qt::NoFocus);
    connect(m_back, &QPushButton::clicked, this, &OtaPage::backout);

    auto *accent = new QFrame(bar);
    accent->setFixedWidth(4);
    accent->setStyleSheet(QStringLiteral(
        "background: #00E5FF; border: none; border-radius: 2px;"));

    m_title = new QLabel(QStringLiteral("系统升级"), bar);
    m_title->setStyleSheet(QStringLiteral(
        "color:#EEF4FF; font-size:26px; font-weight:bold;"));

    m_sub = new QLabel(QStringLiteral("SYSTEM OTA UPDATE"), bar);
    m_sub->setStyleSheet(QStringLiteral("color:#617594; font-size:15px;"));

    m_chip = new QLabel(QStringLiteral("待命"), bar);
    m_chip->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_chip->setStyleSheet(QStringLiteral("color:#94A7C6; font-size:17px;"));

    auto *barLay = new QHBoxLayout(bar);
    barLay->setContentsMargins(18, 6, 22, 6);
    barLay->setSpacing(12);
    barLay->addWidget(m_back);
    barLay->addWidget(accent);
    barLay->addWidget(m_title);
    barLay->addWidget(m_sub);
    barLay->addStretch(1);
    barLay->addWidget(m_chip);

    /* ---------------- 左：版本信息 ---------------- */
    auto *info = new QFrame(this);
    info->setStyleSheet(cardQss());
    auto *infoLay = new QVBoxLayout(info);
    infoLay->setContentsMargins(20, 14, 20, 14);
    infoLay->setSpacing(10);

    auto *infoHead = new QLabel(QStringLiteral("● 版本信息"), info);
    infoHead->setStyleSheet(QStringLiteral(
        "color:#00E5FF; font-size:19px; font-weight:bold;"));
    infoLay->addWidget(infoHead);

    auto addRow = [&](const QString &k, QLabel *&v) {
        auto *row = new QWidget(info);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(10);
        auto *kl = new QLabel(k, row);
        kl->setFixedWidth(150);
        kl->setStyleSheet(QStringLiteral("color:#7E8FAE; font-size:17px;"));
        v = new QLabel(QStringLiteral("—"), row);
        v->setStyleSheet(QStringLiteral(
            "color:#EEF4FF; font-size:18px; font-weight:bold;"));
        v->setTextInteractionFlags(Qt::TextSelectableByMouse);
        h->addWidget(kl);
        h->addWidget(v, 1);
        infoLay->addWidget(row);
    };
    addRow(QStringLiteral("磁盘上的版本"), m_lDisk);
    addRow(QStringLiteral("运行的版本"),   m_lRun);
    addRow(QStringLiteral("服务端版本"),   m_lSrv);

    m_lNotes = new QLabel(QStringLiteral("—"), info);
    m_lNotes->setWordWrap(true);
    m_lNotes->setStyleSheet(QStringLiteral(
        "color:#9FB0CC; font-size:16px; background: rgba(255,255,255,12);"
        "border-radius: 6px; padding: 8px;"));
    infoLay->addWidget(new QLabel(QStringLiteral("服务端说明"), info));
    infoLay->addWidget(m_lNotes, 1);

    /* ---------------- 右：操作 + 日志 ---------------- */
    auto *ops = new QFrame(this);
    ops->setStyleSheet(cardQss());
    auto *opsLay = new QVBoxLayout(ops);
    opsLay->setContentsMargins(20, 14, 20, 14);
    opsLay->setSpacing(10);

    auto *opsHead = new QLabel(QStringLiteral("● 操作"), ops);
    opsHead->setStyleSheet(QStringLiteral(
        "color:#00E5FF; font-size:19px; font-weight:bold;"));
    opsLay->addWidget(opsHead);

    auto *btnRow = new QWidget(ops);
    auto *bh = new QHBoxLayout(btnRow);
    bh->setContentsMargins(0, 0, 0, 0);
    bh->setSpacing(10);

    m_check = new QPushButton(QStringLiteral("检查更新"), btnRow);
    m_check->setStyleSheet(btnQss(kC_CYAN, 18, false));
    m_check->setMinimumHeight(46);
    m_check->setFocusPolicy(Qt::NoFocus);
    connect(m_check, &QPushButton::clicked, this, &OtaPage::onCheck);

    m_update = new QPushButton(QStringLiteral("立即升级"), btnRow);
    m_update->setStyleSheet(btnQss(kC_GREEN, 18, true));
    m_update->setMinimumHeight(46);
    m_update->setFocusPolicy(Qt::NoFocus);
    connect(m_update, &QPushButton::clicked, this, &OtaPage::onUpdate);

    m_rollback = new QPushButton(QStringLiteral("回滚"), btnRow);
    m_rollback->setStyleSheet(btnQss(kC_AMBER, 18, false));
    m_rollback->setMinimumHeight(46);
    m_rollback->setFocusPolicy(Qt::NoFocus);
    connect(m_rollback, &QPushButton::clicked, this, &OtaPage::onRollback);

    bh->addWidget(m_check, 1);
    bh->addWidget(m_update, 1);
    bh->addWidget(m_rollback, 1);
    opsLay->addWidget(btnRow);

    auto *hint = new QLabel(
        QStringLiteral("升级会重启车机应用，本页面会随之关闭；"
                       "再次打开此页可看到结果。日志：%1")
            .arg(QString::fromLatin1(kToolLog)), ops);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#7E8FAE; font-size:15px;"));
    opsLay->addWidget(hint);

    m_log = new QTextEdit(ops);
    m_log->setReadOnly(true);
    m_log->setLineWrapMode(QTextEdit::NoWrap);
    opsLay->addWidget(m_log, 1);

    m_bar = bar;
    m_info = info;
    m_ops = ops;
}

void OtaPage::layoutUi()
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_bar);

    auto *body = new QWidget(this);
    auto *bodyLay = new QHBoxLayout(body);
    bodyLay->setContentsMargins(16, 12, 16, 16);
    bodyLay->setSpacing(14);
    m_info->setFixedWidth(470);
    bodyLay->addWidget(m_info);
    bodyLay->addWidget(m_ops, 1);
    root->addWidget(body, 1);
}

/* ------------------------------------------------------------------ 工具函数 */
QString OtaPage::runCapture(const QStringList &programAndArgs)
{
    if (programAndArgs.isEmpty())
        return QString();
    QProcess p;
    p.start(programAndArgs.at(0), programAndArgs.mid(1));
    if (!p.waitForStarted(3000))
        return QString();
    if (!p.waitForFinished(8000)) {
        p.kill();
        p.waitForFinished(1000);
    }
    return QString::fromUtf8(p.readAllStandardOutput())
         + QString::fromUtf8(p.readAllStandardError());
}

/* 板子上没有 strings，用 grep -a 把 ELF 当文本扫（busybox grep 支持 -a）。
 *
 * ⚠ 这一步是**同步阻塞**的：2.4MB 的 ELF 在板子上 grep 一次约 0.86 秒（实测）。
 *   所以：① 整个状态采集合并成**一次** sh 调用（原来 5 次 QProcess = 4 秒多）；
 *        ② 只在构造和 showEvent 里做，不多做；
 *        ③ 外面套超时，waitForFinished 期间 Qt 不处理事件，绝不能让界面卡死。
 */
QString OtaPage::fileVersionTag(const QString &path)
{
    if (!QFileInfo::exists(path))
        return QString();
    const QString out = runCapture(QStringList()
        << QStringLiteral("grep")
        << QStringLiteral("-ao")
        << QString::fromLatin1(SMARTCAR_OTA_VERSION_TAG) + QStringLiteral("[0-9A-Za-z._-]*")
        << path);
    const QString first = out.section('\n', 0, 0).trimmed();
    if (first.isEmpty())
        return QString();
    return first.mid(QString::fromLatin1(SMARTCAR_OTA_VERSION_TAG).size());
}

/* 版本采集用的板端小脚本。
 *
 * ⚠ 为什么不把这段逻辑写成 `sh -c "...."` 一行内联：
 *   里面要嵌 `grep -ao 'SMARTCAR_OTA_VERSION=...'` 这种带单引号的参数，
 *   而 QProcess 是**直接 execvp、不经过 shell**，于是 argv 里带上引号后，
 *   内层 sh 会把引号当字面量 —— 实测结果就是 APP= / RUN= 全空
 *   （日志里 "ota页: 磁盘=(无) 运行=(无)" 但 exe= 是对的，就是这个问题）。
 *   写成文件只有一处转义风险，且**打开就能调**，省掉一整轮猜谜。
 */
static const char *kVerProbeScript =
    "#!/bin/sh\n"
    "# 由 OtaPage 生成，把版本事实以 KV 形式打出来（每行一个）\n"
    "APP=/userdata/smart_car/Smart_in-car_entertainment\n"
    "P=$(pidof Smart_in-car_entertainment 2>/dev/null | awk '{print $1}')\n"
    "E=\n"
    "[ -n \"$P\" ] && E=$(readlink /proc/$P/exe 2>/dev/null)\n"
    "echo \"APP=$(grep -ao 'SMARTCAR_OTA_VERSION=[0-9A-Za-z._-]*' \"$APP\" 2>/dev/null | head -1)\"\n"
    "echo \"PID=$P\"\n"
    "echo \"EXE=$E\"\n"
    "if [ -n \"$E\" ]; then\n"
    "  echo \"RUN=$(grep -ao 'SMARTCAR_OTA_VERSION=[0-9A-Za-z._-]*' \"$E\" 2>/dev/null | head -1)\"\n"
    "else\n"
    "  echo \"RUN=\"\n"
    "fi\n";

/* 一次 sh 调用把所有版本事实都取回来（板子上每次 QProcess 启动都要几百毫秒，
 * 拆成 5 次会把界面卡住好几秒）。返回是否有任何版本信息。 */
bool OtaPage::collectVersions(QString *disk, QString *run)
{
    const QString probe = QStringLiteral("/tmp/smartcar_ota_verprobe.sh");
    QByteArray rawOut;
    {
        QFile f(probe);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            f.write(kVerProbeScript);
            f.close();
        }
    }
    /* 直接 exec /bin/sh <脚本>。QProcess 是 execvp 不经 shell，
     * 参数里没有引号，不会被二次解释 —— 这一段是踩过坑之后的写法。 */
    QProcess p;
    p.start(QStringLiteral("/bin/sh"), QStringList() << probe);
    QString err;
    if (!p.waitForStarted(3000)) {
        err = QStringLiteral("无法启动 /bin/sh: ") + p.errorString();
    } else {
        if (!p.waitForFinished(10000)) {
            p.kill();
            p.waitForFinished(1000);
            err = QStringLiteral("探测脚本超时");
        } else {
            rawOut = p.readAllStandardOutput();
        }
    }
    const QString out = QString::fromUtf8(rawOut);
    qInfo().noquote() << QStringLiteral("ota页: 探测脚本输出=%1")
                             .arg(out.isEmpty() ? QStringLiteral("(空)") : out.trimmed());
    if (!err.isEmpty())
        qWarning().noquote() << QStringLiteral("ota页: %1").arg(err);

    QString appS, runS, exe;
    for (const QString &ln : out.split('\n')) {
        const QString t = ln.trimmed();
        if (t.startsWith(QStringLiteral("APP=")))
            appS = t.mid(4).trimmed();
        else if (t.startsWith(QStringLiteral("RUN=")))
            runS = t.mid(4).trimmed();
        else if (t.startsWith(QStringLiteral("EXE=")))
            exe = t.mid(4).trimmed();
    }
    /* ⚠ 坑（本次踩了两个小时）：
     *   SMARTCAR_OTA_VERSION_TAG 是宏拼接的结果 ——
     *       #define SMARTCAR_OTA_VERSION_TAG "SMARTCAR_OTA_VERSION=" SMARTCAR_OTA_VERSION_STR
     *   也就是说这个 tag **本身就带着版本号**（"SMARTCAR_OTA_VERSION=v1.3.4"，27 字节）。
     *   拿它去 indexOf/mid 剥离，等于"从 27 字节的串里跳过 27 字节"，结果永远是空串。
     *   正确做法：前缀另用字面量 kTagPrefix，即 "SMARTCAR_OTA_VERSION="（21 字节）。
     *   日志里 `t.len=27` 就是决定性证据 —— 一眼看穿。 */
    static const QString kTagPrefix = QStringLiteral("SMARTCAR_OTA_VERSION=");
    auto strip = [](const QString &s, const QString &t) {
        const int i = s.indexOf(t, 0, Qt::CaseSensitive);
        return i < 0 ? QString() : s.mid(i + t.size()).trimmed();
    };
    if (disk)
        *disk = strip(appS, kTagPrefix);
    if (run)
        *run = strip(runS, kTagPrefix);
    /* 打日志：页面上的版本是从哪来的必须可追溯，不然出了问题只能猜。
     * 也方便用 grep 'ota页' /var/log/smartcar.log 核对界面显示值。 */
    const QString diskS = strip(appS, kTagPrefix);
    const QString runS2 = strip(runS, kTagPrefix);
    qInfo().noquote() << QStringLiteral("ota页: 磁盘=%1 运行=%2 exe=%3")
                             .arg(diskS.isEmpty() ? QStringLiteral("(无)") : diskS,
                                  runS2.isEmpty() ? QStringLiteral("(无)") : runS2,
                                  exe.isEmpty() ? QStringLiteral("(无)") : exe);
    return !diskS.isEmpty() || !runS2.isEmpty();
}

void OtaPage::appendLog(const QString &s)
{
    if (s.isEmpty())
        return;
    m_log->moveCursor(QTextCursor::End);
    m_log->insertPlainText(s);
    m_log->moveCursor(QTextCursor::End);
    /* 只保留最近若干行，避免长时间运行把内存吃满（板子只有 ~500MB 内存） */
    if (m_log->document()->blockCount() > 400) {
        QTextCursor c(m_log->document());
        c.movePosition(QTextCursor::Start);
        c.movePosition(QTextCursor::Down, QTextCursor::KeepAnchor, 150);
        c.removeSelectedText();
    }
}

void OtaPage::loadToolLogTail()
{
    QFile f(QString::fromLatin1(kToolLog));
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    const QString all = QString::fromUtf8(f.readAll());
    f.close();
    const QStringList lines = all.split('\n', Qt::SkipEmptyParts);
    appendLog(QStringLiteral("---- %1 最近 %2 行 ----\n")
                  .arg(QString::fromLatin1(kToolLog))
                  .arg(qMin(40, lines.size())));
    const int from = qMax(0, lines.size() - 40);
    for (int i = from; i < lines.size(); ++i)
        appendLog(lines.at(i) + QStringLiteral("\n"));
    appendLog(QStringLiteral("----\n"));
}

void OtaPage::refreshStatus()
{
    QString disk, run;
    collectVersions(&disk, &run);

    m_lDisk->setText(disk.isEmpty() ? QStringLiteral("(无版本串，老构建)")
                                    : disk);
    m_lRun->setText(run.isEmpty() ? QStringLiteral("(进程未运行)")
                                  : run);

    /* 找板端工具 */
    m_tool.clear();
    if (QFileInfo::exists(QString::fromLatin1(kToolInstalled)))
        m_tool = QString::fromLatin1(kToolInstalled);
    else if (QFileInfo::exists(QString::fromLatin1(kToolLocal)))
        m_tool = QString::fromLatin1(kToolLocal);

    if (m_tool.isEmpty()) {
        m_chip->setText(QStringLiteral("● 未找到 smartcar-ota"));
        m_chip->setStyleSheet(QStringLiteral("color:#FF1744; font-size:17px;"));
        m_check->setEnabled(false);
        m_update->setEnabled(false);
        m_rollback->setEnabled(false);
        if (m_log->document()->isEmpty()) {
            appendLog(QStringLiteral("!! 没有找到板端 OTA 工具。\n"));
            appendLog(QStringLiteral("   期望位置之一：%1 或 %2\n")
                          .arg(QString::fromLatin1(kToolInstalled),
                               QString::fromLatin1(kToolLocal)));
        }
        return;
    }

    m_chip->setText(QStringLiteral("● 工具就绪"));
    m_chip->setStyleSheet(QStringLiteral("color:#00E676; font-size:17px;"));
    if (!m_busy) {
        m_check->setEnabled(true);
        m_update->setEnabled(true);
        m_rollback->setEnabled(true);
    }
    if (m_log->document()->isEmpty())
        appendLog(QStringLiteral("板端工具: %1\n"
                                 "本应用版本: %2\n")
                      .arg(m_tool, QString::fromLatin1(SMARTCAR_OTA_VERSION_TAG)));
}

void OtaPage::setBusy(bool busy, const QString &what)
{
    m_busy = busy;
    m_check->setEnabled(!busy);
    m_update->setEnabled(!busy);
    m_rollback->setEnabled(!busy);
    m_back->setEnabled(!busy);
    if (busy) {
        m_elapsed = 0;
        m_chip->setText(QStringLiteral("● %1 0s").arg(what));
        m_chip->setStyleSheet(QStringLiteral("color:#FFC400; font-size:17px;"));
        m_tick->start();
    } else {
        m_tick->stop();
    }
}

void OtaPage::onTick()
{
    ++m_elapsed;
    m_chip->setText(m_chip->text().replace(QRegularExpression(QStringLiteral("\\d+s$")),
                                           QStringLiteral("%1s").arg(m_elapsed)));
}

/* ------------------------------------------------------------------ 动作 */
void OtaPage::onCheck()
{
    if (m_busy || m_tool.isEmpty())
        return;
    m_queue.clear();
    m_queue << QStringLiteral("check");
    m_lastResult.clear();
    appendLog(QStringLiteral("\n$ smartcar-ota check\n"));
    m_lSrv->setText(QStringLiteral("查询中…"));
    m_lNotes->setText(QStringLiteral("—"));
    runNext();
}

void OtaPage::onUpdate()
{
    if (m_busy || m_tool.isEmpty())
        return;
    /* 先 check 再 update：让日志里留下"从哪个版本升到哪个版本"的完整证据链。
     * update 用 --force：用户点了"立即升级"就是要装，哪怕版本号相同
     * （重装可以修复被破坏的安装）。 */
    m_queue.clear();
    m_queue << QStringLiteral("check") << QStringLiteral("update --force");
    m_lastResult.clear();
    appendLog(QStringLiteral("\n$ smartcar-ota check && smartcar-ota update --force\n"));
    appendLog(QStringLiteral("注意：安装成功后 S98smartcar restart 会杀掉本应用，"
                             "界面会消失，属正常现象。\n"));
    runNext();
}

void OtaPage::onRollback()
{
    if (m_busy || m_tool.isEmpty())
        return;
    m_queue.clear();
    m_queue << QStringLiteral("rollback");
    m_lastResult.clear();
    appendLog(QStringLiteral("\n$ smartcar-ota rollback\n"));
    runNext();
}

void OtaPage::runNext()
{
    if (m_queue.isEmpty()) {
        setBusy(false, QString());
        /* 升级成功的情况下 S98smartcar restart 正在杀本进程：
         * 这时再 refreshStatus() 读到的还是"旧的自己"（/proc/self/exe 还没换），
         * 会把界面刷成误导性的结论。所以升级后只提示，不去读版本。 */
        if (m_lastResult.contains(QStringLiteral("OTA_UPDATE_OK"))) {
            m_chip->setText(QStringLiteral("● 升级成功，等待应用重启"));
            m_chip->setStyleSheet(QStringLiteral("color:#00E676; font-size:17px;"));
        } else {
            loadToolLogTail();
            refreshStatus();
        }
        return;
    }
    const QString args = m_queue.takeFirst();
    const QStringList parts = args.split(' ', Qt::SkipEmptyParts);

    setBusy(true, QStringLiteral("执行 %1").arg(args));
    if (!m_proc) {
        m_proc = new QProcess(this);
        m_proc->setProcessChannelMode(QProcess::MergedChannels);
        connect(m_proc, &QProcess::readyReadStandardOutput,
                this, &OtaPage::onProcStdout);
        connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, &OtaPage::onProcFinished);
    }
    m_proc->start(m_tool, parts);
    if (!m_proc->waitForStarted(4000))
        appendLog(QStringLiteral("!! 无法启动 %1\n").arg(m_tool));
}

void OtaPage::onProcStdout()
{
    if (!m_proc)
        return;
    const QString s = QString::fromUtf8(m_proc->readAllStandardOutput());
    appendLog(s);
    for (const QString &ln : s.split('\n', Qt::SkipEmptyParts)) {
        const QString t = ln.trimmed();
        if (t.startsWith(QStringLiteral("OTA_")))
            m_lastResult = t;
        /* 顺手从 check 输出里把服务端版本/说明显出来 */
        const QRegularExpression reVer(
            QStringLiteral("(?:服务端版本|最新版本|server version)\\s*[:：]\\s*(\\S+)"));
        const QRegularExpressionMatch mv = reVer.match(t);
        if (mv.hasMatch())
            m_lSrv->setText(mv.captured(1));

        const QRegularExpression reNote(
            QStringLiteral("(?:说明|notes?)\\s*[:：]\\s*(.+)$"));
        const QRegularExpressionMatch mn = reNote.match(t);
        if (mn.hasMatch())
            m_lNotes->setText(mn.captured(1).trimmed());
    }
}

void OtaPage::onProcFinished(int code, int status)
{
    Q_UNUSED(status);
    appendLog(QStringLiteral("[exit %1]\n").arg(code));

    /* 结果判定：优先看工具自己吐的 OTA_* 行，其次看退出码。
     * 这样界面上的结论和命令行上看到的是同一套判据，不会互相打架。 */
    if (m_lastResult.contains(QStringLiteral("OTA_UPDATE_AVAILABLE"))) {
        m_chip->setText(QStringLiteral("● 发现新版本"));
        m_chip->setStyleSheet(QStringLiteral("color:#00E676; font-size:17px;"));
    } else if (m_lastResult.contains(QStringLiteral("OTA_UP_TO_DATE"))) {
        m_chip->setText(QStringLiteral("● 已是最新"));
        m_chip->setStyleSheet(QStringLiteral("color:#94A7C6; font-size:17px;"));
    } else if (m_lastResult.contains(QStringLiteral("OTA_UPDATE_OK"))) {
        m_chip->setText(QStringLiteral("● 升级成功"));
        m_chip->setStyleSheet(QStringLiteral("color:#00E676; font-size:17px;"));
    } else if (m_lastResult.contains(QStringLiteral("ROLLED_BACK"))) {
        m_chip->setText(QStringLiteral("● 升级失败已回滚"));
        m_chip->setStyleSheet(QStringLiteral("color:#FFC400; font-size:17px;"));
    } else if (m_lastResult.contains(QStringLiteral("FAILED"))
               || m_lastResult.contains(QStringLiteral("ROLLBACK_FAILED"))) {
        m_chip->setText(QStringLiteral("● 失败"));
        m_chip->setStyleSheet(QStringLiteral("color:#FF1744; font-size:17px;"));
    }
    runNext();
}

void OtaPage::showEvent(QShowEvent *e)
{
    QWidget::showEvent(e);
    refreshStatus();
}
