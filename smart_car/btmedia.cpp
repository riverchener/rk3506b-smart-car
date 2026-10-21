#include "btmedia.h"

#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStringList>

namespace {
const char *kMyMusicDir = "/userdata/smart_car/myMusic";
const int   kFfStepMs   = 10000;    // 手机播放器一次快进/快退大约 10 秒
const int   kFfMaxPress = 10;       // 一次拖拽最多发 10 次，防止"拖一下跳十分钟"

/* ---------------------------------------------------------------------------
 * 占位符表：手机/播放器在"没有元数据"时发来的**字面占位串**。
 * 实测这台手机在视频/无元数据态发的就是 `Not Provided`（原来被原样显示到界面上），
 * 所以必须归一化成空，而不是当歌名。比较时：大小写不敏感、去首尾空白、
 * 去掉一层包裹的 <>/[]/【】/"'、忽略结尾的点。
 * ------------------------------------------------------------------------- */
const char *const kPlaceholderWords[] = {
    "not provided", "notprovided", "not available", "no title", "no artist",
    "no album", "unknown", "unknown artist", "unknown title", "unknown album",
    "undefined", "n/a", "null", "none", "nil",
    "未知", "未知曲目", "未知歌手", "未知专辑", "未知歌曲", "未提供", "暂无", "暂无歌词",
    "--", "-", "—", "－", "...", "…", "/",
};

/* 平台名 —— **只用来做徽标/提示**，不用来判定（判定只看字段形态，见 smartcarResolveTrack）。
 * 分两组："artist 恰好是视频平台名"能佐证视频形态；而"QQ音乐"这种音乐平台名
 * 恰恰说明是音乐，不能当成视频拦掉。 */
const char *const kVideoPlatformNames[] = {
    "抖音", "douyin", "tiktok", "快手", "kuaishou", "哔哩哔哩", "bilibili", "b站",
    "西瓜视频", "小红书", "微博", "weibo", "youtube", "视频", "小视频", "短片", "直播",
    "微信", "wechat", "朋友圈", "腾讯视频", "爱奇艺", "优酷", "微视",
};
const char *const kMusicPlatformNames[] = {
    "qq音乐", "qqmusic", "网易云音乐", "酷狗音乐", "酷我音乐", "汽水音乐", "咪咕音乐",
    "华为音乐", "小米音乐", "spotify", "apple music",
};

bool smartcarHasCjk(const QString &s)
{
    for (const QChar &c : s) {
        const ushort u = c.unicode();
        if ((u >= 0x3040 && u <= 0x30FF)        // 平假名/片假名
                || (u >= 0x3400 && u <= 0x9FFF) // CJK 统一表意
                || (u >= 0xF900 && u <= 0xFAFF)
                || (u >= 0xFF66 && u <= 0xFF9F)) // 半角片假名
            return true;
    }
    return false;
}

/* 去一层包裹符号：<unknown> / 【未知】 / "Not Provided" → 里的内容 */
QString smartcarStripWrappers(const QString &s)
{
    if (s.size() < 2)
        return s;
    const QString opens  = QStringLiteral("<[({\u3010\u300a\"'");
    const QString closes = QStringLiteral(">])}\u3011\u300b\"'");
    const int i = opens.indexOf(s.at(0));
    if (i >= 0 && closes.at(i) == s.at(s.size() - 1))
        return s.mid(1, s.size() - 2).trimmed();
    return s;
}

/* 含字母/数字/表意文字才算"有内容"：纯标点、纯 emoji（Symbol_Other）都算空 */
bool smartcarHasTextContent(const QString &s)
{
    for (const QChar &c : s) {
        if (c.isLetter() || c.isDigit() || c.isNumber())
            return true;
    }
    return false;
}

bool smartcarIsPlatformIn(const QString &s, const char *const *tab, int n)
{
    const QString low = s.trimmed().toLower();
    if (low.isEmpty())
        return false;
    for (int i = 0; i < n; ++i) {
        if (low == QString::fromUtf8(tab[i]))
            return true;
    }
    return false;
}

/* 命中的平台/App 名（只作徽标）：artist → album → title，**整串精确匹配** */
QString smartcarKnownPlatform(const QString &artist, const QString &album, const QString &title)
{
    const QString cands[3] = { smartcarNormalizeMeta(artist),
                               smartcarNormalizeMeta(album),
                               smartcarNormalizeMeta(title) };
    const int nv = int(sizeof(kVideoPlatformNames) / sizeof(kVideoPlatformNames[0]));
    for (const QString &c : cands) {
        if (!c.isEmpty() && smartcarIsPlatformIn(c, kVideoPlatformNames, nv))
            return c;
    }
    const int nm = int(sizeof(kMusicPlatformNames) / sizeof(kMusicPlatformNames[0]));
    for (const QString &c : cands) {
        if (!c.isEmpty() && smartcarIsPlatformIn(c, kMusicPlatformNames, nm))
            return c;
    }
    return QString();
}

bool smartcarIsVideoPlatformName(const QString &s)
{
    const int nv = int(sizeof(kVideoPlatformNames) / sizeof(kVideoPlatformNames[0]));
    return smartcarIsPlatformIn(smartcarNormalizeMeta(s), kVideoPlatformNames, nv);
}

/* 破折号：半角 '-'、全角 '－'、en dash、em dash */
bool smartcarIsDashChar(QChar c)
{
    const ushort u = c.unicode();
    return u == 0x2D || u == 0xFF0D || u == 0x2013 || u == 0x2014;
}

const QRegularExpression &smartcarDashRe()
{
    static const QRegularExpression re(QStringLiteral("[\\-\uFF0D\u2013\u2014]"));
    return re;
}

/* 掐掉字符串开头/结尾的空白与破折号（"歌名 - 歌手" 里那个 " - " 要吃掉） */
QString smartcarTrimDashEdges(QString s)
{
    while (!s.isEmpty() && s.at(0).isSpace())
        s.remove(0, 1);
    while (!s.isEmpty() && smartcarIsDashChar(s.at(0)))
        s.remove(0, 1);
    while (!s.isEmpty() && s.at(s.size() - 1).isSpace())
        s.chop(1);
    while (!s.isEmpty() && smartcarIsDashChar(s.at(s.size() - 1)))
        s.chop(1);
    return s.trimmed();
}

/*
 * 把手机上报的字段拆成"候选词"：
 *   手机 Artist 字段实测是 `歌名-歌手/合作者` 形式（如
 *   `Take Me Hand-DAISHI DANCE (ダイシ・ダンス)/Cécile Corbel`），
 *   Album 是专辑名，Title 是**滚动歌词行**（不可当标题用）。
 * 拆词之后拿去和本地 myMusic 里的文件名比，比整串比更容错
 * （本地文件名常带 "(with 杨瑞代)" 之类后缀）。
 *
 * ⚠️ 调用顺序有讲究：**第一个参数决定匹配用的"主词"**（见 smartcarFindInDir），
 * 所以匹配时一定要把**歌名**放在第一个参数，否则会拿歌手名去命中文件名。
 */
QStringList smartcarTrackTokens(const QString &artist, const QString &album, const QString &title)
{
    const QRegularExpression sep(
        QStringLiteral("[\\s\\-–—_/\\\\()（）\\[\\]【】,.，、·|]+"));
    QStringList out;
    auto add = [&out, &sep](const QString &s) {
        const QStringList parts = s.split(sep, Qt::SkipEmptyParts);
        for (const QString &p : parts) {
            const QString t = p.trimmed();
            if (t.size() >= 2)              // 单字词太容易误命中
                out << t;
        }
        if (s.simplified().size() >= 2)
            out << s.simplified();          // 整串也试一次
    };
    add(artist);
    add(album);
    add(title);
    return out;
}

/* ---------------------------------------------------------------------------
 * 这段元数据是"某首歌"还是"视频/短视频 App 之类根本配不上的东西"？
 *
 * 为什么需要（用户实测报的问题）：手机在抖音等 App 里放视频时，AVRCP 上报的
 * Artist/Album 是**视频/频道信息**（有时干脆是 App 名或空），本地 myMusic 里
 * 当然没有对应文件。原来的代码在这种"没配上"的情况下**什么都不做** ——
 * 于是界面上还挂着**上一首歌**的歌词和海报，用户看到的就是
 * "放抖音还是显示上一首的歌词和封面"。
 *
 * 现在这一层只做一件事：**不再显示本地那套东西**（歌词/封面），
 * 并按形态给一句中性提示。判据全部是字段形态（见 smartcarResolveTrack），
 * 平台名只用来佐证；宁可漏判（退回"匹配不到就不显示"的默认行为），不要误判把正常歌拦掉。
 * ------------------------------------------------------------------------- */
bool isNonMusicSource(const QString &artist, const QString &album, const QString &title)
{
    return smartcarResolveTrack(artist, album, title).form != SmartcarMetaForm::Music;
}

/*
 * 在 myMusic 里按文件名找同名文件。
 * 命中规则**故意收紧**：只要命中"主词"（Artist 的第一个词，通常就是歌名）就算，
 * 否则必须命中 ≥2 个词 —— 不然同一歌手的不同歌会张冠李戴，
 * 歌词/封面配错比配不上更糟。
 */
QString smartcarFindInDir(const QString &dirPath, const QString &glob,
                          const QStringList &cands)
{
    const QDir dir(dirPath);
    if (!dir.exists())
        return QString();
    const QString primary = cands.isEmpty() ? QString() : cands.first();
    const QStringList files = dir.entryList({glob}, QDir::Files, QDir::Name);
    for (const QString &f : files) {
        const QString b = QFileInfo(f).completeBaseName().simplified();
        if (!primary.isEmpty() && b.contains(primary, Qt::CaseInsensitive))
            return dir.filePath(f);
        int hits = 0;
        for (const QString &tok : cands) {
            if (b.contains(tok, Qt::CaseInsensitive) && ++hits >= 2)
                return dir.filePath(f);
        }
    }
    return QString();
}
}   // namespace

