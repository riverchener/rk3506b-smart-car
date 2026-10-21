#ifndef BTMEDIA_H
#define BTMEDIA_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QElapsedTimer>

/*
 * 蓝牙媒体（AVRCP CT）—— 让车机真正控制手机播放
 * ============================================================================
 *
 * 手机放歌时：手机 = A2DP source + AVRCP Target，车机 = A2DP sink。
 * 车机要发「暂停/上一首/下一首」必须是 AVRCP Controller。本板实测结论：
 *
 *   ✗ BlueALSA      只有 TG，没有 CT
 *   ✗ PulseAudio    只有 AVRCP 绝对音量，没有传输控制/元数据（也没有 MPRIS）
 *   ✓ BlueZ 自己的  org.bluez.MediaControl1  Play/Pause/Stop/Next/Previous
 *                  org.bluez.MediaPlayer1    Status/Position/Track{Title,Artist,Album,Duration}
 *                  两个接口在 BlueZ 5.77 里都在（标着 Deprecated 但完全可用）
 *
 * 车机侧把这些封装在 `bt-ctl bt-media ...`（脚本，已上板实测）：
 *     bt-ctl bt-media status   ->  state=playing / title= / artist= / album= / duration= / position=
 *     bt-ctl bt-media play|pause|next|prev
 *     bt-ctl bt-volume <0-100> ->  走 PulseAudio（等价于改手机音量）
 *
 * 本类只做两件事：每秒钟取一次状态并发出信号；把界面上的按键转成上面这些命令。
 * 走 QProcess 而不是 QtDBus，是为了和现有代码（wifi-ctl / bt-ctl）保持一致的风格，
 * 也免得给 .pro 增加 QtDBus 依赖。
 *
 * 注意：AVRCP 没有 seek 命令，所以蓝牙音源**不支持拖动定位**——
 *       界面应如实提示，不要假装能拖（seekSupported() 恒为 false）。
 */
/*
 * 这两个是**自由函数**（不是 BtMedia 成员），放在类外面是因为 Audio.cpp 也要用。
 *
 * smartcarLooksLikeNonMusicSource()：
 *   判断 AVRCP 上报的这组元数据"像不像本地曲库里的歌"。
 *   手机在抖音/快手等 App 里放视频时，Artist/Album 往往是视频/频道信息或干脆为空，
 *   本地 myMusic 里没有对应文件 —— 这时候如果还按"上一首歌"留着歌词和海报，
 *   用户看到的就是"放视频还显示上一首的歌词和封面"（用户实测报过）。
 *   命中时上层应该**清掉**本地歌词/封面，而不是继续显示上一首的。
 *
 * smartcarPrimaryCandidate()：诊断用，返回匹配时真正拿去比对的第一个候选词。
 */
bool    smartcarLooksLikeNonMusicSource(const QString &artist, const QString &album,
                                        const QString &title);
QString smartcarPrimaryCandidate(const QString &artist, const QString &album,
                                 const QString &title);

/* ============================================================================
 * 元数据「按字段形态」判定与曲目解析 —— 全部是**纯函数**（不碰文件系统、不看 App 名）
 * ============================================================================
 *
 * 【为什么必须按形态判定】同一台手机（Redmi K70 Ultra）实测，AVRCP 三个字段的
 * **语义会随 App/播放形态变**：
 *
 *   QQ音乐 正常态    artist=林俊杰                  album=上一首情歌(国语精选) title=不为谁而作的歌
 *                    → artist=歌手、album=专辑、title=歌名（语义正常）
 *   QQ音乐 滚动歌词  artist=不为谁而作的歌-林俊杰    album=上一首情歌(国语精选) title=和他们关心的 的地方
 *                    → artist 变"歌名-歌手"、**title 变滚动歌词行**（绝不能当歌名）
 *   视频/无元数据    artist=(空) album=(空)          title=Not Provided
 *                    → "Not Provided" 是手机发来的**字面占位符**（不能原样显示）
 *   多歌手           artist=腾格尔/W8VES/沙一汀EL/三国杀 album=傲视群雄…专辑 title=傲视群雄
 *                    → 音乐（多歌手串 + 专辑非空，别过度判成视频）
 *   B站              artist=左盟主Linux              album=(空)                title=Not Provided
 *                    → artist=UP 主、没有可用歌名 ⇒ 视频形态
 *
 * 所以判定**字段形态**（谁像歌名、谁像占位符、谁像歌词行），不判定 App 名；
 * App/平台名只用于**徽标与提示**（platform / smartcarFormName()）。
 *
 * 【总原则】宁可中性克制，绝不张冠李戴：拿不准就显示中性的提示与占位盘，
 * 而不是猜一个歌名/歌手/封面摆上去。
 * ========================================================================== */

