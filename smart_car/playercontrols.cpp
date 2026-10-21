#include "playercontrols.h"
#include "commonhelper.h"

#include <QBoxLayout>
#include <QSlider>
#include <QStyle>
#include <QToolButton>
#include <QComboBox>
#include <QAudio>

// 按钮放大（原设计只有 24px，手指点不到）
static const int kBtnSize = 56;

PlayerControls::PlayerControls(QWidget *parent)
    : QWidget(parent)
{
    // 播放按钮，默认播放状态
    m_playButton = new QToolButton(this);
    QIcon customIcon(":/images/play.png");
    m_playButton->setIcon(customIcon);
    m_playButton->setIconSize(QSize(40, 40));
    m_playButton->setFixedSize(kBtnSize + 12, kBtnSize + 12);
    m_playButton->setStyleSheet(
        "QToolButton { "
        "background-color: transparent; "
        "}"
    );

    connect(m_playButton, &QAbstractButton::clicked, this, &PlayerControls::playClicked);

    // 停止按钮，默认不可用
    m_stopButton = new QToolButton(this);
    QIcon stopIcon(":/images/stop.png");
    m_stopButton->setIcon(stopIcon);
    m_stopButton->setIconSize(QSize(32, 32));
    m_stopButton->setFixedSize(kBtnSize, kBtnSize);
    m_stopButton->setEnabled(false);
    m_stopButton->setStyleSheet("QToolButton { background-color: transparent; }");

    connect(m_stopButton, &QAbstractButton::clicked, this, &PlayerControls::stop);

    // 下一曲按钮
    m_nextButton = new QToolButton(this);
    QIcon nextIcon(":/images/next.png");
    m_nextButton->setIcon(nextIcon);
    m_nextButton->setIconSize(QSize(32, 32));
    m_nextButton->setFixedSize(kBtnSize, kBtnSize);
    m_nextButton->setStyleSheet("QToolButton { background-color: transparent; }");

    connect(m_nextButton, &QAbstractButton::clicked, this, &PlayerControls::next);

    // 上一曲按钮
    m_previousButton = new QToolButton(this);
    QIcon lastIcon(":/images/last.png");
    m_previousButton->setIcon(lastIcon);
    m_previousButton->setIconSize(QSize(32, 32));
    m_previousButton->setFixedSize(kBtnSize, kBtnSize);
    m_previousButton->setStyleSheet("QToolButton { background-color: transparent; }");

    connect(m_previousButton, &QAbstractButton::clicked, this, &PlayerControls::previous);

    // 静音按钮
    m_muteButton = new QToolButton(this);
    QIcon soundopenIcon(":/images/sound_open.png");
    m_muteButton->setIcon(soundopenIcon);
    m_muteButton->setIconSize(QSize(32, 32));
    m_muteButton->setFixedSize(kBtnSize, kBtnSize);
    m_muteButton->setStyleSheet("QToolButton { background-color: transparent; }");

    connect(m_muteButton, &QAbstractButton::clicked, this, &PlayerControls::muteClicked);

    // 水平方向的音量滑块，设置其范围为0到100
    m_volumeSlider = new QSlider(Qt::Horizontal, this);
    m_volumeSlider->setMinimumWidth(140);
    m_volumeSlider->setFixedHeight(40);            // 46 -> 手指更好按（槽 12px，其余是容错区）
    m_volumeSlider->setRange(0, 100);
    m_volumeSlider->setStyleSheet("QToolButton { background-color: rgba(0, 0, 0, 0); }");
    CommonHelper::makeSliderThick(m_volumeSlider, 12);

    connect(m_volumeSlider, &QSlider::valueChanged, this, &PlayerControls::onVolumeSliderValueChanged);

    // 播放速率组合框
    m_rateBox = new QComboBox(this);
    m_rateBox->addItem("0.5x", QVariant(0.5));
    m_rateBox->addItem("1.0x", QVariant(1.0));
    m_rateBox->addItem("2.0x", QVariant(2.0));
    m_rateBox->setCurrentIndex(1);
    m_rateBox->setMinimumSize(80, 44);
    m_rateBox->setStyleSheet(
        "QComboBox {"
        "background-color: transparent; "  // 设置背景为透明
        "border: 1px solid white; "  // 设置边框为白色，这里边框宽度为1px，可根据需求调整宽度
        "color: white; "  // 设置字体颜色为白色
        "}"
        "QComboBox::drop-down {"
        "subcontrol-origin: padding; "
        "subcontrol-position: top right; "
        "width: 15px; "  // 下拉箭头区域宽度，可按需调整
        "border-left-width: 1px; "
        "border-left-color: white; "  // 下拉箭头区域左边框颜色设置为白色，保持整体边框风格一致
        "border-left-style: solid; "
        "}"
        "QComboBox::down-arrow {"
            "image: url(:/images/bottom.png); "  // 可自定义下拉箭头图标，这里需替换为实际路径
            "width: 20px;"
            "height: 20px;"
        "}"
    );

    connect(m_rateBox, QOverload<int>::of(&QComboBox::activated), this, &PlayerControls::updateRate);

    // 创建水平布局对象，用于放置所有的控制按钮和部件，设置其边距为0
    QBoxLayout *layout = new QHBoxLayout;
    layout->setMargin(0);
    layout->setSpacing(10);
    layout->addWidget(m_stopButton);
    layout->addWidget(m_previousButton);
    layout->addWidget(m_playButton);
    layout->addWidget(m_nextButton);
    layout->addWidget(m_muteButton);
    layout->addWidget(m_volumeSlider);
    layout->addWidget(m_rateBox);
    // 设置PlayerControls窗口的布局为上述创建的水平布局
    setLayout(layout);
}

