#include "Audio.h"
#include "ui_Audio.h"
#include "uiadapter.h"
#include "commonhelper.h"
#include <QPainterPath>
#include <QElapsedTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QSignalBlocker>
#include <QRadialGradient>

/* ============================================================================
 * 蓝牙音源（AVRCP CT）实现
 *
 * 目标：音乐页在手机放歌时显示手机的内容 —— 歌名/歌手、进度、歌词高亮，
 * 并且页面上的按键控制的是**手机**（而不是本地播放器）。
 *
 * 数据来自 BtMedia（每秒轮询 `bt-ctl bt-media status`，底层是 BlueZ 的
 * org.bluez.MediaControl1 / MediaPlayer1，见 btmedia.h 的说明）。
 *
 * 切换策略：不新增按钮，**自动跟随** —— 手机在放/暂停就切到蓝牙音源，
 * 手机断开就回到本地音源。车机上这比让人去找按钮更符合直觉。
 * ========================================================================== */

/* ============================================================================
 * 快进 / 快退按钮的"脸"（圆底 + 两个实心白三角）
 *
 * 为什么要整张画出来，而不是"样式表设个圆角 + 图标设两个三角"：
 *   1) Qt 样式表的 `border-radius: 50%` **无效**（只认像素长度），结果就是
 *      一个黑色方块 —— 上一版截图就是这样；
 *   2) ⏮ ▶ ⏭ ♡ 这几个键用的是美术给的圆形 PNG，圆底比按钮本身小一圈、
 *      底色是 rgb(48,47,53)（用取色器量的）。为了"风格与播放键一致"，
 *      这里直接按同样的观感画一张图当图标，和那些资源图是一个路子。
 * ========================================================================== */
static QPixmap smartcarJumpFace(int sz, bool forward)
{
    QPixmap pm(sz, sz);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    /* 圆底：四周留一点边（美术那张 ⏮/▶ 的圆约占按钮 85%，这里对齐观感） */
    const qreal inset = sz * 0.075;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(48, 47, 53));
    p.drawEllipse(QRectF(inset, inset, sz - inset * 2, sz - inset * 2));

    /* 两个实心白三角（快退就左右镜像） */
    p.setBrush(QColor(255, 255, 255));
    const qreal h  = sz * 0.30;                  // 三角高
    const qreal gp = sz * 0.06;                  // 两个三角之间的缝
    const qreal tw = (sz * 0.34 - gp) / 2.0;     // 单个三角底边
    const qreal y0 = (sz - h) / 2.0;
    const qreal x0 = (sz - (tw * 2 + gp)) / 2.0;
    for (int i = 0; i < 2; ++i) {
        const qreal x = x0 + i * (tw + gp);
        QPolygonF tri;
        if (forward)
            tri << QPointF(x, y0) << QPointF(x + tw, y0 + h / 2) << QPointF(x, y0 + h);
        else
            tri << QPointF(x + tw, y0) << QPointF(x, y0 + h / 2) << QPointF(x + tw, y0 + h);
        p.drawPolygon(tri);
    }
    p.end();
    return pm;
}

void Audio::layoutBtControlRow(bool entering)
{
    if (!ui || !ui->btn_play || !m_ffBtn || !m_rewBtn)
        return;

    if (!entering) {
        /* 恢复进蓝牙前的几何（本地页面保持原样） */
        if (!m_btOrigGeom.isEmpty()) {
            if (m_btOrigGeom.size() >= 4) {
                ui->btn_loop->setGeometry(m_btOrigGeom[0]);
                ui->btn_pre->setGeometry(m_btOrigGeom[1]);
                ui->btn_play->setGeometry(m_btOrigGeom[2]);
                ui->btn_next->setGeometry(m_btOrigGeom[3]);
            }
            if (m_btOrigGeom.size() >= 5 && ui->pushButton_2)
                ui->pushButton_2->setGeometry(m_btOrigGeom[4]);
            if (m_btOrigGeom.size() >= 6 && ui->horizontalSlider)
                ui->horizontalSlider->setGeometry(m_btOrigGeom[5]);
            if (m_btOrigGeom.size() >= 7 && ui->label_2)
                ui->label_2->setGeometry(m_btOrigGeom[6]);
            if (m_btOrigGeom.size() >= 8 && ui->label_3)
                ui->label_3->setGeometry(m_btOrigGeom[7]);
            m_btOrigGeom.clear();
            qInfo().noquote() << "bt-media: 控制条已还原为本地排版";
        }
        return;
    }

    /* 一整排 7 个：⟲ ⏮ ⏪ ▶ ⏩ ⏭ ♡ —— 同尺寸、同间隔；
     * 总宽超出可用宽度就**整排一起缩小**（用户要求：放不下就整排一起缩）。 */
    const int bw = ui->btn_play->width();
    const int bh = ui->btn_play->height();
    const int gap = 14;
    const int n = 7;
    const int cy0 = ui->btn_play->y() + bh / 2;                 // 整排共用同一条中线
    const int left = ui->btn_loop->x();
    const int right = ui->pushButton_2
                        ? ui->pushButton_2->x() + ui->pushButton_2->width()
                        : ui->btn_next->x() + ui->btn_next->width();
    const int avail = right - left;
    int w = bw, g = gap;
    if (n * bw + (n - 1) * gap > avail) {
        w = qMax(36, (avail - (n - 1) * gap) / n);
        g = qMax(6, (avail - n * w) / (n - 1));
    }
    const int h  = w;
    const int cy = cy0 - h / 2;

    /* 记下原始几何（只记一次），切回本地时恢复 */
    if (m_btOrigGeom.isEmpty()) {
        m_btOrigGeom << ui->btn_loop->geometry() << ui->btn_pre->geometry()
                     << ui->btn_play->geometry() << ui->btn_next->geometry()
                     << (ui->pushButton_2 ? ui->pushButton_2->geometry() : QRect())
                     << (ui->horizontalSlider ? ui->horizontalSlider->geometry() : QRect())
                     << (ui->label_2 ? ui->label_2->geometry() : QRect())
                     << (ui->label_3 ? ui->label_3->geometry() : QRect());
    }

    /* 图标按最终按钮尺寸重画（整张脸就是图标），保证与圆钮比例一致 */
    const int isz = w;
    m_rewBtn->setIcon(QIcon(smartcarJumpFace(isz, false)));
    m_rewBtn->setIconSize(QSize(isz, isz));
    m_ffBtn->setIcon(QIcon(smartcarJumpFace(isz, true)));
    m_ffBtn->setIconSize(QSize(isz, isz));

    int x = left;
    const QVector<QWidget *> row = {ui->btn_loop, ui->btn_pre, m_rewBtn, ui->btn_play,
                                    m_ffBtn, ui->btn_next, ui->pushButton_2};
    for (QWidget *item : row) {
        if (!item)
            continue;
        item->setGeometry(x, cy, w, h);
        item->raise();
        x += w + g;
    }
    /* 进度条与两侧时间标签对齐到这一排的两端。
     * ⚠️ 不这么做的话，滑块会盖住左边的时间、右边的时间又被滑块盖住 ——
     * 上一版截图里 "01:23" 被压成 "01:2" 就是这个原因。 */
    if (ui->label_2 && ui->label_3 && ui->horizontalSlider) {
        const int tw = ui->label_2->width();
        const int ty = ui->label_2->y();
        const int th = ui->label_2->height();
        ui->label_2->setGeometry(left, ty, tw, th);
        ui->label_3->setGeometry(x - g - tw, ty, tw, th);
        const int sx = left + tw + 6;
        const int ex = x - g - tw - 6;
        if (ex > sx + 40)
            ui->horizontalSlider->setGeometry(sx, ui->horizontalSlider->y(),
                                              ex - sx, ui->horizontalSlider->height());
    } else if (ui->horizontalSlider) {
        ui->horizontalSlider->setGeometry(left, ui->horizontalSlider->y(),
                                          x - g - left, ui->horizontalSlider->height());
    }
    m_rewBtn->raise();
    m_ffBtn->raise();
    qInfo().noquote() << "bt-media: 控制条重排 btn=" << w << "x" << h << "gap=" << g
                      << " 可用宽" << avail
                      << " 播放键" << ui->btn_play->geometry()
                      << " ⏪" << m_rewBtn->geometry() << " ⏩" << m_ffBtn->geometry()
                      << " ♡" << (ui->pushButton_2 ? ui->pushButton_2->geometry() : QRect());
}

/* ============================================================================
 * 蓝牙占位封面
 *
 * ⚠️ 先说清楚：**AVRCP 这条链路上拿不到手机的封面**。BlueZ 的
 * org.bluez.MediaPlayer1 只给 Title/Artist/Album/Duration/Position，
 * 没有 AVRCP 1.6 的封面图像句柄，也没有实现 BIP(Obex) 取图那套
 * （板上的 obexd 里没有 bip 插件）——所以"手机的海报"从协议上就传不过来。
 * 能做到的是：车机曲库里有同一首歌 → 用本地那份的封面（见 onBtTrackChanged），
 * 否则显示下面这张"黑胶唱片"占位盘，而不是继续显示上一首的封面。
 * ========================================================================== */
static QPixmap smartcarBtPlaceholderCover()
{
    const int sz = 240;
    QPixmap pm(sz, sz);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    QRadialGradient g(sz / 2.0, sz / 2.0, sz / 2.0);
    g.setColorAt(0.0,  QColor(46, 84, 140));
    g.setColorAt(0.55, QColor(24, 32, 48));
    g.setColorAt(1.0,  QColor(13, 15, 22));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(QRectF(0, 0, sz, sz));

    p.setBrush(Qt::NoBrush);                       // 唱片纹路
    for (int i = 1; i <= 6; ++i) {
        p.setPen(QPen(QColor(255, 255, 255, 18), 1));
        const qreal r = sz * (0.30 + 0.055 * i);
        p.drawEllipse(QPointF(sz / 2.0, sz / 2.0), r, r);
    }

    p.setPen(Qt::NoPen);                           // 中心标签
    p.setBrush(QColor(20, 60, 110));
    p.drawEllipse(QPointF(sz / 2.0, sz / 2.0), sz * 0.20, sz * 0.20);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(160, 210, 255), 3));
    p.drawEllipse(QPointF(sz / 2.0, sz / 2.0), sz * 0.20, sz * 0.20);

    QFont f = p.font();
    f.setPixelSize(qRound(sz * 0.19));
    f.setBold(true);
    p.setFont(f);
    p.setPen(QColor(235, 245, 255));
    p.drawText(QRectF(0, 0, sz, sz), Qt::AlignCenter, QStringLiteral("BT"));
    p.end();
    return pm;
}

/* ============================================================================
 * 「无封面」占位图 = **黑胶唱片**（2026-10-18 二次返工：胶片带 -> 黑胶）
 *
 * 为什么必须存在这一张：唱盘（discRotator）是**有状态的控件** ——
 * updateDiscCover() 一旦画上某首歌的封面，就一直是那张，直到有人再调它一次。
 * 历史上 mediaPlaylistCurrentIndexChanged() 只在"新歌有封面"时才调，
 * 于是"没有封面的歌"会继续挂着**上一首**的海报（用户实测：播《李荣浩-不遗憾》
 * 却显示《等你下课》的周杰伦封面）。现在切歌一律重画，没有封面就画这张唱片。
 *
 * 为什么用 QPainter 画而不是往 images/ 加一张 png：
 *   ① 不用改 res.qrc —— 本项目踩过"换了图但没 touch res.qrc ⇒ rcc 静默嵌旧图"的坑；
 *   ② 尺寸/配色跟着常量走，不会和 240x240 的唱盘对不上；
 *   ③ 省 rootfs 空间（板子 rootfs 常年只剩 1.3 MB 左右）。
 *
 * ★ 白边（上一版胶片占位的实测病灶，用户要求必须去掉）：
 *   上一版把"胶片带"画成 (232,234,238) 的近白色矩形，而且**没有裁剪** ——
 *   带子竖着贯穿整张图，穿过圆周的地方就变成两片贴着盘面边缘的亮楔形。
 *   真屏实测（v00_buyihan_before_rot.png）：135°/315° 方向、盘面边缘内侧 4~6px
 *   的像素是 (219,222,226)，而页面底色只有 (10,14,23) —— 亮了约 20 倍，
 *   这就是用户说的"白边"。**它不是外层控件/样式表画的**，是 pixmap 里画出来的。
 *   本版两条硬约束：
 *     ① pm.fill(Qt::transparent) —— 底色透明，圆**外**不留任何不透明像素；
 *     ② 盘面外沿用圆形遮罩收口，边缘是"近黑 -> 透明"的抗锯齿过渡，
 *        方向朝更黑，永远不会出现浅色描边。
 *
 * 画法参数（对齐参考图 424x419；参考图盘面直径 420 ⇒ 半径 210，
 * 下面所有比例按 R=118 折算）：
 *   盘面   : R = sz/2 - 2 = 118，圆心 (120,120)；径向渐变 中 #181818 -> 外 #0B0B0B
 *   唱片纹 : 10 圈 1px 同心圆，半径 35..104 均匀分布，颜色 #242424 / #2E2E2E 交替
 *            （最外圈特意停在 104：贴到盘沿会被看成"一圈亮边"，见下方函数内注释）
 *   高光   : 1 片斜向淡柔光（alpha 16 -> 0），只为让"盘面在转"看得出来
 *            （同心圆 + 圆标 + 中心孔全是旋转对称的，光靠它们转起来看不出动）
 *   圆标   : 半径 48（≈盘面 40%，按用户给的参数；参考图实测 65/210 = 31%），暗红 #990701
 *   中心孔 : 半径 4，纯黑
 *   不画任何文字，不画任何浅色描边。
 * ========================================================================== */
