#pragma once

#include "src/channel_tree_model.hpp"

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QPointer>
#include <QSet>
#include <QString>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class ApiClient;
class ChatWindow;
class Gateway;
class QJsonDocument;
class QJsonObject;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

private:
    void startWithToken(const QString &token);
    void loadAccountData();
    void onJsonReceived(const QString &tag, const QJsonDocument &doc);
    void onDispatch(const QString &type, const QJsonObject &data);
    void updateUserInfo(const QJsonObject &user);
    void updateUserSettings(const QJsonObject &settings);
    void applyStatus(const QJsonObject &source);
    void rebuildChannelTree();
    void captureExpansionState();
    void applyExpansionState();
    bool defaultExpanded(const QModelIndex &index) const;

    void openChannel(const QString &channelId);
    void openChatWindow(const Channel &channel);
    bool findChannel(const QString &channelId, Channel *channel) const;
    QList<QPair<QString, QString>> forwardTargets() const;
    void routeDispatch(const QString &type, const QJsonObject &data);

    Ui::MainWindow *ui;
    ApiClient *m_api = nullptr;
    Gateway *m_gateway = nullptr;
    ChannelTreeModel *m_channelModel = nullptr;
    QString m_token;
    QString m_userId;
    QString m_userDisplayName;
    QList<Channel> m_privateChannels;
    QList<Guild> m_guilds;
    QSet<QString> m_knownKeys;
    QSet<QString> m_expandedKeys;
    QHash<QString, QPointer<ChatWindow>> m_chatWindows;
};