/* ============================================================================
 * 元数据「按字段形态」解析 —— 纯函数实现（可离线单测，见 vmctl/pftest_meta.cpp）
 * ========================================================================== */

bool smartcarIsPlaceholderMeta(const QString &raw)
{
    const QString s = raw.simplified();          // 去首尾空白 + 压缩内部空白
    if (s.isEmpty())
        return true;
    if (!smartcarHasTextContent(s))              // 全标点 / 全 emoji / 全空白
        return true;

    const QString inner = smartcarStripWrappers(s);
    const QString cands[2] = { s, inner };
    const int n = int(sizeof(kPlaceholderWords) / sizeof(kPlaceholderWords[0]));
    for (const QString &c : cands) {
        QString low = c.toLower();
        while (low.endsWith(QLatin1Char('.')) || low.endsWith(QStringLiteral("。")))
            low.chop(1);
        low = low.trimmed();
        if (low.isEmpty())
            return true;
        for (int i = 0; i < n; ++i) {
            if (low == QString::fromUtf8(kPlaceholderWords[i]))
                return true;
        }
    }
    return false;
}

QString smartcarNormalizeMeta(const QString &raw)
{
    if (smartcarIsPlaceholderMeta(raw))
        return QString();
    return raw.simplified();
}

QString smartcarNormalizeArtistList(const QString &raw)
{
    const QString s = smartcarNormalizeMeta(raw);
    if (s.isEmpty())
        return s;
    const QStringList parts =
        s.split(QRegularExpression(QStringLiteral("[\uFF0F/\u3001]")), Qt::SkipEmptyParts);
    QStringList out;
    for (const QString &p : parts) {
        const QString t = p.simplified();
        if (!t.isEmpty() && !out.contains(t))
            out << t;
    }
    return out.isEmpty() ? s : out.join(QStringLiteral(" / "));
}

