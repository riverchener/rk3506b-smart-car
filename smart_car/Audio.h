#ifndef Audio_H
#define Audio_H

#include <QWidget>
#include <QMediaPlayer>
#include <QMediaPlaylist>
#include <QMediaMetaData>
#include <QFileInfoList>
#include <QDir>
#include <QPainter>
#include <QLabel>
#include <QPushButton>
#include <QListWidgetItem>
#include "rotatablewidget.h"
#include "btmedia.h"

namespace Ui {
class Audio;
}

/* 媒体信息结构体 */
struct MediaObjectInfo {
    // 用于保存歌曲文件名
    QString fileName;
    // 用于保存歌曲文件路径
    QString filePath;
    // 歌词文件路径
    QString lyricsPath;
};

class Audio : public QWidget
{
    Q_OBJECT

public:
    explicit Audio(QWidget *parent = 0);
    ~Audio();

    void audio_working();
    void clearConnections();
signals:
    void back();

private slots:
    // 播放
    void on_btn_play_clicked(bool checked);
    // 上一曲
    void on_btn_pre_clicked();
    // 下一曲
    void on_btn_next_clicked();
    // 循环播放
    void on_btn_loop_clicked(bool checked);
    /* 媒体状态改变 */
    void mediaPlayerStateChanged(QMediaPlayer::State);
    /* 媒体列表项改变 */
    void mediaPlaylistCurrentIndexChanged(int);
    /* 媒体总长度改变 */
    void musicPlayerDurationChanged(qint64);
    /* 媒体播放位置改变 */
    void mediaPlayerPositionChanged(qint64);

    void onMetaDataAvailableChanged(bool available);
    // 歌词更新
    void updateLyrics(qint64 currentTime);
    // 列表项被点击
    void on_listWidget_itemClicked(QListWidgetItem *item);
    // 水平滑块
    void on_horizontalSlider_sliderReleased();

    void on_pushButton_clicked();
    // 垂直滑块
    void on_verticalSlider_sliderReleased();

    /* ---------- 蓝牙音源（AVRCP CT：车机控制手机播放） ---------- */
    // 「本地 / 蓝牙」音源切换
    void toggleBtMode();
    // 手机上报的歌名/歌手/专辑变化
    void onBtTrackChanged(const QString &title, const QString &artist, const QString &album);
    // 手机播放位置/总时长（毫秒）—— 驱动同一条进度条与歌词高亮
    void onBtPositionChanged(int positionMs, int durationMs);
    // 手机播放状态（playing/paused/stopped/disconnected）
    void onBtStateChanged(const QString &state);
    // 手机连接状态变化：自动切音源（连上→蓝牙，断开→本地）
    void onBtConnectionChanged(bool connected);
    /* 去抖后真正执行音源切换 */
    void applyBtSourceForConnection(bool connected);
    /* 【仅自检】停掉"连接状态去抖"定时器 —— 供 SMARTCAR_BT_FAKE_META 使用：
     * 自检时没有真手机，AVRCP 会报断开并把音源自动切回本地，
     * 那条自动切换会把刚摆好的界面冲掉，导致看不出修复效果。 */
    void cancelConnDebounceForTest();

