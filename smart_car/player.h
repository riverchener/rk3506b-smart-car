#ifndef PLAYER_H
#define PLAYER_H

#include "playercontrols.h"

#include <QWidget>
#include <QMediaPlayer>
// 引入播放列表相关头文件，用于管理媒体播放列表
#include <QMediaPlaylist>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QFileInfo>
#include <QDebug>
#include <QVariant>
#include <QList>

QT_BEGIN_NAMESPACE
class QAbstractItemView;
class QLabel;
class QMediaPlayer;
class QModelIndex;
class QPushButton;
class QSlider;
class QStatusBar;
class QVideoProbe;
class QVideoWidget;
class QAudioProbe;
QT_END_NAMESPACE

class PlaylistModel;
class HistogramWidget;

class Player : public QWidget
{
    Q_OBJECT

public:

    explicit Player(QWidget *parent = nullptr);
    ~Player();

    bool isPlayerAvailable() const;

    void addToPlaylist(const QList<QUrl> &urls);
    void setCustomAudioRole(const QString &role);
    QString findVideoPath(const QString &videoName);
    void loadPlaylistFromDatabase();
    bool isVideoAlreadyExists(const QString &videoName);
    // 【2026-10-18 ②(d)】播放列表为空时显示/撤掉"还没有视频"的占位提示
    void updateEmptyHint();
    QPushButton *openButton;
    QPushButton *backButton;
    PlayerControls *controls;
    void player_working();
    void clearConnections();
protected:
    // 【2026-10-18 ②(d)】跟随 m_playlistView 的 resize，让占位提示一直贴合列表区
    bool eventFilter(QObject *obj, QEvent *event) override;
signals:
    // 是否全屏
    void fullScreenChanged(bool fullScreen);
    // 从视频播放器界面跳回主界面
    void back();
private slots:
    // 打开文件
    void open();
    // 视频播放器界面跳回主界面
    void backout();
    // 当媒体的时长发生改变时被调用，传入改变后的时长值（以毫秒为单位）
    void durationChanged(qint64 duration);
    // 当媒体的播放位置发生改变时被调用，传入当前播放位置（以毫秒为单位）
    void positionChanged(qint64 progress);
    // 当媒体的元数据发生改变时被调用
    void metaDataChanged();

    // 用于处理点击上一曲按钮的操作
    void previousClicked();

    // 根据传入的秒数设置媒体的播放位置
    void seek(int seconds);
    // 当在播放列表视图中点击某个项目时被调用，传入点击项目的索引
    void jump(const QModelIndex &index);
    // 当播放列表的当前索引发生改变时被调用，传入新的当前索引值
    void playlistPositionChanged(int);

    // 当媒体播放器的媒体状态发生改变时被调用，传入改变后的媒体状态
    void statusChanged(QMediaPlayer::MediaStatus status);
    // 当媒体播放器的缓冲进度发生改变时被调用，传入新的缓冲进度值（以百分比表示）
    void bufferingProgress(int progress);
    // 当视频是否可用的状态发生改变时被调用，传入一个布尔值表示视频是否可用
    void videoAvailableChanged(bool available);

    // 槽函数，用于显示媒体播放器的错误信息
    void displayErrorMessage();

private:
    // 根据传入的信息设置媒体曲目相关的信息显示
    void setTrackInfo(const QString &info);
    // 根据传入的信息设置窗口底部状态信息的显示
    void setStatusInfo(const QString &info);
    // 根据媒体播放器的媒体状态处理鼠标光标显示，如在加载等状态时显示忙碌光标
    void handleCursor(QMediaPlayer::MediaStatus status);
    // 根据当前播放信息更新播放时长相关的信息显示
    void updateDurationInfo(qint64 currentInfo);

    // 媒体播放器对象，用于播放各种媒体文件
    QMediaPlayer *m_player = nullptr;
    // 播放列表对象，用于管理媒体播放的顺序等
    QMediaPlaylist *m_playlist = nullptr;
    // 视频显示部件，用于在窗口中显示视频内容
    QVideoWidget *m_videoWidget = nullptr;
    // 用于显示多媒体文件的封面图片的标签部件
    QLabel *m_coverLabel = nullptr;
    // 播放进度条部件，用于显示和控制媒体播放的进度
    QSlider *m_slider = nullptr;
    // 用于显示播放时长信息的标签部件
    QLabel *m_labelDuration = nullptr;
    // 全屏按钮部件，用于切换窗口的全屏模式
    QPushButton *m_fullScreenButton = nullptr;
    // 用于显示播放状态相关信息的标签部件
    QLabel *m_statusLabel = nullptr;
    // 窗口底部状态信息栏部件，用于显示各种状态信息
    QStatusBar *m_statusBar = nullptr;
    // 播放列表管理模型对象，用于对播放列表进行更高级的管理操作
    PlaylistModel *m_playlistModel = nullptr;
    // 列表视图部件，用于显示播放列表中的项目
    QAbstractItemView *m_playlistView = nullptr;
    // 用于存储媒体曲目相关信息的字符串，如曲目名称、艺术家等
    QString m_trackInfo;
    // 用于存储窗口底部状态信息的字符串，如播放状态、缓冲状态等
    QString m_statusInfo;
    // 用于存储媒体文件的总时长（以毫秒为单位）
    qint64 m_duration;

    QSqlDatabase m_database; // 数据库对象
    QList<QString> m_videoListFromDB; // 用于存储从数据库查询到的视频名称列表
    // 【2026-10-18 ②(d)】播放列表为空时的占位提示（浮在 m_playlistView 上，列表有内容就隐藏）
    QLabel *m_emptyHint = nullptr;
};

#endif // PLAYER_H
