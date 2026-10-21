#ifndef SYSMON_H
#define SYSMON_H

/*
 * SysMon —— 系统健康实时监控页
 *
 * 显示 RK3506B 的：CPU 使用率、内存使用率、芯片温度、磁盘使用率，
 * 以及负载、运行时间、进程数、系统分区占用等辅助信息。
 * 数据来自 /proc 和 /sys（见 Hal::sysInfo），每秒刷新一次。
 *
 * 界面用 paintEvent 自绘：深色背景 + 圆角卡片 + 进度条 + 状态色，
 * 尺寸全部按页面宽高的比例算，所以配合 UiAdapter 在 1280x720 上满屏且不会错位。
 */

#include <QWidget>
#include <QTimer>
#include "hal.h"

class SysMon : public QWidget
{
    Q_OBJECT

public:
    explicit SysMon(QWidget *parent = nullptr);

    // 供离屏诊断用：立刻刷新一次数据
    void refreshNow();

signals:
    void backout();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private slots:
    void tick();

private:
    void drawCard(QPainter &p, const QRect &r, const QString &title,
                  const QString &value, const QString &sub,
                  double percent, const QColor &accent, bool showBar);
    void drawInfoTile(QPainter &p, const QRect &r, const QString &label, const QString &value);
    void updateBackRect();
    static QColor usageColor(double percent);

    Hal::SysInfo m_info;
    QTimer *m_timer;
    QRect m_backRect;
    int m_cpuSamples = 0;   // 头一两次采样 CPU 差值不准，界面上先显示“采样中”
};

#endif // SYSMON_H
