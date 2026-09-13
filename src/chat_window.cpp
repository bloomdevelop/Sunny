#include "chat_window.hpp"

#include "api_client.hpp"
#include "media_utils.hpp"
#include "message.hpp"
#include "message_widget.hpp"
#include "user_popup.hpp"

#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QScrollBar>
#include <QStatusBar>
#include <QTextEdit>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <algorithm>

namespace {

// Snowflake IDs are decimal strings of the same length, so a plain string
// comparison places them in chronological order.
int compareMessageIds(const QString &a, const QString &b)
{
    if (a.isEmpty() || b.isEmpty())
        return 0;
    return QString::compare(a, b, Qt::CaseInsensitive);
}

class MessageComposer : public QTextEdit
{
    Q_OBJECT
public:
    explicit MessageComposer(QWidget *parent = nullptr)
        : QTextEdit(parent)
    {
        setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
        connect(document(), &QTextDocument::contentsChanged, this,
                &MessageComposer::updateHeight);
        connect(this, &QTextEdit::textChanged, this, &MessageComposer::updateHeight);
        updateHeight();
    }

    std::function<void()> sendHandler;

    int singleLineHeight() const
    {
        const int contents = qRound(document()->documentMargin() * 2)
            + contentsMargins().top() + contentsMargins().bottom() + frameWidth() * 2;
        return fontMetrics().lineSpacing() + contents;
    }

public slots:
    void updateHeight()
    {
        const int maxHeight = maximumHeight();
        const int minHeight = qMin(singleLineHeight(), maxHeight);
        const int docHeight = qRound(document()->size().height())
            + contentsMargins().top() + contentsMargins().bottom() + frameWidth() * 2;
        setFixedHeight(qBound(minHeight, docHeight, maxHeight));
        setVerticalScrollBarPolicy(height() >= maxHeight ? Qt::ScrollBarAsNeeded
                                                         : Qt::ScrollBarAlwaysOff);
    }

protected:
    void keyPressEvent(QKeyEvent *event) override
    {
        const bool isEnter =
            event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
        if (isEnter && !(event->modifiers() & Qt::ShiftModifier)) {
            if (sendHandler)
                sendHandler();
            event->accept();
            return;
        }
        QTextEdit::keyPressEvent(event);
    }
};

QString encodeEmojiPath(const Reaction &reaction)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(reaction.pathValue()));
}

void applyReactionChange(QList<Reaction> &reactions, const Reaction &reaction, bool add)
{
    const QString key = reaction.key();
    int index = -1;
    for (int i = 0; i < reactions.size(); ++i) {
        if (reactions.at(i).key() == key) {
            index = i;
            break;
        }
    }

    if (add) {
        if (index < 0) {
            Reaction created = reaction;
            created.count = 1;
            created.me = reaction.me;
            reactions.append(created);
        } else {
            reactions[index].count += 1;
            reactions[index].me = true;
        }
    } else if (index >= 0) {
        reactions[index].count -= 1;
        reactions[index].me = false;
        if (reactions[index].count <= 0)
            reactions.removeAt(index);
    }
}

} // namespace

ChatWindow::ChatWindow(const Channel &channel, const QString &currentUserId, ApiClient *api,
                       QWidget *parent)
    : QMainWindow(parent)
    , m_channel(channel)
    , m_currentUserId(currentUserId)
    , m_api(api)
{
    setAttribute(Qt::WA_DeleteOnClose);
    QString title = channel.displayName;
    if (channel.type == Channel::GuildText || channel.type == Channel::GuildVoice
        || channel.type == Channel::GuildLink) {
        title.prepend(QLatin1Char('#'));
    }
    setWindowTitle(title + QStringLiteral(" — Sunny"));
    resize(760, 620);

    buildUi();
    buildMenus();

    connect(m_api, &ApiClient::taggedJsonReceived, this, &ChatWindow::onJsonReceived);
    connect(m_api, &ApiClient::taggedError, this, &ChatWindow::onApiError);

    m_resolver.channelNames.insert(m_channel.id, m_channel.displayName);

    loadInitialMessages();
}

ChatWindow::~ChatWindow() = default;