bool smartcarHasTitleArtistForm(const QString &artist)
{
    const QString a = smartcarNormalizeMeta(artist);
    if (a.isEmpty())
        return false;
    auto it = smartcarDashRe().globalMatch(a);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const int p = m.capturedStart();
        const QString l = a.left(p).trimmed();
        const QString r = a.mid(p + m.capturedLength()).trimmed();
        if (l.size() < 2 || r.isEmpty())
            continue;
        /* 不能把 Jay-Z / A-Lin 这种连字符艺名拆成"歌名-歌手"：
         * 要求"左边带空格"或"任一侧是 CJK"或"右边够长(≥4)"。 */
        if (l.contains(QLatin1Char(' ')) || smartcarHasCjk(l) || smartcarHasCjk(r)
                || r.size() >= 4)
            return true;
    }
    return false;
}

QString smartcarDashTitlePart(const QString &artist, const QStringList &localBaseNames)
{
    const QString a = smartcarNormalizeMeta(artist);
    if (a.isEmpty() || !smartcarHasTitleArtistForm(a))
        return QString();
    QStringList cands;
    auto it = smartcarDashRe().globalMatch(a);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString l = a.left(m.capturedStart()).trimmed();
        if (l.size() >= 2)
            cands << l;
    }
    if (cands.isEmpty())
        return QString();

    /* 歌名本身可能含 '-'：优先取"最长且能对上本地文件名"的前缀 */
    if (!localBaseNames.isEmpty()) {
        QString best;
        for (const QString &c : cands) {
            for (const QString &b : localBaseNames) {
                if (b.contains(c, Qt::CaseInsensitive) && c.size() > best.size())
                    best = c;
            }
        }
        if (!best.isEmpty())
            return best;
    }
    return cands.first();
}

bool smartcarLooksLikeLyricLine(const QString &title, const QString &artist)
{
    const QString t = smartcarNormalizeMeta(title);
    if (t.isEmpty())
        return false;                            // 空/占位符 ⇒ 不是歌词行，是"没有标题"
    const QString a = smartcarNormalizeMeta(artist);

    /* 1) artist 是"歌名-歌手"形态，而 title 不等于那个歌名 ⇒ title 只可能是滚动歌词行 */
    const QString tp = smartcarDashTitlePart(a);
    if (!tp.isEmpty())
        return (t != tp);

    if (a.isEmpty())
        return false;

    /* 2) title 里包含整个 artist 串且更长（如 artist=周杰伦, title="词：周杰伦"） */
    if (t.size() > a.size() && t.contains(a, Qt::CaseInsensitive))
        return true;

    /* 3) 典型的歌词前缀 */
    static const QRegularExpression lyrPrefix(
        QStringLiteral("^(词|曲|作词|作曲|编曲|制作|Lyric|Lyrics)\\s*[:\uFF1A]"));
    if (lyrPrefix.match(t).hasMatch())
        return true;
    return false;
}

