#include "player.h"
#include "playercontrols.h"
#include "playlistmodel.h"
#include "videowidget.h"
#include "uiadapter.h"
#include "commonhelper.h"

#include <QMediaService>
#include <QMediaPlaylist>
#include <QVideoProbe>
#include <QAudioProbe>
#include <QMediaMetaData>
#include <QtWidgets>

// 包含数据库操作相关头文件
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QDebug>
#include <QVariant>
#include <QList>

Player::Player(QWidget *parent)
    : QWidget(parent)
{

    // 设置部件的大小策略为忽略大小，即根据视频内容或父布局等因素自动调整大小
    setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);

    // 获取当前部件的调色板对象
    QPalette p = palette();
    // 将调色板的窗口颜色设置为黑色，这样视频显示区域的背景色将为黑色
    QColor backgroundColor(51,51,51);
    p.setColor(QPalette::Window, backgroundColor);
    // 将设置好的调色板应用到当前部件上
    setPalette(p);

    // 创建QMediaPlayer对象，用于播放媒体文件，设置其音频角色，并输出支持的音频角色信息
    m_player = new QMediaPlayer(this);

    m_player->setAudioRole(QAudio::VideoRole);
    qInfo() << "Supported audio roles:";
    for (QAudio::Role role : m_player->supportedAudioRoles())
        qInfo() << "    " << role;

    // 创建QMediaPlaylist对象，用于管理播放列表
    m_playlist = new QMediaPlaylist();
    m_player->setPlaylist(m_playlist);

    // 创建VideoWidget对象，用于显示视频内容
    m_videoWidget = new VideoWidget(this);
    m_player->setVideoOutput(m_videoWidget);

    // 创建PlaylistModel对象，用于管理播放列表的数据模型
    m_playlistModel = new PlaylistModel(this);
    m_playlistModel->setPlaylist(m_playlist);

    // 用于在界面上显示播放列表
    m_playlistView = new QListView(this);
    m_playlistView->setModel(m_playlistModel);
    m_playlistView->setCurrentIndex(m_playlistModel->index(m_playlist->currentIndex(), 0));
    m_playlistView->setStyleSheet("background-color: transparent; color:white");
    // 用于表示媒体播放的进度条，设置其范围为0到媒体总时长（以秒为单位）
    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, m_player->duration() / 1000);
    // 进度条做粗一点，手指才好拖：控件留 46px 高，槽画 14px，其余是容错区
    m_slider->setMinimumHeight(46);
    m_slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    CommonHelper::makeSliderThick(m_slider, 14);

    // 用于显示媒体的总时长等相关信息
    m_labelDuration = new QLabel(this);


    // 用于打开文件选择对话框以添加媒体文件到播放列表
    openButton = new QPushButton(tr("打开"), this);
    openButton->setMinimumSize(110, 52);
    openButton->setStyleSheet("QPushButton { background-color: transparent; "
                              "color: white; "
                              "border: 2px solid white; "
                              "border-radius: 8px; "
                              "font-size: 22px; "
                              "}"
                              "QPushButton:pressed { background-color: lightgray; "
                              "}"
    );
    // 用于返回主界面
    backButton = new QPushButton(tr("返回"), this);
    backButton->setMinimumSize(110, 52);
    backButton->setStyleSheet("QPushButton { background-color: transparent; "
                              "color: white; "
                              "border: 2px solid white; "
                              "border-radius: 8px; "
                              "font-size: 22px; "
                              "}"
                              "QPushButton:pressed { background-color: lightgray; "
                              "}"
    );
    // 设置其初始状态、音量等属性为m_player的当前状态和音量
    controls = new PlayerControls(this);
    controls->setState(m_player->state());
    controls->setVolume(m_player->volume());
    controls->setMuted(controls->isMuted());

    // 用于切换视频播放的全屏模式
    m_fullScreenButton = new QPushButton(tr("全屏"), this);
    m_fullScreenButton->setCheckable(true);
    m_fullScreenButton->setMinimumSize(110, 52);
    m_fullScreenButton->setStyleSheet("QPushButton { background-color: transparent; "
                              "color: white; "
                              "border: 2px solid white; "
                              "border-radius: 8px; "
                              "font-size: 22px; "
                              "}"
                              "QPushButton:pressed { background-color: lightgray; "
                              "}"
    );


    // 创建水平布局对象，用于放置视频显示部件和播放列表视图部件
    // 给底部控制条留足高度（原来太窄、按钮太小）
    QBoxLayout *displayLayout = new QHBoxLayout;
    displayLayout->addWidget(m_videoWidget, 3);
    displayLayout->addWidget(m_playlistView, 1);

    // 用于放置各种控制按钮和部件
    QBoxLayout *controlLayout = new QHBoxLayout;
    controlLayout->setMargin(6);
    controlLayout->setSpacing(12);
    controlLayout->addWidget(openButton);
    controlLayout->addStretch(1);
    controlLayout->addWidget(backButton);
    controlLayout->addStretch(1);
    controlLayout->addWidget(controls);
    controlLayout->addStretch(1);
    controlLayout->addWidget(m_fullScreenButton);


    // 创建垂直布局对象，用于组合整个播放器的界面布局
    QBoxLayout *layout = new QVBoxLayout;
    layout->addLayout(displayLayout, 1);
    layout->setSpacing(6);
    layout->setMargin(6);

    QHBoxLayout *hLayout = new QHBoxLayout;
    hLayout->addWidget(m_slider);
    hLayout->addWidget(m_labelDuration);
    layout->addLayout(hLayout);
    layout->addLayout(controlLayout);


    // 在QNX系统下，主窗口没有标题栏等装饰，创建一个状态栏用于显示状态信息
    // 设置状态栏不启用鼠标抓取功能，因为在没有标题栏的情况下该功能可能不太好用
    // 将状态栏添加到整体布局中
    #if defined(Q_OS_QNX)
    m_statusLabel = new QLabel;
    m_statusBar = new QStatusBar;
    m_statusBar->addPermanentWidget(m_statusLabel);
    m_statusBar->setSizeGripEnabled(false);
    layout->addWidget(m_statusBar);
    #endif

    // 设置Player窗口的布局为上述创建的整体布局
    setLayout(layout);

    // 检查媒体播放器是否可用
    if (!isPlayerAvailable()) {
        QMessageBox::warning(this, tr("Service not available"),
                             tr("The QMediaPlayer object does not have a valid service.\n"\
                                "Please check the media service plugins are installed."));

        controls->setEnabled(false);
        m_playlistView->setEnabled(false);
        openButton->setEnabled(false);
        m_fullScreenButton->setEnabled(false);
    }

    // 创建并打开数据库连接
    m_database = QSqlDatabase::addDatabase("QSQLITE");
    // 设置数据库文件路径为当前目录下的media_player.db
    m_database.setDatabaseName(QCoreApplication::applicationDirPath() + "/media_player.db");

    if (!m_database.open()) {
        qDebug() << "无法打开数据库：" << m_database.lastError().text();
        return;
    }

    // 创建名为video的表
    QSqlQuery createTableQuery;
    createTableQuery.prepare("CREATE TABLE IF NOT EXISTS video ("
                             "id INTEGER PRIMARY KEY AUTOINCREMENT,"
                             "video_name TEXT UNIQUE"
                             ")");

    if (!createTableQuery.exec()) {
        qDebug() << "创建表失败：" << createTableQuery.lastError().text();
    }

    // 程序启动时，从数据库中读取数据并添加到播放列表
    loadPlaylistFromDatabase();

    // 调用元数据改变的槽函数，初始化相关的元数据显示等操作
    metaDataChanged();
    UiAdapter::fit(this);


}
// 工作函数，当线程启动时开启各种槽函数的连接
void Player::player_working()
{
    connect(m_player, &QMediaPlayer::durationChanged, this, &Player::durationChanged);
    connect(m_player, &QMediaPlayer::positionChanged, this, &Player::positionChanged);
    connect(m_player, QOverload<>::of(&QMediaPlayer::metaDataChanged), this, &Player::metaDataChanged);
    connect(m_playlist, &QMediaPlaylist::currentIndexChanged, this, &Player::playlistPositionChanged);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, &Player::statusChanged);
    connect(m_player, &QMediaPlayer::bufferStatusChanged, this, &Player::bufferingProgress);
    connect(m_player, &QMediaPlayer::videoAvailableChanged, this, &Player::videoAvailableChanged);
    connect(m_player, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this, &Player::displayErrorMessage);
    connect(m_playlistView, &QAbstractItemView::activated, this, &Player::jump);
    connect(m_slider, &QSlider::sliderMoved, this, &Player::seek);
    connect(openButton, &QPushButton::clicked, this, &Player::open);
    connect(backButton, &QPushButton::clicked, this, &Player::backout);
    connect(controls, &PlayerControls::play, m_player, &QMediaPlayer::play);
    connect(controls, &PlayerControls::pause, m_player, &QMediaPlayer::pause);
    connect(controls, &PlayerControls::stop, m_player, &QMediaPlayer::stop);
    connect(controls, &PlayerControls::next, m_playlist, &QMediaPlaylist::next);
    connect(controls, &PlayerControls::previous, this, &Player::previousClicked);
    connect(controls, &PlayerControls::changeVolume, m_player, &QMediaPlayer::setVolume);
    connect(controls, &PlayerControls::changeMuting, m_player, &QMediaPlayer::setMuted);
    connect(controls, &PlayerControls::changeRate, m_player, &QMediaPlayer::setPlaybackRate);
    connect(controls, &PlayerControls::stop, m_videoWidget, QOverload<>::of(&QVideoWidget::update));
    connect(m_player, &QMediaPlayer::stateChanged, controls, &PlayerControls::setState);
    connect(m_player, &QMediaPlayer::volumeChanged, controls, &PlayerControls::setVolume);
    connect(m_player, &QMediaPlayer::mutedChanged, controls, &PlayerControls::setMuted);

}
Player::~Player()
{
    // 关闭数据库连接
    if (m_database.isOpen()) {
        m_database.close();
    }

}
// 清理连接
void Player::clearConnections()
 {

     disconnect(m_player, &QMediaPlayer::durationChanged, this, &Player::durationChanged);
     disconnect(m_player, &QMediaPlayer::positionChanged, this, &Player::positionChanged);
     disconnect(m_player, QOverload<>::of(&QMediaPlayer::metaDataChanged), this, &Player::metaDataChanged);
     disconnect(m_player, &QMediaPlayer::mediaStatusChanged, this, &Player::statusChanged);
     disconnect(m_player, &QMediaPlayer::bufferStatusChanged, this, &Player::bufferingProgress);
     disconnect(m_player, &QMediaPlayer::videoAvailableChanged, this, &Player::videoAvailableChanged);
     disconnect(m_player, QOverload<QMediaPlayer::Error>::of(&QMediaPlayer::error), this, &Player::displayErrorMessage);
     disconnect(m_playlistView, &QAbstractItemView::activated, this, &Player::jump);
     disconnect(m_slider, &QSlider::sliderMoved, this, &Player::seek);
     disconnect(openButton, &QPushButton::clicked, this, &Player::open);
     disconnect(backButton, &QPushButton::clicked, this, &Player::backout);
     disconnect(controls, &PlayerControls::play, m_player, &QMediaPlayer::play);
     disconnect(controls, &PlayerControls::pause, m_player, &QMediaPlayer::pause);
     disconnect(controls, &PlayerControls::stop, m_player, &QMediaPlayer::stop);
     disconnect(controls, &PlayerControls::next, m_playlist, &QMediaPlaylist::next);
     disconnect(controls, &PlayerControls::previous, this, &Player::previousClicked);
     disconnect(controls, &PlayerControls::changeVolume, m_player, &QMediaPlayer::setVolume);
     disconnect(controls, &PlayerControls::changeMuting, m_player, &QMediaPlayer::setMuted);
     disconnect(controls, &PlayerControls::changeRate, m_player, &QMediaPlayer::setPlaybackRate);
     disconnect(controls, &PlayerControls::stop, m_videoWidget, QOverload<>::of(&QVideoWidget::update));
     disconnect(m_player, &QMediaPlayer::stateChanged, controls, &PlayerControls::setState);
     disconnect(m_player, &QMediaPlayer::volumeChanged, controls, &PlayerControls::setVolume);
     disconnect(m_player, &QMediaPlayer::mutedChanged, controls, &PlayerControls::setMuted);
}
// 判断媒体播放器是否可用
bool Player::isPlayerAvailable() const
{
    return m_player->isAvailable();
}