    /*
     * 控制条重排：蓝牙模式下把 ⟲ ⏮ ⏪ ▶ ⏩ ⏭ ♡ 摆成一排（同尺寸同间隔，
     * 放不下就整排一起缩小）；entering=false 时恢复进蓝牙前的原始几何。
     * 只在进入/退出蓝牙时调用 —— 本地页面保持原有排版不变。
     */
    void layoutBtControlRow(bool entering);
    /* 蓝牙音量：把界面滑块对齐到手机当前音量（进蓝牙时调一次） */
    void syncBtVolume();
    /* 诊断：打印音乐页所有 label 与列表每行的文字/几何（排查"这段字是谁画的"） */
    void dumpAudioWidgets(const char *tag);
    /* 切回本地音源时，把进度条/时间/歌词/播放键/高亮整套交回本地播放器 */
    void restoreLocalDisplay();
    /* 跑一条 bt-ctl 子命令（异步） */
    void runBtCtl(const QStringList &args, const QString &tag);
    /* 让出声卡给本地播放（PA 独占 hw:0,0，不释放本地播放器打不开设备） */
    void releaseAlsaForLocal();

private:
    Ui::Audio *ui;
    /* 蓝牙音源：轮询 bt-ctl bt-media status / 下发 MediaControl1 命令 */
    BtMedia *m_bt = nullptr;
    bool m_btMode = false;              // true = 界面正在显示手机播放的内容
    QPushButton *m_btToggle = nullptr;  // 「本地/蓝牙」切换按钮
    QPushButton *m_ffBtn = nullptr;     // 蓝牙模式下的快进（进度条禁止拖拽，改用按键）
    QPushButton *m_rewBtn = nullptr;    // 蓝牙模式下的快退
    QTimer *m_volDebounce = nullptr;    // 音量滑块防抖（拖动时别每像素都 fork 一个 bt-ctl）
    QTimer *m_connDebounce = nullptr;   // 连接状态去抖（避免音源来回翻）
    bool    m_pendingBtConn = false;    // 去抖后要应用的连接状态
    /* 蓝牙音源的"通路保活"：release 会把 PA 的 sink 用户级挂起（黏性，PA 自己不会
     * 恢复），所以进了蓝牙要周期性确认挂起已解除、蓝牙环路还在。 */
    QTimer *m_btKeepAlive = nullptr;
    /* 切到蓝牙时会 stop() 本地播放器（为了释放声卡），这里记住位置/状态，
     * 切回本地时恢复。 */
    qint64  m_localResumePos = 0;
    bool    m_localWasPlaying = false;
    bool    m_btAcquired = false;       // 手机这次播放是否已确认过安卓音频通路
    QListWidgetItem *m_btTrackItem = nullptr;   // 蓝牙模式下显示手机曲目的那一行
    /* 蓝牙模式下列表第一行（"📱 手机正在播放 · <徽标>"）—— 徽标 = 手机播放器的
     * 名字（MediaPlayer1.Name，bt-ctl 拿得到才有）或形态名（在线音乐/视频）。 */
    QListWidgetItem *m_btStatusItem = nullptr;
    /* 列表里现在放的是"手机正在播放"那两行（而不是本地曲库）—— 见 scanSongs()
     * 里的 fillPlaylist：这种情况必须**整条重建** mediaPlaylist/mediaObjectInfo，
     * 不能只重建显示列表（否则切回本地后 mediaObjectInfo 是空的，
     * 歌词/时长/高亮全丢）。 */
    bool    m_listHasBtRows = false;
    QString m_btLocalTitle;                     // 进蓝牙前 label_4 的内容（切回本地要还原）
    QVector<QRect> m_btOrigGeom;                // 进蓝牙前的控制条几何（切回时还原）
    /* --- 蓝牙模式去重/一次性动作的标记（手机 Title 每句歌词都变，不能每次都重做） --- */
    QString m_btTrackKey;                       // 上一次的 artist|album（换歌才重匹配歌词、重画封面）
    bool    m_btLrcMatched = false;             // 本地 myMusic 里是否匹配到了 .lrc
    bool    m_btPlaceholderDisc = false;        // 蓝牙占位封面是否已经画过（QPainter 240x240 不便宜）
//    QPixmap rotatingPixmap;
    RotatableWidget *rotatableWidget;
    QWidget *baseWidget;
//    QPointF rotationCenter;
    QPropertyAnimation *animation;

    RotatableWidget *rotatableWidget2;
    QWidget *baseWidget2;
    QPropertyAnimation *animation2;
    // 唱片（旋转封面）对象，换歌时更新它的图片
    RotatableWidget *discRotator = nullptr;

    RotatableWidget *rotatableWidget3 = nullptr;
    QWidget *baseWidget3;
    QPropertyAnimation *animation3 = nullptr;

    /* 媒体播放器，用于播放音乐 */
    /* ⚠️ 必须显式初始化成 nullptr：构造函数里 `setAlbumArt()` / `firstSongCoverPixmap()`
     * 跑在 `mediaPlayerInit()` **之前**（Audio.cpp 构造函数），那时这两个指针还是
     * 未初始化的野值 —— 2026-10-18 第一版封面修复就因为 firstSongCoverPixmap()
     * 里 `if (mediaPlaylist && mediaPlaylist->currentIndex()...)` 拿到野指针，
     * 启动即 SIGSEGV（板上实测 dmesg: unhandled page fault at 0x00120009）。 */
    QMediaPlayer *musicPlayer = nullptr;
    /* 媒体列表 */
    QMediaPlaylist *mediaPlaylist = nullptr;
    /* 媒体信息存储 */
    QVector<MediaObjectInfo> mediaObjectInfo;

    QString currentLyricsPath;

    /* ---------- 歌词（一次解析好，支持 上一句/当前/下一句 同时显示） ---------- */
    struct LyricLine {
        qint64 time = 0;      // 毫秒
        QString text;
    };
    QVector<LyricLine> m_lyrics;
    int m_lyricIndex = -1;
    // 三行歌词控件（作为 label_4 的兄弟插入到同一布局里）
    QWidget *m_lyricWidget = nullptr;
    QLabel *m_lyricPrev = nullptr;
    QLabel *m_lyricCur = nullptr;
    QLabel *m_lyricNext = nullptr;
    void buildLyricWidget();
    void parseLyrics(const QString &path);
    void showLyricAt(int index);
    // 重排整页布局（左：列表+控制条；右：唱片+歌词）
    void buildLayout();

    /* 扫描歌曲 */
    void scanSongs();
    /* 媒体播放器类初始化 */
    void mediaPlayerInit();

    void setAlbumArt(QPixmap pixmap);
    /* 当前播放位置那首歌**自己的**封面（内嵌 APIC/PICTURE 优先，其次同名 jpg/png）；
     * 取不到就返回 QPainter 画的"胶片"占位图 —— 绝不借用别的歌的封面。
     * （2026-10-18 修「海报串图」：函数名沿用旧名，语义已从"第一张非空封面"改掉） */
    QPixmap firstSongCoverPixmap();
    // 从音频文件读它自己的封面；没有返回**空** QPixmap（调用方必须用胶片占位兜底）
    static QPixmap embeddedCover(const QString &audioPath);
    // 更新唱片上的封面；传空 QPixmap = 显示胶片占位（**不会**保留上一张）
    void updateDiscCover(const QPixmap &cover);
};

#endif // WIDGET_H
