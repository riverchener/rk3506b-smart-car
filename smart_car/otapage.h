#ifndef OTAPAGE_H
#define OTAPAGE_H

/*
 * OtaPage —— 「系统升级」页（2026-10-12 新增）
 * ============================================================================
 * 这个页面**不做 OTA 逻辑**，它只是板端命令行工具 `smartcar-ota` 的一层遥控：
 *
 *     查版本 -> QProcess 调 `smartcar-ota check`
 *     升级   -> QProcess 调 `smartcar-ota check` + `smartcar-ota update --force`
 *     回滚   -> QProcess 调 `smartcar-ota rollback`
 *
 * 为什么这么分层（而不是在 Qt 里用 QNetworkAccessManager 自己下载、自己换文件）：
 *   1. OTA 升级要在**应用进程之外**替换应用自己的可执行文件并重启自己，
 *      让被替换的进程自己去覆盖自己、再杀掉自己，本身就很别扭；
 *      交给 init 脚本 + shell 工具是最自然、最容易恢复的做法。
 *   2. 升级过程中应用会被 kill，"界面上的实时进度"其实看不到。所以真正的
 *      进度与结果记录必须落在文件里（/var/log/smartcar-ota.log），
 *      页面重开后再读日志把结果呈现出来 —— 而不是假装能实时显示进度。
 *   3. 命令行工具能脱离界面单独用、能在 adb/串口上用、能写进文档被复现，
 *      界面只是它的一个入口。面试时"界面 + 工具"的分层也讲得通。
 *
 * 尺寸：直接用 1280x720 真实坐标（和 MainWindow::buildDesktop 一致），
 * **不调用 UiAdapter::fit()** —— 那一套是按 1024x600 设计画布 ×1.2 缩放，
 * 本页用布局管理器自适应，少一层缩放就少一类"显示不全"的坑。
 */

#include <QWidget>
#include <QString>
#include <QStringList>

class QLabel;
class QPushButton;
class QTextEdit;
class QProcess;
class QTimer;

class OtaPage : public QWidget
{
    Q_OBJECT

public:
    explicit OtaPage(QWidget *parent = nullptr);

    /* 进入页面/操作结束后重新采集版本信息 */
    void refreshStatus();

signals:
    void backout();

protected:
    void showEvent(QShowEvent *e) override;

private slots:
    void onCheck();
    void onUpdate();
    void onRollback();
    void onProcStdout();
    void onProcFinished(int code, int status);
    void onTick();

private:
    void buildUi();
    void layoutUi();
    void runNext();                       // 顺序执行队列里的下一条命令
    void setBusy(bool busy, const QString &what);
    void appendLog(const QString &s);
    void loadToolLogTail();
    static QString runCapture(const QStringList &programAndArgs);
    static QString fileVersionTag(const QString &path);
    /* 一次 sh 调用把所有版本事实取回来（见 .cpp 里的说明：板子上每次
     * QProcess 启动都要几百毫秒，拆成 5 次会把界面卡住好几秒）。 */
    static bool collectVersions(QString *disk, QString *run);

    QWidget *m_bar = nullptr;
    QWidget *m_info = nullptr;
    QWidget *m_ops = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_sub = nullptr;
    QLabel *m_chip = nullptr;
    QLabel *m_lDisk = nullptr;
    QLabel *m_lRun = nullptr;
    QLabel *m_lSrv = nullptr;
    QLabel *m_lNotes = nullptr;
    QTextEdit *m_log = nullptr;
    QPushButton *m_back = nullptr;
    QPushButton *m_check = nullptr;
    QPushButton *m_update = nullptr;
    QPushButton *m_rollback = nullptr;
    QProcess *m_proc = nullptr;
    QTimer *m_tick = nullptr;

    QStringList m_queue;      // 待执行的 smartcar-ota 参数，如 "update --force"
    QString m_tool;           // smartcar-ota 的实际路径
    QString m_lastResult;     // 最后一条 OTA_* 结果行
    bool m_busy = false;
    int m_elapsed = 0;
};

#endif // OTAPAGE_H
