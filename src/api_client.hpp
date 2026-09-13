#pragma once

#include <QObject>
#include <QNetworkRequestFactory>
#include <QRestAccessManager>
#include <QByteArray>

class ApiClient : public QObject
{
    Q_OBJECT
public:
    explicit ApiClient(const QUrl &baseUrl, QObject *parent = nullptr);
    void setToken(const QString &token);
    void getJson(const QString &path);
    void postJson(const QString &path, const QJsonDocument &doc);
    void patchJson(const QString &path, const QJsonDocument &doc);
    void deleteJson(const QString &path);
    void setCommonHeader(const QByteArray &name, const QByteArray &value);

signals:
    void jsonReceived(const QJsonDocument &doc);
    void error(const QString &msg);

private:
    QRestAccessManager *m_api;
    QNetworkRequestFactory m_factory;
    QString m_token;
};