// 视频页文件对话框的几何钳制（720p 屏上"打开/取消"按钮行原来整排在屏外）
//
// 【2026-10-18 ②(c)】真屏实测（诊断报告 §3.2/§3.4，图 _ipvideo/03_open_dialog_rot.png）：
//   对话框的可见区是 x=332..1279、y=193..719 —— 右、下正好贴死屏幕边界，
//   于是**按钮行、竖向滚动条、列表最后一行全在屏外**，用户点不到"打开"。
// 原因：Qt 的 QFileDialog 会按自己的 sizeHint 摆，而这块板子的屏是 1280x720
//（原始 KMS 帧 720x1280 竖屏，weston 旋转 90°），页面自己走 UiAdapter::fit()
// 做了适配，**对话框和提示框没做**。
//
// 做法：显式钳进屏幕可用区，并居中。取 min(屏幕, 自然大小) 且不小于一个可用下限，
// 这样按钮行一定在屏内；再 setFixedSize() 把尺寸钉死，避免 kiosk-shell
//（/var/log/weston.log: Loading module 'kiosk-shell.so'）把它拉大。
static void clampDialogToScreen(QDialog *dlg, int minW = 640, int minH = 480)
{
    if (!dlg)
        return;
    QRect avail = UiAdapter::screenGeometry(dlg);
    if (avail.width() <= 0 || avail.height() <= 0)
        avail = QRect(0, 0, 1280, 720);          // 兜底：本板逻辑分辨率
    const QSize hint = dlg->sizeHint();
    const int w = qMin(avail.width(),  qMax(minW, hint.width()));
    const int h = qMin(avail.height(), qMax(minH, hint.height()));
    dlg->setMinimumSize(0, 0);
    dlg->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
    dlg->setFixedSize(w, h);
    dlg->move(avail.x() + qMax(0, (avail.width() - w) / 2),
              avail.y() + qMax(0, (avail.height() - h) / 2));
}

