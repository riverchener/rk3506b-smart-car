#ifndef Hardware_H
#define Hardware_H

#include <QMainWindow>
#include <QTimer>
#include <QCheckBox>
#include <QShowEvent>
#include <QHideEvent>
#include <QLabel>
#include "buzzer.h"
#include "dht11.h"
#include "led.h"
#include "mq135.h"
#include "sr04.h"

QT_BEGIN_NAMESPACE
namespace Ui { class Hardware; }
QT_END_NAMESPACE

class Hardware : public QMainWindow
{
    Q_OBJECT

public:
    Hardware(QWidget *parent = nullptr);
    ~Hardware();
    // 手动报警器开关：true=开始“滴滴滴”间歇报警，false=停止并恢复正常状态
    void setManualAlarm(bool on);
    bool manualAlarm() const { return m_manualAlarm; }
    // 自动报警（超声波 <30cm）：也是“滴滴滴”间歇，不是长鸣
    void setAutoAlarm(bool on);
    // 后台保持近距离警告：开启后退出环境检测页仍然持续测距报警
    bool backgroundAlarm() const { return m_bgAlarm; }
    void loadBackgroundAlarmSetting();

signals:
    // 后台警告开关状态变化（主页用它更新“安全警告开启中/关闭”文字）
    void backgroundAlarmChanged(bool on);
    void back1();
public slots:
    void dht11_handler(void);
    void sr04_handler(void);
    void mq135_handler(void);
    void hardware_working(void);

private slots:
    void on_back1_clicked();
    void onBeepTick();
    void onBgAlarmToggled(bool on);
    void onBgAlarmTick();
    void onBgAlarmSaveTimer();

protected:
    void paintEvent(QPaintEvent *event) override;
    // 页面显示/隐藏时切换"前台/后台"测距职责
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

private:
    Ui::Hardware *ui;
    DHT11* dht11;
    Sr04* sr04;
    Buzzer* buzzer;
    Led* led;
    Mq135* mq135;

    /* MQ-135 的"状态 + 真实电压"副标签（在代码里加进空气质量卡片，见构造函数） */
    QLabel *m_labelMqSub = nullptr;

    bool dht11Valid = false;
    bool sr04Valid = false;
    bool mq135Valid = false;
    int mq135Last = 0;

    // 手动报警器（主页左下角按钮）
    bool m_manualAlarm = false;
    // 自动报警（超声波 <30cm）
    bool m_autoAlarm = false;
    bool m_beepOn = false;
    QTimer *m_beepTimer = nullptr;

    /*
     * 后台保持近距离警告：
     *   开启后，退出环境检测页仍然持续测距，<30cm 就报警。
     *   状态持久化到 /userdata/smart_car/bg_alarm.conf，重启后保留。
     */
    bool m_bgAlarm = false;
    QTimer *m_bgTimer = nullptr;        // 后台测距轮询（200ms）
    QTimer *m_bgSaveTimer = nullptr;    // 状态变化后延迟保存，避免频繁写 flash
};
#endif // MAINWINDOW_H
