#pragma once

#include <QObject>
#include <QUrl>
#include <QString>
#include <QWebSocket>

class WebSocket : public QObject
{
    Q_OBJECT
public:
    explicit WebSocket(QObject *parent = nullptr);
    void connectTo(const QUrl &url);
    void send(const QString &msg);

signals:
    void messageReceived(const QString &msg);
    void connected();
    void disconnected();

private:
    QWebSocket m_ws;
};
