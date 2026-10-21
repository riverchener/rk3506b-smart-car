#ifndef BLUETOOTHPAGE_H
#define BLUETOOTHPAGE_H

/*
 * 蓝牙界面：显示附近设备，点击即可配对/连接。
 *
 * 界面结构（按 1024x600 设计画布，UiAdapter 负责缩放到实际屏幕）：
 *   顶部：返回 + 标题 + 蓝牙开关 + 扫描按钮 + 状态文字
 *   中部：设备列表（点一下选中，再用下面的按钮操作）
 *   底部：操作按钮（配对 / 连接 / 断开 / 忘记）+ 提示行
 *
 * 交互选择：不用"每行一个按钮"，因为那在 1024x600 里太挤、手指也难点。
 * 改成"选中一行 + 底部大按钮"，和车机其它页面的操作习惯一致。
 */

#include <QWidget>

#include "btmanager.h"

QT_BEGIN_NAMESPACE
class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QTimer;
QT_END_NAMESPACE

class BluetoothPage : public QWidget
{
    Q_OBJECT
public:
    explicit BluetoothPage(QWidget *parent = nullptr);

    BtManager *manager() const { return m_bt; }
    // 当前选中设备的地址（没选返回空）
    QString selectedAddress() const;

public slots:
    void bluetooth_working();          // 进入页面时调用（与其它页面命名一致）
    void triggerScanForTest();         // 自检用：等价于点一下"扫描设备"

protected:
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

signals:
    void backout();

private slots:
    void onDevicesChanged(const QVector<BtDevice> &devices);
    void onStateChanged();
    void onSelectionChanged();
    void onScanClicked();
    void onPowerClicked();
    void onPairClicked();
    void onConnectClicked();
    void onDisconnectClicked();
    void onForgetClicked();
    void onBackClicked();
    void onBtError(const QString &msg);
    void onBtInfo(const QString &msg);
    void refreshTick();

private:
    void rebuildList();
    void updateHeader();
    void updateButtons();
    void setHint(const QString &text, bool isError = false);

    BtManager *m_bt = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_stateLabel = nullptr;
    QLabel *m_hint = nullptr;
    QPushButton *m_back = nullptr;
    QPushButton *m_powerBtn = nullptr;
    QPushButton *m_scanBtn = nullptr;
    QPushButton *m_pairBtn = nullptr;
    QPushButton *m_connBtn = nullptr;
    QPushButton *m_discBtn = nullptr;
    QPushButton *m_forgetBtn = nullptr;
    QListWidget *m_list = nullptr;
    QTimer *m_tick = nullptr;

    QVector<BtDevice> m_devices;
    bool m_entered = false;
};

#endif // BLUETOOTHPAGE_H
