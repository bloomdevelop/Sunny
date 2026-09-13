#include "message_widget.hpp"

#include "media_utils.hpp"

#include <AeroQt/util/flowlayout.h>

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLocale>
#include <QMenu>
#include <QPushButton>
#include <QStyle>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

namespace {

QString formatTimestamp(const QDateTime &timestamp, const QDateTime &now)
{
    if (!timestamp.isValid())
        return {};

    const QDate date = timestamp.toLocalTime().date();
    const QDate today = now.date();
    const QString time = timestamp.toLocalTime().time().toString(
        QLocale::system().timeFormat(QLocale::ShortFormat));

    if (date == today)
        return QObject::tr("Today at %1").arg(time);
    if (date == today.addDays(-1))
        return QObject::tr("Yesterday at %1").arg(time);
    if (date.year() == today.year())
        return QObject::tr("%1 at %2")
            .arg(date.toString(QLocale::system().dateFormat(QLocale::ShortFormat)), time);
    return QLocale::system().toString(timestamp.toLocalTime(), QLocale::ShortFormat);
}

QLabel *createRichLabel(const QString &html, const MentionResolver &resolver, QWidget *parent,
                        const std::function<void(const QUrl &)> &onLink)
{
    Q_UNUSED(resolver);
    auto *label = new QLabel(parent);
    label->setTextFormat(Qt::RichText);
    label->setText(html);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextBrowserInteraction | Qt::TextSelectableByMouse);
    label->setOpenExternalLinks(false);
    label->setFocusPolicy(Qt::ClickFocus);

    if (onLink) {
        QObject::connect(label, &QLabel::linkActivated, label, [onLink](const QString &link) {
            const QUrl url(link);
            if (url.isValid())
                onLink(url);
        });
    }
    return label;
}

QWidget *createFileCard(const QIcon &icon, const QString &title, const QString &subtitle,
                        const QUrl &url, QWidget *parent,
                        const std::function<void(const QUrl &)> &onLink)
{
    auto *button = new QToolButton(parent);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setIcon(icon);
    button->setIconSize(QSize(32, 32));
    button->setText(subtitle.isEmpty() ? title : title + QLatin1Char('\n') + subtitle);
    button->setAutoRaise(true);
    button->setCursor(Qt::PointingHandCursor);
    button->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    button->setStyleSheet(QStringLiteral(
        "QToolButton{border:1px solid #d7d7d7;background-color:#fafafa;padding:6px;text-align:left;}"
        "QToolButton:hover{border:1px solid #7da2ce;background-color:#f0f6fd;}"));
    if (url.isValid()) {
        QObject::connect(button, &QToolButton::clicked, button, [onLink, url] { onLink(url); });
    }
    return button;
}

QWidget *createAttachmentWidget(const Attachment &attachment, QWidget *parent,
                                const std::function<void(const QUrl &)> &onLink)
{
    auto *container = new QWidget(parent);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);

    const QUrl url = attachment.displayUrl();

    const auto buildContent = [attachment, url, onLink, container]() -> QWidget * {
        if (attachment.isImage() && url.isValid()) {
            auto *image = new AsyncImageLabel(container);
            image->setSourceUrl(url, QSize(420, 350));
            if (!attachment.description.isEmpty())
                image->setToolTip(attachment.description);
            return image;
        }

        QIcon icon;
        QString subtitle = Media::formatFileSize(attachment.size);
        if (attachment.isVideo())
            icon = QIcon::fromTheme(QStringLiteral("video-x-generic"),
                                    QApplication::style()->standardIcon(QStyle::SP_MediaPlay));
        else if (attachment.isAudio()) {
            icon = QIcon::fromTheme(QStringLiteral("audio-x-generic"),
                                    QApplication::style()->standardIcon(QStyle::SP_MediaVolume));
            if (attachment.duration > 0)
                subtitle = Media::formatDuration(attachment.duration);
        } else if (attachment.expired) {
            icon = QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning);
            subtitle = QObject::tr("Expired - %1").arg(subtitle);
        } else {
            icon = QIcon::fromTheme(QStringLiteral("text-x-generic"),
                                    QApplication::style()->standardIcon(QStyle::SP_FileIcon));
        }

        return createFileCard(icon, attachment.filename, subtitle, url, container, onLink);
    };

    if (attachment.isSpoiler() && !attachment.isAudio()) {
        auto *reveal = new QPushButton(QObject::tr("SPOILER - click to reveal"), container);
        reveal->setCursor(Qt::PointingHandCursor);
        reveal->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
        layout->addWidget(reveal);
        QObject::connect(reveal, &QPushButton::clicked, container,
                         [layout, reveal, buildContent] {
            layout->removeWidget(reveal);
            reveal->deleteLater();
            layout->addWidget(buildContent());
        });
    } else {
        layout->addWidget(buildContent());
    }

    return container;
}

