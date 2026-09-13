#pragma once

#include <QObject>
#include <QNetworkRequestFactory>
#include <QRestAccessManager>
#include <QByteArray>
#include <QJsonDocument>

class ApiClient : public QObject
{
    Q_OBJECT
public:
    explicit ApiClient(const QUrl &baseUrl, QObject *parent = nullptr);
    void setToken(const QString &token);
    void getJson(const QString &path);
    void getJson(const QString &path, const QString &tag);
    void postJson(const QString &path, const QJsonDocument &doc);
    void postJson(const QString &path, const QString &tag, const QJsonDocument &doc);
    void postMultipart(const QString &path, const QString &tag, const QJsonDocument &payload,
                       const QStringList &filePaths);
    void putJson(const QString &path, const QString &tag, const QJsonDocument &doc = QJsonDocument());
    void patchJson(const QString &path, const QJsonDocument &doc);
    void deleteJson(const QString &path);
    void deleteJson(const QString &path, const QString &tag);
    void setCommonHeader(const QByteArray &name, const QByteArray &value);

signals:
    void jsonReceived(const QJsonDocument &doc);
    void taggedJsonReceived(const QString &tag, const QJsonDocument &doc);
    void error(const QString &msg);
    void taggedError(const QString &tag, const QString &msg);

private:
    QRestAccessManager *m_api;
    QNetworkRequestFactory m_factory;
    QString m_token;
};