static QPixmap smartcarVinylPlaceholderCover(int sz = 240)
{
    const qreal c = sz / 2.0;
    const qreal R = sz / 2.0 - 2.0;          // 118：四周留 2px，防止被唱盘尺寸吃掉

    QPixmap pm(sz, sz);
    pm.fill(Qt::transparent);                // ★ 透明底：白边的根源就是这里填了浅色
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);

    /* 盘面：近黑；中心略亮、外沿压到近全黑（是"暗过渡"，不是浅色描边） */
    QRadialGradient face(c, c, R);
    face.setColorAt(0.00, QColor(0x18, 0x18, 0x18));
    face.setColorAt(0.55, QColor(0x12, 0x12, 0x12));
    face.setColorAt(0.90, QColor(0x0E, 0x0E, 0x0E));
    face.setColorAt(1.00, QColor(0x0B, 0x0B, 0x0B));
    p.setPen(Qt::NoPen);
    p.setBrush(face);
    p.drawEllipse(QPointF(c, c), R, R);

    /* 唱片纹：10 圈 1px 同心圆（35 -> 104 均匀），暗灰深浅交替。
     * ⚠ 最外圈为什么只到 104（真屏返工一次）：一开始按"35..115"画，最外圈离盘沿只剩
     * 2~3px，在暗背景上被看成"盘面外圈一道亮描边" —— 正是用户要去的白边。
     * 收到 104 后，盘沿 104..118 是一条干净的光面过渡带（"lead-out"区），边界不再有亮环。 */
    p.setBrush(Qt::NoBrush);
    const int kGrooves = 10;
    const qreal gR0 = 35.0, gR1 = 104.0;
    for (int i = 0; i < kGrooves; ++i) {
        const qreal r = gR0 + (gR1 - gR0) * i / (kGrooves - 1);
        const int v = (i % 2) ? 0x2E : 0x24;
        p.setPen(QPen(QColor(v, v, v), 1.0));
        p.drawEllipse(QPointF(c, c), r, r);
    }

    /* 一片极淡的斜向柔光（不是环、也不在边缘）：同心圆 + 圆标 + 中心孔全是旋转对称的，
     * 光靠它们"盘面在转"根本看不出来。这片 alpha 16 的柔光随 pixmap 一起转，
     * 既让在转看得出来（真屏连拍能拍到它挪位置），又远不到"亮边"的程度。 */
    {
        QRadialGradient sheen(c - R * 0.40, c - R * 0.40, R * 1.05);
        sheen.setColorAt(0.00, QColor(255, 255, 255, 16));
        sheen.setColorAt(0.45, QColor(255, 255, 255, 7));
        sheen.setColorAt(1.00, QColor(255, 255, 255, 0));
        p.setPen(Qt::NoPen);
        p.setBrush(sheen);
        p.drawEllipse(QPointF(c, c), R, R);
    }

    /* 中心圆标：暗红（参考图实测 (153,7,1)），半径 48 ≈ 盘面 40% */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x99, 0x07, 0x01));
    p.drawEllipse(QPointF(c, c), 48.0, 48.0);
    /* 圆标外沿一道更暗的 1px 收边（参考图里圆标边缘有一圈略深的红） */
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0x6E, 0x04, 0x00), 1.0));
    p.drawEllipse(QPointF(c, c), 47.5, 47.5);

    /* 中心孔：很小，纯黑 */
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0x00, 0x00, 0x00));
    p.drawEllipse(QPointF(c, c), 4.0, 4.0);
    p.end();

    /* 圆形收口：圆外一律透明。用 DestinationIn 遮罩（抗锯齿），
     * 边缘是"近黑 -> 透明"的渐变，不会像 clip 那样留硬边或亮边。 */
    QPixmap out(sz, sz);
    out.fill(Qt::transparent);
    {
        QPainter q(&out);
        q.setRenderHint(QPainter::Antialiasing, true);
        q.drawPixmap(0, 0, pm);
        QPainterPath mask;
        mask.addEllipse(QPointF(c, c), R, R);
        q.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        q.fillPath(mask, Qt::black);
    }
    return out;
}

void Audio::syncBtVolume()
{
    if (!ui || !ui->verticalSlider)
        return;
    QProcess *p = new QProcess(this);
    p->setProgram(QStringLiteral("/userdata/smart_car/bt-ctl"));
    /* bt-prepare = 车机喇叭抬到底线音量(只抬不降) + 读回手机当前音量。
     * 为什么要抬底线：板上 PA sink 默认只有 33%（-29dB），手机音量拉到 100%
     * 也还是闷的 —— 用户会以为"音量调不动"。 */
    p->setArguments({QStringLiteral("bt-prepare")});
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, p](int, QProcess::ExitStatus) {
                const QString out = QString::fromUtf8(p->readAllStandardOutput());
                p->deleteLater();
                for (const QString &line : out.split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
                    if (line.startsWith(QLatin1String("sink=")))
                        qInfo().noquote() << "bt-media: 车机喇叭" << line.mid(5);
                }
                const int i = out.indexOf(QLatin1String("volume="));
                if (i < 0)
                    return;
                const int v = out.mid(i + 7).trimmed()
                                 .section(QLatin1Char('\n'), 0, 0).toInt();
                if (v <= 0)
                    return;
                /* 只把界面滑块对齐到手机的真实音量，不要再把命令发回去 */
                QSignalBlocker block(ui->verticalSlider);
                ui->verticalSlider->setValue(qBound(0, v, 100));
                qInfo().noquote() << "bt-media: 手机当前音量 =" << v;
            });
    p->start();
}

void Audio::dumpAudioWidgets(const char *tag)
{
    if (!ui)
        return;
    qInfo().noquote() << "bt-dbg ====" << tag << "===="
                      << "音源=" << (m_btMode ? "蓝牙" : "本地");
    for (QLabel *l : this->findChildren<QLabel *>()) {
        if (!l || l->text().isEmpty())
            continue;
        QString t = l->text();
        t.replace(QLatin1Char('\n'), QLatin1Char('|'));
        qInfo().noquote() << "bt-dbg label" << l->objectName() << l->geometry()
                          << (l->isHidden() ? "hidden" : "shown")
                          << QStringLiteral("[%1]").arg(t.left(30));
    }
    if (ui->listWidget) {
        qInfo().noquote() << "bt-dbg listWidget rows=" << ui->listWidget->count()
                          << " 当前行=" << ui->listWidget->currentRow()
                          << " geom=" << ui->listWidget->geometry();
        for (int i = 0; i < ui->listWidget->count() && i < 4; ++i) {
            QString t = ui->listWidget->item(i)->text();
            t.replace(QLatin1Char('\n'), QLatin1Char('|'));
            qInfo().noquote() << "bt-dbg   row" << i
                              << QStringLiteral("[%1]").arg(t.left(40));
        }
    }
}

/* 秒/毫秒 → "mm:ss" */
static QString smartcarFmtTime(qint64 ms)
{
    const qint64 s = qMax<qint64>(0, ms) / 1000;
    return QStringLiteral("%1:%2")
            .arg(s / 60, 2, 10, QLatin1Char('0'))
            .arg(s % 60, 2, 10, QLatin1Char('0'));
}

/* ============================================================================
 * 长文本一律按控件宽度用 QFontMetrics::elidedText 截断
 *
 * 为什么必须做（历史踩过的坑）：手机的歌名/歌手动辄二三十个字符（中英日混排、
 * 多歌手串），直接 setText 会**压到旁边的控件上**（实测"7 个中文字压到旁边标签"），
 * 而在 1280x720 的固定布局里，控件宽度是死的、文字长度不是。
 * 界面只负责"不溢出"，完整文本写日志（见 onBtTrackChanged 的 bt-meta 行）。
 * ========================================================================== */
static QString smartcarElideText(const QFontMetrics &fm, const QString &text, int widthPx)
{
    if (widthPx <= 8 || text.isEmpty())
        return text;
    QStringList out;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &ln : lines)
        out << fm.elidedText(ln, Qt::ElideRight, widthPx);
    return out.join(QLatin1Char('\n'));
}

static QString smartcarElideLabel(const QLabel *l, const QString &text)
{
    if (!l)
        return text;
    return smartcarElideText(l->fontMetrics(), text, l->width() - 6);
}

static QString smartcarElideListItem(QListWidget *w, const QString &text)
{
    if (!w || !w->viewport())
        return text;
    return smartcarElideText(w->fontMetrics(), text, w->viewport()->width() - 28);
}

/*
 * 把"界面显示"整套交回本地播放器。
 *
 * 为什么必须显式做：蓝牙音源下这四样东西的来源和量纲都不同 ——
 *   进度条 = 手机位置（**毫秒**）、时间 = 手机给的、歌词 = 本地没匹配上时
 *   用的是手机上报的歌词行、播放键状态 = 手机状态。
 * 切回本地时如果只恢复列表和封面，用户看到的就是"进度条还是蓝牙的、
 * 歌词还是手机的"（用户报过）。这里一次性写回本地播放器的真实值。
 */
void Audio::restoreLocalDisplay()
{
    if (!ui)
        return;

    /* 1) 列表高亮 → 当前本地歌 */
    int idx = mediaPlaylist ? mediaPlaylist->currentIndex() : -1;
    if (ui->listWidget && ui->listWidget->count() > 0) {
        if (idx < 0 || idx >= ui->listWidget->count())
            idx = 0;
        ui->listWidget->setCurrentRow(idx);
    }

    /* 2) 歌词 → 当前本地歌的 .lrc，并按本地播放位置高亮 */
    if (idx >= 0 && idx < mediaObjectInfo.size()) {
        currentLyricsPath = mediaObjectInfo.at(idx).lyricsPath;
        parseLyrics(currentLyricsPath);
        updateLyrics(musicPlayer ? musicPlayer->position() : 0);
    } else {
        currentLyricsPath.clear();
        parseLyrics(QString());
    }

    /* 3) 进度条/时间 → 本地播放器的量纲（秒）与值 */
    const qint64 dur = musicPlayer ? musicPlayer->duration() : 0;
    const qint64 pos = musicPlayer ? musicPlayer->position() : 0;
    if (ui->label_3)
        ui->label_3->setText(dur > 0 ? smartcarFmtTime(dur) : QStringLiteral("00:00"));
    if (ui->label_2)
        ui->label_2->setText(smartcarFmtTime(pos));
    if (ui->horizontalSlider) {
        ui->horizontalSlider->setRange(0, dur > 0 ? int(dur / 1000) : 0);
        ui->horizontalSlider->setValue(int(pos / 1000));
    }

    /* 4) 播放键状态与唱片转动跟本地播放器走 */
    const bool playing = musicPlayer
                         && musicPlayer->state() == QMediaPlayer::PlayingState;
    if (ui->btn_play) {
        QSignalBlocker block(ui->btn_play);   // 只改显示，不要再触发"点击"语义
        ui->btn_play->setChecked(playing);
    }
    if (animation2) {
        if (playing) animation2->start();
        else         animation2->stop();
    }

    qInfo().noquote() << "bt-media: 已把显示交回本地播放器 pos=" << pos
                      << " dur=" << dur << " 歌词=" << (m_lyrics.isEmpty() ? "无" : "已解析")
                      << " 播放器状态=" << (playing ? "playing" : "非播放");
}

void Audio::runBtCtl(const QStringList &args, const QString &tag)
{
    QProcess *p = new QProcess(this);
    p->setProgram(QStringLiteral("/userdata/smart_car/bt-ctl"));
    p->setArguments(args);
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [p, tag](int, QProcess::ExitStatus) {
                const QString out = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
                p->deleteLater();
                qInfo().noquote() << "bt-media:" << tag << "->" << out;
            });
    p->start();
}

/*
 * 切回本地音源时**必须**把声卡从 PulseAudio 手里要回来。
 *
 * 板上 /etc/asound.conf：pcm.!default = softvol -> dmixer -> hw:0,0，
 * 也就是应用走 ALSA 默认设备时用的是 dmix；而 PulseAudio 的 module-alsa-card
 * 是**直接独占 hw:0,0** 的。两边互抢的实测证据：
 *   - PA 占卡时   aplay -D default → 退出码 1  Device or resource busy（本地没声音，
 *                 而 gstreamer 会悄悄退化成不出声的输出，进度条照走）
 *   - 应用占卡时  PA 的 alsa-sink 打不开 hw:0,0（蓝牙没声音）
 * 切几次音源就会出现"哪个音源都没声音"。
 *
 * 所以让两个音频服务**互斥**：本地音源时把 PA 整个停掉（`bt-ctl audio-release`），
 * 蓝牙音源时再启动它（`bt-ctl audio-acquire`）。
 */
void Audio::releaseAlsaForLocal()
{
    QProcess *p = new QProcess(this);
    p->setProgram(QStringLiteral("/userdata/smart_car/bt-ctl"));
    p->setArguments({QStringLiteral("audio-release")});
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, p](int, QProcess::ExitStatus) {
                const QString out = QString::fromUtf8(p->readAllStandardOutput()).trimmed();
                p->deleteLater();
                qInfo().noquote() << "bt-media: 让出声卡给本地播放 ->" << out;
                /* 如果本地播放器此刻"正在播放"却是因为打不开声卡在空转，
                 * 释放完声卡后重启一次播放，让它重新打开设备。 */
                if (!musicPlayer || musicPlayer->state() != QMediaPlayer::PlayingState)
                    return;
                const qint64 pos = musicPlayer->position();
                QTimer::singleShot(1500, this, [this, pos]() {
                    if (m_btMode || !musicPlayer
                            || musicPlayer->state() != QMediaPlayer::PlayingState)
                        return;
                    musicPlayer->stop();
                    musicPlayer->setPosition(pos);
                    musicPlayer->play();
                    qInfo().noquote() << "bt-media: 本地播放已重启（重新打开声卡）pos=" << pos;
                });
            });
    p->start();
}

