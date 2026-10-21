#ifndef COMMONHELPER_H
#define COMMONHELPER_H

#include <QApplication>
#include <QAbstractSlider>
#include <QDesktopWidget>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QProcess>
#include <QPushButton>
#include <QRegExp>
#include <QSettings>
#include <QSlider>
#include <QStringLiteral>
#include <QTime>
#include <QTranslator>

/**
 * @brief 公共辅助类
 * 1. 设置皮肤样式
 * 2. 判断是否是IP地址
 * 3. 模糊延时
 * 4. 窗体剧中显示
 * 5. 设置为开机启动
 * 6. 设置为应用重启
 */
class CommonHelper : public QObject
{
public:
    CommonHelper() = delete;
    CommonHelper(const CommonHelper&) = delete;
    CommonHelper &operator=(const CommonHelper&) = delete;

    // 设置皮肤样式
    static void setStyleSheet(const QString &styleSheet, QObject *widget)
    {
        QFile file(styleSheet);
        if (file.open(QIODevice::ReadOnly))
        {
            QWidget *ptr = qobject_cast<QWidget *>(widget);
            if (ptr) {
                ptr->setStyleSheet(file.readAll());
            }
            else {
                qApp->setStyleSheet(file.readAll());
            }

            file.close();
        }
    }

    // 判断是否是IP地址
    static bool isIP(const QString &ip)
    {
        QRegExp regExp("((2[0-4]\\d|25[0-5]|[01]?\\d\\d?)\\.){3}(2[0-4]\\d|25[0-5]|[01]?\\d\\d?)");
        return regExp.exactMatch(ip);
    }

    /*
     * 给进度条/音量条换一套"粗一点"的样式。
     *
     * 默认 QSlider 的槽只有 4~6px，手指按不住；这里把槽画到 thickness 像素、
     * 圆点做到 thickness*1.9，并且**不改动控件本身的高度**（控件高度由调用方给，
     * 一般给 44~48px，多出来的部分就是手指的容错区）。
     */
    static void makeSliderThick(QAbstractSlider *slider, int thickness = 14)
    {
        if (!slider)
            return;
        const int h = qMax(6, thickness);
        const int half = h / 2;
        const int handle = qMax(18, h + 14);
        const int radius = handle / 2;

        slider->setStyleSheet(QString(
            "QSlider { background: transparent; }"
            /* 横向：槽 + 已播部分 + 圆点 */
            "QSlider::groove:horizontal { height: %1px; margin: 0px;"
            "  background: rgba(255,255,255,80); border-radius: %2px; }"
            "QSlider::sub-page:horizontal { background: #4FC3F7; border-radius: %2px; }"
            "QSlider::add-page:horizontal { background: rgba(255,255,255,70); border-radius: %2px; }"
            "QSlider::handle:horizontal { width: %3px; margin: -%4px 0px;"
            "  border-radius: %5px; background: #FFFFFF; border: 2px solid #1F4E79; }"
            "QSlider::handle:horizontal:pressed { background: #4FC3F7; }"
            /* 纵向：音量 */
            "QSlider::groove:vertical { width: %1px; margin: 0px;"
            "  background: rgba(255,255,255,80); border-radius: %2px; }"
            "QSlider::sub-page:vertical { background: rgba(255,255,255,70); border-radius: %2px; }"
            "QSlider::add-page:vertical { background: #4FC3F7; border-radius: %2px; }"
            "QSlider::handle:vertical { height: %3px; margin: 0px -%4px;"
            "  border-radius: %5px; background: #FFFFFF; border: 2px solid #1F4E79; }"
            "QSlider::handle:vertical:pressed { background: #4FC3F7; }"
        ).arg(h).arg(half).arg(handle).arg((handle - h) / 2).arg(radius));
    }

    /*
     * 返回键统一放大：板子是电阻/电容触摸屏，原来 20x30、31x28 这种尺寸手指根本点不到。
     * 图标是用 border-image 贴的，按钮放大图标会跟着放大，不会虚。
     */
    static void makeBackButtonBig(QPushButton *btn, int w = 64, int h = 64, int fontSize = 20)
    {
        if (!btn)
            return;
        btn->setMinimumSize(w, h);
        btn->setCursor(Qt::PointingHandCursor);
        if (!btn->text().isEmpty()) {
            btn->setStyleSheet(QString(
                "QPushButton { background: rgba(255,255,255,0.88); color: #1F4E79;"
                "  border: 2px solid #1F4E79; border-radius: 10px;"
                "  font-size: %1px; font-weight: bold; padding: 4px 18px; }"
                "QPushButton:pressed { background: #1F4E79; color: white; }").arg(fontSize));
        }
    }

    // 模糊延时
    static void sleep(unsigned long msec)
    {
        QTime deiTime = QTime::currentTime().addMSecs(msec);
        while (QTime::currentTime() < deiTime)
        {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 100);
        }
    }

    // 窗体剧中显示
    static void formInCenter(QWidget &widget)
    {
        QDesktopWidget w;
        int deskWidth = w.width();
        int deskHeight = w.height();

        QPoint centerPoint((deskWidth - widget.width()) / 2, (deskHeight - widget.height()) / 2);
        widget.move(centerPoint);
    }

    // 设置为开机启动
    static void autoRunWithSystem(bool isAutoRun, const QString &appName, const QString &appPath)
    {
        QSettings reg(QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run"), QSettings::NativeFormat);

        if (isAutoRun)
            reg.setValue(appName, appPath);
        else
            reg.setValue(appName, "");
    }

    // 应用重启
    static void reboot()
    {
        QString program = QApplication::applicationFilePath();
        QStringList argument = QApplication::arguments();
        QString workDirectory = QDir::currentPath();
        QProcess::startDetached(program, argument, workDirectory);
        QApplication::exit();
    }
	
    static void setTranslator(const QString &file)
    {
        auto translator = new QTranslator(qApp);
        if (translator->load(file))
            qApp->installTranslator(translator);
    }

	// 文字转语音
	/*
	#include <QTextToSpeech>
	QT += texttospeech
	static bool textToSpeech(const QString &text, double volume=1.0, double rate=0.0, double pitch=0.0)
	{
		if (QTextToSpeech::availableEngines().isEmpty())
			return false;

		static QTextToSpeech speech(qApp);

		if (speech.state() != QTextToSpeech::Ready)
			return false;

		speech.setVolume(volume);
		speech.setRate(rate);
		speech.setPitch(pitch);
		speech.say(text);

		return true;
	}
	*/
};

#endif // COMMONHELPER_H
