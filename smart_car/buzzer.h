#ifndef BUZZER_H
#define BUZZER_H

#include <QMainWindow>

class Buzzer : public QMainWindow
{
    Q_OBJECT
public:
    explicit Buzzer(QWidget *parent = nullptr);
    // 兼容原有调用方式：buf == 0 表示“报警/响”，非 0 表示静音
    int buzzer_write(char buf);
signals:

private:
};

#endif // BUZZER_H