/* 元数据形态（只分三类，够用且不越界判定） */
enum class SmartcarMetaForm {
    NoInfo = 0,     // 三个字段都不可用（空/占位符/纯标点 emoji）→「无曲目信息」
    Video,          // 没有可用歌名但有 artist/album；或 artist 就是视频平台名 →「视频源」
    Music           // 其余 →「在线音乐」
};

/* 解析结果：界面要显示的**歌名/歌手/专辑**，以及判定用的形态/徽标 */
struct SmartcarTrackInfo {
    QString title;              // 歌名（归一化后；空 = 没有可用歌名 ⇒ 界面显示「（未知曲目）」）
    QString artist;             // 歌手 / UP 主（多歌手串已规范成 "A / B / C"）
    QString album;              // 专辑（归一化后，可能为空）
    QString lyricLine;          // 手机 Title 是**歌词行**时的原文（否则空）
    QString platform;           // 命中的平台/App 名（**只作徽标/提示**，可为空）
    SmartcarMetaForm form = SmartcarMetaForm::NoInfo;
};

/* 占位符/无意义值？（大小写与首尾空白不敏感）
 * 命中即视为**空**：Not Provided / <unknown> / Unknown / 未知 / -- / - / N/A / null /
 * 纯空白 / 全为标点或 emoji。 */
bool    smartcarIsPlaceholderMeta(const QString &raw);

/* 归一化一个元数据字段：去首尾空白、压缩内部空白；占位符/纯标点 emoji → 空串 */
QString smartcarNormalizeMeta(const QString &raw);

/* 多歌手串规范化："/"、"／"、"、" 连接 → " / "，去重、去首尾分隔符 */
QString smartcarNormalizeArtistList(const QString &raw);

/* 手机 Title 字段是不是「滚动歌词行」（不可当歌名）？判据全部是字段形态：
 *   1) artist 是"歌名-歌手"形态，而 title **不等于**那个歌名 ⇒ title 是歌词行；
 *   2) title 里包含整个 artist 串且更长（如 artist=周杰伦, title=词：周杰伦）；
 *   3) title 以 词：/曲：/作词/作曲/编曲/Lyrics 开头。 */
bool    smartcarLooksLikeLyricLine(const QString &title, const QString &artist);

/* artist 是否"歌名-歌手"形态（破折号两侧都有内容，且不像 Jay-Z / A-Lin 这种连字符艺名） */
bool    smartcarHasTitleArtistForm(const QString &artist);

/* 从"歌名-歌手"里取歌名：默认取**第一个**破折号之前的部分；
 * 给了本地文件名提示（myMusic 里的文件名主干）时取"最长且能匹配本地文件名"的前缀 ——
 * 这样歌名本身含 '-' 也能切对。 */
QString smartcarDashTitlePart(const QString &artist,
                              const QStringList &localBaseNames = QStringList());

/* 核心：把 AVRCP 上报的 (artist, album, title) 解析成"歌名/歌手/专辑 + 形态"。
 * localBaseNames 可选（见 smartcarLocalBaseNames()）。 */
SmartcarTrackInfo smartcarResolveTrack(const QString &artist, const QString &album,
                                       const QString &title,
                                       const QStringList &localBaseNames = QStringList());

/* 形态名（徽标/提示用）："视频" / "在线音乐" / "无曲目信息" */
QString smartcarFormName(SmartcarMetaForm form);

/* 歌词区的中性提示语（**任何情况下都不会误导**）：
 *   在线音乐 · 暂无歌词 / 视频源 · 无歌词 / 无曲目信息 */
