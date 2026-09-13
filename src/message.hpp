#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <memory>

struct User
{
    QString id;
    QString username;
    QString discriminator;
    QString globalName;
    QString avatar;
    int avatarColor = -1;
    int flags = 0;
    bool bot = false;
    bool system = false;

    static User fromJson(const QJsonObject &object);
    bool isValid() const { return !id.isEmpty(); }
    QString displayName() const;
    QString tag() const;
    QString mentionTag() const;
};

struct Attachment
{
    QString id;
    QString filename;
    QString title;
    QString description;
    QString contentType;
    QString url;
    QString proxyUrl;
    QString placeholder;
    QString waveform;
    qint64 size = 0;
    int width = 0;
    int height = 0;
    int flags = 0;
    int duration = 0;
    bool nsfw = false;
    bool expired = false;

    enum Flag {
        IsSpoiler = 1 << 3,
        ContainsExplicitMedia = 1 << 4,
        IsAnimated = 1 << 5,
    };

    static Attachment fromJson(const QJsonObject &object);

    bool isImage() const;
    bool isVideo() const;
    bool isAudio() const;
    bool isSpoiler() const { return flags & IsSpoiler; }
    bool isAnimated() const { return flags & IsAnimated; }
    QString displayUrl() const { return proxyUrl.isEmpty() ? url : proxyUrl; }
};

struct EmbedMedia
{
    QString url;
    QString proxyUrl;
    QString description;
    QString contentType;
    QString placeholder;
    int width = 0;
    int height = 0;
    int flags = 0;
    int duration = 0;
    bool valid = false;

    static EmbedMedia fromJson(const QJsonObject &object);
    QString displayUrl() const { return proxyUrl.isEmpty() ? url : proxyUrl; }
};

struct EmbedField
{
    QString name;
    QString value;
    bool inlineField = false;
};

struct Embed
{
    QString type;
    QString title;
    QString description;
    QString url;
    QDateTime timestamp;
    int color = -1;
    QString authorName;
    QString authorUrl;
    QString authorIconUrl;
    QString providerName;
    QString providerUrl;
    QString footerText;
    QString footerIconUrl;
    EmbedMedia thumbnail;
    EmbedMedia image;
    EmbedMedia video;
    EmbedMedia audio;
    QList<EmbedField> fields;
    bool nsfw = false;

    static Embed fromJson(const QJsonObject &object);
};

struct Reaction
{
    QString emojiId;
    QString emojiName;
    bool animated = false;
    int count = 0;
    bool me = false;

    static Reaction fromJson(const QJsonObject &object);
    bool isCustom() const { return !emojiId.isEmpty(); }
    // "name:id" for a custom emoji, the raw Unicode sequence otherwise.
    QString pathValue() const;
    QString key() const;
    QString display() const;
};

struct MessageReference
{
    QString channelId;
    QString messageId;
    QString guildId;
    int type = 0;

    enum Type {
        Reply = 0,
        Forward = 1,
    };

    static MessageReference fromJson(const QJsonObject &object);
    bool isValid() const { return !messageId.isEmpty(); }
};

struct MessageSnapshot
{
    QString content;
    QDateTime timestamp;
    QDateTime editedTimestamp;
    QList<Embed> embeds;
    QList<Attachment> attachments;
    int type = 0;
    int flags = 0;

    static MessageSnapshot fromJson(const QJsonObject &object);
};

struct Message
{
    QString id;
    QString channelId;
    User author;
    int type = 0;
    int flags = 0;
    QString content;
    QDateTime timestamp;
    QDateTime editedTimestamp;
    bool pinned = false;
    bool mentionEveryone = false;
    QList<User> mentions;
    QList<Embed> embeds;
    QList<Attachment> attachments;
    QList<Reaction> reactions;
    MessageReference reference;
    bool hasReference = false;
    QList<MessageSnapshot> snapshots;
    // present and null when the reply target no longer resolves
    bool hasReferencedMessage = false;
    std::shared_ptr<Message> referencedMessage;
    bool failed = false;

    enum Type {
        Default = 0,
        RecipientAdd = 1,
        RecipientRemove = 2,
        Call = 3,
        ChannelNameChange = 4,
        ChannelIconChange = 5,
        ChannelPinnedMessage = 6,
        UserJoin = 7,
        Reply = 19,
    };

    enum Flag {
        SuppressEmbeds = 1 << 2,
        SuppressNotifications = 1 << 12,
        VoiceMessage = 1 << 13,
    };

    static Message fromJson(const QJsonObject &object);

    bool isForward() const { return hasReference && reference.type == MessageReference::Forward; }
    bool isReply() const { return hasReference && reference.type == MessageReference::Reply; }
    bool isSystem() const
    {
        return type != Default && type != Reply && type != ChannelPinnedMessage
               && type != UserJoin;
    }
    bool hasMedia() const { return !attachments.isEmpty() || !embeds.isEmpty(); }
};