void Audio::toggleBtMode()
{
    m_btMode = !m_btMode;
    if (!m_bt)
        return;
    if (m_btMode) {
        /* 切到蓝牙：**停掉**本地播放器，而不是只 pause。
         * ⚠️ QMediaPlayer::pause() 只是把 gstreamer 管线置为 PAUSED，
         * **ALSA 设备还一直开着** —— 于是 PulseAudio 再也拿不回 hw:0,0，
         * 蓝牙音频就没声音；反过来本地也一样。两边互抢，切几次就两边都没声。
         * stop() 会拆掉管线、关闭设备，把声卡让给 PA。
         * 播放位置记下来，切回本地时恢复。 */
        if (musicPlayer) {
            m_localWasPlaying = (musicPlayer->state() == QMediaPlayer::PlayingState);
            m_localResumePos  = musicPlayer->position();
            musicPlayer->stop();
            qInfo().noquote() << "bt-media: 已停止本地播放器并释放声卡（记住 pos="
                              << m_localResumePos << " wasPlaying=" << m_localWasPlaying << "）";
        }
        m_bt->startPolling(400);
        /* 收回声卡给蓝牙：PA 重新占用 hw:0,0 并重建蓝牙环路（切到本地时让出去过） */
        runBtCtl({QStringLiteral("audio-acquire")}, QStringLiteral("收回声卡给蓝牙"));
        if (m_btToggle)
            m_btToggle->setText(QStringLiteral("音源：蓝牙"));
        /* 进度条只做"实时跟进"，不接受拖拽：
         * ⚠️ 不能 setEnabled(false) —— 自定义样式下禁用态不重绘手柄，看起来像"不跟进"。
         * 保持 enabled，只让它对鼠标透明。 */
        if (ui && ui->horizontalSlider) {
            ui->horizontalSlider->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            ui->horizontalSlider->setFocusPolicy(Qt::NoFocus);
            ui->horizontalSlider->setToolTip(QStringLiteral("蓝牙音源：进度条仅显示，用 << / >> 快退快进"));
        }
        if (m_ffBtn)  m_ffBtn->show();
        if (m_rewBtn) m_rewBtn->show();
        /* 把一整排摆成 ⟲ ⏮ ⏪ ▶ ⏩ ⏭ ♡（同尺寸同间隔，放不下整排缩小） */
        layoutBtControlRow(true);
        /* 每次进蓝牙都重新做一次"换歌才做"的事（匹配歌词 / 画占位封面） */
        m_btTrackKey.clear();
        m_btLrcMatched = false;
        m_btPlaceholderDisc = false;
        /* 滑块对齐到手机当前音量（否则界面显示的和手机实际音量对不上） */
        syncBtVolume();
        /* 左侧列表从"本地曲库"变成"手机正在播的曲目" */
        if (ui && ui->listWidget) {
            ui->listWidget->clear();
            m_listHasBtRows = true;      // 这两行不在 mediaPlaylist 里，切回本地要整条重建
            auto *it = new QListWidgetItem(QStringLiteral("📱 手机正在播放"));
            it->setForeground(QColor(0x7f, 0xc4, 0xff));
            ui->listWidget->addItem(it);
            m_btStatusItem = it;         // 这一行挂徽标（App 名 / 形态名）
            m_btTrackItem = new QListWidgetItem(QStringLiteral("（等待手机上报歌名…）"));
            ui->listWidget->addItem(m_btTrackItem);
        }
        /* ⚠️ 切进蓝牙时**主动按当前元数据刷一次界面**。
         * 为什么必须刷：BtMedia 只在"字段发生变化"时才发 trackChanged，而手机
         * 很可能在我们进蓝牙**之前**就已经上报过这组字段了（进页面前后字段没变）
         * —— 不刷这一下，界面会一直停在"（等待手机上报歌名…）"，
         * 直到手机换下一首/下一句歌词（实测：手机暂停着不动时就会一直空着）。 */
        onBtTrackChanged(m_bt->title(), m_bt->artist(), m_bt->album());
        qInfo().noquote() << "bt-media: 切到蓝牙音源（本地播放器已暂停，进度条已禁止拖拽）";
    } else {
        /* 切回本地：暂停手机，避免和本地音乐混在一起；本地播放交回原有按键 */
        m_bt->pause();
        if (m_btToggle)
            m_btToggle->setText(QStringLiteral("音源：本地"));
        if (ui && ui->horizontalSlider) {
            ui->horizontalSlider->setAttribute(Qt::WA_TransparentForMouseEvents, false);
            ui->horizontalSlider->setFocusPolicy(Qt::StrongFocus);
            ui->horizontalSlider->setToolTip(QString());
        }
        if (m_ffBtn)  m_ffBtn->hide();
        if (m_rewBtn) m_rewBtn->hide();
        m_btTrackItem = nullptr;
        m_btStatusItem = nullptr;
        m_btTrackKey.clear();
        m_btLrcMatched = false;
        m_btPlaceholderDisc = false;
        scanSongs();                 // 恢复本地曲库列表与封面
        /* 关键：把声卡要回来 —— PulseAudio 的 module-alsa-card 直接独占 hw:0,0，
         * 而本地播放器走的是 asound.conf 里的 dmix，两边互抢：本地没声音/蓝牙没声音
         * 都可能。这里直接**把 PA 停掉**（音频服务互斥，见 bt-ctl audio-release）。 */
        releaseAlsaForLocal();
        /* 恢复切走之前的播放位置（切到蓝牙时 stop() 过，位置会被清零） */
        if (musicPlayer && m_localResumePos > 0) {
            musicPlayer->setPosition(m_localResumePos);
            qInfo().noquote() << "bt-media: 本地播放位置已恢复 pos=" << m_localResumePos;
        }
        /* 列表刚被重建过，选中行会被清成 -1；把高亮放回"当前那首歌"上，
         * 否则用户看到的是"本地播放却没有一行是亮的"。 */
        restoreLocalDisplay();
        updateDiscCover(firstSongCoverPixmap());
        if (ui && ui->label_4 && !m_btLocalTitle.isEmpty()) {
            ui->label_4->setText(m_btLocalTitle);   // 还原左上角歌名区
            m_btLocalTitle.clear();
        }
        /* 控制条几何还原（<< / >> 腾位时挪动过 ⟲ ⏮ ▶ ⏭ 与进度条） */
        layoutBtControlRow(false);
        qInfo().noquote() << "bt-media: 回到本地音源（已暂停手机）";
        dumpAudioWidgets("切回本地后");
    }
    m_bt->refresh();          // 立刻刷一次，不等下一个轮询周期
}

void Audio::onBtStateChanged(const QString &state)
{
    if (!m_btMode)
        return;
    /* 关键：把手机的播放状态回写到播放按钮，否则图标永远是本地那一套 */
    const bool playing = (state == QLatin1String("playing"));
    /* 手机开始推流了 → 确认蓝牙音频通路在。
     * 为什么需要：切回本地时我们会把 loopback 卸掉让出声卡；如果那一刻手机
     * 没在推流，PA 里的 bluez source 可能还没出现，"收回"时建不起来 ——
     * 之后手机再播就没人把通路接回去了（表现：蓝牙没声音）。 */
    if (playing && !m_btAcquired) {
        m_btAcquired = true;
        runBtCtl({QStringLiteral("audio-acquire")},
                 QStringLiteral("手机开始播放，确认蓝牙音频通路"));
    } else if (!playing) {
        m_btAcquired = false;
    }
    if (ui && ui->btn_play) {
        if (ui->btn_play->isChecked() != playing)
            ui->btn_play->setChecked(playing);
    }
    if (animation2) {
        if (playing)
            animation2->start();
        else
            animation2->stop();
    }

    /* ⚠️ 这里原来有一句：state == "disconnected" 时把歌词区写成「蓝牙已断开」。
     * 已删（2026-10-13）。原因有两个，都是实测踩出来的：
     *   1) onBtTrackChanged() 刚把歌词区写成"当前歌曲/视频的信息 + 中性提示"，
     *      几百毫秒后这里又把它覆盖成「蓝牙已断开」—— 两处抢同一个控件，
     *      用户看到的是**一闪而过的正确信息被无关文字顶掉**。
     *   2) 断开这件事**列表第一行和音源按钮已经说清楚了**（"📱 手机正在播放"
     *      会消失、按钮回到「音源：本地」），再往歌词区塞一句是重复且误导
     *      （歌词区应该只负责歌词/曲目，不负责连接状态）。
     * 所以断开状态只保留上面按钮/动画的更新，不碰歌词区。 */
}

void Audio::onBtTrackChanged(const QString &title, const QString &artist, const QString &album)
{
    if (!m_btMode || !m_bt)
        return;

    /*
     * ★ 关键（2026-10-14 重写）：**不要**拿手机裸字段当"歌名/歌手"用。
     *
     * 实测同一台手机在同一个 App 里都会换字段语义：QQ音乐正常态是
     * (artist=歌手, album=专辑, title=歌名)，一进滚动歌词态就变成
     * (artist="歌名-歌手", title=滚动歌词行)；放视频时 title 可能是占位符
     * "Not Provided"。原来这里写 `primary = artist.isEmpty() ? title : artist`
     * ——于是正常态把**歌手**当歌名显示（用户实测：第一行显示=林俊杰），
     * 换歌时又可能把歌词行/占位符当歌名。
     *
     * 现在统一交给 btmedia.cpp 里的纯函数 smartcarResolveTrack() 按**字段形态**
     * 解析出 歌名/歌手/形态（判定谓词与实测样例见 docs/蓝牙元数据与歌词显示策略.md）。
     */
    const SmartcarTrackInfo info = m_bt->trackInfo();

    /* "换歌才做的事"（匹配本地 .lrc、重画封面）按**解析后**的曲目标识去重：
     * 滚动歌词形态下手机 Title 每一句都在变，但解析出来的歌名/歌手是稳定的，
     * 所以既不会每句重扫目录，也不会因为 Title 变了就误判成换歌。 */
    const QString trackKey = info.title + QLatin1Char('|') + info.artist
                             + QLatin1Char('|') + QString::number(int(info.form));
    const bool newTrack = (trackKey != m_btTrackKey);
    m_btTrackKey = trackKey;

    /* ---------------- 徽标：只用**形态名**（在线音乐 / 视频 / 无曲目信息） ----------------
     * ⚠️ 曾经试过显示手机侧 App 名（`org.bluez.MediaPlayer1.Name`，真机上确实拿到过"抖音"），
     * 但用户明确要求**不显示手机侧的 App 名**：
     *   · App 名和"这组元数据是不是音乐"没有必然关系（抖音里也能放完整歌曲）；
     *   · App 名会随手机上次用过的播放器漂移，显示出来反而干扰判断。
     * 所以徽标只表达"这组元数据被判成什么形态"；元数据里恰好写着的平台名
     * （如 artist=抖音）也不进徽标，只进日志备查。 */
    const QString badge = smartcarFormName(info.form);

    /* ---------------- 歌词区 ----------------
     * 1) 本地 myMusic 有对应 .lrc            → 按手机上报的位置高亮三行（真歌词）；
     * 2) 没有 .lrc，但手机 Title 确实是歌词行 → 直接用手机给的歌词行滚动显示；
     * 3) 其余（歌名/占位符/视频）             → **明确的中性提示**，绝不拿歌名冒充歌词，
     *    更不会留着上一首的字。
     */
    if (newTrack) {
        const QString lrc = m_bt->matchLyricFile();
        m_btLrcMatched = !lrc.isEmpty();
        parseLyrics(lrc);                    // 空路径 = 清掉上一首的行表
        if (!m_btLrcMatched) {
            /* 三行歌词标签必须显式清空，否则上一首的句子会一直留在屏幕上。 */
            if (m_lyricPrev) m_lyricPrev->clear();
            if (m_lyricCur)  m_lyricCur->clear();
            if (m_lyricNext) m_lyricNext->clear();
        }
    }
    if (!m_btLrcMatched) {
        const bool phoneLyrics = (info.form == SmartcarMetaForm::Music)
                                 && !info.lyricLine.isEmpty();
        if (m_lyricNext)
            m_lyricNext->clear();
        if (m_lyricPrev)
            m_lyricPrev->clear();
        if (m_lyricCur) {
            if (phoneLyrics) {
                const QStringList h = m_bt->phoneLyricHistory();
                const QString prev = (h.size() >= 2) ? h.at(h.size() - 2) : QString();
                const QString cur  = h.isEmpty() ? info.lyricLine : h.last();
                if (m_lyricPrev) m_lyricPrev->setText(smartcarElideLabel(m_lyricPrev, prev));
                m_lyricCur->setText(smartcarElideLabel(m_lyricCur, cur));
            } else {
                /* 中性提示：三种形态各有各的说法，任何一种都不会误导。 */
                m_lyricCur->setText(smartcarElideLabel(m_lyricCur, smartcarLyricHint(info.form)));
            }
        }
    }

    /* ---------------- 第一行=歌名，第二行=歌手（专辑只在没有歌手时兜底） ----------------
     * 都是**解析后**的字段：正常态=title/artist，滚动歌词态=从 artist 里拆出来的
     * "歌名/歌手"。
     * ⚠️ 视频形态下 artist 是**频道/UP 主**（真机实测 `artist=油里油气资源库`），
     * 直接摆在第二行会被当成"歌手" —— 加"频道："前缀，让它一眼就是频道名，
     * 绝不张冠李戴（徽标那边同时已经写着"视频"）。 */
    QString primary = info.title;
    if (primary.isEmpty())
        primary = QStringLiteral("（未知曲目）");
    QString secondary = info.artist;
    if (!secondary.isEmpty() && info.form == SmartcarMetaForm::Video)
        secondary = QStringLiteral("频道：") + secondary;
    if (secondary.isEmpty())
        secondary = info.album;
    QString head = primary;
    if (!secondary.isEmpty() && secondary != primary)
        head += QLatin1Char('\n') + secondary;

    /* 可见的那一行就是左侧列表里的"手机正在播"；label_4 是老布局的歌名区（新布局里
     * 已被歌词控件取代并隐藏），一起写，切回本地时好还原。
     * ⚠️ 一律 elide：控件宽度有限，长歌名会压到旁边（历史踩过）。 */
    if (m_btTrackItem)
        m_btTrackItem->setText(smartcarElideListItem(ui ? ui->listWidget : nullptr, head));
    /* 徽标那一行（列表第一行）：App 名 / 形态名 */
    if (m_btStatusItem)
        m_btStatusItem->setText(QStringLiteral("📱 手机正在播放 · ") + badge);
    if (ui && ui->label_4) {
        if (m_btLocalTitle.isEmpty())
            m_btLocalTitle = ui->label_4->text();       // 记下本地内容，切回时还原
        ui->label_4->setText(smartcarElideLabel(ui->label_4, head));
    }

    /* 海报（唱盘封面）：
     * AVRCP 链路上 BlueZ **不提供封面**（没有图像句柄/BIP），所以"手机正在显示
     * 的那张图"从协议上就传不过来。能做到的：
     *   形态=音乐 且 车机曲库里有同一首歌 → 用本地那份的封面（内嵌 ID3 或同名 jpg）；
     *   其余（视频/无元数据/本地没有）    → 画一张中性的黑胶占位盘。
     * 关键是**换歌时一定重画**：绝不再挂着上一首的封面（用户会以为"海报不对"）。 */
    if (newTrack || !m_btPlaceholderDisc) {
        m_btPlaceholderDisc = true;
        QPixmap cov;
        if (info.form == SmartcarMetaForm::Music) {
            const QString local = m_bt->matchLocalAudioFile();
            if (!local.isEmpty())
                cov = embeddedCover(local);
        }
        if (cov.isNull())
            cov = smartcarBtPlaceholderCover();
        updateDiscCover(cov);
    }

    /* ---------------- 日志：每次元数据变化一行，完整文本（不截断） ----------------
     * 格式固定，便于 grep 取证：
     *   title=/artist=/album=（手机原样）+ 判定=/歌名=/歌手=/歌词=有|无（解析结果）
     * 平台名（元数据里恰好等于"抖音/B站"这类）只在日志里备查，不进界面徽标。 */
    qInfo().noquote() << "bt-meta: title=[" << title << "] artist=[" << artist
                      << "] album=[" << album << "]"
                      << " 判定=" << smartcarFormName(info.form)
                      << " 歌名=[" << info.title << "] 歌手=[" << info.artist << "]"
                      << " 歌词=" << (m_btLrcMatched ? "有" : "无")
                      << " 徽标=[" << badge << "]"
                      << (info.platform.isEmpty()
                              ? QString()
                              : QStringLiteral(" 平台=[%1]").arg(info.platform))
                      << (info.lyricLine.isEmpty()
                              ? QString()
                              : QStringLiteral(" 手机Title=歌词行[%1]").arg(info.lyricLine));
}

