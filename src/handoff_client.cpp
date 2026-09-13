#include "handoff_client.hpp"
#include "api_client.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace {

QByteArray nativeUserAgent()
{
#if defined(Q_OS_ANDROID)
    return QByteArrayLiteral("Fluxer Android");
#elif defined(Q_OS_IOS)
    return QByteArrayLiteral("Fluxer iOS");
#elif defined(Q_OS_LINUX)
    return QByteArrayLiteral("Fluxer Linux");
#else
    return QByteArrayLiteral("Fluxer Desktop");
#endif
}

QByteArray reportedOs()
{
#if defined(Q_OS_ANDROID)
    return QByteArrayLiteral("android");
#elif defined(Q_OS_IOS)
    return QByteArrayLiteral("ios");
#elif defined(Q_OS_MACOS)
    return QByteArrayLiteral("macos");
#elif defined(Q_OS_WIN)
    return QByteArrayLiteral("windows");
#else
    return QByteArrayLiteral("linux");
#endif
}

} // namespace

HandoffClient::HandoffClient(const QUrl &baseUrl, QObject *parent)
    : QObject(parent)
    , m_api(new ApiClient(baseUrl, this))
    , m_timer(new QTimer(this))
{
    // Identify as a native Fluxer client so the approving device sees a name.
    m_api->setCommonHeader(QByteArrayLiteral("User-Agent"), nativeUserAgent());

    QJsonObject properties;
    properties.insert(QStringLiteral("os"), QString::fromLatin1(reportedOs()));
    m_api->setCommonHeader(
        QByteArrayLiteral("X-Fluxer-Client-Properties"),
        QJsonDocument(properties).toJson(QJsonDocument::Compact).toBase64());

    m_timer->setInterval(1500);
    connect(m_timer, &QTimer::timeout, this, &HandoffClient::poll);
    connect(m_api, &ApiClient::jsonReceived, this, &HandoffClient::handleJson);
    connect(m_api, &ApiClient::error, this, &HandoffClient::handleError);
}

void HandoffClient::start()
{
    if (m_active)
        return;

    m_active = true;
    m_awaitingInitiate = true;
    m_api->postJson(QStringLiteral("/v1/auth/handoff/initiate"),
                    QJsonDocument(QJsonObject{}));
}

void HandoffClient::stop()
{
    m_active = false;
    m_awaitingInitiate = false;
    m_timer->stop();
}

/**
 * @brief Cancels and deletes the token
 *
 * Must be used before completing
 *
 */
void HandoffClient::cancel()
{
    if (m_active && !m_code.isEmpty())
        m_api->deleteJson(QStringLiteral("/v1/auth/handoff/") + m_code);
    stop();
}

void HandoffClient::handleJson(const QJsonDocument &doc)
{
    if (!m_active)
        return;

    const QJsonObject obj = doc.object();

    if (m_awaitingInitiate) {
        m_awaitingInitiate = false;
        m_code = obj.value(QStringLiteral("code")).toString();
        m_pollSecret = obj.value(QStringLiteral("poll_secret")).toString();

        if (m_code.isEmpty() || m_pollSecret.isEmpty()) {
            stop();
            emit failed(tr("The instance did not return a handoff code."));
            return;
        }

        emit codeReceived(
            m_code,
            QDateTime::fromString(obj.value(QStringLiteral("expires_at")).toString(),
                                  Qt::ISODate));

        m_timer->start();
        poll();  // don't wait a full interval for the first poll
        return;
    }

    const QString status = obj.value(QStringLiteral("status")).toString();
    if (status == QLatin1String("completed")) {
        const QString token = obj.value(QStringLiteral("token")).toString();
        m_active = false;      // clear first so a later cancel() won't delete it
        m_timer->stop();

        if (token.isEmpty()) {
            emit failed(tr("The handoff completed without a session token."));
            return;
        }
        emit completed(token, obj.value(QStringLiteral("user_id")).toString());
    } else if (status == QLatin1String("expired")) {
        stop();
        emit expired();
    } else if (status != QLatin1String("pending")) {
        // Covers malformed/error bodies that ApiClient treats as a reply.
        stop();
        emit failed(tr("Unexpected handoff response."));
    }
}

void HandoffClient::handleError(const QString &message)
{
    if (!m_active)
        return;
    stop();
    emit failed(message);
}

void HandoffClient::poll()
{
    if (!m_active || m_code.isEmpty())
        return;

    QJsonObject body;
    body.insert(QStringLiteral("poll_secret"), m_pollSecret);
    m_api->postJson(
        QStringLiteral("/v1/auth/handoff/") + m_code + QStringLiteral("/status"),
        QJsonDocument(body));
}