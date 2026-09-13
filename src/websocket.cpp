#include "websocket.hpp"

WebSocket::WebSocket(QObject *parent)
    : QObject(parent)
{
    connect(&m_ws, &QWebSocket::connected, this, &WebSocket::connected);
    connect(&m_ws, &QWebSocket::disconnected, this, &WebSocket::disconnected);
    connect(&m_ws, &QWebSocket::textMessageReceived, this, &WebSocket::messageReceived);
    connect(&m_ws, &QWebSocket::errorOccurred, this,
            [this](QAbstractSocket::SocketError) { emit errorOccurred(m_ws.errorString()); });
}

void WebSocket::connectTo(const QUrl &url)
{
    m_ws.open(url);
}

void WebSocket::send(const QString &msg)
{
    m_ws.sendTextMessage(msg);
}

void WebSocket::close()
{
    m_ws.close();
}