void Audio::onBtPositionChanged(int positionMs, int durationMs)
{
    if (!m_btMode)
        return;

    /* 进度条：范围=总时长，值=当前位置。
     * ⚠️ 手机上报的 Duration **经常是 0**（实测这台 Redmi K70 Ultra 就是 dur=0 pos=0，
     * AVRCP 不上报时长）。这种情况如实显示 "--:--"，**不要**摆出 "00:00/00:00" 骗人。
     * 另外解析层曾经出过把 `uint32` 里的 32 当数值的 bug（已修），所以这里对
     * 过小的"时长"也留一道防线：小于 1 秒的一律当作没给。 */
    const bool durUsable = (durationMs >= 1000);
    const int max = durUsable ? durationMs : 300000;   // 没给时长时给个名义范围，滑块仍能体现位置
    if (ui && ui->horizontalSlider) {
        if (ui->horizontalSlider->maximum() != max)
            ui->horizontalSlider->setMaximum(max);
        if (!ui->horizontalSlider->isSliderDown())
            ui->horizontalSlider->setValue(qBound(0, positionMs, max));
    }

    /* 时间标签：label_2=当前、label_3=总时长。
     * 本地播放器的槽在蓝牙模式下不再更新它们（本地已暂停），
     * 不自己写就会一直显示本地那首歌的时间。 */
    if (ui && ui->label_2) {
        const int s = qMax(0, positionMs) / 1000;
        ui->label_2->setText(QStringLiteral("%1:%2")
                                 .arg(s / 60, 2, 10, QLatin1Char('0'))
                                 .arg(s % 60, 2, 10, QLatin1Char('0')));
    }
    if (ui && ui->label_3) {
        if (durUsable) {
            const int s = durationMs / 1000;
            ui->label_3->setText(QStringLiteral("%1:%2")
                                     .arg(s / 60, 2, 10, QLatin1Char('0'))
                                     .arg(s % 60, 2, 10, QLatin1Char('0')));
        } else {
            ui->label_3->setText(QStringLiteral("--:--"));   // 手机没给可信时长，如实显示
        }
    }

    /* 诊断：每 ~3 秒把真实数值打一行，用来判断"进度条不跟进"到底卡在哪
     * （手机 Position 是否在变 / 滑块范围是否合理 / 是否被覆盖） */
    static int s_dbg = 0;
    if (++s_dbg % 15 == 0) {
        qInfo().noquote() << "bt-media[dbg] pos=" << positionMs << "dur=" << durationMs
                          << "手机给的位置=" << (m_bt && m_bt->positionFromPhone() ? "是" : "否(本地推算)")
                          << "sliderMax=" << (ui && ui->horizontalSlider ? ui->horizontalSlider->maximum() : -1)
                          << "sliderVal=" << (ui && ui->horizontalSlider ? ui->horizontalSlider->value() : -1)
                          << "label2=" << (ui && ui->label_2 ? ui->label_2->text() : QString())
                          << "label3=" << (ui && ui->label_3 ? ui->label_3->text() : QString());
    }

    /* 歌词高亮：复用现有实现（它本来就接收一个毫秒位置参数）。
     * 蓝牙流的位置就是手机上报的 Position，不需要自己计时。 */
    updateLyrics(positionMs);
}

