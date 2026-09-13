#include "gateway.hpp"

#include <QJsonDocument>
#include <QJsonValue>
#include <QSysInfo>
#include <QTimer>
#include <QUrlQuery>

#include <algorithm>

namespace {

constexpr int kMaxReconnectDelayMs = 30000;
constexpr int kBaseReconnectDelayMs = 1000;
constexpr int kMaxReconnectAttempts = 10;

} // namespace

Gateway::Gateway(QObject *parent)
    : QObject(parent)
    , m_heartbeatTimer(new QTimer(this))
    , m_reconnectTimer(new QTimer(this))
{
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this] {
        if (!m_shouldReconnect || m_gatewayUrl.isEmpty())
            return;
        openSocket();
    });

    connect(&m_socket, &WebSocket::connected, this, [this] {
        m_reconnectAttempts = 0;
        emit connected();
    });
    connect(&m_socket, &WebSocket::disconnected, this, [this] {
        m_heartbeatTimer->stop();
        emit disconnected();
        startReconnect();
    });
    connect(&m_socket, &WebSocket::messageReceived, this, &Gateway::handleMessage);
    connect(&m_socket, &WebSocket::errorOccurred, this, &Gateway::errorOccurred);
    connect(m_heartbeatTimer, &QTimer::timeout, this, &Gateway::sendHeartbeat);
}

void Gateway::connectTo(const QUrl &gatewayUrl, const QString &token)
{
    m_gatewayUrl = gatewayUrl;
    m_token = token;
    resetSession();

    m_shouldReconnect = true;
    m_reconnectTimer->stop();
    m_reconnectAttempts = 0;

    openSocket();
}

void Gateway::openSocket()
{
    QUrl url = m_gatewayUrl;
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("v"), QStringLiteral("1"));
    query.addQueryItem(QStringLiteral("encoding"), QStringLiteral("json"));
    url.setQuery(query);

    m_heartbeatTimer->stop();
    m_socket.close();
    m_socket.connectTo(url);
}

void Gateway::disconnectFromGateway()
{
    m_shouldReconnect = false;
    m_reconnectTimer->stop();
    m_heartbeatTimer->stop();
    m_socket.close();
}

void Gateway::resetSession()
{
    m_sessionId.clear();
    m_sequence = -1;
    m_hasSession = false;
}

void Gateway::handleMessage(const QString &message)
{
    const QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject())
        return;

    const QJsonObject payload = doc.object();
    const int op = payload.value(QStringLiteral("op")).toInt(-1);

    switch (op) {
    case 10: { // Hello
        const QJsonObject data = payload.value(QStringLiteral("d")).toObject();
        const int interval = data.value(QStringLiteral("heartbeat_interval")).toInt(41250);
        m_heartbeatTimer->start(interval);
        if (m_hasSession)
            resume();
        else
            identify();
        break;
    }
    case 11: // Heartbeat ACK
        break;
    case 1: // The server asks for a heartbeat
        sendHeartbeat();
        break;
    case 7: // Reconnect
        m_heartbeatTimer->stop();
        m_socket.close();
        break;
    case 9: // Invalid session: start over
        resetSession();
        QTimer::singleShot(1000, this, [this] {
            if (m_shouldReconnect && !m_gatewayUrl.isEmpty())
                identify();
        });
        break;
    case 0: { // Dispatch
        m_sequence = payload.value(QStringLiteral("s")).toInt(m_sequence);
        const QString type = payload.value(QStringLiteral("t")).toString();
        const QJsonObject data = payload.value(QStringLiteral("d")).toObject();

        if (type == QLatin1String("READY")) {
            m_sessionId = data.value(QStringLiteral("session_id")).toString();
            m_hasSession = !m_sessionId.isEmpty();
        }

        emit dispatch(type, data);

        if (type == QLatin1String("READY") || type == QLatin1String("RESUMED"))
            emit ready();
        break;
    }
    default:
        break;
    }
}

void Gateway::identify()
{
    QJsonObject properties;
    properties.insert(QStringLiteral("os"), QSysInfo::productType());
    properties.insert(QStringLiteral("browser"), QStringLiteral("Fluxer Client"));
    properties.insert(QStringLiteral("device"), QStringLiteral("desktop"));

    QJsonObject data;
    data.insert(QStringLiteral("token"), m_token);
    data.insert(QStringLiteral("properties"), properties);

    QJsonObject payload;
    payload.insert(QStringLiteral("op"), 2);
    payload.insert(QStringLiteral("d"), data);

    m_socket.send(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}

void Gateway::resume()
{
    QJsonObject data;
    data.insert(QStringLiteral("token"), m_token);
    data.insert(QStringLiteral("session_id"), m_sessionId);
    data.insert(QStringLiteral("seq"), m_sequence);

    QJsonObject payload;
    payload.insert(QStringLiteral("op"), 6);
    payload.insert(QStringLiteral("d"), data);

    m_socket.send(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}

void Gateway::sendHeartbeat()
{
    QJsonObject payload;
    payload.insert(QStringLiteral("op"), 1);
    payload.insert(QStringLiteral("d"),
                   m_sequence < 0 ? QJsonValue() : QJsonValue(m_sequence));

    m_socket.send(QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}

void Gateway::startReconnect()
{
    if (!m_shouldReconnect || m_gatewayUrl.isEmpty())
        return;
    if (m_reconnectAttempts >= kMaxReconnectAttempts) {
        emit errorOccurred(tr("Connection lost. Reconnect attempts exhausted."));
        return;
    }

    const int delay = std::min(kBaseReconnectDelayMs * (1 << m_reconnectAttempts),
                               kMaxReconnectDelayMs);
    ++m_reconnectAttempts;
    m_reconnectTimer->start(delay);
}