SmartcarTrackInfo smartcarResolveTrack(const QString &artist, const QString &album,
                                       const QString &title,
                                       const QStringList &localBaseNames)
{
    SmartcarTrackInfo out;
    const QString a0 = smartcarNormalizeMeta(artist);
    const QString t0 = smartcarNormalizeMeta(title);
    out.album = smartcarNormalizeMeta(album);
    out.platform = smartcarKnownPlatform(artist, album, title);   // 只作徽标

    const bool lyricLike = smartcarLooksLikeLyricLine(title, artist);
    if (lyricLike)
        out.lyricLine = title.simplified();      // 原文保留（歌词行要原样滚动）

    /* "歌名-歌手"形态（这台手机的 QQ音乐滚动歌词态就是这样） */
    const QString tp = smartcarDashTitlePart(a0, localBaseNames);
    QString ta;
    if (!tp.isEmpty()) {
        /* 歌名后面那一段（" - 周杰伦" / "-林俊杰" / "-DAISHI DANCE…"）就是歌手串。
         * ⚠️ 必须先吃空格再吃破折号：` - 周杰伦` 的第一个字符是空格，
         * 只判破折号会留下 "- 周杰伦" 这种脏副标题（单测抓到过）。 */
        ta = smartcarNormalizeArtistList(
                 smartcarTrimDashEdges(a0.mid(a0.indexOf(tp) + tp.size())));
    }

    if (t0.isEmpty()) {
        /* title 空/占位符：能从 artist 里解析出"歌名-歌手"就用它，
         * 否则把 artist 当歌名（视频/无元数据场景的常见形态）。 */
        if (!tp.isEmpty()) {
            out.title = tp;
            out.artist = ta;
        } else if (!a0.isEmpty()) {
            /* Artist 当"歌名"显示 —— 视频/无元数据场景常把频道名/UP 主放这里。
             * ⚠️ 但若 Artist 本身就是**平台名**（抖音/哔哩哔哩…），它只是徽标，
             * 不能顶到"歌名"位置（那才是张冠李戴），宁可显示「（未知曲目）」。 */
            if (out.platform.isEmpty())
                out.title = a0;
        }
        /* 只剩专辑时**不**把专辑当歌名 —— 专辑 ≠ 歌名，宁可显示「（未知曲目）」 */
    } else if (lyricLike) {
        if (!tp.isEmpty()) {
            out.title = tp;
            out.artist = ta;
        } else {
            out.artist = smartcarNormalizeArtistList(a0);
            /* 歌名留空 ⇒ 界面显示「（未知曲目）」：绝不把歌词行当歌名（用户实测报过） */
        }
    } else {
        out.title = t0;
        out.artist = smartcarNormalizeArtistList(a0);
        /* artist 是"歌名-歌手"而 Title 已经是那个歌名 ⇒ 副标题用歌手那一段 */
        if (!tp.isEmpty() && tp == t0 && !ta.isEmpty())
            out.artist = ta;
    }

    /* ---------------- 形态判定（按字段形态 + 平台名佐证） ----------------
     * 规则（用户给定，且被真机实测反复验证）：
     *   · 三个字段都不可用                     ⇒ 无曲目信息
     *   · 能解析出明确的"歌名-歌手"             ⇒ 音乐（强证据，优先）
     *   · artist 整串恰好是视频平台名            ⇒ 视频
     *   · **album 空 且 artist 非空**           ⇒ 视频/频道形态
     *     （真机实测：`artist=油里油气资源库 album=(空) title=有个18厘米的男朋友会发生什么事？`
     *      就是 B站/抖音的"UP 主 + 视频标题"；而 `多歌手串 + album 非空` 依旧是音乐）
     *   · 没有可用歌名（title 空/占位符）        ⇒ 视频
     *   · 其余                                 ⇒ 音乐
     */
    const bool allEmpty = a0.isEmpty() && out.album.isEmpty() && t0.isEmpty();
    const bool parsedPair = !tp.isEmpty() && !ta.isEmpty();
    if (allEmpty) {
        out.form = SmartcarMetaForm::NoInfo;
    } else if (parsedPair) {
        out.form = SmartcarMetaForm::Music;      // 明确的"歌名-歌手"⇒ 一定是音乐
    } else if (smartcarIsVideoPlatformName(out.platform)) {
        out.form = SmartcarMetaForm::Video;
    } else if (out.album.isEmpty() && !a0.isEmpty()) {
        /* album 空 + artist 非空 ⇒ 视频/频道形态（用户规则③）。
         * 真机实测：`artist=油里油气资源库 album=(空) title=有个18厘米的男朋友会发生什么事？`
         * 就是 B站/抖音的"UP 主 + 视频标题"。代价：某台手机的音乐 App 若**不发专辑字段**，
         * 也会被归到这里（歌名/歌手仍显示正确，只是提示语写成"视频源"，宁可中性不猜）。 */
        out.form = SmartcarMetaForm::Video;
    } else if (t0.isEmpty()) {
        out.form = SmartcarMetaForm::Video;      // 没有可用歌名 ⇒ 视频/频道形态
    } else {
        out.form = SmartcarMetaForm::Music;
    }

    /* 视频形态下 artist 往往只是频道名/平台名：把它顶到"歌名"位置显示更贴切，
     * 免得出现"歌手=哔哩哔哩"这种把 App 名当歌手的怪显示。 */
    if (out.form == SmartcarMetaForm::Video) {
        if (!out.artist.isEmpty()
                && (smartcarIsVideoPlatformName(out.artist) || out.title.isEmpty())) {
            if (out.title.isEmpty())
                out.title = out.artist;
            out.artist.clear();
        }
    }
    return out;
}

