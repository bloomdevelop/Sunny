#pragma once

#include "channel_tree_model.hpp"
#include "markdown.hpp"
#include "message.hpp"

#include <QHash>
#include <QList>
#include <QMainWindow>
#include <QPointer>
#include <QStringList>

class ApiClient;
class MessageWidget;
class QHBoxLayout;
class QLabel;
class QScrollArea;
class QTextEdit;
class QToolButton;
class QVBoxLayout;

class ChatWindow : public QMainWindow
{
    Q_OBJECT
public:
    ChatWindow(const Channel &channel, const QString &currentUserId, ApiClient *api,
               QWidget *parent = nullptr);
    ~ChatWindow() override;

    QString channelId() const { return m_channel.id; }
    Channel channel() const { return m_channel; }

    void setForwardTargets(const QList<QPair<QString, QString>> &targets);
    void setCurrentUserName(const QString &displayName);

    // Gateway event handlers.
    void handleIncomingMessage(const QJsonObject &messageObject);
    void handleMessageUpdate(const QJsonObject &messageObject);
    void handleMessageDelete(const QString &messageId);
    void handleMessageDeleteBulk(const QList<QString> &messageIds);
    void handleReactionUpdate(const QJsonObject &data, bool added);
    void handleReactionRemoveAll(const QString &messageId);
    void handleReactionRemoveEmoji(const QString &messageId, const QJsonObject &emoji);
    void syncSessionState(const QString &sessionId);

signals:
    void channelActivated(const QString &channelId);

private:
    void buildUi();
    void buildMenus();
    void loadOlderMessages();
    void onJsonReceived(const QString &tag, const QJsonDocument &doc);
    void onApiError(const QString &tag, const QString &message);

    void appendMessage(const Message &message, bool atTop);
    void insertMessageSorted(const Message &message);
    int indexForMessageId(const QString &messageId) const;
    void prependMessages(const QList<Message> &messages);
    MessageWidget *createMessageWidget(const Message &message, bool compact);
    MessageWidget *insertMessageWidget(const Message &message, int index, bool compact);
    void removeMessageWidget(const QString &messageId);
    bool shouldGroupWith(const Message &previous, const Message &current) const;
    MessageWidget *widgetForMessage(const QString &messageId) const;
    void flushPendingMessages();
    void clearMessages();
    void loadInitialMessages(bool keepPendingMessages = false);

    void sendMessage();
    void startReply(const Message &target);
    void cancelReply();
    void updateReplyBanner();

    void toggleReaction(const QString &messageId, const Reaction &reaction, bool add);
    void addReactionWithDialog(const QString &messageId, QPoint globalPos);
    void deleteMessage(const QString &messageId);
    void copyMessageText(const QString &messageId);

    void showUserPopup(const User &user, QPoint globalPos);
    void forwardMessage(const Message &message);
    void scrollToMessage(const QString &messageId);

    void pickAttachments();
    void updateAttachmentStrip();

    void addKnownUser(const User &user);

    Channel m_channel;
    QString m_currentUserId;
    QString m_currentUserName;
    ApiClient *m_api = nullptr;

    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_messageContainer = nullptr;
    QVBoxLayout *m_messageLayout = nullptr;
    QTextEdit *m_composer = nullptr;
    QToolButton *m_sendButton = nullptr;
    QToolButton *m_attachButton = nullptr;
    QWidget *m_replyBanner = nullptr;
    QLabel *m_replyLabel = nullptr;
    QWidget *m_attachmentStrip = nullptr;
    QHBoxLayout *m_attachmentLayout = nullptr;

    QList<Message> m_messages;
    QHash<QString, MessageWidget *> m_messageWidgets;
    QList<Message> m_pendingMessages;
    QList<QString> m_recentlyDeleted;

    // Single resolver instance updated in place; widgets read it while they
    // are built, so no per-widget snapshot of the user/channel maps is kept.
    MentionResolver m_resolver;

    Message m_replyTarget;
    bool m_hasReplyTarget = false;

    QStringList m_pendingFiles;
    QString m_oldestMessageId;
    QString m_sessionId;
    bool m_loadingOlder = false;
    bool m_initialLoaded = false;

    QList<QPair<QString, QString>> m_forwardTargets;
};