void ChatWindow::buildUi()
{
    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_scrollArea = new QScrollArea(central);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    m_messageContainer = new QWidget(m_scrollArea);
    m_messageLayout = new QVBoxLayout(m_messageContainer);
    m_messageLayout->setContentsMargins(4, 8, 4, 8);
    m_messageLayout->setSpacing(0);
    m_messageLayout->addStretch(1);
    m_scrollArea->setWidget(m_messageContainer);
    layout->addWidget(m_scrollArea, 1);

    connect(m_scrollArea->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        if (!m_initialLoaded || m_loadingOlder || m_oldestMessageId.isEmpty())
            return;
        if (value <= m_scrollArea->verticalScrollBar()->minimum())
            loadOlderMessages();
    });

    // Reply banner
    m_replyBanner = new QWidget(central);
    auto *replyLayout = new QHBoxLayout(m_replyBanner);
    replyLayout->setContentsMargins(10, 4, 10, 4);
    replyLayout->setSpacing(6);
    m_replyLabel = new QLabel(m_replyBanner);
    m_replyLabel->setTextFormat(Qt::RichText);
    replyLayout->addWidget(m_replyLabel, 1);
    auto *cancelReplyButton = new QToolButton(m_replyBanner);
    cancelReplyButton->setText(QStringLiteral("×"));
    cancelReplyButton->setAutoRaise(true);
    cancelReplyButton->setToolTip(tr("Cancel reply"));
    connect(cancelReplyButton, &QToolButton::clicked, this, &ChatWindow::cancelReply);
    replyLayout->addWidget(cancelReplyButton);
    m_replyBanner->hide();
    layout->addWidget(m_replyBanner);

    // Attachment strip
    m_attachmentStrip = new QWidget(central);
    m_attachmentLayout = new QHBoxLayout(m_attachmentStrip);
    m_attachmentLayout->setContentsMargins(10, 4, 10, 4);
    m_attachmentLayout->setSpacing(6);
    m_attachmentStrip->hide();
    layout->addWidget(m_attachmentStrip);

    // Composer
    auto *composerRow = new QWidget(central);
    auto *composerLayout = new QHBoxLayout(composerRow);
    composerLayout->setContentsMargins(8, 4, 8, 4);
    composerLayout->setSpacing(6);

    m_attachButton = new QToolButton(composerRow);
    m_attachButton->setIcon(QIcon::fromTheme(QStringLiteral("mail-attachment")));
    m_attachButton->setToolTip(tr("Attach files"));
    m_attachButton->setAutoRaise(true);
    m_attachButton->setIconSize(QSize(22, 22));
    connect(m_attachButton, &QToolButton::clicked, this, &ChatWindow::pickAttachments);
    composerLayout->addWidget(m_attachButton, 0, Qt::AlignBottom);

    auto *composer = new MessageComposer(composerRow);
    composer->setPlaceholderText(tr("Message %1").arg(m_channel.displayName));
    composer->setAcceptRichText(false);
    composer->setMaximumHeight(110);
    composer->updateHeight();
    composer->sendHandler = [this] { sendMessage(); };
    m_composer = composer;
    composerLayout->addWidget(m_composer, 1);

    m_sendButton = new QToolButton(composerRow);
    m_sendButton->setText(tr("Send"));
    m_sendButton->setIcon(QIcon::fromTheme(QStringLiteral("mail-send")));
    m_sendButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    m_sendButton->setCursor(Qt::PointingHandCursor);
    connect(m_sendButton, &QToolButton::clicked, this, &ChatWindow::sendMessage);
    composerLayout->addWidget(m_sendButton, 0, Qt::AlignBottom);

    layout->addWidget(composerRow);
    setCentralWidget(central);

    statusBar()->showMessage(tr("Loading messages..."));
}

void ChatWindow::buildMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    QAction *closeAction = fileMenu->addAction(tr("&Close"));
    closeAction->setShortcut(QKeySequence::Close);
    connect(closeAction, &QAction::triggered, this, &QWidget::close);

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction *copyAction = editMenu->addAction(tr("&Copy"));
    copyAction->setShortcut(QKeySequence::Copy);
    connect(copyAction, &QAction::triggered, m_composer, &QTextEdit::copy);
    QAction *pasteAction = editMenu->addAction(tr("&Paste"));
    pasteAction->setShortcut(QKeySequence::Paste);
    connect(pasteAction, &QAction::triggered, m_composer, &QTextEdit::paste);
    QAction *selectAllAction = editMenu->addAction(tr("Select &All"));
    selectAllAction->setShortcut(QKeySequence::SelectAll);
    connect(selectAllAction, &QAction::triggered, m_composer, &QTextEdit::selectAll);

    QMenu *messageMenu = menuBar()->addMenu(tr("&Message"));
    QAction *attachAction = messageMenu->addAction(tr("&Attach Files..."));
    connect(attachAction, &QAction::triggered, this, &ChatWindow::pickAttachments);
    QAction *sendAction = messageMenu->addAction(tr("&Send Message"));
    sendAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return));
    connect(sendAction, &QAction::triggered, this, &ChatWindow::sendMessage);
    messageMenu->addSeparator();
    QAction *reloadAction = messageMenu->addAction(tr("&Reload History"));
    connect(reloadAction, &QAction::triggered, this, [this] {
        clearMessages();
        loadInitialMessages();
    });
}