// 获取媒体播放器当前状态
QMediaPlayer::State PlayerControls::state() const
{
    return m_playerState;
}

// 设置媒体播放器状态
void PlayerControls::setState(QMediaPlayer::State state)
{
    QIcon customIcon(":/images/play.png");
    QIcon pauseIcon(":/images/pause.png");
    if (state!= m_playerState) {
        m_playerState = state;

        switch (state) {
        case QMediaPlayer::StoppedState:
            // 当媒体播放器处于停止状态时，设置停止按钮不可用，播放按钮图标为播放图标
            m_stopButton->setEnabled(false);
            m_playButton->setIcon(customIcon);
            break;
        case QMediaPlayer::PlayingState:
            // 当媒体播放器处于播放状态时，设置停止按钮可用，播放按钮图标为暂停图标
            m_stopButton->setEnabled(true);
            m_playButton->setIcon(pauseIcon);
            break;
        case QMediaPlayer::PausedState:
            // 当媒体播放器处于暂停状态时，设置停止按钮可用，播放按钮图标为播放图标
            m_stopButton->setEnabled(true);
            m_playButton->setIcon(customIcon);
            break;
        }
    }
}

// 获取当前音量值
int PlayerControls::volume() const
{
    qreal linearVolume =  QAudio::convertVolume(m_volumeSlider->value() / qreal(100),
                                                QAudio::LogarithmicVolumeScale,
                                                QAudio::LinearVolumeScale);

    return qRound(linearVolume * 100);
}

// 设置音量值
void PlayerControls::setVolume(int volume)
{
    qreal logarithmicVolume = QAudio::convertVolume(volume / qreal(100),
                                                    QAudio::LinearVolumeScale,
                                                    QAudio::LogarithmicVolumeScale);

    m_volumeSlider->setValue(qRound(logarithmicVolume * 100));
}

// 判断是否静音
bool PlayerControls::isMuted() const
{
    return m_playerMuted;
}

// 设置静音状态
void PlayerControls::setMuted(bool muted)
{
     QIcon soundopenIcon(":/images/sound_open.png");
      QIcon soundcloseIcon(":/images/sound_close.png");
    if (muted!= m_playerMuted) {
        m_playerMuted = muted;

        m_muteButton->setIcon(muted
               ? soundopenIcon
                : soundcloseIcon);
    }
}


void PlayerControls::playClicked()
{
    switch (m_playerState) {
    case QMediaPlayer::StoppedState:
    case QMediaPlayer::PausedState:
        // 如果媒体播放器处于停止状态或暂停状态，则发送播放信号
        emit play();
        break;
    case QMediaPlayer::PlayingState:
        // 如果媒体播放器处于播放状态，则发送暂停信号
        emit pause();
        break;
    }
}


void PlayerControls::muteClicked()
{
    emit changeMuting(!m_playerMuted);
}


qreal PlayerControls::playbackRate() const
{
    return m_rateBox->itemData(m_rateBox->currentIndex()).toDouble();
}


void PlayerControls::setPlaybackRate(float rate)
{
    for (int i = 0; i < m_rateBox->count(); ++i) {
        if (qFuzzyCompare(rate, float(m_rateBox->itemData(i).toDouble()))) {
            m_rateBox->setCurrentIndex(i);
            return;
        }
    }

    m_rateBox->addItem(QString("%1x").arg(rate), QVariant(rate));
    m_rateBox->setCurrentIndex(m_rateBox->count() - 1);
}


void PlayerControls::updateRate()
{
    emit changeRate(playbackRate());
}


void PlayerControls::onVolumeSliderValueChanged()
{
    emit changeVolume(volume());
}