QString smartcarLyricHint(SmartcarMetaForm form);

/* myMusic 里的文件名主干（不含扩展名），带 60 秒缓存 —— 给 smartcarDashTitlePart 用 */
QStringList smartcarLocalBaseNames();

class BtMedia : public QObject
{
    Q_OBJECT

public:
    explicit BtMedia(QObject *parent = nullptr);

    /*
     * 注意：初始状态写成 disconnected 而不是 unknown，有两个原因：
     *   1) 第一次轮询读到"手机已连接"时，connectionChanged(true) 才会被发出来，
     *      界面才能"手机一连上就自动切到蓝牙音源"；
     *   2) connected() 在第一次轮询回来之前必须是 false，否则进音乐页会误判。
     */
    bool    connected() const { return m_state != QLatin1String("disconnected")
                                    && m_state != QLatin1String("unknown"); }
    QString state()     const { return m_state; }     // playing/paused/stopped/disconnected/unknown
    QString title()     const { return m_title; }
    QString artist()    const { return m_artist; }
    QString album()     const { return m_album; }
    int     position()  const { return m_position; }  // ms
    int     duration()  const { return m_duration; }  // ms
    bool    playing()   const { return m_state == QLatin1String("playing"); }
    static bool seekSupported() { return false; }     // AVRCP 无 seek

    /* 界面用：把当前曲目匹配到本地歌词文件（myMusic/*.lrc），找不到返回空 */
    QString matchLyricFile() const;

    /* 界面用：本地曲库(myMusic)里有没有同一首歌（用来取封面，AVRCP 不传封面） */
    QString matchLocalAudioFile() const;

    /* 位置是手机给的真值（true）还是本地墙钟推算（false）；日志/排查用 */
    bool positionFromPhone() const { return m_posFromPhone; }

    /*
     * 【界面要用的都在这里】按字段形态解析好的"歌名/歌手/专辑 + 形态"。
     * 裸的 title()/artist()/album() 是手机**原样**上报的字段，语义随形态变，
     * 界面**不要**直接拿去显示（这正是"显示成歌手而不是歌名"的病根）。
     */
    const SmartcarTrackInfo &trackInfo() const { return m_info; }
    SmartcarMetaForm form() const { return m_info.form; }

    /*
     * 手机上报的"歌词行"历史（旧 → 新，最多 3 行）。
     * 实测这台手机把**当前歌词行**放在 AVRCP 的 Title 字段里（每一句都在变），
     * 所以本地 myMusic 里没有对应 .lrc 时，界面可以直接拿这几行照样滚动显示歌词，
     * 而不是干巴巴地写一句"未匹配到歌词"。
     */
    const QStringList &phoneLyricHistory() const { return m_lyricHist; }

    /*
     * 【仅离线自检用】把元数据设成给定值，并**走真正的 trackChanged 信号**
     * 发出去（和真机同一条链路）。给 SMARTCAR_BT_FAKE_META 那条自检路径用：
     * 手机不在场时也想验证"换成配不上本地的曲子（抖音等）时，
     * 上一首残留的歌词/海报有没有被清掉"，以及"歌名/占位符/歌词行三种形态各显示什么"。
     * 正常运行时绝不会被调用（只有设了那个环境变量才会走到）。
     */
    void setMetaForTest(const QString &artist, const QString &album, const QString &title);

    /*
     * 手机（profile 层）到底还连着没有 —— 直接查 BlueZ 的 Device1.Connected
     * （实现走 `bt-ctl status` 的 `connected=<mac>`，一次同步 QProcess）。
     *
     * 【为什么不能用 state() / connected() 代替】
     * 那两个来自 AVRCP 的 MediaPlayer1.Status。放**视频**（抖音等）时，
     * 手机的媒体会话常常不向 AVRCP 暴露播放器/状态，于是 Status 读不到 ⇒
     * state() 变成 unknown/disconnected。但此时**蓝牙链路（A2DP 音频）其实好着**，
     * 声音也还在放。如果拿它当"连接与否"用，就会误判成"手机断开了"，
     * 进而切回本地音源 → 把 A2DP 音频通路拆掉（声音断/卡）。
     * 所以凡是要判断"链路还在不在"（保活、是否让出声卡、是否自动切音源），
     * 必须用这个函数，不能用 state()。
     */
    bool linkConnected() const;

public slots:
    void refresh();                                  // 立刻取一次状态
    void startPolling(int intervalMs = 400);
    void stopPolling();

