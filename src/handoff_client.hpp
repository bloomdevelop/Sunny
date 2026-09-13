#pragma once

#include <QObject>
#include <QDateTime>
#include <QJsonDocument>
#include <QString>
#include <QUrl>

class ApiClient;
class QTimer;

class HandoffClient : public QObject
{
    Q_OBJECT
public:
    explicit HandoffClient(const QUrl &baseUrl, QObject *parent = nullptr);

    void start();
    void stop();
    void cancel();

signals:
    void codeReceived(const QString &code, const QDateTime &expiresAt);
    void completed(const QString &token, const QString &userId);
    void expired();
    void failed(const QString &msg);

private:
    void handleJson(const QJsonDocument &doc);
    void handleError(const QString &msg);
    void poll();

    ApiClient *m_api;
    QTimer *m_timer;
    QString m_code;
    QString m_pollSecret;
    bool m_active = false;
    bool m_awaitingInitiate = false;
};