QString smartcarFormName(SmartcarMetaForm form)
{
    switch (form) {
    case SmartcarMetaForm::Music:  return QStringLiteral("在线音乐");
    case SmartcarMetaForm::Video:  return QStringLiteral("视频");
    case SmartcarMetaForm::NoInfo: break;
    }
    return QStringLiteral("无曲目信息");
}

QString smartcarLyricHint(SmartcarMetaForm form)
{
    switch (form) {
    case SmartcarMetaForm::Music:  return QStringLiteral("在线音乐 · 暂无歌词");
    case SmartcarMetaForm::Video:  return QStringLiteral("视频源 · 无歌词");
    case SmartcarMetaForm::NoInfo: break;
    }
    return QStringLiteral("无曲目信息");
}

QStringList smartcarLocalBaseNames()
{
    /* myMusic 内容不会在运行中变（换曲库要重新部署），缓存 60 秒足够，
     * 免得 400ms 一次的元数据轮询每次都去列目录。 */
    static QStringList cache;
    static QElapsedTimer ttl;
    if (!ttl.isValid() || ttl.elapsed() > 60000) {
        ttl.restart();
        cache.clear();
        const QDir dir(QString::fromUtf8(kMyMusicDir));
        if (dir.exists()) {
            const QStringList files = dir.entryList(QDir::Files, QDir::Name);
            for (const QString &f : files)
                cache << QFileInfo(f).completeBaseName().simplified();
        }
    }
    return cache;
}

/* ---- 下面两个是**跨文件**用的（Audio.cpp 里要调），所以必须放在匿名命名空间外 ---- */

/* "这段元数据不像本地曲库里的歌"（视频/短视频 App 等）—— 详见匿名命名空间里的说明 */
bool smartcarLooksLikeNonMusicSource(const QString &artist, const QString &album,
                                     const QString &title)
{
    return isNonMusicSource(artist, album, title);
}

/* 诊断用：匹配时真正拿去比对的第一个候选词（命中规则里的"主词"） */
QString smartcarPrimaryCandidate(const QString &artist, const QString &album,
                                 const QString &title)
{
    const QStringList c = smartcarTrackTokens(artist, album, title);
    return c.isEmpty() ? QString() : c.first();
}

BtMedia::BtMedia(QObject *parent)
    : QObject(parent)
{
    /* 轮询：400ms（原来 1s，界面状态/歌词会慢一拍） */
    m_timer.setInterval(400);
    connect(&m_timer, &QTimer::timeout, this, &BtMedia::refresh);

    /* 插值：200ms 发一次 tick，界面据此平滑推进位置 */
    m_tickTimer.setInterval(200);
    connect(&m_tickTimer, &QTimer::timeout, this, &BtMedia::tick);
}

QString BtMedia::btCtlPath()
{
    const QString p = QStringLiteral("/userdata/smart_car/bt-ctl");
    if (QFileInfo::exists(p))
        return p;
    return QStringLiteral("bt-ctl");
}

void BtMedia::startPolling(int intervalMs)
{
    m_timer.setInterval(qMax(200, intervalMs));
    if (!m_timer.isActive())
        m_timer.start();
    if (!m_tickTimer.isActive())
        m_tickTimer.start();
    refresh();
}

void BtMedia::stopPolling()
{
    m_timer.stop();
    m_tickTimer.stop();
}

int BtMedia::positionNow() const
{
    if (m_state != QLatin1String("playing"))
        return m_position;
    return m_position + static_cast<int>(m_sincePoll.elapsed());
}

void BtMedia::runAsync(const QStringList &args)
{
    QProcess *p = new QProcess(this);
    p->setProgram(btCtlPath());
    p->setArguments(args);
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            p, &QObject::deleteLater);
    p->start();
}

/* 用户刚下过命令：这一小段时间里的轮询结果不可信（见 btmedia.h 的说明） */
void BtMedia::noteUserCommand(const char *what)
{
    m_cmdGrace.restart();
    qInfo().noquote() << "bt-media: 下发命令" << what
                      << " 本地状态(命令前)=" << m_state;
}

/* 乐观更新：按下按钮立刻改本地状态并通知界面，不等手机回话。
 * ⚠️ 但**手机没连的时候绝对不能乐观**：那样 connected() 会变成 true，
 * 界面就会莫名其妙地自动切到蓝牙音源（实测手机断开时，切本地里的 pause()
 * 会引发"蓝牙/本地"来回抖，连带声卡的让出/收回也反复执行）。 */
