#include "videowidget.h"

#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QStackedLayout>
#include <QVBoxLayout>

// VideoWidget类继承自QVideoWidget，用于创建一个自定义的视频显示部件，可处理一些特定的用户交互事件
VideoWidget::VideoWidget(QWidget *parent)
    : QVideoWidget(parent)
{

   setStyleSheet("background-color: transparent;");

}




// 重写鼠标双击事件处理函数，用于处理鼠标双击时的操作
void VideoWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    // 切换部件的全屏状态，即如果当前是全屏则设置为非全屏，反之亦然
    setFullScreen(!isFullScreen());

    // 接受该事件，表示该事件已被处理，不再向上传递给父部件或其他处理函数
    event->accept();
}

// 重写鼠标按下事件处理函数，用于处理鼠标按下时的操作
// 这里直接调用父类QVideoWidget的鼠标按下事件处理函数来处理该事件，以保持默认的鼠标按下处理行为
void VideoWidget::mousePressEvent(QMouseEvent *event)
{
    QVideoWidget::mousePressEvent(event);
}
