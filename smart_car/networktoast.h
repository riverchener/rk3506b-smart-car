#ifndef NETWORKTOAST_H
#define NETWORKTOAST_H

/*
 * NetworkToast —— “已成功连接网络！”提示框
 *
 * 样式沿用项目里硬件页那种白底圆角卡片风格（default1/info.png 一套视觉），
 * 但按 1280x720 屏放大了字号，保证看得清、点得到。
 *
 * 两个用法：
 *   showToast(parent)  非阻塞浮层：盖在主界面上，5 秒后自动关闭，也可点“确定”
 *   showModal(parent)  模态对话框：阻塞直到点“确定”（离屏诊断截图用）
 */

#include <QDialog>
#include <QString>

class NetworkToast : public QDialog
{
    Q_OBJECT
public:
    // 通用提示：iconPath = 资源图标，title = 主标题（大字），detail = 明细（可空）
    explicit NetworkToast(QWidget *parent, const QString &iconPath,
                          const QString &title, const QString &detail);
    NetworkToast(QWidget *parent, const QString &ssid, const QString &ip);

    static void showMessage(QWidget *parent, const QString &iconPath,
                            const QString &title, const QString &detail,
                            int autoCloseMs = 6000);
    static void showToast(QWidget *parent, const QString &ssid, const QString &ip);
    static void showDisconnected(QWidget *parent, const QString &reason);
    static void showModal(QWidget *parent, const QString &ssid, const QString &ip);

private:
    void buildUi(const QString &iconPath, const QString &title, const QString &detail);
    void centerOnScreen();

    QString m_icon;
    QString m_title;
    QString m_detail;
};

#endif // NETWORKTOAST_H