void ChatWindow::setForwardTargets(const QList<QPair<QString, QString>> &targets)
{
    m_forwardTargets = targets;
}

void ChatWindow::setCurrentUserName(const QString &displayName)
{
    m_currentUserName = displayName;
    if (!m_currentUserId.isEmpty() && !displayName.isEmpty()) {
        m_resolver.userNames.insert(m_currentUserId, displayName);
    }
}

void ChatWindow::loadInitialMessages(bool keepPendingMessages)
{
    if (!keepPendingMessages)
        m_pendingMessages.clear();

    statusBar()->showMessage(tr("Loading messages..."));
    m_api->getJson(QStringLiteral("/v1/channels/%1/messages?limit=50").arg(m_channel.id),
                   QStringLiteral("history:") + m_channel.id);
}

void ChatWindow::loadOlderMessages()
{
    if (m_oldestMessageId.isEmpty())
        return;

    m_loadingOlder = true;
    statusBar()->showMessage(tr("Loading older messages..."));
    m_api->getJson(QStringLiteral("/v1/channels/%1/messages?limit=50&before=%2")
                       .arg(m_channel.id, m_oldestMessageId),
                   QStringLiteral("older:") + m_channel.id);
}

void ChatWindow::onJsonReceived(const QString &tag, const QJsonDocument &doc)
{
    const QString historyTag = QStringLiteral("history:") + m_channel.id;
    const QString olderTag = QStringLiteral("older:") + m_channel.id;
    const QString sendTag = QStringLiteral("send:") + m_channel.id;
    const QString deleteTag = QStringLiteral("delete:") + m_channel.id;

    if (tag == historyTag) {
        const QJsonArray array = doc.array();
        QList<Message> messages;
        for (int i = array.size() - 1; i >= 0; --i)
            messages.append(Message::fromJson(array.at(i).toObject()));

        for (const Message &message : messages)
            appendMessage(message, false);

        // The oldest message the server returned is the paging cursor; live
        // messages that arrived while waiting keep their own positions.
        QString oldest;
        for (const Message &message : messages) {
            if (message.id.isEmpty())
                continue;
            if (oldest.isEmpty() || compareMessageIds(message.id, oldest) < 0)
                oldest = message.id;
        }
        m_oldestMessageId = oldest;

        m_initialLoaded = true;
        flushPendingMessages();

        QTimer::singleShot(0, this, [this] {
            QScrollBar *bar = m_scrollArea->verticalScrollBar();
            bar->setValue(bar->maximum());
        });

        statusBar()->showMessage(messages.isEmpty() ? tr("No messages yet.")
                                                    : tr("Loaded %1 messages.").arg(messages.size()),
                                 4000);
        return;
    }

    if (tag == olderTag) {
        const int oldMaximum = m_scrollArea->verticalScrollBar()->maximum();
        const int oldValue = m_scrollArea->verticalScrollBar()->value();

        const QJsonArray array = doc.array();
        QList<Message> messages;
        for (int i = array.size() - 1; i >= 0; --i)
            messages.append(Message::fromJson(array.at(i).toObject()));
        prependMessages(messages);

        m_loadingOlder = false;
        QTimer::singleShot(0, this, [this, oldMaximum, oldValue] {
            QScrollBar *bar = m_scrollArea->verticalScrollBar();
            bar->setValue(oldValue + (bar->maximum() - oldMaximum));
        });
        statusBar()->showMessage(tr("Loaded %1 older messages.").arg(messages.size()), 4000);
        return;
    }

    if (tag == sendTag) {
        if (doc.isObject()) {
            const Message message = Message::fromJson(doc.object());
            addKnownUser(message.author);
            appendMessage(message, false);
            QTimer::singleShot(0, this, [this] {
                QScrollBar *bar = m_scrollArea->verticalScrollBar();
                bar->setValue(bar->maximum());
            });
        }
        m_sendButton->setEnabled(true);
        m_composer->clear();
        m_pendingFiles.clear();
        updateAttachmentStrip();
        cancelReply();
        statusBar()->showMessage(tr("Message sent."), 3000);
        return;
    }

    if (tag == deleteTag) {
        const QString messageId = doc.object().value(QStringLiteral("id")).toString();
        if (!messageId.isEmpty())
            handleMessageDelete(messageId);
        statusBar()->showMessage(tr("Message deleted."), 3000);
        return;
    }

    if (tag.startsWith(QLatin1String("forward:")) && doc.isObject()) {
        const QString targetId = tag.mid(QStringLiteral("forward:").size());
        const Message message = Message::fromJson(doc.object());
        if (targetId == m_channel.id && !message.id.isEmpty()) {
            addKnownUser(message.author);
            appendMessage(message, false);
            QTimer::singleShot(0, this, [this] {
                QScrollBar *bar = m_scrollArea->verticalScrollBar();
                bar->setValue(bar->maximum());
            });
        }
        return;
    }

    if (tag == QStringLiteral("reaction"))
        return;
}

