#pragma once

#include "markdown.hpp"
#include "message.hpp"

#include <QHash>
#include <QWidget>

class QLabel;
class QToolButton;
class QVBoxLayout;

class MessageWidget : public QWidget
{
    Q_OBJECT
public:
    explicit MessageWidget(const Message &message, const MentionResolver &resolver = {},
                           QWidget *parent = nullptr);

    const Message &message() const { return m_message; }
    QString messageId() const { return m_message.id; }

    void setCompact(bool compact);
    void setCurrentUserId(const QString &userId);
    void setReactions(const QList<Reaction> &reactions);

    // Highlights the message briefly, used when jumping from a reply.
    void flash();

signals:
    void authorClicked(const User &user);
    void replyRequested(const Message &message);
    void forwardRequested(const Message &message);
    void reactionToggled(const QString &messageId, const Reaction &reaction, bool add);
    void reactionAddRequested(const QString &messageId, QPoint globalPos);
    void deleteRequested(const QString &messageId);
    void copyRequested(const QString &messageId);
    void referenceClicked(const QString &messageId);
    void channelLinkClicked(const QString &channelId);

protected:
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void buildUi(const MentionResolver &resolver);
    QWidget *buildHeader(const MentionResolver &resolver);
    void rebuildReactions(QVBoxLayout *target);
    void handleLink(const QUrl &url);
    User userForId(const QString &id) const;

    Message m_message;
    QString m_currentUserId;
    bool m_compact = false;

    QWidget *m_header = nullptr;
    QWidget *m_reactionRow = nullptr;
    QVBoxLayout *m_bodyLayout = nullptr;
};