// 打开文件并将所选文件插入数据库
void Player::open()
{
    // 设置一个QFileDialog对象
    QFileDialog fileDialog(this);
    // 设置文件对话框的接受模式为打开文件，标题为 Open Files
    fileDialog.setAcceptMode(QFileDialog::AcceptOpen);
    fileDialog.setWindowTitle(tr("Open Files"));

    // 获取支持的MIME文件类型
    QStringList supportedMimeTypes = m_player->supportedMimeTypes();
    if (!supportedMimeTypes.isEmpty()) {
        supportedMimeTypes.append("audio/x-m3u"); // MP3 playlists
        fileDialog.setMimeTypeFilters(supportedMimeTypes);
    }

    /*
     * 【2026-10-18 ②(b)「过滤」】补上按后缀的白名单。
     *
     * 原来只 setMimeTypeFilters()，全仓**没有** setNameFilters —— 实测真屏
     * 列表里 .conf/.sh/.ko/.db 全在，一个视频都没有（诊断 §3.2/§3.3(b)）。
     * 注意 Qt 的语义：**setNameFilters() 会清掉 mime 过滤**，所以它必须放在
     * setMimeTypeFilters() 之后调用；再 selectNameFilter() 把默认项设成视频那条。
     */
    const QString videoFilter = tr("视频 (*.mp4 *.avi *.mkv *.mov *.ts *.webm)");
    const QString allFilter = tr("所有文件 (*)");
    fileDialog.setNameFilters(QStringList() << videoFilter << allFilter);
    fileDialog.selectNameFilter(videoFilter);

    /*
     * 【2026-10-18 ②(a)「目录」】默认视频目录：程序目录/Videos（= /userdata/smart_car/Videos，
     * 与 findVideoPath() 的默认位置一致）。这个目录在板上**曾经整个被删掉**，
     * 于是 setDirectory() 指向不存在的目录 ⇒ Qt 退回当前工作目录（S98smartcar
     * 启动时 cd /userdata/smart_car），列表里就全是系统文件（诊断 §3.4）。
     * 这里在建目录失败（程序目录不可写）时退到 /userdata/Videos —— 仍是 /userdata
     * 大分区，**绝不写 rootfs**（rootfs 只剩 ~1.4 MB）。
     */
    const QString appDir = QCoreApplication::applicationDirPath();
    QString videosDir = appDir + "/Videos";
    if (!QDir().mkpath(videosDir)) {
        videosDir = QStringLiteral("/userdata/Videos");
        QDir().mkpath(videosDir);
        qWarning().noquote() << "视频目录无法创建，退回到" << videosDir;
    }
    fileDialog.setDirectory(videosDir);

    // 【2026-10-18 ②(c)】把对话框钳进 1280x720，保证"打开/取消"按钮行在屏内
    clampDialogToScreen(&fileDialog);

    // 选择文件
    if (fileDialog.exec() == QDialog::Accepted) {
        QList<QUrl> selectedUrls = fileDialog.selectedUrls();

        // 将所选文件保存到数据库中，并添加到播放列表
        QSqlQuery insertQuery;
        for (const QUrl& url : selectedUrls) {
            QFileInfo fileInfo(url.toLocalFile());
            QString videoNameWithoutExtension = fileInfo.completeBaseName();

            insertQuery.prepare("INSERT INTO video (video_name) VALUES (:video_name)");
            insertQuery.bindValue(":video_name", videoNameWithoutExtension);

            // 检查是否已存在相同名称的视频记录，避免重复添加
            if (!isVideoAlreadyExists(videoNameWithoutExtension)) {
                if (!insertQuery.exec()) {
                    qDebug() << "插入数据失败：" << insertQuery.lastError().text();
                }
                m_playlist->addMedia(url);
            } else {
                qDebug() << "文件已存在于数据库中，不重复添加：" + videoNameWithoutExtension;
            }
        }
        updateEmptyHint();     // 有视频了，撤掉"还没有视频"的占位提示
    }
}