QWidget *createEmbedCard(const Embed &embed, const MentionResolver &resolver, QWidget *parent,
                         const std::function<void(const QUrl &)> &onLink)
{
    auto *card = new QWidget(parent);
    QColor accent = embed.color >= 0 ? QColor::fromRgb(static_cast<QRgb>(embed.color))
                                     : QColor(0x9a, 0x9a, 0x9a);
    card->setStyleSheet(
        QStringLiteral("QWidget#embedCard{background-color:#f7f7f7;border-left:4px solid %1;"
                       "border-top:1px solid #e3e3e3;border-right:1px solid #e3e3e3;"
                       "border-bottom:1px solid #e3e3e3;}")
            .arg(accent.name()));
    card->setObjectName(QStringLiteral("embedCard"));
    card->setMaximumWidth(480);

    auto *outer = new QHBoxLayout(card);
    outer->setContentsMargins(10, 8, 10, 8);
    outer->setSpacing(10);

    auto *column = new QVBoxLayout;
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(5);

    if (!embed.authorName.isEmpty()) {
        auto *author = createRichLabel(
            QStringLiteral("<span style=\"color:#4a4a4a;font-size:12px;\">%1</span>")
                .arg(embed.authorName.toHtmlEscaped()),
            resolver, card, onLink);
        column->addWidget(author);
    }

    if (!embed.title.isEmpty()) {
        QString title;
        if (!embed.url.isEmpty()) {
            title = QStringLiteral(
                        "<a href=\"%1\" style=\"font-size:14px;font-weight:bold;"
                        "color:#1f5fa8;text-decoration:none;\">%2</a>")
                        .arg(embed.url.toHtmlEscaped(), embed.title.toHtmlEscaped());
        } else {
            title = QStringLiteral("<span style=\"font-size:14px;font-weight:bold;\">%1</span>")
                        .arg(embed.title.toHtmlEscaped());
        }
        column->addWidget(createRichLabel(title, resolver, card, onLink));
    }

    if (!embed.description.isEmpty()) {
        column->addWidget(
            createRichLabel(Markdown::toHtml(embed.description, resolver), resolver, card, onLink));
    }

    if (!embed.fields.isEmpty()) {
        auto *grid = new QGridLayout;
        grid->setContentsMargins(0, 2, 0, 2);
        grid->setHorizontalSpacing(16);
        grid->setVerticalSpacing(6);
        int row = 0;
        int columnIndex = 0;
        for (const EmbedField &field : embed.fields) {
            const QString fieldHtml =
                QStringLiteral("<b>%1</b><br/>%2")
                    .arg(field.name.toHtmlEscaped(), Markdown::toHtml(field.value, resolver));
            QLabel *fieldLabel = createRichLabel(fieldHtml, resolver, card, onLink);
            if (field.inlineField) {
                grid->addWidget(fieldLabel, row, columnIndex);
                if (++columnIndex >= 3) {
                    columnIndex = 0;
                    ++row;
                }
            } else {
                if (columnIndex != 0) {
                    columnIndex = 0;
                    ++row;
                }
                grid->addWidget(fieldLabel, row, 0, 1, 3);
                ++row;
            }
        }
        column->addLayout(grid);
    }

    auto *mediaRow = new QHBoxLayout;
    mediaRow->setContentsMargins(0, 0, 0, 0);
    mediaRow->setSpacing(10);

    const EmbedMedia &mainMedia = embed.image.valid ? embed.image : embed.thumbnail;
    if (mainMedia.valid && !mainMedia.url.isEmpty()) {
        auto *image = new AsyncImageLabel(card);
        const int width = mainMedia.valid && embed.image.valid ? 400 : 80;
        image->setSourceUrl(mainMedia.displayUrl(), QSize(width, 300));
        if (!mainMedia.description.isEmpty())
            image->setToolTip(mainMedia.description);
        mediaRow->addWidget(image);
    } else if (embed.video.valid) {
        mediaRow->addWidget(createFileCard(
            QApplication::style()->standardIcon(QStyle::SP_MediaPlay),
            QObject::tr("Video"), embed.video.description, QUrl(embed.video.displayUrl()), card,
            onLink));
    } else if (embed.audio.valid) {
        mediaRow->addWidget(createFileCard(
            QApplication::style()->standardIcon(QStyle::SP_MediaVolume),
            QObject::tr("Audio"), embed.audio.description, QUrl(embed.audio.displayUrl()), card,
            onLink));
    }
    mediaRow->addStretch();
    if (mediaRow->count() > 1)
        column->addLayout(mediaRow);

    QString footerText = embed.footerText;
    if (embed.timestamp.isValid()) {
        if (!footerText.isEmpty())
            footerText += QStringLiteral(" • ");
        footerText += QLocale::system().toString(embed.timestamp.toLocalTime(),
                                                 QLocale::ShortFormat);
    }
    if (footerText.isEmpty() && !embed.providerName.isEmpty())
        footerText = embed.providerName;

    if (!footerText.isEmpty()) {
        column->addWidget(createRichLabel(
            QStringLiteral("<span style=\"color:#8a8a8a;font-size:11px;\">%1</span>")
                .arg(footerText.toHtmlEscaped()),
            resolver, card, onLink));
    }

    outer->addLayout(column, 1);

    if (embed.thumbnail.valid && embed.image.valid) {
        auto *thumbnail = new AsyncImageLabel(card);
        thumbnail->setSourceUrl(embed.thumbnail.displayUrl(), QSize(80, 80));
        outer->addWidget(thumbnail, 0, Qt::AlignTop);
    }

    return card;
}

