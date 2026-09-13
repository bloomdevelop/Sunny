#include "api_client.hpp"

#include <QRestAccessManager>
#include <QNetworkAccessManager>
#include <QRestReply>
#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <optional>

/**
 * @brief Creates a new ApiClient::ApiClient
 * @param baseUrl
 * @param parent
 */
ApiClient::ApiClient(const QUrl &baseUrl, QObject *parent)
    : QObject(parent), m_factory(baseUrl)
{
    m_api = new QRestAccessManager(new QNetworkAccessManager(this), this);
}

/**
 * @brief Sets the token as header
 * @param token
 */
void ApiClient::setToken(const QString &token) {
    m_token = token;
    QHttpHeaders h = m_factory.commonHeaders();
    h.replaceOrAppend("Authorization", token);
    m_factory.setCommonHeaders(h);
}

/**
 * @brief Sends a request with GET method
 * @param path
 */
void ApiClient::getJson(const QString &path)
{
    QNetworkRequest req = m_factory.createRequest(path);
    m_api->get(req, this, [this](QRestReply &reply) {
        if (auto json = reply.readJson())
            emit jsonReceived(*json);
        else
            emit error(reply.errorString());
    });
}

/**
 * @brief Sends a request with GET method and identifies the response by tag
 * @param path
 * @param tag
 */
void ApiClient::getJson(const QString &path, const QString &tag)
{
    QNetworkRequest req = m_factory.createRequest(path);
    m_api->get(req, this, [this, tag](QRestReply &reply) {
        if (auto json = reply.readJson())
            emit taggedJsonReceived(tag, *json);
        else
            emit taggedError(tag, reply.errorString());
    });
}

/**
 * @brief Sends a request with POST method
 *
 * You must have the JSON body. Otherwise it doesn't send.
 *
 * @param path
 * @param doc
 */
void ApiClient::postJson(const QString &path, const QJsonDocument &doc)
{
    QNetworkRequest req = m_factory.createRequest(path);
    m_api->post(req, doc, this, [this](QRestReply &reply) {
        if (auto json = reply.readJson())
            emit jsonReceived(*json);
        else
            emit error(reply.errorString());
    });
}

/**
 * @brief Sends a request with POST method and identifies the response by tag
 * @param path
 * @param tag
 * @param doc
 */
void ApiClient::postJson(const QString &path, const QString &tag, const QJsonDocument &doc)
{
    QNetworkRequest req = m_factory.createRequest(path);
    m_api->post(req, doc, this, [this, tag](QRestReply &reply) {
        if (auto json = reply.readJson())
            emit taggedJsonReceived(tag, *json);
        else if (reply.isSuccess())
            emit taggedJsonReceived(tag, QJsonDocument{});
        else
            emit taggedError(tag, reply.errorString());
    });
}

/**
 * @brief Uploads files with a JSON payload as multipart/form-data
 *
 * The payload describes the attachments through their zero-based file index.
 *
 * @param path
 * @param tag
 * @param payload
 * @param filePaths
 */
void ApiClient::postMultipart(const QString &path, const QString &tag,
                              const QJsonDocument &payload, const QStringList &filePaths)
{
    auto *multiPart = new QHttpMultiPart(QHttpMultiPart::FormDataType);

    QHttpPart payloadPart;
    payloadPart.setHeader(QNetworkRequest::ContentDispositionHeader,
                          QVariant(QStringLiteral("form-data; name=\"payload_json\"")));
    payloadPart.setHeader(QNetworkRequest::ContentTypeHeader,
                          QVariant(QStringLiteral("application/json")));
    payloadPart.setBody(payload.toJson(QJsonDocument::Compact));
    multiPart->append(payloadPart);

    QMimeDatabase mimeDatabase;
    for (int index = 0; index < filePaths.size(); ++index) {
        const QString filePath = filePaths.at(index);
        auto *file = new QFile(filePath, multiPart);
        if (!file->open(QIODevice::ReadOnly)) {
            emit taggedError(tag, tr("Could not open %1").arg(filePath));
            multiPart->deleteLater();
            return;
        }

        const QFileInfo info(*file);
        QHttpPart filePart;
        filePart.setHeader(QNetworkRequest::ContentTypeHeader,
                           QVariant(mimeDatabase.mimeTypeForFile(info).name()));
        filePart.setHeader(
            QNetworkRequest::ContentDispositionHeader,
            QVariant(QStringLiteral("form-data; name=\"files[%1]\"; filename=\"%2\"")
                         .arg(index)
                         .arg(info.fileName())));
        filePart.setBodyDevice(file);
        multiPart->append(filePart);
    }

    QNetworkRequest req = m_factory.createRequest(path);
    m_api->post(req, multiPart, this, [this, tag, multiPart](QRestReply &reply) {
        if (auto json = reply.readJson())
            emit taggedJsonReceived(tag, *json);
        else if (reply.isSuccess())
            emit taggedJsonReceived(tag, QJsonDocument{});
        else
            emit taggedError(tag, reply.errorString());
        multiPart->deleteLater();
    });
}

/**
 * @brief Sends a request with PUT method and identifies the response by tag
 * @param path
 * @param tag
 * @param doc
 */
void ApiClient::putJson(const QString &path, const QString &tag, const QJsonDocument &doc)
{
    QNetworkRequest req = m_factory.createRequest(path);
    const auto handler = [this, tag](QRestReply &reply) {
        if (auto json = reply.readJson())
            emit taggedJsonReceived(tag, *json);
        else if (reply.isSuccess())
            emit taggedJsonReceived(tag, QJsonDocument{});
        else
            emit taggedError(tag, reply.errorString());
    };

    if (doc.isNull())
        m_api->put(req, QByteArray(), this, handler);
    else
        m_api->put(req, doc, this, handler);
}

/**
 * @brief Sends a request with PATCH method
 *
 * You must provide an partial JSON body
 *
 * @param path
 * @param doc
 */
void ApiClient::patchJson(const QString &path, const QJsonDocument &doc)
{
    QNetworkRequest req = m_factory.createRequest(path);
    m_api->patch(req, doc, this, [this](QRestReply &reply) {
        if (auto json = reply.readJson())
            emit jsonReceived(*json);
        else
            emit error(reply.errorString());
    });
}

/**
 * @brief Sends an request with DELETE method
 * @param path
 */
void ApiClient::deleteJson(const QString &path)
{
    QNetworkRequest req = m_factory.createRequest(path);
    m_api->deleteResource(req, this, [this](QRestReply &reply) {
        if (auto json = reply.readJson())
            emit jsonReceived(*json);
        else if (reply.isSuccess())
            emit jsonReceived(QJsonDocument{});
        else
            emit error(reply.errorString());
    });
}

/**
 * @brief Sends an request with DELETE method and identifies the response by tag
 * @param path
 * @param tag
 */
void ApiClient::deleteJson(const QString &path, const QString &tag)
{
    QNetworkRequest req = m_factory.createRequest(path);
    m_api->deleteResource(req, this, [this, tag](QRestReply &reply) {
        if (auto json = reply.readJson())
            emit taggedJsonReceived(tag, *json);
        else if (reply.isSuccess())
            emit taggedJsonReceived(tag, QJsonDocument{});
        else
            emit taggedError(tag, reply.errorString());
    });
}

/**
 * @brief Replace or appends new headers
 * @param name
 * @param value
 */
void ApiClient::setCommonHeader(const QByteArray &name, const QByteArray &value)
{
    QHttpHeaders h = m_factory.commonHeaders();
    h.replaceOrAppend(name, value);
    m_factory.setCommonHeaders(h);
}