void ChatWindow::onApiError(const QString &tag, const QString &message)
{
    const bool relevant =
        tag.endsWith(m_channel.id) || tag == QLatin1String("reaction")
        || tag.startsWith(QLatin1String("profile:"));
    if (!relevant)
        return;

    m_sendButton->setEnabled(true);
    statusBar()->showMessage(tr("Error: %1").arg(message), 6000);
}

void ChatWindow::addKnownUser(const User &user)
{
    if (!user.id.isEmpty())
        m_resolver.userNames.insert(user.id, user.displayName());
}

bool ChatWindow::shouldGroupWith(const Message &previous, const Message &current) const
{
    if (previous.author.id != current.author.id || current.author.id.isEmpty())
        return false;
    if (current.isReply() || current.isForward() || current.isSystem())
        return false;
    if (!previous.timestamp.isValid() || !current.timestamp.isValid())
        return false;
    if (previous.timestamp.toLocalTime().date() != current.timestamp.toLocalTime().date())
        return false;
    const qint64 seconds = previous.timestamp.secsTo(current.timestamp);
    return seconds >= 0 && seconds <= 7 * 60;
}

MessageWidget *ChatWindow::createMessageWidget(const Message &message, bool compact)
{
    addKnownUser(message.author);
    for (const User &user : message.mentions)
        m_resolver.userNames.insert(user.id, user.displayName());

    auto *widget = new MessageWidget(message, m_resolver, m_messageContainer);
    widget->setCurrentUserId(m_currentUserId);
    widget->setCompact(compact);

    connect(widget, &MessageWidget::authorClicked, this,
            [this](const User &user) { showUserPopup(user, QCursor::pos()); });
    connect(widget, &MessageWidget::replyRequested, this, &ChatWindow::startReply);
    connect(widget, &MessageWidget::forwardRequested, this, &ChatWindow::forwardMessage);
    connect(widget, &MessageWidget::reactionToggled, this, &ChatWindow::toggleReaction);
    connect(widget, &MessageWidget::reactionAddRequested, this,
            &ChatWindow::addReactionWithDialog);
    connect(widget, &MessageWidget::deleteRequested, this, &ChatWindow::deleteMessage);
    connect(widget, &MessageWidget::copyRequested, this, &ChatWindow::copyMessageText);
    connect(widget, &MessageWidget::referenceClicked, this, &ChatWindow::scrollToMessage);
    connect(widget, &MessageWidget::channelLinkClicked, this, &ChatWindow::channelActivated);

    return widget;
}

void ChatWindow::appendMessage(const Message &message, bool atTop)
{
    if (!message.id.isEmpty() && m_messageWidgets.contains(message.id))
        return;

    if (atTop) {
        MessageWidget *widget = createMessageWidget(message, false);
        m_messages.prepend(message);
        m_messageLayout->insertWidget(0, widget);
        m_messageWidgets.insert(message.id, widget);
        return;
    }

    insertMessageSorted(message);
}

void ChatWindow::insertMessageSorted(const Message &message)
{
    if (message.id.isEmpty() || m_messageWidgets.contains(message.id))
        return;

    const int index = indexForMessageId(message.id);
    bool compact = false;
    if (index > 0)
        compact = shouldGroupWith(m_messages.at(index - 1), message);

    MessageWidget *widget = createMessageWidget(message, compact);
    m_messages.insert(index, message);
    m_messageLayout->insertWidget(index, widget);
    m_messageWidgets.insert(message.id, widget);

    // The follower may now group with this message.
    if (index + 1 < m_messages.size()) {
        const Message &next = m_messages.at(index + 1);
        if (MessageWidget *nextWidget = m_messageWidgets.value(next.id))
            nextWidget->setCompact(shouldGroupWith(message, next));
    }
}