Audio::Audio(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Audio)
{
    ui->setupUi(this);
    // 注意：缩放要放在自定义排版之后做（见构造函数末尾），否则会把布局算乱
    /* 音臂 */
    // 加载旋转图片
    QPixmap rotatingPixmap(":/images/styli.png");
    // 指定控件并初始化坐标用作旋转中心点
    baseWidget = ui->widget_2;
    QPointF rotationCenter(34, 54);
    // 创建RotatableWidget对象，将图片和旋转中心点传递给构造函数
    RotatableWidget *rotatableWidget = new RotatableWidget(baseWidget, rotatingPixmap, rotationCenter);
    rotatableWidget->resize(baseWidget->size()); // 确保RotatableWidget的大小与baseWidget相同
    // 创建动画来旋转 rotatableWidget
    animation = new QPropertyAnimation(rotatableWidget, "rotation");
    animation->setDuration(200); // 动画持续200毫秒

    /* 唱片*/
    // 设置专辑封面
    setAlbumArt(QPixmap(":/images/fengshen.jpg"));

    // 光碟背景：用第一首歌的封面（有内嵌封面就用内嵌的），不再用固定的 disc.png
    // 封面资源在 scanSongs() 里准备好，这里先用一张占位，之后会刷新
    QPixmap discPixmap = firstSongCoverPixmap();
    QPixmap rotatingPixmap2 = discPixmap.scaled(240, 240, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    // 裁成正方形 + 圆形，避免非等比封面被拉变形 / 露出方块
    {
        const int side = qMin(rotatingPixmap2.width(), rotatingPixmap2.height());
        if (rotatingPixmap2.width() != side || rotatingPixmap2.height() != side)
            rotatingPixmap2 = rotatingPixmap2.copy((rotatingPixmap2.width() - side) / 2,
                                                   (rotatingPixmap2.height() - side) / 2, side, side);
        QPixmap round(side, side);
        round.fill(Qt::transparent);
        QPainter rp(&round);
        rp.setRenderHint(QPainter::Antialiasing, true);
        QPainterPath circle;
        circle.addEllipse(0, 0, side, side);
        rp.setClipPath(circle);
        rp.drawPixmap(0, 0, rotatingPixmap2);
        rp.end();
        rotatingPixmap2 = round;
    }
    baseWidget2 = ui->widget_3;
    QPointF rotationCenter2(baseWidget2->width()/2, baseWidget2->height()/2);
    RotatableWidget *rotatableWidget2 = new RotatableWidget(baseWidget2, rotatingPixmap2, rotationCenter2);
    rotatableWidget2->setAutoCenter(true);   // 唱片绕自身中心转
    discRotator = rotatableWidget2;   // 记下来，换歌时更新封面
    rotatableWidget2->resize(baseWidget2->size()); // 确保RotatableWidget的大小与baseWidget相同
    // 创建动画来旋转 rotatableWidget
    animation2 = new QPropertyAnimation(rotatableWidget2, "rotation");
    animation2->setStartValue(0); // 开始角度为0
    animation2->setEndValue(360); // 结束角度为360
    animation2->setDuration(4000); // 动画持续4000毫秒
    animation2->setLoopCount(-1); // 设置动画无限循环


    /* 媒体播放器初始化 */

    mediaPlayerInit();

    /* 扫描歌曲 */
    scanSongs();

    /* ---------- 蓝牙音源（AVRCP CT）：每秒取一次手机播放状态 ----------
     * 手机在放歌时音乐页会自动切到蓝牙音源（见 onBtStateChanged），
     * 歌名/歌手/进度/歌词都来自手机，按键控制手机本身。 */
    m_bt = new BtMedia(this);
    connect(m_bt, &BtMedia::trackChanged,    this, &Audio::onBtTrackChanged);
    connect(m_bt, &BtMedia::positionChanged, this, &Audio::onBtPositionChanged);
    connect(m_bt, &BtMedia::stateChanged,    this, &Audio::onBtStateChanged);
    /* 手机一连上就自动切到蓝牙音源（断开自动回本地）——用户要求"连上蓝牙后
     * 进音乐播放器默认就是蓝牙界面"，这条覆盖"在音乐页里手机才连上"的情况。
     * 走 1.2 秒去抖：手机侧连接状态短暂跳变时不要跟着翻。 */
    m_connDebounce = new QTimer(this);
    m_connDebounce->setSingleShot(true);
    m_connDebounce->setInterval(1200);
    connect(m_connDebounce, &QTimer::timeout, this, [this]() {
        applyBtSourceForConnection(m_pendingBtConn);
    });
    /* 蓝牙音源通路保活：每 10 秒确认一次"PA 的 sink 没被挂起 + 蓝牙环路在"。
     * 为什么需要：切回本地时我们让 PA 挂起 sink 并卸掉 loopback（用户级挂起是
     * 黏性的，PA 不会自动恢复）；如果那一刻手机还没开始推流，"收回"时建不起来
     * loopback —— 事后手机再播就没人接回去（表现：蓝牙没声音）。 */
    m_btKeepAlive = new QTimer(this);
    m_btKeepAlive->setInterval(10000);
    connect(m_btKeepAlive, &QTimer::timeout, this, [this]() {
        /* ⚠️ 用 linkConnected() 而不是 connected()：放视频时 AVRCP 的 Status
         * 可能读不到（connected() 会变 false），但链路其实好着，保活必须继续做。 */
        if (m_btMode && m_bt && m_bt->linkConnected())
            runBtCtl({QStringLiteral("audio-acquire")}, QStringLiteral("蓝牙通路保活"));
    });
    m_btKeepAlive->start();
    connect(m_bt, &BtMedia::connectionChanged, this, &Audio::onBtConnectionChanged,
            Qt::UniqueConnection);
    m_bt->startPolling(400);

    /* 200ms 插值 tick：进度条与歌词跟随真实时间平滑推进（不是一秒一跳） */
    connect(m_bt, &BtMedia::tick, this, [this]() {
        if (m_btMode && m_bt)
            onBtPositionChanged(m_bt->positionNow(), m_bt->duration());
    });

    /* 音量滑块：蓝牙音源下调的是**手机音量**（AVRCP 绝对音量）。
     * 拖动过程中就要跟着变（否则用户以为调不动），但每个像素都 fork 一个 bt-ctl
     * 太重，所以 250ms 防抖。 */
    m_volDebounce = new QTimer(this);
    m_volDebounce->setSingleShot(true);
    m_volDebounce->setInterval(250);
    connect(m_volDebounce, &QTimer::timeout, this, [this]() {
        if (m_btMode && m_bt && ui && ui->verticalSlider)
            m_bt->setVolume(ui->verticalSlider->value());
    });
    if (ui && ui->verticalSlider) {
        connect(ui->verticalSlider, &QSlider::valueChanged, this, [this](int) {
            if (m_btMode && m_volDebounce)
                m_volDebounce->start();
        });
    }

    /* 「本地 / 蓝牙」音源切换按钮
     * 早先按播放键的 geometry 摆放，但那时布局还没生效（geometry 可能就是 0），
     * 按钮尺寸成了 0×0 → 用户根本看不到。现在：固定放**页面右下角**、
     * 给醒目样式、布局稳定后再摆一次并 raise() 置顶。 */
    m_btToggle = new QPushButton(this);
    m_btToggle->setText(QStringLiteral("音源：本地"));
    m_btToggle->setStyleSheet(QStringLiteral(
        "QPushButton{background:#1e5aa8;color:#ffffff;border:2px solid #7fc4ff;"
        "border-radius:10px;font-size:20px;font-weight:bold;padding:6px 14px;}"
        "QPushButton:pressed{background:#2f7fd8;}"));
    m_btToggle->resize(240, 72);
    m_btToggle->move(width() - 260, height() - 100);
    m_btToggle->show();
    m_btToggle->raise();
    connect(m_btToggle, &QPushButton::clicked, this, &Audio::toggleBtMode);

    /* 快退 / 快进按钮：蓝牙模式下**代替拖拽**（AVRCP 没有绝对 seek）。
     * 位置由 layoutBtControlRow() 摆到播放键两侧：⟲ ⏮ ⏪ ▶ ⏩ ⏭ ♡，
     * 黑底白字圆钮、与播放键同风格、等间隔，平时隐藏。 */
    m_rewBtn = new QPushButton(this);
    m_ffBtn  = new QPushButton(this);
    for (QPushButton *b : {m_rewBtn, m_ffBtn}) {
        /* ⚠️ 不要"拷贝播放键的 styleSheet 再剔除 image/icon 行"：那是多条
         * `QPushButton{...}` 规则，按 ';' 切开拼回去会拼出语法错误的样式表，
         * Qt 会**整条丢弃**；而 `border-radius: 50%` 也是无效写法（只认像素）。
         * 所以：样式表只负责"透明"，圆底和三角都在 smartcarJumpFace() 里画成图标。 */
        b->setStyleSheet(QStringLiteral(
            "QPushButton { background: transparent; border: none; padding: 0px; }"));
        b->setIcon(QIcon(smartcarJumpFace(36, b == m_ffBtn)));
        b->setIconSize(QSize(36, 36));
        b->resize(36, 36);
        b->hide();
    }
    auto sendBtKey = [this](const QString &what) {
        if (!m_bt)
            return;
        QProcess *p = new QProcess(this);
        p->setProgram(QStringLiteral("/userdata/smart_car/bt-ctl"));
        p->setArguments({QStringLiteral("bt-media"), what});
        connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                p, &QObject::deleteLater);
        p->start();
        qInfo().noquote() << "bt-media: 用户按下" << what;
        QTimer::singleShot(800, m_bt, &BtMedia::refresh);
    };
    connect(m_rewBtn, &QPushButton::clicked, this, [sendBtKey]() { sendBtKey(QStringLiteral("rew")); });
    connect(m_ffBtn,  &QPushButton::clicked, this, [sendBtKey]() { sendBtKey(QStringLiteral("ff")); });
    /* 布局稳定后（UiAdapter::fit() 跑完）做一次自检准备。
     * 控制条本身**不在本地模式下重排** —— 那会把本地页面的按钮也一起缩小；
     * 只在真正进蓝牙（layoutBtControlRow(true)）时才摆成 7 个一排。 */
    QTimer::singleShot(360, this, [this]() {
        if (!ui || !ui->btn_play || !m_ffBtn || !m_rewBtn)
            return;
        /* 先决定音源（手机连着就走蓝牙），再决定要不要"让出声卡" ——
         * 顺序反了会互相打架：一边刚收回声卡、另一边又把它挂起。 */
        /* 离线自检：SMARTCAR_BT_UI_PREVIEW=1 时直接摆出蓝牙那一排并显示 ⏪/⏩，
         * 不用连手机就能截图核对图标/间距（正常启动不设这个变量）。 */
        if (qEnvironmentVariableIsSet("SMARTCAR_BT_UI_PREVIEW")) {
            layoutBtControlRow(true);
            m_rewBtn->show();
            m_ffBtn->show();
            qInfo().noquote() << "bt-media: UI 预览模式，已摆出蓝牙控制条并显示 ⏪ / ⏩";
        }
        /* 离线自检：SMARTCAR_BT_AUTOSTART=1 时直接切到蓝牙音源 —— 手机已连上并
         * 在放歌时，配合 --page=audio 就能截图核对"标题两行/歌词/进度/音量"
         * 这些只有手机在播才看得到的东西（正常启动不设这个变量）。 */
        if (qEnvironmentVariableIsSet("SMARTCAR_BT_AUTOSTART") && !m_btMode) {
            qInfo().noquote() << "bt-media: 自检模式，直接切到蓝牙音源";
            toggleBtMode();
        }
        /* 启动时如果**最终是本地音源**、手机也没连着，就把声卡从 PA 手里要回来：
         * 上一次会话（比如应用重启前正在放蓝牙）可能把卡留在 PA 手里，
         * 那样本地播放会"设备忙"没声音。放在音源决定之后，避免和收回声卡打架。 */
        if (!m_btMode && m_bt && !m_bt->linkConnected())
            runBtCtl({QStringLiteral("audio-release")}, QStringLiteral("启动时让出声卡"));
        /* 自检：SMARTCAR_BT_CLICKTEST=1 时自动按播放键 4 次，并在按后 200/600/
         * 1200/2500ms 打印按钮 checked 与手机状态 —— 专门用来验证"按一下图标
         * 闪两下"那个抖动是否真的没了（人不在车边也能跑）。 */
        if (qEnvironmentVariableIsSet("SMARTCAR_BT_CLICKTEST")) {
            QTimer *t = new QTimer(this);
            t->setInterval(4000);
            connect(t, &QTimer::timeout, this, [this, t]() {
                static int n = 0;
                ++n;
                qInfo().noquote() << "bt-clicktest: === 第" << n << "次点击 === 点击前 checked="
                                  << ui->btn_play->isChecked()
                                  << " 本地状态=" << (m_bt ? m_bt->state() : QString());
                ui->btn_play->click();
                const int marks[] = {200, 600, 1200, 2500};
                for (int ms : marks) {
                    QTimer::singleShot(ms, this, [this, ms]() {
                        qInfo().noquote() << "bt-clicktest:   +" << ms << "ms checked="
                                          << ui->btn_play->isChecked()
                                          << " 本地状态=" << (m_bt ? m_bt->state() : QString())
                                          << " 手机位置=" << (m_bt ? m_bt->positionNow() : 0);
                    });
                }
                if (n >= 4)
                    t->stop();
            });
            t->start();
            qInfo().noquote() << "bt-media: 按键自检已启动（每 4 秒自动点一次播放键）";
        }
        /* 自检：SMARTCAR_BT_BACKTEST=<毫秒> —— 进蓝牙后过一会儿自动切回本地，
         * 用来核对"切回本地时列表/高亮是否干净"（用户报过残留"📱 手机正在播放"）。 */
        const int backMs = qEnvironmentVariableIntValue("SMARTCAR_BT_BACKTEST");
        if (backMs > 0) {
            QTimer::singleShot(backMs, this, [this]() {
                if (m_btMode) {
                    qInfo().noquote() << "bt-media: 自检——自动切回本地音源";
                    toggleBtMode();
                }
            });
            /* 切回本地 6 秒后：按一次播放键，看**本地播放器**到底动没动
             * （state/error/position）——"切回本地无法正常播放"要分清是
             * 显示没切回来，还是本地播放器根本打不开声卡（PA 占着 ALSA）。 */
            QTimer::singleShot(backMs + 6000, this, [this]() {
                qInfo().noquote() << "bt-backtest: 点击前 本地播放器 state="
                                  << int(musicPlayer ? musicPlayer->state() : -1)
                                  << " error=" << int(musicPlayer ? musicPlayer->error() : -1)
                                  << " mediaStatus=" << int(musicPlayer ? musicPlayer->mediaStatus() : -1)
                                  << " volume=" << (musicPlayer ? musicPlayer->volume() : -1)
                                  << " pos=" << (musicPlayer ? musicPlayer->position() : -1)
                                  << " dur=" << (musicPlayer ? musicPlayer->duration() : -1);
                if (ui && ui->btn_play)
                    ui->btn_play->click();
                QTimer::singleShot(4000, this, [this]() {
                    qInfo().noquote() << "bt-backtest: 点击 4 秒后 state="
                                      << int(musicPlayer ? musicPlayer->state() : -1)
                                      << " error=" << int(musicPlayer ? musicPlayer->error() : -1)
                                      << " volume=" << (musicPlayer ? musicPlayer->volume() : -1)
                                      << " pos=" << (musicPlayer ? musicPlayer->position() : -1)
                                      << " label2=" << (ui && ui->label_2 ? ui->label_2->text() : QString());
                    if (ui)
                        dumpAudioWidgets("本地点播放 4 秒后");
                });
            });
        }
    });
    /* 诊断：把界面上所有 label 的 名字/坐标/文字 打一遍（含隐藏的）。
     * 用途：确认"某段文字到底是哪个控件画的"，别再靠猜控件名。
     * 注意：不能用 isVisible() 过滤 —— 页面没被 show 的时候所有子控件都不是 visible，
     * 那样会一行都不打（上一版就是这么白跑的）。 */
    QTimer::singleShot(700, this, [this]() { dumpAudioWidgets("启动 700ms"); });

    /*
     * 离线自检：SMARTCAR_BT_FAKE_META="<artist>|<album>|<title>" ——
     * 先让界面装成"本地曲库那首歌"（封面=本地封面、歌词=本地歌词），
     * 再走**真实信号链路**喂一组手机元数据，用来在**手机不在场**时验证：
     *   · 换到一首配不上本地的曲子（视频等）时，上一首的歌词/海报有没有被清掉；
     *   · 歌名/歌手/占位符/滚动歌词行四种形态各自显示成什么（判据见 bt-meta 日志行）。
     * 正常启动不设这个变量，完全无副作用。
     */
    {
        const QString fake = qEnvironmentVariable("SMARTCAR_BT_FAKE_META");
        if (!fake.isEmpty()) {
            const QStringList f = fake.split(QLatin1Char('|'));
            const QString fa = f.value(0), fb = f.value(1), ft = f.value(2);

            /* ① 就位：切到蓝牙音源显示（只动界面，不碰音频通路），
             *    并让 BtMedia 报告"本地那首歌" —— 歌词/封面都会按本地那份来。
             *    放在 9 秒：等应用把首页/音乐页**真正画上屏**之后再改，
             *    这样"改之前"那一帧也已经在屏上了。 */
            QTimer::singleShot(9000, this, [this]() {
                m_btMode = true;
                cancelConnDebounceForTest();     // 防止"没真手机 → 自动切回本地"冲掉界面
                /* ⚠️ 自检期间必须停掉 400ms 的元数据轮询，否则：
                 *   · 手机（或"无设备"）读回来的**真实**字段和刚注入的假值不同
                 *     ⇒ 400ms 后就发一次 trackChanged 把界面覆盖回真实状态；
                 *   · setMetaForTest 把状态置成 playing，而真实状态是 disconnected
                 *     ⇒ 触发 connectionChanged ⇒ 1.2 秒后"手机已断开，自动回到本地音源"
                 *     ⇒ 整页切回本地，屏幕上根本看不到要验证的形态。
                 * （实测日志：注入后约 1.4 秒出现"手机已断开，自动回到本地音源"。）
                 * 正常运行时轮询照常跑，这条只在设了 SMARTCAR_BT_FAKE_META 时生效。 */
                if (m_bt)
                    m_bt->stopPolling();
                /* 自检帧要和真机一致：音源按钮文字、进度条/时间标签都由蓝牙链路驱动
                 * （停掉轮询后不会再自动更新，所以这里手动推一次位置/时长）。
                 * 真机 dur=0 时 label_3 显示 "--:--"，不显示 00:00。 */
                if (m_btToggle)
                    m_btToggle->setText(QStringLiteral("音源：蓝牙"));
                onBtPositionChanged(0, 0);
                m_btTrackKey.clear();
                m_btLrcMatched = false;
                m_btPlaceholderDisc = false;
                if (m_btTrackItem == nullptr && ui && ui->listWidget) {
                    ui->listWidget->clear();
                    m_listHasBtRows = true;
                    auto *it = new QListWidgetItem(QStringLiteral("📱 手机正在播放"));
                    it->setForeground(QColor(0x7f, 0xc4, 0xff));
                    ui->listWidget->addItem(it);
                    m_btStatusItem = it;
                    m_btTrackItem = new QListWidgetItem(QStringLiteral("（等待手机上报歌名…）"));
                    ui->listWidget->addItem(m_btTrackItem);
                }
                m_bt->setMetaForTest(QStringLiteral("等你下课 (with 杨瑞代) - 周杰伦"),
                                     QStringLiteral("等你下课"),
                                     QStringLiteral("词：周杰伦"));
                qInfo().noquote() << "pf-fake: 已装成【本地那首歌】歌词="
                                  << (m_btLrcMatched ? "命中" : "无")
                                  << "（下面马上换成假元数据）";
                dumpAudioWidgets("pf-fake 换歌之前");
            });

            /* ② 1.5 秒后喂假元数据，**走 trackChanged 信号**（和真机同一条链路）。
             *   注意：这里必须**再置一次 m_btMode**——应用的 AVRCP 轮询发现"没连接"
             *   会自动切回本地音源（m_btMode=false），而 onBtTrackChanged 开头就有
             *   `if (!m_btMode) return;`。真机场景里手机连着、视频在放，m_btMode 是 true，
             *   所以自检里也要把它按回 true，才能走到和真机同一条分支。 */
            QTimer::singleShot(12000, this, [this, fa, fb, ft]() {
                m_btMode = true;
                cancelConnDebounceForTest();     // 自检期间别让去抖把音源切回去
                if (m_bt)
                    m_bt->stopPolling();         // 同上：别再让真实状态覆盖假元数据
                qInfo().noquote() << "pf-fake: 现在喂假元数据 artist=[" << fa
                                  << "] album=[" << fb << "] title=[" << ft << "]";
                m_bt->setMetaForTest(fa, fb, ft);
                if (m_btToggle)
                    m_btToggle->setText(QStringLiteral("音源：蓝牙"));
                onBtPositionChanged(0, 0);       // 自检帧的进度条/时间标签也按蓝牙链路走
                /* 纯程序化改控件文字有时不会让 weston 立刻呈现新帧（实测抓到过
                 * "日志已变、真屏还是上一帧"）。这里显式 schedule 一次整页重绘，
                 * 并顺带把窗口顶一下（raise），保证有一帧真的提交上去。 */
                if (m_lyricWidget) m_lyricWidget->update();
                this->update();
                this->raise();
                /* 连抓两次：应用的连接去抖可能在这几百毫秒内又把 m_btMode 翻回本地，
                 * 而那会让 onBtTrackChanged 早退。第一次抓"修正后的界面"，
                 * 第二次（若被翻回）留证据说明是自检环境问题，不是修复没生效。 */
                QTimer::singleShot(400, this, [this]() {
                    m_btMode = true;
                    dumpAudioWidgets("pf-fake 换歌之后(1)");
                });
                QTimer::singleShot(1600, this, [this]() {
                    m_btMode = true;
                    dumpAudioWidgets("pf-fake 换歌之后(2)");
                });
            });
        }
    }

    /* buildLayout()/UiAdapter::fit() 之后宽高才是最终值，再摆一次 */
    QTimer::singleShot(300, this, [this]() {
        if (!m_btToggle)
            return;
        m_btToggle->move(width() - m_btToggle->width() - 20,
                         height() - m_btToggle->height() - 20);
        m_btToggle->raise();
        qInfo().noquote() << "bt-media: 音源按钮已放置 geometry="
                          << m_btToggle->geometry() << " 页面=" << this->size();
    });

    /* 进度条松手：蓝牙模式下按差值做相对快进/快退（AVRCP 没有绝对 seek） */
    if (ui && ui->horizontalSlider) {
        connect(ui->horizontalSlider, &QSlider::sliderReleased, this, [this]() {
            if (m_btMode && m_bt)
                m_bt->seekRelative(ui->horizontalSlider->value());
        });
    }

    /* 重排布局：左=选歌列表+底部进度条/按键，右=唱片+下方歌词 */
    if (qEnvironmentVariable("SMARTCAR_DISABLE_NEWLAYOUT") != "1") {
        buildLayout();

        buildLyricWidget();

        updateDiscCover(firstSongCoverPixmap());
    }
    // 排版完成后再做屏幕适配（满屏 + 按屏宽缩放字体）
    UiAdapter::fit(this);


//    ui->listWidget->setStyleSheet(R"(
//        QListWidget::verticalScrollBar {
//            border:none;
//            background:transparent;
//        }
//        QListWidget::item {
//            color:white;
//            height:60px;
//        }
//        QListWidget::verticalScrollBar:handle {
//            border: none;
//            background-color:#aaaaaa;
//            width: 20px; /* 设置滚动条宽度 */
//        }
//        QListWidget::verticalScrollBar:add-page,
//        QListWidget::verticalScrollBar:sub-page {
//            height: 0px; /* 隐藏按钮 */
//            border: none;
//            background: none;
//        }
//    )");






}
void Audio::onBtConnectionChanged(bool connected)
{
    /* 连接状态先去抖再动作：手机侧的 AVRCP 连接状态会短暂跳变
     * （实测：断开时 status 偶尔会报出一瞬间的非 disconnected），
     * 直接跟着切就会"蓝牙/本地"来回翻。这里等状态稳定 1.2 秒再切一次。 */
    m_pendingBtConn = connected;
    if (m_connDebounce)
        m_connDebounce->start();
}