void BtMedia::play()
{
    noteUserCommand("play");
    runAsync({QStringLiteral("bt-media"), QStringLiteral("play")});
    if (!connected()) {
        qInfo().noquote() << "bt-media: 手机未连接，只发命令、不做乐观状态更新";
        QTimer::singleShot(250, this, &BtMedia::refresh);
        return;
    }
    if (m_state != QLatin1String("playing")) {
        m_state = QStringLiteral("playing");
        m_sincePoll.restart();
        emit stateChanged(m_state);
    }
    QTimer::singleShot(250, this, &BtMedia::refresh);
}

void BtMedia::pause()
{
    noteUserCommand("pause");
    runAsync({QStringLiteral("bt-media"), QStringLiteral("pause")});
    if (!connected()) {
        qInfo().noquote() << "bt-media: 手机未连接，只发命令、不做乐观状态更新";
        QTimer::singleShot(250, this, &BtMedia::refresh);
        return;
    }
    if (m_state != QLatin1String("paused")) {
        m_position = positionNow();          // 定住当前位置
        m_state = QStringLiteral("paused");
        emit stateChanged(m_state);
    }
    QTimer::singleShot(250, this, &BtMedia::refresh);
}

void BtMedia::next()
{
    noteUserCommand("next");
    runAsync({QStringLiteral("bt-media"), QStringLiteral("next")});
    m_position = 0;
    emit positionChanged(0, m_duration);
    QTimer::singleShot(500, this, &BtMedia::refresh);
}

void BtMedia::previous()
{
    noteUserCommand("prev");
    runAsync({QStringLiteral("bt-media"), QStringLiteral("prev")});
    m_position = 0;
    emit positionChanged(0, m_duration);
    QTimer::singleShot(500, this, &BtMedia::refresh);
}

void BtMedia::toggle()
{
    if (playing())
        pause();
    else
        play();
}

void BtMedia::setVolume(int percent)
{
    const int v = qBound(0, percent, 100);
    runAsync({QStringLiteral("bt-volume"), QString::number(v)});
}

/* 拖动进度条：AVRCP 没有绝对 seek，折算成快进/快退次数 */
void BtMedia::seekRelative(int deltaMs)
{
    const int cur = positionNow();
    const int delta = deltaMs - cur;         // 用户拖到的位置 - 当前真实位置
    int presses = qAbs(delta) / kFfStepMs;
    /* 小拖也要有反应：差值 ≥3 秒就给一次，否则用户会以为"拖不动" */
    if (presses == 0 && qAbs(delta) >= 3000)
        presses = 1;
    if (presses < 1)
        return;
    if (presses > 15)
        presses = 15;

    const QString cmd = (delta > 0) ? QStringLiteral("ff") : QStringLiteral("rew");
    noteUserCommand(delta > 0 ? "ff" : "rew");
    qInfo().noquote() << "bt-media: 相对定位 delta=" << delta << "ms →" << cmd << presses << "次";
    for (int i = 0; i < presses; ++i)
        runAsync({QStringLiteral("bt-media"), cmd});

    /* 之后用真实 Position 校正 */
    QTimer::singleShot(1200, this, &BtMedia::refresh);
}

