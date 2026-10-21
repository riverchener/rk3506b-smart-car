#ifndef PLAYERCONTROLS_H
#define PLAYERCONTROLS_H

#include <QIcon>
#include <QMediaPlayer>
#include <QWidget>

QT_BEGIN_NAMESPACE
class QAbstractButton;
class QAbstractSlider;
class QComboBox;
QT_END_NAMESPACE


class PlayerControls : public QWidget
{
    Q_OBJECT

public:
    explicit PlayerControls(QWidget *parent = nullptr);

    // 获取媒体播放器的当前播放状态，返回值为QMediaPlayer::State类型
    QMediaPlayer::State state() const;
    // 获取媒体播放器的当前音量值
    int volume() const;
    // 判断媒体播放器是否处于静音状态
    bool isMuted() const;
    // 获取媒体播放器的当前播放速率
    qreal playbackRate() const;

public slots:
    // 用于设置媒体播放器的播放状态
    void setState(QMediaPlayer::State state);
    // 用于设置媒体播放器的音量值
    void setVolume(int volume);
    // 用于设置媒体播放器是否处于静音状态
    void setMuted(bool muted);
    // 用于设置媒体播放器的播放速率
    void setPlaybackRate(float rate);

signals:
    // 播放操作
    void play();
    // 暂停操作
    void pause();
    // 停止操作
    void stop();
    // 切换到下一曲操作
    void next();
    // 切换到上一曲操作
    void previous();
    // 通知其他对象音量已改变
    void changeVolume(int volume);
    // 通知其他对象静音状态已改变
    void changeMuting(bool muting);
    // 通知其他对象播放速率已改变
    void changeRate(qreal rate);

private slots:
    // 实现播放相关逻辑
    void playClicked();
    // 实现静音相关逻辑
    void muteClicked();
    // 用于更新播放速率相关设置，可能根据用户选择或其他条件更新速率值
    void updateRate();
    // 根据新的音量值设置媒体播放器音量的逻辑
    void onVolumeSliderValueChanged();

private:
    // 存储媒体播放器的当前播放状态，初始化为QMediaPlayer::StoppedState（停止状态）
    QMediaPlayer::State m_playerState = QMediaPlayer::StoppedState;
    // 存储媒体播放器是否处于静音状态，初始化为false（非静音）
    bool m_playerMuted = false;

    QAbstractButton *m_playButton = nullptr;
    QAbstractButton *m_stopButton = nullptr;
    QAbstractButton *m_nextButton = nullptr;
    QAbstractButton *m_previousButton = nullptr;
    QAbstractButton *m_muteButton = nullptr;
    QAbstractSlider *m_volumeSlider = nullptr;
    QComboBox *m_rateBox = nullptr;

};

#endif // PLAYERCONTROLS_H