void Player::backout()
{
    // 暂停播放（如果正在播放）
    if (m_player->state() == QMediaPlayer::PlayingState)
    {
        m_player->pause();
    }

    // 将播放位置设置为0
    m_player->setPosition(0);

    // 重置进度条位置
    m_slider->setValue(0);
    emit this->back();
}

// 检查指定视频名称是否已存在于数据库中的函数
bool Player::isVideoAlreadyExists(const QString &videoName)
{
    QSqlQuery checkQuery;
    checkQuery.prepare("SELECT COUNT(*) FROM video WHERE video_name = :video_name");
    checkQuery.bindValue(":video_name", videoName);

    if (checkQuery.exec()) {
        if (checkQuery.next()) {
            int count = checkQuery.value(0).toInt();
            return count > 0;
        }
    }

    return false;
}

// 从数据库中加载播放列表数据
void Player::loadPlaylistFromDatabase()
{
    // 查询数据库
    QSqlQuery selectQuery("SELECT * FROM video");
    while (selectQuery.next()) {
        int id = selectQuery.value(0).toInt();
        QString videoName = selectQuery.value(1).toString();

        // 根据视频名称查找对应的文件路径
        QString videoPath = findVideoPath(videoName);

        // 如果找到文件路径，则创建QUrl并添加到播放列表
        if (!videoPath.isEmpty()) {
            QUrl videoUrl = QUrl::fromLocalFile(videoPath);
            m_playlist->addMedia(videoUrl);
        } else {
            /*
             * 【2026-10-18 ②(d)】原来这里**静默跳过**：DB 里有记录、文件却找不到
             * （板上 DB 唯一一条 amp_new 的实体文件早就没了），右侧播放列表就整块
             * 黑着 0 条，用户只会以为"页面坏了"（诊断 §3.3/§3.4）。
             * 现在留一条日志，并靠 updateEmptyHint() 在列表区给出可见提示。
             */
            qWarning().noquote() << "视频文件找不到，跳过：" << videoName;
        }
    }
    updateEmptyHint();
}