void appendContentWidgets(QVBoxLayout *layout, const QString &content, const QList<Embed> &embeds,
                          const QList<Attachment> &attachments, const MentionResolver &resolver,
                          QWidget *parent, const std::function<void(const QUrl &)> &onLink)
{
    if (!content.isEmpty()) {
        layout->addWidget(createRichLabel(Markdown::toHtml(content, resolver), resolver, parent,
                                          onLink));
    }

    for (const Embed &embed : embeds)
        layout->addWidget(createEmbedCard(embed, resolver, parent, onLink), 0, Qt::AlignLeft);

    for (const Attachment &attachment : attachments)
        layout->addWidget(createAttachmentWidget(attachment, parent, onLink), 0, Qt::AlignLeft);
}

QString systemMessageText(const Message &message)
{
    const QString author = message.author.displayName();
    switch (message.type) {
    case Message::RecipientAdd:
        return QObject::tr("%1 added a recipient to the group.").arg(author);
    case Message::RecipientRemove:
        return QObject::tr("%1 removed a recipient from the group.").arg(author);
    case Message::Call:
        return QObject::tr("%1 started a call.").arg(author);
    case Message::ChannelNameChange:
        return QObject::tr("%1 changed the group name.").arg(author);
    case Message::ChannelIconChange:
        return QObject::tr("%1 changed the group icon.").arg(author);
    case Message::ChannelPinnedMessage:
        return QObject::tr("%1 pinned a message to this channel.").arg(author);
    case Message::UserJoin:
        return QObject::tr("%1 joined the server.").arg(author);
    default:
        return QObject::tr("%1 triggered a system message.").arg(author);
    }
}

} // namespace