void Audio::cancelConnDebounceForTest()
{
    if (m_connDebounce)
        m_connDebounce->stop();
    qInfo().noquote() << "pf-fake: 已停掉连接去抖定时器（自检期间不再自动切音源）";
}

void Audio::applyBtSourceForConnection(bool connected)
{    /*
     * 自动跟随音源（用户要求：连上蓝牙后进音乐播放器就该是蓝牙界面）。
     * 只在**连接状态稳定变化**时自动切一次；用户手动按「音源」按钮的选择不会被
     * 一直推翻（比如手机连着但他就想听本地曲库）。
     * 另外：本地播放器**正在放歌**时不抢音源（否则一上车连上手机，
     * 正在听的车机本地歌会被直接暂停）。
     *
     * ⚠️ 入参 connected 来自 AVRCP 状态，**放视频（抖音等）时会是 false**。
     * 所以这里一律以 profile 层的 linkConnected() 为准：
     * 只要蓝牙链路还在，就**绝不**自动切回本地音源 ——
     * 切回去会 releaseAlsaForLocal() 把 A2DP 音频通路拆掉，声音直接断。
     */
    const bool link = m_bt ? m_bt->linkConnected() : connected;
    if (link != connected) {
        qInfo().noquote() << "bt-media: AVRCP 报告 connected=" << connected
                          << "，但蓝牙链路 =" << (link ? "在" : "不在")
                          << "（放视频时 AVRCP 常问不到状态，以链路为准）";
    }
    const bool localPlaying = musicPlayer
                              && musicPlayer->state() == QMediaPlayer::PlayingState;
    if (link && !m_btMode) {
        if (localPlaying) {
            qInfo().noquote() << "bt-media: 手机已连接，但本地正在播放，暂不切换音源";
            return;
        }
        qInfo().noquote() << "bt-media: 手机已连接，自动切到蓝牙音源";
        toggleBtMode();
    } else if (!link && m_btMode) {
        qInfo().noquote() << "bt-media: 手机已断开，自动回到本地音源";
        toggleBtMode();
    }
}

void Audio::audio_working()
{
    /* 媒体信号槽连接 */
    connect(musicPlayer,
            SIGNAL(stateChanged(QMediaPlayer::State)),
            this,
            SLOT(mediaPlayerStateChanged(QMediaPlayer::State)));
    connect(mediaPlaylist,
            SIGNAL(currentIndexChanged(int)),
            this,
            SLOT(mediaPlaylistCurrentIndexChanged(int)));
    connect(musicPlayer, SIGNAL(durationChanged(qint64)),
            this,
            SLOT(musicPlayerDurationChanged(qint64)));
    connect(musicPlayer,
            SIGNAL(positionChanged(qint64)),
            this,
            SLOT(mediaPlayerPositionChanged(qint64)));
    connect(musicPlayer, &QMediaPlayer::metaDataAvailableChanged,
            this, &Audio::onMetaDataAvailableChanged);

    // 在播放器的播放进度更新的槽函数中调用updateLyrics()
    connect(musicPlayer, &QMediaPlayer::positionChanged,
            this, &Audio::updateLyrics);
    //
    connect(mediaPlaylist, &QMediaPlaylist::currentIndexChanged,
            this, &Audio::mediaPlaylistCurrentIndexChanged);

    // 关键：进入音乐页时，currentIndexChanged 在“首播”那首歌上不会触发，
    // 所以要主动为当前/第一首歌准备好歌词，否则歌词区永远是空的。
    int idx = mediaPlaylist->currentIndex();
    if (idx < 0 && mediaPlaylist->mediaCount() > 0)
        idx = 0;
    if (idx >= 0) {
        mediaPlaylistCurrentIndexChanged(idx);
    }

    /*
     * 进音乐页的**默认音源**：手机连着就用蓝牙音源，没连就是本地曲库。
     * （用户要求："连上蓝牙后进入音乐播放器应默认先进入蓝牙音源界面"。）
     * 注意这里必须写成"仅在状态不符时才切"，不能无条件 toggle ——
     * audio_working() 每次进页面都会被调用。
     */
    if (m_bt) {
        const bool localPlaying = musicPlayer
                                  && musicPlayer->state() == QMediaPlayer::PlayingState;
        /* ⚠️ 一律用 linkConnected()（profile 层"链路在不在"）判断，
         * 不能用 connected()（AVRCP 状态）—— 放视频时后者会是 false，
         * 那会导致"进一次音乐页就把蓝牙音源切掉、A2DP 通路被拆"。 */
        const bool btLink = m_bt->linkConnected();
        if (localPlaying && !m_btMode) {
            qInfo().noquote() << "bt-media: 进音乐页，本地正在播放，保持本地音源";
        } else if (btLink && !m_btMode) {
            qInfo().noquote() << "bt-media: 手机已连接，进音乐页默认用蓝牙音源";
            toggleBtMode();
        } else if (!btLink && m_btMode) {
            qInfo().noquote() << "bt-media: 手机未连接，进音乐页用本地曲库";
            toggleBtMode();
        } else {
            qInfo().noquote() << "bt-media: 进音乐页，音源保持"
                              << (m_btMode ? "蓝牙" : "本地");
        }
    }
}
void Audio::clearConnections()
{
    // 断开音乐播放器状态改变信号的连接
    disconnect(musicPlayer,
               SIGNAL(stateChanged(QMediaPlayer::State)),
               this,
               SLOT(mediaPlayerStateChanged(QMediaPlayer::State)));

    // 断开媒体播放列表当前索引改变信号的连接
    disconnect(mediaPlaylist,
               SIGNAL(currentIndexChanged(int)),
               this,
               SLOT(mediaPlaylistCurrentIndexChanged(int)));

    // 断开音乐播放器时长改变信号的连接
    disconnect(musicPlayer, SIGNAL(durationChanged(qint64)),
               this,
               SLOT(musicPlayerDurationChanged(qint64)));

    // 断开音乐播放器播放位置改变信号的连接
    disconnect(musicPlayer,
               SIGNAL(positionChanged(qint64)),
               this,
               SLOT(mediaPlayerPositionChanged(qint64)));

    // 断开音乐播放器元数据可用改变信号的连接
    disconnect(musicPlayer, &QMediaPlayer::metaDataAvailableChanged,
               this, &Audio::onMetaDataAvailableChanged);

    // 断开音乐播放器播放位置改变信号与更新歌词槽函数的连接
    disconnect(musicPlayer, &QMediaPlayer::positionChanged,
               this, &Audio::updateLyrics);

    // 断开媒体播放列表当前索引改变信号与另一个同名槽函数的连接（如果有重复连接情况需要注意）
    disconnect(mediaPlaylist, &QMediaPlaylist::currentIndexChanged,
               this, &Audio::mediaPlaylistCurrentIndexChanged);

}
Audio::~Audio()
{
    delete ui;
}


// 扫描歌曲目录，将歌曲添加到媒体列表中，并为有歌词的歌曲保存歌词路径
void Audio::scanSongs()
{
    /*
     * ⚠️ 列表必须**先清空再填**：从蓝牙音源切回本地时，列表里还留着
     * "📱 手机正在播放 / （等待手机上报歌名…）"那两行；而"正在播放"的高亮是按
     * 播放列表 index 调 setCurrentRow() 设的（index=0）—— 高亮就落在蓝牙那行上，
     * 看起来正是"本地播放时亮的是手机正在播放"（用户报的就是这个）。
     *
     * 另外 mediaPlaylist / mediaObjectInfo 只在第一次扫描时填充：
     * 每次进音乐页都重扫一遍会往里重复追加（切几次蓝牙就重复几份），
     * 列表本身每次都按当前目录重建，所以显示永远是干净的。
     */
    if (ui && ui->listWidget)
        ui->listWidget->clear();
    /*
     * 【2026-10-13 修】增量重建的判据必须包含"列表刚被蓝牙占过"。
     *
     * 切到蓝牙音源时 toggleBtMode() 会把 listWidget 换成
     * "📱 手机正在播放 / （等待手机上报歌名…）"两行（那两行**不在** mediaPlaylist 里）。
     * 切回本地时如果只走 fillPlaylist=false 分支（只 addItem 显示名），
     * mediaPlaylist / mediaObjectInfo 都不会被填 —— 而 mediaObjectInfo 一旦为空：
     *   · restoreLocalDisplay() 认为"这一首没有歌词" → 清空歌词区（本地歌没歌词了）；
     *   · 时长/时长条也拿不到 mediaObjectInfo 里的信息。
     * 所以"列表被蓝牙占过"时强制整条重建（先 clear 再全部重新 addMedia，
     * 不会重复追加，因为 mediaPlaylist 自己也清了）。
     */
    const bool fillPlaylist = mediaObjectInfo.isEmpty() || m_listHasBtRows;
    if (fillPlaylist) {
        mediaObjectInfo.clear();
        if (mediaPlaylist)
            mediaPlaylist->clear();
        m_listHasBtRows = false;
    }

    // 获取当前程序路径下的myMusic目录
    QDir dir(QCoreApplication::applicationDirPath()
             + "/myMusic");

    // 获取歌曲目录绝对路径
    QDir dirbsolutePath(dir.absolutePath());
    qDebug() << dirbsolutePath.path() <<endl;

    /* 如果目录存在 */
    if (dirbsolutePath.exists()) {
        /* 定义过滤器 */
        QStringList filter;
        /* 获取所有以.mp3结尾的文件 */
        filter << "*.mp3";
        /* 获取符合条件文件的信息 */
        QFileInfoList files =
                dirbsolutePath.entryInfoList(filter, QDir::Files);

        // 歌词映射：歌曲文件名 -> 歌词文件完整路径
        QHash<QString, QString> songToLyricsMap;
        // 添加歌词文件的搜索
        filter.clear(); // 清空过滤器
        filter << "*.lrc";
        QFileInfoList lrcFiles = dirbsolutePath.entryInfoList(filter, QDir::Files);
        // 将去除扩展名的文件以及文件绝对路径插入songToLyricsMap
        // 注意：这里 key 要去掉首尾空格 —— 歌词文件名常写成 "歌名 .lrc"（.lrc 前多个空格），
        // 直接用 completeBaseName() 会得到 "歌名 " 与 mp3 的 "歌名" 对不上，导致歌词永远匹配不到。
        for (const QFileInfo &lrcFileInfo : lrcFiles) {
            QString baseName = lrcFileInfo.completeBaseName().trimmed();
            songToLyricsMap.insert(baseName, lrcFileInfo.absoluteFilePath());
        }

        /* 遍历 */
        for (int i = 0; i < files.count(); i++) {
            MediaObjectInfo info;
            QString baseName = files.at(i).completeBaseName().trimmed(); // 去掉扩展名 + 首尾空格
            QString fileName = baseName;
            /* 列表那一行显示成两行："歌名 - 歌手" 换行 "歌手"。
             * （原来写的是 fileName.split("-").at(1)，文件名里**没有** '-' 时
             *   会越界访问直接把程序打崩，这里补上判断。） */
            const QStringList parts = fileName.split(QLatin1Char('-'));
            info.fileName = (parts.size() > 1)
                                ? (fileName + QLatin1Char('\n') + parts.at(1).trimmed())
                                : fileName;
            // 获取对应索引下的文件的绝对路径
            info.filePath = QString::fromUtf8(files.at(i)
                                              .filePath()
                                              .toUtf8()
                                              .data());

            // 获取歌词路径
            QString lyricsPath = songToLyricsMap.value(baseName);
            if (!fillPlaylist) {
                /* 播放列表已经建好了（这次只是回到音乐页），
                 * 只重建显示用的列表，不要再往播放列表里重复追加。 */
                ui->listWidget->addItem(info.fileName);
                continue;
            }
            /* 媒体列表添加歌曲 */
            if (mediaPlaylist->addMedia(
                        QUrl::fromLocalFile(info.filePath))) {
                // 如果歌词文件存在，则保存其路径
                if (!lyricsPath.isEmpty()) {
                    info.lyricsPath = lyricsPath;
                }
                /* 添加到容器数组里储存 */
                mediaObjectInfo.append(info);
                /* 添加歌曲名字至列表 */
                ui->listWidget->addItem(info.fileName);
            } else {
                qDebug()<<
                           mediaPlaylist->errorString()
                           .toUtf8().data()
                        << endl;
                qDebug()<< "  Error number:"
                         << mediaPlaylist->error()
                         << endl;
            }
        }
    }
    else {
        /*
         * ⚠️ 这句以前是 qDebug()（默认不落盘），结果"音乐页歌单空白"排查了整晚 ——
         * 真因就是这个目录不在（刷机把 /userdata/smart_car/myMusic 冲掉了），
         * 但日志里只看得到一句不显眼的 "dir is no exists"。
         * 现在改用 qWarning() 并把**完整路径**与后果一起打出来，一眼可定位；
         * 曲库不随二进制部署（见 vmctl/deploy_new_ui.py 的媒体推送步骤）。
         */
        qWarning().noquote()
                << "音乐页：[曲库目录不存在] 路径 =" << dirbsolutePath.absolutePath()
                << "  ⇒ 歌单为空。请在 PC 上执行 vmctl/deploy_media（或手动"
                   " `adb push <repo>/myMusic/. /userdata/smart_car/myMusic/`）后重启应用。";
    }

}
// 根据播放进度更新歌词显示
void Audio::updateLyrics(qint64 currentTime) {
    if (m_lyrics.isEmpty())
        return;
    // 找到最后一句 startTime <= currentTime 的
    int idx = -1;
    for (int i = 0; i < m_lyrics.size(); ++i) {
        if (m_lyrics.at(i).time <= currentTime)
            idx = i;
        else
            break;
    }
    if (idx != m_lyricIndex)
        showLyricAt(idx);
}

