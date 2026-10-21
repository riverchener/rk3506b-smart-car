#ifndef VEHICLESTATUSPAGE_H
#define VEHICLESTATUSPAGE_H

#include <QWidget>
#include <QVector>

class CanMgr;
class CarStatusPanel;
class QListWidget;
class QLabel;
class QPushButton;
class QShowEvent;

/* ============================================================================
 * VehicleStatusPage —— 「车辆状态」独立页面（重塑：全屏一体化 HUD 驾驶舱）
 * ============================================================================
 * 2026-10-03 全新重构：
 * 1. 彻底推翻旧版左右并排双卡片和占据大半屏幕的拥挤列表；
 * 2. 居中一体化大面板 CarStatusPanel（内含全尺寸写实车模 + 环绕式 HUD 遥测仪表）；
 * 3. 底部改为科技感极简悬浮「CAN 实时数据监控流」浮动条（52px 高度，保证自绘面板获得
 *    高达 580px 的充裕垂直渲染空间，缩放比 s 达到 1.05~1.10，彻底杜绝小字号模糊）；
 * 4. 功能与协议 100% 保持：所有 CAN 帧报文解析解码、过滤展示、实时帧率统计与状态 chip。
 * ==========================================================================*/
class VehicleStatusPage : public QWidget
{
    Q_OBJECT

public:
    explicit VehicleStatusPage(CanMgr *can, QWidget *parent = nullptr);

signals:
    void back();

protected:
    void paintEvent(QPaintEvent *e) override;
    void showEvent(QShowEvent *e) override;

private slots:
    void onFrame(quint32 id, const QByteArray &data);
    void refreshChip();

private:
    static QString decode(quint32 id, const QByteArray &d);
    void applyFixedHeights();

    CanMgr         *m_can = nullptr;
    CarStatusPanel *m_panel = nullptr;
    QListWidget    *m_frames = nullptr;
    QLabel         *m_chip = nullptr;
    QLabel         *m_count = nullptr;
    QPushButton    *m_back = nullptr;
    QWidget        *m_topBar = nullptr;
    QWidget        *m_accent = nullptr;
    QWidget        *m_frameDock = nullptr;
    QLabel         *m_latestFrame = nullptr;

    int             m_n = 0;
};

#endif // VEHICLESTATUSPAGE_H