MessageWidget::MessageWidget(const Message &message, const MentionResolver &resolver, QWidget *parent)
    : QWidget(parent)
    , m_message(message)
{
    setObjectName(QStringLiteral("messageWidget"));
    buildUi(resolver);
}

void MessageWidget::buildUi(const MentionResolver &resolver)
{
    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(8, 4, 8, 4);
    root->setSpacing(3);
    m_bodyLayout = root;

    m_header = buildHeader(resolver);
    root->addWidget(m_header);

    const auto onLink = [this](const QUrl &url) { handleLink(url); };

    if (m_message.isSystem()) {
        root->addWidget(createRichLabel(
            QStringLiteral("<i><span style=\"color:#7a7a7a;\">%1</span></i>")
                .arg(systemMessageText(m_message).toHtmlEscaped()),
            resolver, this, onLink));
    } else {
        if (m_message.isReply()) {
            QString replyHtml;
            if (m_message.hasReferencedMessage && m_message.referencedMessage) {
                const Message &referenced = *m_message.referencedMessage;
                const QString snippet = Markdown::toPlainText(referenced.content);
                replyHtml =
                    QStringLiteral(
                        "<span style=\"color:#7a7a7a;\">↪ </span>"
                        "<a href=\"fluxer://message/%1\" style=\"color:#3b6ea5;"
                        "text-decoration:none;\">%2</a>"
                        "<span style=\"color:#7a7a7a;\"> %3</span>")
                        .arg(referenced.id.toHtmlEscaped(),
                             referenced.author.displayName().toHtmlEscaped(),
                             snippet.toHtmlEscaped());
            } else {
                replyHtml = QStringLiteral(
                                "<span style=\"color:#7a7a7a;\">↪ </span>"
                                "<span style=\"color:#7a7a7a;\">%1</span>")
                                .arg(tr("Original message was deleted"));
            }
            root->addWidget(createRichLabel(replyHtml, resolver, this, onLink));
        }

        if (m_message.isForward() && !m_message.snapshots.isEmpty()) {
            root->addWidget(createRichLabel(
                QStringLiteral("<span style=\"color:#7a7a7a;font-size:11px;\">%1</span>")
                    .arg(tr("Forwarded").toHtmlEscaped()),
                resolver, this, onLink));

            for (const MessageSnapshot &snapshot : std::as_const(m_message.snapshots)) {
                auto *snapshotRow = new QWidget(this);
                auto *snapshotLayout = new QHBoxLayout(snapshotRow);
                snapshotLayout->setContentsMargins(0, 0, 0, 0);
                snapshotLayout->setSpacing(8);

                auto *bar = new QFrame(snapshotRow);
                bar->setFixedWidth(3);
                bar->setStyleSheet(QStringLiteral("background-color:#c8c8c8;border:none;"));
                snapshotLayout->addWidget(bar, 0, Qt::AlignTop);

                auto *snapshotColumn = new QVBoxLayout;
                snapshotColumn->setContentsMargins(0, 0, 0, 0);
                snapshotColumn->setSpacing(3);
                appendContentWidgets(snapshotColumn, snapshot.content, snapshot.embeds,
                                     snapshot.attachments, resolver, snapshotRow, onLink);
                snapshotLayout->addLayout(snapshotColumn, 1);
                root->addWidget(snapshotRow);
            }
        } else {
            appendContentWidgets(root, m_message.content, m_message.embeds, m_message.attachments,
                                 resolver, this, onLink);
        }
    }

    m_reactionRow = nullptr;
    rebuildReactions(root);

    root->addStretch();
}