void BtMedia::refresh()
{
    if (m_busy)
        return;

    m_busy = true;
    QProcess *p = new QProcess(this);
    p->setProgram(btCtlPath());
    p->setArguments({QStringLiteral("bt-media"), QStringLiteral("status")});
    connect(p, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, p](int, QProcess::ExitStatus) {
                m_busy = false;
                const QString out = QString::fromUtf8(p->readAllStandardOutput());
                p->deleteLater();

                QString state = m_state, title = m_title, artist = m_artist, album = m_album;
                int pos = m_position, dur = m_duration;

                const QStringList lines = out.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
                for (const QString &line : lines) {
                    const int eq = line.indexOf(QLatin1Char('='));
                    if (eq <= 0)
                        continue;
                    const QString k = line.left(eq).trimmed();
                    const QString v = line.mid(eq + 1).trimmed();
                    if (k == QLatin1String("state"))         state  = v;
                    else if (k == QLatin1String("title"))    title  = v;
                    else if (k == QLatin1String("artist"))   artist = v;
                    else if (k == QLatin1String("album"))    album  = v;
                    else if (k == QLatin1String("position")) pos    = v.toInt();
                    else if (k == QLatin1String("duration")) dur    = v.toInt();
                }

                const bool connChanged = (state == QLatin1String("disconnected"))
                                       != (m_state == QLatin1String("disconnected"));

                /*
                 * 宽限期：刚下过命令（播放/暂停/上一首/下一首/快进快退）的 1.5 秒内，
                 * 轮询回来的状态和曲目很可能是**命令下发之前**的旧值 —— 直接采纳就会
                 * 让界面"按下 → 乐观更新 → 被旧值翻回去 → 下次又翻回来"地闪两下
                 * （用户实测能看到这个抖动）。这段时间只采纳进度/时长。
                 */
                if (m_cmdGrace.isValid() && m_cmdGrace.elapsed() < kCmdGraceMs) {
                    if (state != m_state || title != m_title
                            || artist != m_artist || album != m_album) {
                        qInfo().noquote() << "bt-media: 宽限期内忽略回读 state=" << state
                                          << " title=" << title.left(16)
                                          << "（本地仍是" << m_state << "）";
                    }
                    state = m_state;
                    title = m_title;
                    artist = m_artist;
                    album = m_album;
                }

                const bool stChanged   = (state != m_state);
                const bool trkChanged  = (title != m_title) || (artist != m_artist) || (album != m_album);

                /*
                 * 位置/时长：
                 * ⚠️ 这里曾经被 bt-ctl 的解析 bug 坑惨 —— dbus-send 打印的类型名
                 * `uint32` 里那个 "32" 被当成数值，于是 position/duration 恒为 32，
                 * 进度条一直在 0 秒。修好解析后手机给的是**真值**（实测
                 * Position=138057 / Duration=259973）。
                 *
                 * 现在的策略：
                 *   手机位置**在变** → 直接采纳（与手机进度条严格一致）；
                 *   手机位置**卡住不动**（有些播放器确实不上报）→ 保留本地按墙钟
                 *   累加的值，别每 400ms 把进度打回原点。
                 */
                if (pos != m_lastRawPos) {
                    m_lastRawPos = pos;
                    m_rawSameCount = 0;
                    m_posFromPhone = true;
                } else if (state == QLatin1String("playing") && ++m_rawSameCount >= 5) {
                    m_posFromPhone = false;          // 播放中连续 5 次(≈2s)不动 → 不可信
                }

                /* 换歌时，如果手机不给位置，就把本地时钟归零，否则新歌的进度条
                 * 会从上一首的位置接着走。 */
                const bool trackSwitched = (artist != m_artist) || (album != m_album);
                if (trackSwitched && !m_posFromPhone) {
                    m_position = 0;
                    m_sincePoll.restart();
                }
                const int newPos = m_posFromPhone ? pos : positionNow();

                /* ⚠️ 顺序有讲究：先把新字段落到成员上、**重算解析结果**（m_info），
                 * 下面"歌词历史"要用 m_info.lyricLine（判断 Title 是不是歌词行）。 */
                m_state = state; m_title = title; m_artist = artist; m_album = album;
                updateResolvedTrack();               // 按字段形态解析出歌名/歌手/形态

                /* 手机把**当前歌词行**塞在 Title 字段里（实测，每一句都在变）：
                 * 一变就记一行，滚动留最近 3 行 —— 本地 myMusic 没有对应 .lrc 时，
                 * 界面就用这几行显示歌词（见 phoneLyricHistory()）。
                 * ⚠️ 只在 Title **确实像歌词行**时才记：正常态下 Title 是歌名，
                 * 把歌名当歌词行塞进歌词区同样是误导（用户要的就是"别张冠李戴"）。 */
                const bool titleIsLyric = !m_info.lyricLine.isEmpty();
                if (trackSwitched || !titleIsLyric)
                    m_lyricHist.clear();
                if (titleIsLyric
                        && (m_lyricHist.isEmpty() || m_lyricHist.last() != m_info.lyricLine)) {
                    m_lyricHist.append(m_info.lyricLine);
                    while (m_lyricHist.size() > 3)
                        m_lyricHist.removeFirst();
                }

                m_position = qMax(0, newPos); m_duration = dur;
                m_sincePoll.restart();               // 插值基准时间

                if (stChanged)   emit stateChanged(m_state);
                if (trkChanged)  emit trackChanged(m_title, m_artist, m_album);
                emit positionChanged(m_position, m_duration);
                if (connChanged) emit connectionChanged(connected());
                if (stChanged || trkChanged) emit changed();
            });
    p->start();
}

/*
 * 直接问 BlueZ：这台手机在 **profile 层**还连着没有？
 *
 * 实现走 `bt-ctl status`（一次同步 QProcess，只在"连接状态去抖后"或保活时调，
 * 不在 400ms 轮询里调，所以不会拖住界面）。该命令输出里的
 * `connected=<mac>` 就是 Device1.Connected —— 放视频时 AVRCP 的 Status 可能
 * 读不到，但这里依然是 mac，这正是我们要的区分。
 */
bool BtMedia::linkConnected() const
{
    QProcess p;
    p.setProgram(btCtlPath());
    p.setArguments(QStringList{QStringLiteral("status")});
    p.start();
    if (!p.waitForFinished(1500)) {
        p.kill();
        p.waitForFinished(300);
        /* 查不到就**保守认为还连着**：宁可多做一次保活（幂等、无害），
         * 也不要因为一次查询超时就把 A2DP 音频通路拆掉。 */
        return true;
    }
    const QString out = QString::fromUtf8(p.readAllStandardOutput());
    for (const QString &line : out.split(QLatin1Char('\n'))) {
        const QString l = line.trimmed();
        if (l.startsWith(QLatin1String("connected="))) {
            const QString v = l.mid(10).trimmed();
            return !v.isEmpty() && v != QLatin1String("none");
        }
    }
    return true;    // 没这行（命令被改过）也按"还连着"处理，理由同上
}