// 解析整个 lrc 到内存（一次解析，播放时只做二分/线性查找）
void Audio::parseLyrics(const QString &path)
{
    m_lyrics.clear();
    m_lyricIndex = -1;
    if (path.isEmpty())
        return;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    QRegExp timeRegex("\\[(\\d+):(\\d+\\.\\d+)\\]");
    while (!stream.atEnd()) {
        QString line = stream.readLine();
        if (timeRegex.indexIn(line) == -1)
            continue;
        LyricLine ll;
        ll.time = timeRegex.cap(1).toInt() * 60000 + (qint64)(timeRegex.cap(2).toFloat() * 1000);
        ll.text = line.remove(timeRegex).trimmed();
        if (!ll.text.isEmpty())
            m_lyrics.append(ll);
    }
    file.close();
    qInfo() << "歌词已解析:" << path << m_lyrics.size() << "句";
}

// 重排整页布局：在 1000x600 设计画布上用绝对坐标摆放，之后由 UiAdapter 统一缩放。
// 目标：左 = 返回栏 + 选歌列表 + 底部进度条/按键；右 = 唱片(叠唱臂) + 下方歌词。
void Audio::buildLayout()
{
    // 1) 把 .ui 里那个带布局的容器整体隐藏，改用自建画布（绝对定位，结果可预期）
    if (QWidget *old = ui->layoutWidget)
        old->hide();

    QWidget *canvas = new QWidget(this);
    canvas->setObjectName("audioCanvas");
    canvas->setGeometry(0, 0, 1000, 600);
    canvas->setStyleSheet("QWidget#audioCanvas { background: transparent; }");
    canvas->show();

    // 需要摆的控件都挂到画布上
    QWidget *items[] = {
        ui->pushButton, ui->label_5, ui->listWidget, ui->label_2, ui->label_3,
        ui->horizontalSlider, ui->btn_loop, ui->btn_pre, ui->btn_play, ui->btn_next,
        ui->pushButton_2, ui->verticalSlider, ui->widget_3, ui->widget_2, ui->label_4,
        ui->widget      // .ui 里那个原来显示专辑封面的方块（黑底），必须一起收进来
    };
    for (QWidget *w : items) {
        if (!w) continue;
        w->setParent(canvas);
        w->setMinimumSize(0, 0);
        w->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
        w->show();
    }

    auto place = [](QWidget *w, int x, int y, int ww, int hh) {
        if (w) w->setGeometry(x, y, ww, hh);
    };

    /* ---------------- 左列 ---------------- */
    const int leftX = 20, leftW = 520;

    place(ui->pushButton, leftX, 8, 64, 64);             // 返回（原来 40x40，手指点不到）
    place(ui->label_5, leftX + 78, 20, 300, 36);         // 标题

    place(ui->listWidget, leftX, 82, leftW, 316);        // 歌单

    // 进度条 + 时间：控件给到 46px 高（槽只有 14px，多出来的是手指容错区）
    CommonHelper::makeSliderThick(ui->horizontalSlider, 14);
    place(ui->label_2, leftX, 406, 60, 30);
    place(ui->horizontalSlider, leftX + 66, 400, leftW - 132, 46);
    place(ui->label_3, leftX + leftW - 60, 406, 60, 30);

    // 按键行（居中于左列）
    const int bLoop = 56, bBig = 84, bFav = 56, gap = 22;
    int total = bLoop + gap + bBig + gap + bBig + gap + bBig + gap + bFav;
    int bx = leftX + (leftW - total) / 2;
    const int byBig = 452, bySmall = 466;
    place(ui->btn_loop, bx, bySmall, bLoop, bLoop);          bx += bLoop + gap;
    place(ui->btn_pre,  bx, byBig,   bBig,  bBig);           bx += bBig + gap;
    place(ui->btn_play, bx, byBig,   bBig,  bBig);           bx += bBig + gap;
    place(ui->btn_next, bx, byBig,   bBig,  bBig);           bx += bBig + gap;
    place(ui->pushButton_2, bx, bySmall, bFav, bFav);

    /* ---------------- 右列 ---------------- */
    const int rightX = 560, rightW = 420;

    // 音量条：原来 26px 宽，现在 48px（视觉槽 14px 居中）
    CommonHelper::makeSliderThick(ui->verticalSlider, 14);
    place(ui->verticalSlider, rightX + rightW - 46, 40, 48, 200);   // 音量

    // 唱片（widget_3 内是旋转封面）
    const int discSize = 240;
    const int discX = rightX + (rightW - discSize) / 2;
    place(ui->widget_3, discX, 30, discSize, discSize);
    // 容器背景清透明，否则会在圆形封面下露出黑框
    ui->widget_3->setStyleSheet("background: transparent; border: none;");
    ui->widget_3->setAutoFillBackground(false);
    // 里面的旋转封面控件要跟外框一样大，否则会被裁成方形小块
    if (discRotator) {
        discRotator->setParent(ui->widget_3);
        discRotator->setGeometry(0, 0, discSize, discSize);
        discRotator->lower();
        discRotator->show();
    }
    if (ui->widget_3->layout())
        ui->widget_3->layout()->setEnabled(false);   // 不让 .ui 里的布局再改尺寸

    // 唱臂叠在唱片右上
    place(ui->widget_2, discX + discSize - 90, 8, 150, 160);
    ui->widget_2->raise();

    // 歌词区（三行歌词控件将插入这里）
    place(ui->label_4, rightX + 10, 300, rightW - 20, 200);

    // .ui 里原有的专辑封面方块：不再需要（唱片本身就是封面），收起来免得露黑底
    if (ui->widget) {
        ui->widget->setStyleSheet("background: transparent; border: none;");
        place(ui->widget, 0, 0, 1, 1);
        ui->widget->hide();
    }


}

void Audio::buildLyricWidget()
{
    if (m_lyricWidget)
        return;
    m_lyricWidget = new QWidget(ui->label_4->parentWidget());
    m_lyricWidget->setObjectName("lyricBox");
    m_lyricWidget->setStyleSheet("QWidget#lyricBox { background: transparent; border: none; }");

    QVBoxLayout *v = new QVBoxLayout(m_lyricWidget);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(4);

    auto makeLabel = [&](int px, const QColor &c, bool bold) {
        QLabel *l = new QLabel(m_lyricWidget);
        l->setAlignment(Qt::AlignCenter);
        l->setWordWrap(false);
        QFont f = l->font();
        f.setPixelSize(px);
        f.setBold(bold);
        l->setFont(f);
        l->setStyleSheet(QString("color: rgba(%1,%2,%3,%4); background: transparent; border: none;")
                             .arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha()));
        return l;
    };
    m_lyricPrev = makeLabel(26, QColor(255, 255, 255, 110), false);
    m_lyricCur  = makeLabel(36, QColor(120, 200, 255, 255), true);
    m_lyricNext = makeLabel(26, QColor(255, 255, 255, 110), false);
    v->addWidget(m_lyricPrev);
    v->addWidget(m_lyricCur);
    v->addWidget(m_lyricNext);
    // 先在布局里占好位置（父页面此时还没显示，不会提前露出来）
    m_lyricWidget->setVisible(true);

    // 绝对定位到 label_4 的位置（画布方案下不用布局管理器，避免尺寸被算成 0）
    if (ui->label_4) {
        m_lyricWidget->setParent(ui->label_4->parentWidget());
        m_lyricWidget->setGeometry(ui->label_4->geometry());
        ui->label_4->hide();
    }
    m_lyricWidget->raise();
    m_lyricWidget->show();
    // 前奏提示（这样即使还没播放也能看到歌词区在哪）
    if (m_lyricPrev) m_lyricPrev->setText(QString());
    if (m_lyricCur)  m_lyricCur->setText(QStringLiteral("暂无歌词"));
    if (m_lyricNext) m_lyricNext->setText(QString());
}

// 显示第 index 句歌词（带上一句/下一句）
void Audio::showLyricAt(int index)
{
    if (!m_lyricWidget)
        return;
    m_lyricIndex = index;

    if (index < 0) {   // 前奏
        m_lyricPrev->setText(QString());
        m_lyricCur->setText(m_lyrics.isEmpty()
                                ? QStringLiteral("暂无歌词")
                                : QStringLiteral("前奏…"));
        m_lyricNext->setText(m_lyrics.isEmpty() ? QString() : m_lyrics.first().text);
        return;
    }

    const QString cur = m_lyrics.at(index).text;
    const QString prev = (index > 0) ? m_lyrics.at(index - 1).text : QString();
    const QString next = (index + 1 < m_lyrics.size()) ? m_lyrics.at(index + 1).text : QString();

    m_lyricPrev->setText(prev);
    m_lyricCur->setText(cur);
    m_lyricNext->setText(next);
}

// 从音频文件读内嵌封面（用 Qt 自带的多媒体元数据）
QPixmap Audio::embeddedCover(const QString &audioPath)
{
    QPixmap pm;
    if (audioPath.isEmpty())
        return pm;
    /* 查找规则（2026-10-18 修「海报串图」时定下，顺序按用户要求：
     *   ① 音频**内嵌**封面：WAV/MP3 的 ID3v2 APIC、FLAC 的 PICTURE /
     *      VorbisComment METADATA_BLOCK_PICTURE —— 统一由 QMediaPlayer 的
     *      CoverArtImage 暴露出来（板上实测：FLAC 里 base64 的 METADATA_BLOCK_PICTURE
     *      也能读到）；
     *   ② 没有内嵌封面，就看**同名图片**：myMusic/<不含扩展名的基名>.jpg|jpeg|png|bmp
     *      （例如「李荣浩-不遗憾.mp3」配「李荣浩-不遗憾.jpg」；基名按
     *      QFileInfo::completeBaseName() 取，所以「等你下课 (with 杨瑞代) - 周杰伦.lrc」
     *      这种带空格的文件名也照样对得上）；
     *   ③ 两者都没有 ⇒ 返回**空** QPixmap（调用方必须换成黑胶占位，绝不留上一张）。
     * 注意顺序：内嵌优先 —— 用户明确要求"每首歌用自己的封面"，同名图只是兜底手段。 */
    QMediaPlayer probe;
    probe.setMedia(QUrl::fromLocalFile(audioPath));
    // QMediaPlayer 需要异步加载，这里给一小段事件循环
    QElapsedTimer t; t.start();
    while (!probe.isMetaDataAvailable() && t.elapsed() < 1500) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    if (probe.isMetaDataAvailable()) {
        const QVariant v = probe.metaData(QMediaMetaData::CoverArtImage);
        if (v.isValid()) {
            const QImage img = v.value<QImage>();
            if (!img.isNull())
                pm = QPixmap::fromImage(img);
        }
        if (pm.isNull()) {
            const QUrl u = probe.metaData(QMediaMetaData::CoverArtUrlLarge).value<QUrl>();
            if (!u.isEmpty())
                pm.load(u.toString());
        }
    }
    if (pm.isNull()) {
        /* ② 兜底：同名图片 */
        const QFileInfo fi(audioPath);
        const QStringList exts = {"jpg", "jpeg", "png", "bmp"};
        for (const QString &e : exts) {
            const QString cand = fi.absolutePath() + "/" + fi.completeBaseName() + "." + e;
            if (!QFile::exists(cand))
                continue;
            QPixmap cand_Pm;
            if (cand_Pm.load(cand) && !cand_Pm.isNull()) {
                pm = cand_Pm;
                break;
            }
        }
    }
    return pm;
}

/* 当前该显示的那首歌的封面（函数名沿用旧名，Audio.h / 3 个调用点不用改）。
 *
 * ⚠️ 2026-10-18 修「海报串图」：原来这里是"遍历整个曲库，返回**第一张非空**封面" ——
 *    那是**借别人的封面**：曲库里 2 首歌、只有《等你下课》有封面时，
 *    《李荣浩-不遗憾》也会拿去显示（用户看到的"串图"就是这么来的）。
 *    现在改成"只认当前播放位置那一首"，取不到就返回黑胶占位，绝不借用。 */
QPixmap Audio::firstSongCoverPixmap()
{
    int idx = 0;
    if (mediaPlaylist && mediaPlaylist->currentIndex() >= 0)
        idx = mediaPlaylist->currentIndex();
    if (idx >= 0 && idx < mediaObjectInfo.size()) {
        const QPixmap pm = embeddedCover(mediaObjectInfo.at(idx).filePath);
        if (!pm.isNull())
            return pm;
    }
    return smartcarVinylPlaceholderCover(240);
}