QWidget *MessageWidget::buildHeader(const MentionResolver &resolver)
{
    auto *header = new QWidget(this);
    auto *layout = new QHBoxLayout(header);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    const QString nameHtml =
        QStringLiteral(
            "<a href=\"fluxer://user/%1\" style=\"font-weight:bold;color:#1a3d6d;"
            "text-decoration:none;\">%2</a>")
            .arg(m_message.author.id.toHtmlEscaped(),
                 m_message.author.displayName().toHtmlEscaped());
    layout->addWidget(createRichLabel(nameHtml, resolver, header,
                                      [this](const QUrl &url) { handleLink(url); }));

    if (m_message.author.bot) {
        auto *badge = new QLabel(tr("BOT"), header);
        badge->setStyleSheet(QStringLiteral("border:1px solid #9a9a9a;border-radius:2px;"
                                            "padding:0px 3px;color:#5a5a5a;font-size:9px;"));
        layout->addWidget(badge);
    }
    if (m_message.author.system) {
        auto *badge = new QLabel(tr("SYSTEM"), header);
        badge->setStyleSheet(QStringLiteral("border:1px solid #9a9a9a;border-radius:2px;"
                                            "padding:0px 3px;color:#5a5a5a;font-size:9px;"));
        layout->addWidget(badge);
    }

    auto *timestamp = new QLabel(formatTimestamp(m_message.timestamp, QDateTime::currentDateTime()),
                                 header);
    timestamp->setStyleSheet(QStringLiteral("color:#8a8a8a;font-size:11px;"));
    if (m_message.timestamp.isValid()) {
        timestamp->setToolTip(QLocale::system().toString(m_message.timestamp.toLocalTime(),
                                                         QLocale::LongFormat));
    }
    layout->addWidget(timestamp);

    if (m_message.editedTimestamp.isValid()) {
        auto *edited = new QLabel(tr("(edited)"), header);
        edited->setStyleSheet(QStringLiteral("color:#8a8a8a;font-size:11px;"));
        layout->addWidget(edited);
    }

    layout->addStretch();
    return header;
}

void MessageWidget::rebuildReactions(QVBoxLayout *target)
{
    if (m_reactionRow) {
        target->removeWidget(m_reactionRow);
        m_reactionRow->deleteLater();
        m_reactionRow = nullptr;
    }

    if (m_message.reactions.isEmpty())
        return;

    m_reactionRow = new QWidget(this);
    auto *flow = new FlowLayout(m_reactionRow, 0, 4, 4);

    for (const Reaction &reaction : std::as_const(m_message.reactions)) {
        auto *button = new QToolButton(m_reactionRow);
        button->setText(QStringLiteral("%1  %2").arg(reaction.display()).arg(reaction.count));
        button->setCheckable(true);
        button->setChecked(reaction.me);
        button->setCursor(Qt::PointingHandCursor);
        button->setStyleSheet(
            QStringLiteral("QToolButton{border:1px solid %1;border-radius:9px;padding:1px 7px;"
                           "background-color:%2;}"
                           "QToolButton:checked{border:1px solid #4a90d9;background-color:#d6e6f8;}")
                .arg(reaction.me ? QStringLiteral("#4a90d9") : QStringLiteral("#d0d0d0"),
                     reaction.me ? QStringLiteral("#d6e6f8") : QStringLiteral("#f2f2f2")));
        const Reaction captured = reaction;
        connect(button, &QToolButton::clicked, this, [this, captured] {
            emit reactionToggled(m_message.id, captured, !captured.me);
        });
        flow->addWidget(button);
    }

    auto *addButton = new QToolButton(m_reactionRow);
    addButton->setText(QStringLiteral("+"));
    addButton->setCursor(Qt::PointingHandCursor);
    addButton->setStyleSheet(QStringLiteral(
        "QToolButton{border:1px solid #d0d0d0;border-radius:9px;padding:1px 7px;"
        "background-color:#f2f2f2;}"));
    connect(addButton, &QToolButton::clicked, this, [this, addButton] {
        QMenu menu(this);
        const QStringList quick = { QStringLiteral("👍"), QStringLiteral("❤️"),
                                    QStringLiteral("😂"), QStringLiteral("🎉"),
                                    QStringLiteral("👀"), QStringLiteral("✅") };
        for (const QString &emoji : quick)
            menu.addAction(emoji);
        menu.addSeparator();
        QAction *custom = menu.addAction(tr("Other..."));
        QAction *chosen = menu.exec(addButton->mapToGlobal(QPoint(0, 0)));
        if (!chosen)
            return;

        if (chosen == custom) {
            emit reactionAddRequested(m_message.id, addButton->mapToGlobal(QPoint(0, 0)));
        } else {
            Reaction reaction;
            reaction.emojiName = chosen->text();
            emit reactionToggled(m_message.id, reaction, true);
        }
    });
    flow->addWidget(addButton);

    target->addWidget(m_reactionRow);
}