/*
 * 【2026-10-18 ②(d)】播放列表为空时给一句可读的占位提示。
 *
 * 为什么需要：右侧播放列表来自 DB（loadPlaylistFromDatabase），而板上 DB 只有一条
 * 文件已丢失的 amp_new ⇒ 列表 0 条 ⇒ 整个右半边是一块黑，和"视频区"分不开，
 * 极易被判成"页面显示不全/坏了"。
 *
 * 实现取舍：不新增布局层级（那会动到 displayLayout 的比例和 UiAdapter 的缩放记录），
 * 只用一块 QLabel 浮在 QListView 上面：透明、不吃鼠标事件（WA_TransparentForMouseEvents），
 * 列表有内容时隐藏；几何由 eventFilter 跟着 m_playlistView 的 resize 走。
 */
void Player::updateEmptyHint()
{
    if (!m_playlistView)
        return;

    const bool empty = !m_playlist || m_playlist->mediaCount() == 0;
    if (!m_emptyHint) {
        if (!empty)
            return;
        m_emptyHint = new QLabel(m_playlistView);
        m_emptyHint->setAlignment(Qt::AlignCenter);
        m_emptyHint->setWordWrap(true);
        m_emptyHint->setTextFormat(Qt::PlainText);
        m_emptyHint->setText(QStringLiteral("还没有视频\n请把视频文件放到\n/userdata/smart_car/Videos\n再点【打开】添加"));
        m_emptyHint->setStyleSheet(
            "color: rgba(255, 255, 255, 200); background: transparent; border: none;");
        m_emptyHint->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        QFont f = m_emptyHint->font();
        f.setPixelSize(20);
        m_emptyHint->setFont(f);
        m_playlistView->installEventFilter(this);
    }

    m_emptyHint->setVisible(empty);
    if (empty)
        m_emptyHint->setGeometry(m_playlistView->rect().adjusted(6, 6, -6, -6));
}