    void play();
    void pause();
    void toggle();
    void next();
    void previous();
    void setVolume(int percent);                     // 0-100

    /*
     * 相对定位（进度条拖拽用）。
     * AVRCP **没有**绝对 seek 命令（实测 org.bluez.MediaPlayer1 没有 Seek/SetPosition），
     * 只有 FastForward / Rewind（手机播放器每次约 10 秒）。所以这里把"拖到某个位置"
     * 折算成 N 次快进/快退（最多 10 次），松手后由下一次轮询用真实 Position 校正。
     */
    void seekRelative(int deltaMs);

    /* 插值出来的当前位置：轮询间隔内按墙钟推进，避免进度条/歌词"慢一拍" */
    int positionNow() const;

signals:
    void changed();                                                   // 任意字段变化
    void stateChanged(const QString &state);
    void trackChanged(const QString &title, const QString &artist, const QString &album);
    void positionChanged(int positionMs, int durationMs);
    void connectionChanged(bool connected);
    void tick();                                     // 200ms：让界面平滑刷新（位置插值）

private:
    void runAsync(const QStringList &args);          // 后台跑 bt-ctl（不阻塞界面）
    static QString btCtlPath();
    /*
     * 记一次"用户刚下过命令"。
     * 为什么需要：命令下发给手机（再经蓝牙栈）要几百毫秒才生效，而 400ms 的轮询
     * 很可能是**命令之前**就发出去的，它带回来的旧状态会把界面的乐观更新翻回去，
     * 下一次轮询又翻回来 —— 用户看到的就是"按一下图标闪两下"。
     * 宽限期内只采纳进度/时长，状态与曲目字段一律留到下一次轮询再认。
     */
    void noteUserCommand(const char *what);
    static const int kCmdGraceMs = 1500;

    /* 按字段形态重算 m_info —— 每次元数据更新后调一次（很便宜，纯字符串运算） */
    void updateResolvedTrack();

    QString m_state = QLatin1String("disconnected");
    QString m_title, m_artist, m_album;              // 手机**原样**上报的三个字段（语义随形态变）
    SmartcarTrackInfo m_info;                        // 按字段形态解析出来的"歌名/歌手/形态"
    QStringList m_lyricHist;                         // 手机 Title = 当前歌词行（最多留 3 行）
    /*
     * 手机位置是否可信：有些播放器根本不上报 AVRCP Position（报了也恒定不变）。
     * 判定方法：播放中连续几次读到的值都一样 → 认定不可信，改用本地墙钟累加。
     */
    int  m_lastRawPos = -1;
    int  m_rawSameCount = 0;
    bool m_posFromPhone = true;
    int     m_position = 0;
    int     m_duration = 0;
    QTimer  m_timer;                                 // 轮询（400ms）
    QTimer  m_tickTimer;                             // 插值刷新（200ms）
    QElapsedTimer m_sincePoll;                       // 上次轮询到现在的时间
    QElapsedTimer m_cmdGrace;                        // 最近一次"用户命令"的时间戳
    bool    m_busy = false;                          // 上一次 status 还没回来
    /* 歌词匹配缓存：matchLyricFile() 是 const（界面只读调用），
     * 但缓存要写，所以这两个成员必须是 mutable。
     * ⚠️ 缓存键用**解析后的歌名|歌手**（不是手机裸字段）：滚动歌词形态下手机的
     * Title 每句都变、Artist 却是稳定的，用解析结果做键才不会每句都重扫目录。 */
    mutable QString m_lyricCacheKey;                 // 上一次匹配的 歌名|歌手（曲目标识）
    mutable QString m_lyricCacheFile;
    mutable QString m_audioCacheKey;                 // 同上，用于 matchLocalAudioFile()
    mutable QString m_audioCacheFile;
};

#endif // BTMEDIA_H
