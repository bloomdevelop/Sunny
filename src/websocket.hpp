#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QWebSocket>

class WebSocket : public QObject
{
    Q_OBJECT
public:
    explicit WebSocket(QObject *parent = nullptr);

    void connectTo(const QUrl &url);
    void send(const QString &msg);
    void close();

signals:
    void messageReceived(const QString &msg);
    void connected();
    void disconnected();
    void errorOccurred(const QString &message);

private:
    QWebSocket m_ws;
};