// m_playlistView 尺寸一变（含 UiAdapter::fit() 缩放、切页、窗口 resize）就重新贴合提示
bool Player::eventFilter(QObject *obj, QEvent *event)
{
    if (obj == m_playlistView && event->type() == QEvent::Resize && m_emptyHint) {
        m_emptyHint->setGeometry(m_playlistView->rect().adjusted(6, 6, -6, -6));
    }
    return QWidget::eventFilter(obj, event);
}

// 目录里有没有至少一个视频文件（给 findVideoPath 的候选目录排序用）
static bool dirHasVideo(const QString &dir, const QStringList &exts)
{
    QDir d(dir);
    if (!d.exists())
        return false;
    const QFileInfoList files = d.entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
    for (const QFileInfo &fi : files) {
        if (exts.contains(fi.suffix().toLower()))
            return true;
    }
    return false;
}

// 根据视频名称查找对应的文件路径
// 依次在程序目录/Videos、U盘(/mnt/udisk,/udisk)、TF卡(/mnt/sdcard,/sdcard)、
// /userdata/Videos 中查找，方便把大视频放在外置存储上
QString Player::findVideoPath(const QString &videoName)
{
    const QStringList exts = QStringList() << "mp4" << "mkv" << "avi" << "mov" << "ts" << "webm";

    QStringList basePaths;
    const QString appDir = QCoreApplication::applicationDirPath();
    basePaths << appDir + "/Videos";

    const char *envDirs = ::getenv("SMARTCAR_VIDEO_DIRS");
    if (envDirs && *envDirs) {
        const QStringList extra = QString::fromLocal8Bit(envDirs).split(':', QString::SkipEmptyParts);
        for (const QString &e : extra)
            basePaths << e;
    }
    basePaths << "/mnt/udisk/Videos" << "/mnt/sdcard/Videos"
              << "/mnt/udisk" << "/mnt/sdcard"
              << "/userdata/Videos" << "/userdata/smart_car/Videos";

    /*
     * 【2026-10-18 ②(a)】候选目录改为"**真实存在且真的有视频文件**的优先"。
     *
     * 原来的顺序是固定的，appDir/Videos 永远第一；但本板这个目录曾经整个被删掉
     * （诊断报告 §4.2 实测：/userdata/smart_car/Videos 不存在），于是"同一个文件名
     * 在别处有、在这里没有"时，查找顺序完全没意义。现在先扫一遍目录做一次稳定划分：
     * 有视频的目录排前面，其余按原顺序跟在后面，然后才逐个找文件。
     * 不改变任何"找到哪一份"的语义，只是让"真的有视频"的目录先被看。
     */
    QStringList withVideo, others;
    for (const QString &base : basePaths) {
        if (dirHasVideo(base, exts))
            withVideo << base;
        else
            others << base;
    }
    const QStringList ordered = withVideo + others;

    for (const QString &base : ordered) {
        for (const QString &ext : exts) {
            const QString p = base + QDir::separator() + videoName + "." + ext;
            if (QFile::exists(p))
                return p;
        }
    }
    return QString();
}