int ChatWindow::indexForMessageId(const QString &messageId) const
{
    int low = 0;
    int high = m_messages.size();
    while (low < high) {
        const int mid = (low + high) / 2;
        if (compareMessageIds(m_messages.at(mid).id, messageId) < 0)
            low = mid + 1;
        else
            high = mid;
    }
    return low;
}

void ChatWindow::flushPendingMessages()
{
    if (m_pendingMessages.isEmpty())
        return;

    std::stable_sort(m_pendingMessages.begin(), m_pendingMessages.end(),
                     [](const Message &a, const Message &b) {
                         return compareMessageIds(a.id, b.id) < 0;
                     });

    const QList<Message> pending = std::move(m_pendingMessages);
    m_pendingMessages.clear();
    for (const Message &message : pending) {
        if (m_recentlyDeleted.contains(message.id))
            continue;
        addKnownUser(message.author);
        for (const User &user : message.mentions)
            m_resolver.userNames.insert(user.id, user.displayName());
        insertMessageSorted(message);
    }
    QTimer::singleShot(0, this, [this] {
        QScrollBar *bar = m_scrollArea->verticalScrollBar();
        bar->setValue(bar->maximum());
    });
}

void ChatWindow::prependMessages(const QList<Message> &messages)
{
    for (int i = messages.size() - 1; i >= 0; --i)
        appendMessage(messages.at(i), true);

    if (!m_messages.isEmpty())
        m_oldestMessageId = m_messages.first().id;
}

void ChatWindow::clearMessages()
{
    m_messages.clear();
    m_messageWidgets.clear();
    m_pendingMessages.clear();
    m_recentlyDeleted.clear();
    m_oldestMessageId.clear();
    m_initialLoaded = false;
    m_loadingOlder = false;

    while (m_messageLayout->count() > 1) {
        QLayoutItem *item = m_messageLayout->takeAt(0);
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }
}

void ChatWindow::removeMessageWidget(const QString &messageId)
{
    MessageWidget *widget = m_messageWidgets.take(messageId);
    if (!widget)
        return;

    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages.at(i).id == messageId) {
            m_messages.removeAt(i);
            break;
        }
    }
    m_messageLayout->removeWidget(widget);
    widget->deleteLater();
}

MessageWidget *ChatWindow::widgetForMessage(const QString &messageId) const
{
    return m_messageWidgets.value(messageId);
}

void ChatWindow::sendMessage()
{
    const QString text = m_composer->toPlainText();
    if (text.trimmed().isEmpty() && m_pendingFiles.isEmpty())
        return;

    QJsonObject payload;
    if (!text.isEmpty())
        payload.insert(QStringLiteral("content"), text);

    if (m_hasReplyTarget) {
        QJsonObject reference;
        reference.insert(QStringLiteral("message_id"), m_replyTarget.id);
        reference.insert(QStringLiteral("channel_id"), m_replyTarget.channelId);
        reference.insert(QStringLiteral("type"), MessageReference::Reply);
        payload.insert(QStringLiteral("message_reference"), reference);
    }

    const QString path = QStringLiteral("/v1/channels/%1/messages").arg(m_channel.id);
    m_sendButton->setEnabled(false);

    if (!m_pendingFiles.isEmpty()) {
        QJsonArray attachments;
        for (int i = 0; i < m_pendingFiles.size(); ++i) {
            QJsonObject attachment;
            attachment.insert(QStringLiteral("id"), i);
            attachment.insert(QStringLiteral("filename"),
                              QFileInfo(m_pendingFiles.at(i)).fileName());
            attachments.append(attachment);
        }
        payload.insert(QStringLiteral("attachments"), attachments);
        m_api->postMultipart(path, QStringLiteral("send:") + m_channel.id,
                             QJsonDocument(payload), m_pendingFiles);
    } else {
        m_api->postJson(path, QStringLiteral("send:") + m_channel.id, QJsonDocument(payload));
    }
}

void ChatWindow::startReply(const Message &target)
{
    m_replyTarget = target;
    m_hasReplyTarget = true;
    updateReplyBanner();
    m_composer->setFocus();
}