// 把封面画到唱片（旋转控件）上：裁成圆形，避免露出方形边框
void Audio::updateDiscCover(const QPixmap &cover)
{
    if (!discRotator)
        return;

    /* ⚠️ 这里是"串图"的唯一收口：任何一次调用都必须**画点什么**。
     * 原来 cover.isNull() 直接 return —— 等于"新歌没有封面就把上一首的留着"，
     * 用户看到的就是别的歌的海报。现在空封面一律画黑胶唱片占位。 */
    const QPixmap src = cover.isNull() ? smartcarVinylPlaceholderCover(240) : cover;
    QPixmap scaled = src.scaled(240, 240, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    if (scaled.width() != scaled.height()) {
        const int side = qMin(scaled.width(), scaled.height());
        scaled = scaled.copy((scaled.width() - side) / 2, (scaled.height() - side) / 2, side, side);
    }
    // 圆形裁剪（原来用的 disc.png 自带圆形透明边缘，换成封面后必须自己裁）
    const int side = qMin(scaled.width(), scaled.height());
    QPixmap round(side, side);
    round.fill(Qt::transparent);
    QPainter p(&round);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    QPainterPath path;
    path.addEllipse(0, 0, side, side);
    p.setClipPath(path);
    p.drawPixmap(0, 0, scaled);
    p.end();

    discRotator->setPixmap(round);
    discRotator->update();

}
// 初始化媒体播放器
void Audio::mediaPlayerInit()
{
    musicPlayer = new QMediaPlayer(this);
    mediaPlaylist = new QMediaPlaylist(this);
    /* 确保列表是空的 */
    mediaPlaylist->clear();
    /* 设置音乐播放器的列表为mediaPlaylist */
    musicPlayer->setPlaylist(mediaPlaylist);
    /* 设置播放模式，Loop是列循环 */
    mediaPlaylist->setPlaybackMode(QMediaPlaylist::Loop);
}

// 设置专辑封面
void Audio::setAlbumArt(QPixmap pixmap)
{
    int cornerRadius = 86;  // 圆角大小
    // 图像缩放 将输入的pixmap缩放到 172 * 172 大小，同时保持宽高比，并使用平滑变换
    QPixmap Pixmap2 = pixmap.scaled(172, 172, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPixmap roundedPixmap(Pixmap2.size()); // 创建一个跟Pixmap2等大的对象
    roundedPixmap.fill(Qt::transparent); // 颜色填充为透明
    // 将roundedPixmap传入painter对象 以后的操作都是对roundedPixmap操作
    QPainter painter(&roundedPixmap);
    // 开启抗锯齿和平滑图像变换
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.setBrush(QBrush(Pixmap2));
    painter.setPen(Qt::NoPen); // 不绘制边框
    QPainterPath path; // 定义绘制路径
    // 将Pixmap2.rect()绘制成带有圆角的矩形区域添加到path中
    path.addRoundedRect(Pixmap2.rect(), cornerRadius, cornerRadius);
    painter.setClipPath(path); // 设置裁剪路径
    painter.drawPixmap(Pixmap2.rect(), Pixmap2); // 将 Pixmap2 按照 Pixmap2.rect() 所确定的位置和大小进行绘制到 roundedPixmap
    painter.end();
    // 获取当前程序的路径
    QDir dir(QCoreApplication::applicationDirPath());
    QString filePath = dir.filePath("pixmapTmp.jpg");  // 正确拼接文件的完整路径
    bool saved = roundedPixmap.save(filePath);
    // 设置widget属性
    if(saved) {
        ui->widget->setStyleSheet("QWidget#widget{\n"
                                  "border:none;\n"
                                  "border-radius:120px;\n"
                                  "image: url(" + filePath + ");}");
    }
    else {
        ui->widget->setStyleSheet("QWidget#widget{\n"
                                  "border:none;\n"
                                  "border-radius:120px;\n"
                                  "image: url(:/images/fengshen.jpg);}");
    }
}
// 播放/暂停按钮点击事件处理
void Audio::on_btn_play_clicked(bool checked)
{
    /* 蓝牙音源：这个按钮控制的是**手机**（AVRCP CT），不是本地播放器。
     * ⚠️ 用按钮自己的选中状态决定"要播放还是要暂停"，**不要**去 toggle 内部
     * 那份可能已经过期的状态 —— 否则会出现"按播放键结果把歌暂停了"这种事
     * （手机那边状态一变，本地这份要等下一次轮询才知道）。 */
    if (m_btMode) {
        if (m_bt) {
            if (checked) m_bt->play();
            else         m_bt->pause();
        }
        return;
    }
    if(checked) {
        animation->setStartValue(0); // 开始角度
        animation->setEndValue(30); // 结束角度
        animation->start(); // 开始动画
        animation2->start(); // 开始动画
        if(animation3 != nullptr)
            animation3->start();
    }
    else {
        animation->setStartValue(30); // 开始角度为30
        animation->setEndValue(0); // 结束角度为0
        //    animation->setLoopCount(-1); // 设置动画无限循环
        animation->start(); // 开始动画
        animation2->stop(); // 停止动画(disc)
        if(animation3 != nullptr)
            animation3->stop();
    }

    int state = musicPlayer->state();

    switch (state) {
    case QMediaPlayer::StoppedState:
        /* 媒体播放 */
        musicPlayer->play();
        break;
    case QMediaPlayer::PlayingState:
        /* 媒体暂停 */
        musicPlayer->pause();
        break;
    case QMediaPlayer::PausedState:
        musicPlayer->play();
        break;
    }
}
// 上一曲按钮点击事件处理
void Audio::on_btn_pre_clicked()
{
    /* 蓝牙音源：切手机上一首 */
    if (m_btMode) {
        if (m_bt) m_bt->previous();
        return;
    }
    musicPlayer->stop();
    int count = mediaPlaylist->mediaCount();
    if (0 == count)
        return;

    /* 列表上一个 */
    mediaPlaylist->previous();
//    musicPlayer->play();
    emit ui->btn_play->clicked(true);

}
// 下一曲按钮点击事件处理
void Audio::on_btn_next_clicked()
{
    /* 蓝牙音源：切手机下一首 */
    if (m_btMode) {
        if (m_bt) m_bt->next();
        return;
    }
    musicPlayer->stop();
    int count = mediaPlaylist->mediaCount();
    if (0 == count) {
        qDebug() << "mediaPlaylist->mediaCount = 0" << endl;
        return;
    }

    /* 列表下一个 */
    mediaPlaylist->next();
//    musicPlayer->play();
    emit ui->btn_play->clicked(true);

}
// 循环模式按钮点击事件处理
void Audio::on_btn_loop_clicked(bool checked)
{
    if(checked) {
        // 当前项循环
        mediaPlaylist->setPlaybackMode(QMediaPlaylist::CurrentItemInLoop);
    }
    else {
        // 列表循环
        mediaPlaylist->setPlaybackMode(QMediaPlaylist::Loop);
    }
}
// 媒体播放器状态改变时的槽函数
void Audio::mediaPlayerStateChanged(QMediaPlayer::State state)
{
    /* 蓝牙音源下，播放键的状态由**手机**决定（见 onBtStateChanged）；
     * 本地播放器只是被暂停了，它再回写就会把手机的播放状态覆盖掉。 */
    if (m_btMode)
        return;
    //click和check相互独立
    //click用于实现槽函数；check用于改变图片
    switch (state) {
    case QMediaPlayer::StoppedState: // 停止
        ui->btn_play->setChecked(false);
        break;

    case QMediaPlayer::PlayingState: // 播放
        ui->btn_play->setChecked(true);
        break;

    case QMediaPlayer::PausedState: // 暂停
        ui->btn_play->setChecked(false);
        break;
    }
}
// 媒体播放列表当前索引改变时的槽函数
void Audio::mediaPlaylistCurrentIndexChanged(int index)
{
    /* 蓝牙音源下，歌词/封面/列表选中都归手机那条链路管（onBtTrackChanged）。
     * ⚠️ 这个槽在启动时一定会被触发一次（本地播放列表加载第一首），
     *    而且 QMediaPlayer 读 31MB 的 mp3 元数据是**异步**的，往往在
     *    我们画完蓝牙占位封面之后才回来 —— 曾经因此把唱盘又换成本地那首歌的封面。 */
    if (m_btMode)
        return;
    if (-1 == index)
        return;

    /* 设置列表正在播放的项 */
    ui->listWidget->setCurrentRow(index);

    // 检查索引有效性
    if (index >= 0 && index < mediaObjectInfo.size()) {
        // 更新当前歌词路径，并重新解析歌词（三行显示用）
        currentLyricsPath = mediaObjectInfo.at(index).lyricsPath;
        parseLyrics(currentLyricsPath);
        showLyricAt(-1);
        // 唱片封面跟当前歌曲走：有封面用**这首自己的**，没有就画黑胶唱片占位
        // （2026-10-18 修：原来是 `if (!cover.isNull()) updateDiscCover(cover);`
        //   ⇒ 没有封面的歌继续挂着上一首的海报，实测就是"不遗憾 显示 等你下课 的封面"）
        const QPixmap cover = embeddedCover(mediaObjectInfo.at(index).filePath);
        updateDiscCover(cover);          // 空封面由 updateDiscCover 内部换黑胶占位
        qInfo().noquote() << "cover: 换歌 index=" << index
                          << " 曲目=[" << mediaObjectInfo.at(index).fileName.simplified() << "]"
                          << (cover.isNull() ? "封面=无 -> 黑胶占位"
                                             : "封面=内嵌/同名图(自带)");
    } else {
        // 如果索引无效，清除当前歌词路径
        currentLyricsPath.clear();
        parseLyrics(QString());
        showLyricAt(-1);
    }
}
// 媒体播放时长改变时的槽函数
void Audio::musicPlayerDurationChanged(qint64 duration)
{
    /* 蓝牙音源下进度条的量纲和值都来自手机（毫秒），本地播放器这条
     * "秒"量纲的回写会把范围改成 0..265 之类，直接毁掉进度条。 */
    if (m_btMode)
        return;
    // 设置歌曲播放时间范围 duration / 1000 ：毫秒转换为秒
    ui->horizontalSlider->setRange(0, duration / 1000);
    int second  = duration / 1000; // 获取秒
    int minute = second / 60; // 获取分
    second %= 60;

    QString mediaDuration;
    mediaDuration.clear();
    // 分、秒小于10则前面补0，否则正常显示
    if (minute >= 10)
        mediaDuration = QString::number(minute, 10);
    else
        mediaDuration = "0" + QString::number(minute, 10);

    if (second >= 10)
        mediaDuration = mediaDuration
                + ":" + QString::number(second, 10);
    else
        mediaDuration = mediaDuration
                + ":0" + QString::number(second, 10);

    /* 显示媒体总长度时间 */
    ui->label_3->setText(mediaDuration);
}
// 媒体播放位置改变时的槽函数 拖动进度条
void Audio::mediaPlayerPositionChanged(qint64 position)
{
    /* 蓝牙音源下当前时间是手机给的（见 onBtPositionChanged），本地播放器
     * 已经暂停，但暂停那一刻/加载完成时还会回写一次，把 00:00 盖上去。 */
    if (m_btMode)
        return;
    // 判断滑动条是否被按下
    if (!ui->horizontalSlider->isSliderDown())
        ui->horizontalSlider->setValue(position/1000);

    int second  = position / 1000; // 获取总的秒数
    int minute = second / 60; // 算出分
    second %= 60; // 得到剩余的秒

    QString mediaPosition;
    mediaPosition.clear();

    if (minute >= 10)
        mediaPosition = QString::number(minute, 10);
    else
        mediaPosition = "0" + QString::number(minute, 10);

    if (second >= 10)
        mediaPosition = mediaPosition
                + ":" + QString::number(second, 10);
    else
        mediaPosition = mediaPosition
                + ":0" + QString::number(second, 10);

    /* 显示现在播放的时间 */
    ui->label_2->setText(mediaPosition);
}
// 媒体元数据可用状态改变时的槽函数
void Audio::onMetaDataAvailableChanged(bool available) {
    /* 蓝牙音源下唱盘归 onBtTrackChanged 管（本地有同一首歌就用本地封面，
     * 否则占位盘）。本地播放器的元数据是异步回来的，不拦就会覆盖蓝牙封面。 */
    if (m_btMode)
        return;
    if (available) {
        // 获取元数据的图片
        QVariant coverArtVariant = musicPlayer->metaData(QMediaMetaData::CoverArtImage);
        if (coverArtVariant.isValid()) {
            QImage coverArtImage = qvariant_cast<QImage>(coverArtVariant); // 转换成QImage后保存
            if (!coverArtImage.isNull()) {
//                coverArtImage.save("/home/book/Desktop/Program/qt/musicMedia/DescoverArtImage.jpg");
                setAlbumArt(QPixmap::fromImage(coverArtImage));
            } else {
                // 如果没有封面图像，则可以设置一个默认图片
                setAlbumArt(QPixmap(":/images/fengshen.jpg"));
            }
        }
        else
            setAlbumArt(QPixmap(":/images/fengshen.jpg"));
    }
}

// 列表项点击事件处理
void Audio::on_listWidget_itemClicked(QListWidgetItem *item)
{
    /* 蓝牙音源下列表里是"手机正在播"的提示行，点它不该控制任何东西
     * （它下面会 emit clicked(true)，等于给手机发一条 play）。 */
    if (m_btMode)
        return;
    musicPlayer->stop();
    mediaPlaylist->setCurrentIndex(ui->listWidget->row(item));
//    musicPlayer->play();
    emit ui->btn_play->clicked(true);

}
// 水平滑块释放事件处理
void Audio::on_horizontalSlider_sliderReleased()
{
    /* 蓝牙音源下进度条不接受拖拽（AVRCP 没有绝对 seek，用 ⏪/⏩ 代替），
     * 这条是本地播放器的定位逻辑，别让它对手机生效。 */
    if (m_btMode)
        return;
    /* 当进度条被拖动释放后设置媒体播放的位置 */
    musicPlayer->setPosition(ui->horizontalSlider->value() * 1000);
}
// 关闭按钮点击事件处理
void Audio::on_pushButton_clicked()
{
    clearConnections();
    // 设置音乐播放器状态为停止状态
    musicPlayer->stop();
    // 播放/暂停按钮设置为未选中状态
    ui->btn_play->setChecked(false);

    // 重置播放列表索引为 -1
    mediaPlaylist->setCurrentIndex(-1);
    // 清除列表中的当前选中项
    ui->listWidget->setCurrentRow(-1);

    // 将水平滑块设置到初始位置（值为0）
    ui->horizontalSlider->setValue(0);
    // 清空显示的播放时间等信息
    ui->label_2->setText("");
    ui->label_3->setText("");

    // 设置专辑封面为初始默认图片
    setAlbumArt(QPixmap(":/images/fengshen.jpg"));

    // 重置当前歌词路径为空
    currentLyricsPath.clear();
    // 清除歌词显示（三行）
    m_lyrics.clear();
    m_lyricIndex = -1;
    if (m_lyricPrev) m_lyricPrev->setText(QString());
    if (m_lyricCur)  m_lyricCur->setText(QString());
    if (m_lyricNext) m_lyricNext->setText(QString());
    emit this->back();
}
// 垂直滑块释放事件处理
void Audio::on_verticalSlider_sliderReleased()
{
    if (m_btMode) {
        /* 蓝牙音源：声音是"手机 → 车机喇叭"这条链路上的，调本地播放器音量
         * （musicPlayer->setVolume）对耳朵没有任何影响 —— 必须调手机音量
         * （AVRCP 绝对音量，见 bt-ctl bt-volume）。 */
        if (m_bt)
            m_bt->setVolume(ui->verticalSlider->value());
        return;
    }
    musicPlayer->setVolume(ui->verticalSlider->value());
}
