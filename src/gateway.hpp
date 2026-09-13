#pragma once

#include "websocket.hpp"

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QUrl>

class QTimer;

// Implements the main Gateway lifecycle (Hello -> Identify/Resume -> Heartbeat
// -> Ready) and forwards every Dispatch to listeners. The connection is
// re-established automatically after an unexpected close; after a reconnect
// the client is expected to resync state it may have missed.
class Gateway : public QObject
{
    Q_OBJECT
public:
    explicit Gateway(QObject *parent = nullptr);

    void connectTo(const QUrl &gatewayUrl, const QString &token);
    void disconnectFromGateway();

    QString sessionId() const { return m_sessionId; }

signals:
    void connected();     // WebSocket opened
    void ready();         // READY or RESUMED received; session is authenticated
    void disconnected();
    void dispatch(const QString &type, const QJsonObject &data);
    void errorOccurred(const QString &message);

private:
    void handleMessage(const QString &message);
    void identify();
    void resume();
    void sendHeartbeat();
    void startReconnect();
    void openSocket();
    void resetSession();

    WebSocket m_socket;
    QTimer *m_heartbeatTimer = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QString m_token;
    QString m_sessionId;
    QUrl m_gatewayUrl;
    int m_sequence = -1;
    int m_reconnectAttempts = 0;
    bool m_hasSession = false;
    bool m_shouldReconnect = false;
};