void ChatWindow::cancelReply()
{
    m_hasReplyTarget = false;
    m_replyTarget = Message();
    updateReplyBanner();
}

void ChatWindow::updateReplyBanner()
{
    if (!m_hasReplyTarget) {
        m_replyBanner->hide();
        return;
    }

    const QString snippet = Markdown::toPlainText(m_replyTarget.content);
    m_replyLabel->setText(
        tr("Replying to <b>%1</b> — %2")
            .arg(m_replyTarget.author.displayName().toHtmlEscaped(),
                 snippet.toHtmlEscaped()));
    m_replyBanner->show();
}

void ChatWindow::toggleReaction(const QString &messageId, const Reaction &reaction, bool add)
{
    MessageWidget *widget = widgetForMessage(messageId);
    if (!widget)
        return;

    QList<Reaction> reactions = widget->message().reactions;
    Reaction applied = reaction;
    applied.me = add;
    applyReactionChange(reactions, applied, add);
    widget->setReactions(reactions);

    const QString path = QStringLiteral("/v1/channels/%1/messages/%2/reactions/%3/@me")
                             .arg(m_channel.id, messageId, encodeEmojiPath(reaction));
    if (add)
        m_api->putJson(path, QStringLiteral("reaction"));
    else
        m_api->deleteJson(path, QStringLiteral("reaction"));
}

void ChatWindow::addReactionWithDialog(const QString &messageId, QPoint globalPos)
{
    Q_UNUSED(globalPos);
    bool ok = false;
    const QString text = QInputDialog::getText(this, tr("Add Reaction"),
                                               tr("Emoji or :shortcode:"), QLineEdit::Normal,
                                               QString(), &ok);
    if (!ok || text.trimmed().isEmpty())
        return;

    Reaction reaction;
    QString value = text.trimmed();
    if (value.startsWith(QLatin1Char(':')) && value.endsWith(QLatin1Char(':'))
        && value.size() > 2) {
        const QString converted = Markdown::shortcodeToUnicode(value.mid(1, value.size() - 2));
        if (!converted.isEmpty())
            value = converted;
    }
    reaction.emojiName = value;
    toggleReaction(messageId, reaction, true);
}

void ChatWindow::deleteMessage(const QString &messageId)
{
    if (QMessageBox::question(this, tr("Delete Message"),
                              tr("Delete this message? This cannot be undone."))
        != QMessageBox::Yes) {
        return;
    }

    m_api->deleteJson(QStringLiteral("/v1/channels/%1/messages/%2").arg(m_channel.id, messageId),
                      QStringLiteral("delete:") + m_channel.id);
}

void ChatWindow::copyMessageText(const QString &messageId)
{
    MessageWidget *widget = widgetForMessage(messageId);
    if (!widget)
        return;

    QApplication::clipboard()->setText(Markdown::toPlainText(widget->message().content));
    statusBar()->showMessage(tr("Message copied to clipboard."), 3000);
}

void ChatWindow::showUserPopup(const User &user, QPoint globalPos)
{
    if (!user.isValid())
        return;

    auto *popup = new UserInfoPopup(user, this);
    const QString tag = QStringLiteral("profile:") + user.id;

    connect(m_api, &ApiClient::taggedJsonReceived, popup,
            [popup, tag](const QString &receivedTag, const QJsonDocument &doc) {
        if (receivedTag == tag)
            popup->setProfile(doc.object());
    });

    m_api->getJson(QStringLiteral("/v1/users/%1/profile").arg(user.id), tag);

    popup->adjustSize();
    popup->move(globalPos);
    popup->show();
}

void ChatWindow::forwardMessage(const Message &message)
{
    if (m_forwardTargets.isEmpty()) {
        QMessageBox::information(this, tr("Forward Message"),
                                 tr("No other channels are available to forward to."));
        return;
    }

    QStringList names;
    names.reserve(m_forwardTargets.size());
    for (const auto &target : std::as_const(m_forwardTargets))
        names.append(target.second);

    bool ok = false;
    const QString selected = QInputDialog::getItem(this, tr("Forward Message"),
                                                   tr("Forward to:"), names, 0, false, &ok);
    if (!ok || selected.isEmpty())
        return;

    QString targetId;
    for (const auto &target : std::as_const(m_forwardTargets)) {
        if (target.second == selected) {
            targetId = target.first;
            break;
        }
    }
    if (targetId.isEmpty())
        return;

    QJsonObject reference;
    reference.insert(QStringLiteral("message_id"), message.id);
    reference.insert(QStringLiteral("channel_id"), m_channel.id);
    reference.insert(QStringLiteral("type"), MessageReference::Forward);

    QJsonObject payload;
    payload.insert(QStringLiteral("message_reference"), reference);

    m_api->postJson(QStringLiteral("/v1/channels/%1/messages").arg(targetId),
                    QStringLiteral("forward:") + targetId, QJsonDocument(payload));
    statusBar()->showMessage(tr("Message forwarded to %1.").arg(selected), 4000);
}

