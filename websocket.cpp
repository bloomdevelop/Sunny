#include "websocket.hpp"

WebSocket::WebSocket(QObject *parent)
    : QObject(parent)
{
    connect(&m_ws, &QWebSocket::connected, this, &WebSocket::connected);
    connect(&m_ws, &QWebSocket::disconnected, this, &WebSocket::disconnected);
    connect(&m_ws, &QWebSocket::textMessageReceived, this, &WebSocket::messageReceived);
}

void WebSocket::connectTo(const QUrl &url) { m_ws.open(url); }
void WebSocket::send(const QString &msg) { m_ws.sendTextMessage(msg); }