void MessageWidget::setCompact(bool compact)
{
    if (m_compact == compact)
        return;
    m_compact = compact;
    if (m_header)
        m_header->setVisible(!compact);
}

void MessageWidget::setCurrentUserId(const QString &userId)
{
    m_currentUserId = userId;
}

void MessageWidget::setReactions(const QList<Reaction> &reactions)
{
    m_message.reactions = reactions;
    if (m_bodyLayout)
        rebuildReactions(m_bodyLayout);
}

void MessageWidget::flash()
{
    if (m_compact)
        return;

    const QPalette original = palette();
    QPalette highlight = original;
    highlight.setColor(QPalette::Window, QColor(255, 244, 179));
    setAutoFillBackground(true);
    setPalette(highlight);
    QTimer::singleShot(1000, this, [this, original] { setPalette(original); });
}

void MessageWidget::handleLink(const QUrl &url)
{
    if (url.scheme() == QLatin1String("fluxer")) {
        const QString id = url.path().mid(1);
        if (url.host() == QLatin1String("user")) {
            emit authorClicked(userForId(id));
            return;
        }
        if (url.host() == QLatin1String("message")) {
            emit referenceClicked(id);
            return;
        }
        if (url.host() == QLatin1String("channel")) {
            emit channelLinkClicked(id);
            return;
        }
        return;
    }

    QDesktopServices::openUrl(url);
}

User MessageWidget::userForId(const QString &id) const
{
    if (m_message.author.id == id)
        return m_message.author;
    for (const User &user : m_message.mentions) {
        if (user.id == id)
            return user;
    }

    User unknown;
    unknown.id = id;
    unknown.username = id;
    return unknown;
}

void MessageWidget::contextMenuEvent(QContextMenuEvent *event)
{
    if (m_message.isSystem()) {
        QWidget::contextMenuEvent(event);
        return;
    }

    QMenu menu(this);
    QAction *reply = menu.addAction(tr("Reply"));
    QAction *copy = menu.addAction(tr("Copy Text"));
    QAction *react = menu.addAction(tr("Add Reaction"));
    QAction *forward = menu.addAction(tr("Forward"));
    QAction *remove = nullptr;
    if (!m_currentUserId.isEmpty() && m_message.author.id == m_currentUserId)
        remove = menu.addAction(tr("Delete Message"));

    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen)
        return;

    if (chosen == reply)
        emit replyRequested(m_message);
    else if (chosen == copy)
        emit copyRequested(m_message.id);
    else if (chosen == react)
        emit reactionAddRequested(m_message.id, event->globalPos());
    else if (chosen == forward)
        emit forwardRequested(m_message);
    else if (remove && chosen == remove)
        emit deleteRequested(m_message.id);
}