// 用于判断给定的URL是否指向一个.m3u播放列表文件
static bool isPlaylist(const QUrl &url)
{
    if (!url.isLocalFile())
        return false;
    const QFileInfo fileInfo(url.toLocalFile());
    return fileInfo.exists() &&!fileInfo.suffix().compare(QLatin1String("m3u"), Qt::CaseInsensitive);
}

// 将给定的URL列表中的文件添加到播放列表的函数
void Player::addToPlaylist(const QList<QUrl> &urls)
{
    for (auto &url: urls) {
        if (isPlaylist(url))
            m_playlist->load(url);
        else
            m_playlist->addMedia(url);
    }
}

void Player::setCustomAudioRole(const QString &role)
{
    m_player->setCustomAudioRole(role);
}

// 更新进度条的最大值为新的媒体时长
void Player::durationChanged(qint64 duration)
{
    m_duration = duration / 1000;
    m_slider->setMaximum(m_duration);
}


// 如果进度条的滑块没有被用户拖动，则更新进度条的当前值为新的播放位置
void Player::positionChanged(qint64 progress)
{
    if (!m_slider->isSliderDown())
        m_slider->setValue(progress / 1000);

    updateDurationInfo(progress / 1000);
}

// 如果媒体元Data可用，则设置轨道信息（如专辑艺术家和歌曲标题）显示在相关位置
// 并根据封面艺术图片的URL设置封面标签的图片，如果URL为空则设置为空图片
void Player::metaDataChanged()
{
    if (m_player->isMetaDataAvailable()) {
        setTrackInfo(QString("%1 - %2")
           .arg(m_player->metaData(QMediaMetaData::AlbumArtist).toString())
           .arg(m_player->metaData(QMediaMetaData::Title).toString()));

        if (m_coverLabel) {
            QUrl url = m_player->metaData(QMediaMetaData::CoverArtUrlLarge).value<QUrl>();

            m_coverLabel->setPixmap(!url.isEmpty()
                  ? QPixmap(url.toString())
                    : QPixmap());
        }
    }
}

// 如果当前播放位置在开始的5秒内，则切换到播放列表中的上一首曲目；否则将播放位置设置为0
void Player::previousClicked()
{

    if (m_player->position() <= 5000)
        m_playlist->previous();
    else
        m_player->setPosition(0);
}

// 切换视频
void Player::jump(const QModelIndex &index)
{
    if (index.isValid()) {
        m_playlist->setCurrentIndex(index.row());
        m_player->play();
    }
}

// 设置当前索引为所选择的索引
void Player::playlistPositionChanged(int currentItem)
{
    m_playlistView->setCurrentIndex(m_playlistModel->index(currentItem, 0));
}

// 根据传入的秒数设置媒体播放位置的函数
// 通过调用m_player的setPosition函数将播放位置设置为指定的毫秒数（由传入的秒数转换而来）
void Player::seek(int seconds)
{
    m_player->setPosition(seconds * 1000);
}

// 根据不同的媒体状态设置相应的状态信息显示，并处理鼠标光标样式（在加载、缓冲、卡顿等状态时设置为忙碌光标）
void Player::statusChanged(QMediaPlayer::MediaStatus status)
{
    handleCursor(status);

    switch (status) {
    case QMediaPlayer::UnknownMediaStatus:
    case QMediaPlayer::NoMedia:
    case QMediaPlayer::LoadedMedia:
        setStatusInfo(QString());
        break;
    case QMediaPlayer::LoadingMedia:
        setStatusInfo(tr("Loading..."));
        break;
    case QMediaPlayer::BufferingMedia:
    case QMediaPlayer::BufferedMedia:
        setStatusInfo(tr("Buffering %1%").arg(m_player->bufferStatus()));
        break;
    case QMediaPlayer::StalledMedia:
        setStatusInfo(tr("Stalled %1%").arg(m_player->bufferStatus()));
        break;
    case QMediaPlayer::EndOfMedia:
        QApplication::alert(this);
        break;
    case QMediaPlayer::InvalidMedia:
        displayErrorMessage();
        break;
    }
}

