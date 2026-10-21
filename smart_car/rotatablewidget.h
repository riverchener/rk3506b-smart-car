#ifndef ROTATABLEWIDGET_H
#define ROTATABLEWIDGET_H

#include <QWidget>
#include <QPointF>
#include <QPixmap>
#include <QPropertyAnimation>

namespace Ui {
class RotatableWidget;
}

class RotatableWidget : public QWidget
{
    Q_OBJECT
    Q_PROPERTY(qreal rotation READ rotation WRITE setRotation NOTIFY rotationChanged)

public:
    explicit RotatableWidget(QWidget *parent = nullptr,
                             const QPixmap &pixmap = QPixmap(),
                             const QPointF &rotationCenter = QPointF());
    ~RotatableWidget();
    qreal rotation() const;
    void setRotation(qreal newRotation);
    // 更换显示的图片（用于把唱片换成当前歌曲封面）
    void setPixmap(const QPixmap &pixmap) { m_pixmap = pixmap; update(); }
    // 是否自动以控件中心为旋转中心（唱片需要，唱臂需要固定支点所以设 false）
    void setAutoCenter(bool on) { m_autoCenter = on; update(); }

signals:
    void rotationChanged(qreal);

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    Ui::RotatableWidget *ui;
    qreal m_rotation;
    QPixmap m_pixmap;
    QPointF m_rotationCenter; // 旋转中心（autoCenter 为 false 时使用）
    bool m_autoCenter = false; // 默认沿用原来的固定中心，避免影响唱臂

};

#endif // ROTATABLEWIDGET_H
