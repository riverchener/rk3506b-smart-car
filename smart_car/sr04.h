#ifndef SR04_H
#define SR04_H

#include <QMainWindow>
#include <QTimer>

class Sr04 : public QMainWindow
{
    Q_OBJECT
public:
    explicit Sr04(QWidget *parent = nullptr);
    void sr04_read(int *buf);

public slots:
    void timeto_read_sr04data(void);
signals:
    void readyto_read_sr04data(void);
private:
    QTimer *sr04_timer;
    bool lastOk = false;
    int lastUs = 0;
};

#endif // SR04_H