void Player::handleCursor(QMediaPlayer::MediaStatus status)
{
    // 如果媒体处于加载、缓冲或卡顿状态，则设置鼠标光标为忙碌光标；
#ifndef QT_NO_CURSOR
    if (status == QMediaPlayer::LoadingMedia ||
        status == QMediaPlayer::BufferingMedia ||
        status == QMediaPlayer::StalledMedia)
        setCursor(QCursor(Qt::BusyCursor));
    // 否则恢复默认光标样式
    else
        unsetCursor();
#endif
}

// 根据媒体当前状态（是否卡顿）设置相应的状态信息显示为缓冲进度的百分比
void Player::bufferingProgress(int progress)
{
    if (m_player->mediaStatus() == QMediaPlayer::StalledMedia)
        setStatusInfo(tr("Stalled %1%").arg(progress));
    else
        setStatusInfo(tr("Buffering %1%").arg(progress));
}

// 当视频是否可用状态改变时调用的槽函数
void Player::videoAvailableChanged(bool available)
{
    // 如果视频不可用，断开全屏按钮与视频部件全屏设置相关的连接，并将视频部件设置为非全屏状态
    if (!available) {
        disconnect(m_fullScreenButton, &QPushButton::clicked, m_videoWidget, &QVideoWidget::setFullScreen);
        disconnect(m_videoWidget, &QVideoWidget::fullScreenChanged, m_fullScreenButton, &QPushButton::setChecked);
        m_videoWidget->setFullScreen(false);
    // 如果视频可用，重新建立连接，并根据全屏按钮的勾选状态设置视频部件的全屏状态，同时启用颜色设置按钮
    } else {
        connect(m_fullScreenButton, &QPushButton::clicked, m_videoWidget, &QVideoWidget::setFullScreen);
        connect(m_videoWidget, &QVideoWidget::fullScreenChanged, m_fullScreenButton, &QPushButton::setChecked);

        if (m_fullScreenButton->isChecked())
            m_videoWidget->setFullScreen(true);
    }
}


// 更新m_trackInfo成员变量，并根据是否存在状态栏来设置相应的状态信息显示位置（状态栏或窗口标题）的信息
void Player::setTrackInfo(const QString &info)
{
    m_trackInfo = info;

    if (m_statusBar) {
        m_statusBar->showMessage(m_trackInfo);
        m_statusLabel->setText(m_statusInfo);
    } else {
        if (!m_statusInfo.isEmpty())
            setWindowTitle(QString("%1 | %2").arg(m_trackInfo).arg(m_statusInfo));
        else
            setWindowTitle(m_trackInfo);
    }
}


// 更新m_statusInfo成员变量，并根据是否存在状态栏来在状态信息显示位置（状态栏或窗口标题）的信息
void Player::setStatusInfo(const QString &info)
{
    m_statusInfo = info;

    if (m_statusBar) {
        m_statusBar->showMessage(m_trackInfo);
        m_statusLabel->setText(m_statusInfo);
    } else {
        if (!m_statusInfo.isEmpty())
            setWindowTitle(QString("%1 | %2").arg(m_trackInfo).arg(m_statusInfo));
        else
            setWindowTitle(m_trackInfo);
    }
}


// 设置状态信息显示为媒体播放器的错误字符串信息
void Player::displayErrorMessage()
{
    setStatusInfo(m_player->errorString());
}


// 根据当前播放位置和媒体总时长计算并设置时长信息显示格式
void Player::updateDurationInfo(qint64 currentInfo)
{
    QString tStr;
    if (currentInfo || m_duration) {
        QTime currentTime((currentInfo / 3600) % 60, (currentInfo / 60) % 60,
            currentInfo % 60, (currentInfo * 1000) % 1000);
        QTime totalTime((m_duration / 3600) % 60, (m_duration / 60) % 60,
            m_duration % 60, (m_duration * 1000) % 1000);
        QString format = "mm:ss";
        if (m_duration > 3600)
            format = "hh:mm:ss";
        tStr = currentTime.toString(format) + " / " + totalTime.toString(format);
    }
    m_labelDuration->setText(tStr);
}