/* 按字段形态重算"歌名/歌手/专辑/形态"—— 每次元数据更新后调一次（纯字符串运算，很便宜） */
void BtMedia::updateResolvedTrack()
{
    m_info = smartcarResolveTrack(m_artist, m_album, m_title, smartcarLocalBaseNames());
}

void BtMedia::setMetaForTest(const QString &artist, const QString &album, const QString &title)
{    m_artist = artist;
    m_album  = album;
    m_title  = title;
    /* 缓存键也换掉，强制重新匹配（否则会命中上一个曲目的缓存） */
    m_lyricCacheKey.clear();  m_lyricCacheFile.clear();
    m_audioCacheKey.clear();  m_audioCacheFile.clear();
    updateResolvedTrack();
    m_lyricHist.clear();
    if (!m_info.lyricLine.isEmpty())
        m_lyricHist.append(m_info.lyricLine);
    m_state = QLatin1String("playing");
    emit stateChanged(m_state);      // 自检帧要跟真机一样：播放键亮起、唱盘转
    emit trackChanged(m_title, m_artist, m_album);
}

QString BtMedia::matchLyricFile() const
{    /*
     * ⚠️ 缓存键必须是**解析后的曲目标识**（歌名|歌手），不能带手机裸 Title ——
     * 滚动歌词形态下手机 Title 每句都变（等于每次都重扫目录），而解析出来的
     * 歌名/歌手是稳定的。
     */
    const QString key = m_info.title + QLatin1Char('|') + m_info.artist;
    if (key == m_lyricCacheKey)
        return m_lyricCacheFile;

    /* 元数据明显不是本地曲库里的歌（视频/无元数据/连歌名都解析不出来）→ 不要瞎匹配。
     * 这里返回空，上层 Audio::onBtTrackChanged() 会据此**清掉**上一首残留的歌词，
     * 而不是继续显示上一首的（用户报的正是"放抖音还是上一首的歌词"）。 */
    if (m_info.form != SmartcarMetaForm::Music || m_info.title.isEmpty()) {
        m_lyricCacheKey = key;
        m_lyricCacheFile.clear();
        qInfo().noquote() << "bt-media: 跳过歌词匹配（形态=" << smartcarFormName(m_info.form)
                          << " 歌名=[" << m_info.title << "]）裸字段 artist=["
                          << m_artist << "] album=[" << m_album << "]";
        return QString();
    }

    /* ⚠️ 第一个参数必须是**歌名**：smartcarFindInDir 拿第一个参数当"主词"，
     * 放歌手名进去就会"同一歌手的不同歌都算命中"（张冠李戴）。 */
    const QString found = smartcarFindInDir(QString::fromUtf8(kMyMusicDir),
                                            QStringLiteral("*.lrc"),
                                            smartcarTrackTokens(m_info.title, m_info.artist,
                                                                m_info.album));
    m_lyricCacheKey = key;
    m_lyricCacheFile = found;
    if (found.isEmpty())
        qInfo().noquote() << "bt-media: 未找到匹配歌词 歌名=[" << m_info.title
                          << "] 歌手=[" << m_info.artist << "]"
                          << " 主词=" << smartcarPrimaryCandidate(m_info.title, m_info.artist,
                                                                  m_info.album);
    else
        qInfo().noquote() << "bt-media: 歌词匹配到" << found;
    return found;
}

/*
 * 本地曲库里有没有**同一首歌**（用来取封面）。
 * 为什么需要它：AVRCP 这条链路上 BlueZ 根本不提供封面（没有 BIP/图像句柄，
 * 见 btmedia.h 的说明），所以"手机的海报"拿不到；但如果是车机里也有的歌，
 * 用本地那份的封面（内嵌 ID3 或同名 jpg/png）显示出来，看起来就和手机一致了。
 *
 * ⚠️ 同理：形态不是音乐（视频/无元数据）时返回空 ——
 * 让上层把封面**换回中性的 BT 占位盘**，而不是继续挂着上一首的封面。
 */
QString BtMedia::matchLocalAudioFile() const
{
    const QString key = m_info.title + QLatin1Char('|') + m_info.artist;
    if (key == m_audioCacheKey)
        return m_audioCacheFile;

    if (m_info.form != SmartcarMetaForm::Music || m_info.title.isEmpty()) {
        m_audioCacheKey = key;
        m_audioCacheFile.clear();
        qInfo().noquote() << "bt-media: 跳过封面匹配（形态=" << smartcarFormName(m_info.form) << "）";
        return QString();
    }

    const QStringList cands = smartcarTrackTokens(m_info.title, m_info.artist, m_info.album);
    QString found;
    for (const QString &glob : {QStringLiteral("*.mp3"), QStringLiteral("*.flac"),
                                QStringLiteral("*.wav"), QStringLiteral("*.m4a"),
                                QStringLiteral("*.ogg")}) {
        found = smartcarFindInDir(QString::fromUtf8(kMyMusicDir), glob, cands);
        if (!found.isEmpty())
            break;
    }
    m_audioCacheKey = key;
    m_audioCacheFile = found;
    qInfo().noquote() << "bt-media: 本地同名歌曲" << (found.isEmpty() ? "无" : found);
    return found;
}