void ChatWindow::scrollToMessage(const QString &messageId)
{
    MessageWidget *widget = widgetForMessage(messageId);
    if (!widget) {
        statusBar()->showMessage(tr("The referenced message is not loaded."), 4000);
        return;
    }

    m_scrollArea->ensureWidgetVisible(widget, 0, 100);
    widget->flash();
}

void ChatWindow::pickAttachments()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Attach Files"), QString(), tr("All Files (*.*)"));
    if (files.isEmpty())
        return;

    for (const QString &file : files) {
        if (m_pendingFiles.size() >= 10) {
            QMessageBox::information(this, tr("Attach Files"),
                                     tr("At most 10 attachments can be sent at once."));
            break;
        }
        if (!m_pendingFiles.contains(file))
            m_pendingFiles.append(file);
    }
    updateAttachmentStrip();
}

void ChatWindow::updateAttachmentStrip()
{
    while (m_attachmentLayout->count() > 0) {
        QLayoutItem *item = m_attachmentLayout->takeAt(0);
        if (QWidget *widget = item->widget())
            widget->deleteLater();
        delete item;
    }

    if (m_pendingFiles.isEmpty()) {
        m_attachmentStrip->hide();
        return;
    }

    for (const QString &file : std::as_const(m_pendingFiles)) {
        auto *chip = new QWidget(m_attachmentStrip);
        auto *chipLayout = new QHBoxLayout(chip);
        chipLayout->setContentsMargins(6, 2, 2, 2);
        chipLayout->setSpacing(4);
        chip->setStyleSheet(QStringLiteral(
            "QWidget{background-color:#eef2f7;border:1px solid #c8d4e3;border-radius:3px;}"));
        auto *label = new QLabel(QFileInfo(file).fileName(), chip);
        label->setToolTip(file);
        chipLayout->addWidget(label);

        auto *remove = new QToolButton(chip);
        remove->setText(QStringLiteral("×"));
        remove->setAutoRaise(true);
        remove->setToolTip(tr("Remove attachment"));
        connect(remove, &QToolButton::clicked, this, [this, file] {
            m_pendingFiles.removeAll(file);
            updateAttachmentStrip();
        });
        chipLayout->addWidget(remove);

        m_attachmentLayout->addWidget(chip);
    }
    m_attachmentLayout->addStretch();
    m_attachmentStrip->show();
}

void ChatWindow::handleIncomingMessage(const QJsonObject &messageObject)
{
    const Message message = Message::fromJson(messageObject);
    if (message.id.isEmpty() || message.channelId != m_channel.id)
        return;
    if (m_messageWidgets.contains(message.id))
        return;
    // A delete may arrive before the create it deletes.
    if (m_recentlyDeleted.contains(message.id))
        return;

    // Buffer while the initial history is still loading so the page and the
    // live stream cannot interleave out of chronological order.
    if (!m_initialLoaded) {
        m_pendingMessages.append(message);
        return;
    }

    QScrollBar *bar = m_scrollArea->verticalScrollBar();
    const bool atBottom = bar->value() >= bar->maximum() - 120;
    const bool ownMessage = !m_currentUserId.isEmpty() && message.author.id == m_currentUserId;

    appendMessage(message, false);

    if (atBottom || ownMessage) {
        QTimer::singleShot(0, this, [this] {
            QScrollBar *scrollBar = m_scrollArea->verticalScrollBar();
            scrollBar->setValue(scrollBar->maximum());
        });
    } else {
        statusBar()->showMessage(tr("New message from %1").arg(message.author.displayName()),
                                 3000);
    }
}

