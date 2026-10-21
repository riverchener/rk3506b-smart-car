#ifndef DHT11_H
#define DHT11_H

#include <QMainWindow>
#include <QTimer>

class DHT11 : public QMainWindow
{
    Q_OBJECT
public:
    explicit DHT11(QWidget *parent = nullptr);
    void dht11_read(char *buf);

public slots:
    void timeto_read_dht11data(void);
signals:
    void readyto_read_dht11data(void);

private:
    QTimer *dht11_timer;
    bool lastOk = false;
    unsigned char last[4] = {0, 0, 0, 0};
};

#endif // DHT11_H
