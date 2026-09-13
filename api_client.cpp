#include "api_client.hpp"

#include <QRestAccessManager>
#include <QNetworkAccessManager>
#include <QRestReply>
#include <QJsonDocument>
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