void ChatWindow::handleMessageUpdate(const QJsonObject &messageObject)
{
    const Message message = Message::fromJson(messageObject);
    if (message.id.isEmpty())
        return;

    if (!m_initialLoaded) {
        // Replace any buffered copy so the newest representation is kept.
        for (int i = 0; i < m_pendingMessages.size(); ++i) {
            if (m_pendingMessages.at(i).id == message.id) {
                m_pendingMessages[i] = message;
                return;
            }
        }
        m_pendingMessages.append(message);
        return;
    }

    if (!m_messageWidgets.contains(message.id))
        return;

    int index = -1;
    for (int i = 0; i < m_messages.size(); ++i) {
        if (m_messages.at(i).id == message.id) {
            index = i;
            break;
        }
    }
    if (index < 0)
        return;

    m_messages[index] = message;
    const bool compact = index > 0 && shouldGroupWith(m_messages.at(index - 1), message);

    MessageWidget *oldWidget = m_messageWidgets.take(message.id);
    const int position = m_messageLayout->indexOf(oldWidget);
    oldWidget->deleteLater();

    MessageWidget *widget = createMessageWidget(message, compact);
    if (position >= 0)
        m_messageLayout->insertWidget(position, widget);
    else
        m_messageLayout->insertWidget(m_messageLayout->count() - 1, widget);
    m_messageWidgets.insert(message.id, widget);
}

void ChatWindow::handleMessageDelete(const QString &messageId)
{
    if (messageId.isEmpty())
        return;

    m_recentlyDeleted.append(messageId);
    while (m_recentlyDeleted.size() > 512)
        m_recentlyDeleted.removeFirst();

    for (int i = 0; i < m_pendingMessages.size(); ++i) {
        if (m_pendingMessages.at(i).id == messageId) {
            m_pendingMessages.removeAt(i);
            break;
        }
    }

    if (m_messageWidgets.contains(messageId))
        removeMessageWidget(messageId);
}

void ChatWindow::handleMessageDeleteBulk(const QList<QString> &messageIds)
{
    for (const QString &messageId : messageIds)
        handleMessageDelete(messageId);
}

void ChatWindow::handleReactionUpdate(const QJsonObject &data, bool added)
{
    const QString messageId = data.value(QStringLiteral("message_id")).toString();
    const QString userId = data.value(QStringLiteral("user_id")).toString();
    if (messageId.isEmpty())
        return;

    // The local user's own reactions are applied optimistically when clicked.
    // The acting session is not excluded from private-channel reactions, so
    // also ignore the echo of our own session's actions.
    if (!m_currentUserId.isEmpty() && userId == m_currentUserId)
        return;
    const QString sessionId = data.value(QStringLiteral("session_id")).toString();
    if (!sessionId.isEmpty() && !m_sessionId.isEmpty() && sessionId == m_sessionId)
        return;

    MessageWidget *widget = widgetForMessage(messageId);
    if (!widget)
        return;

    const QJsonObject emoji = data.value(QStringLiteral("emoji")).toObject();
    if (emoji.isEmpty())
        return;

    Reaction reaction;
    reaction.emojiId = emoji.value(QStringLiteral("id")).toString();
    reaction.emojiName = emoji.value(QStringLiteral("name")).toString();
    reaction.animated = emoji.value(QStringLiteral("animated")).toBool();

    QList<Reaction> reactions = widget->message().reactions;
    applyReactionChange(reactions, reaction, added);
    widget->setReactions(reactions);
}

void ChatWindow::handleReactionRemoveAll(const QString &messageId)
{
    MessageWidget *widget = widgetForMessage(messageId);
    if (!widget)
        return;

    widget->setReactions({});
}

void ChatWindow::handleReactionRemoveEmoji(const QString &messageId, const QJsonObject &emoji)
{
    MessageWidget *widget = widgetForMessage(messageId);
    if (!widget)
        return;

    Reaction removed;
    removed.emojiId = emoji.value(QStringLiteral("id")).toString();
    removed.emojiName = emoji.value(QStringLiteral("name")).toString();

    QList<Reaction> reactions;
    for (const Reaction &reaction : widget->message().reactions) {
        if (reaction.key() != removed.key())
            reactions.append(reaction);
    }
    widget->setReactions(reactions);
}

void ChatWindow::syncSessionState(const QString &sessionId)
{
    // First READY of a fresh session: the history request already covers the
    // stream from its start, so nothing is missing.
    if (m_sessionId.isEmpty()) {
        if (!sessionId.isEmpty())
            m_sessionId = sessionId;
        return;
    }

    // The session was re-established (RESUMED replay or a fresh Identify), so
    // reload the window; any messages missed while offline are fetched again.
    m_sessionId = sessionId;
    clearMessages();
    loadInitialMessages(false);
}

#include "chat_window.moc"
