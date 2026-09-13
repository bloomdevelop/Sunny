#include "message.hpp"

#include <QJsonArray>
#include <QJsonValue>
#include <QRegularExpression>

namespace {

QDateTime parseTimestamp(const QJsonValue &value)
{
    if (value.isString())
        return QDateTime::fromString(value.toString(), Qt::ISODateWithMs);
    if (value.isDouble())
        return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(value.toDouble()));
    return {};
}

QString stringOrNull(const QJsonValue &value)
{
    return value.isString() ? value.toString() : QString();
}

} // namespace

User User::fromJson(const QJsonObject &object)
{
    User user;
    user.id = object.value(QStringLiteral("id")).toString();
    user.username = object.value(QStringLiteral("username")).toString();
    user.discriminator = object.value(QStringLiteral("discriminator")).toVariant().toString();
    user.globalName = object.value(QStringLiteral("global_name")).toString();
    user.avatar = object.value(QStringLiteral("avatar")).toString();
    if (object.contains(QStringLiteral("avatar_color")))
        user.avatarColor = object.value(QStringLiteral("avatar_color")).toInt(-1);
    user.flags = object.value(QStringLiteral("flags")).toInt();
    user.bot = object.value(QStringLiteral("bot")).toBool();
    user.system = object.value(QStringLiteral("system")).toBool();
    return user;
}

QString User::displayName() const
{
    return globalName.isEmpty() ? username : globalName;
}

QString User::tag() const
{
    if (discriminator.isEmpty() || discriminator == QLatin1String("0"))
        return username;
    return username + QLatin1Char('#') + discriminator;
}

QString User::mentionTag() const
{
    return QStringLiteral("@") + displayName();
}

Attachment Attachment::fromJson(const QJsonObject &object)
{
    Attachment attachment;
    attachment.id = object.value(QStringLiteral("id")).toString();
    attachment.filename = object.value(QStringLiteral("filename")).toString();
    attachment.title = stringOrNull(object.value(QStringLiteral("title")));
    attachment.description = stringOrNull(object.value(QStringLiteral("description")));
    attachment.contentType = object.value(QStringLiteral("content_type")).toString();
    attachment.url = stringOrNull(object.value(QStringLiteral("url")));
    attachment.proxyUrl = stringOrNull(object.value(QStringLiteral("proxy_url")));
    attachment.placeholder = object.value(QStringLiteral("placeholder")).toString();
    attachment.waveform = object.value(QStringLiteral("waveform")).toString();
    attachment.size = static_cast<qint64>(object.value(QStringLiteral("size")).toDouble());
    attachment.width = object.value(QStringLiteral("width")).toInt();
    attachment.height = object.value(QStringLiteral("height")).toInt();
    attachment.flags = object.value(QStringLiteral("flags")).toInt();
    attachment.duration = object.value(QStringLiteral("duration")).toInt();
    attachment.nsfw = object.value(QStringLiteral("nsfw")).toBool();
    attachment.expired = object.value(QStringLiteral("expired")).toBool();
    return attachment;
}

bool Attachment::isImage() const
{
    return contentType.startsWith(QLatin1String("image/"));
}

bool Attachment::isVideo() const
{
    return contentType.startsWith(QLatin1String("video/"));
}

bool Attachment::isAudio() const
{
    return contentType.startsWith(QLatin1String("audio/"));
}

EmbedMedia EmbedMedia::fromJson(const QJsonObject &object)
{
    EmbedMedia media;
    if (object.isEmpty())
        return media;

    media.url = object.value(QStringLiteral("url")).toString();
    media.proxyUrl = object.value(QStringLiteral("proxy_url")).toString();
    media.description = object.value(QStringLiteral("description")).toString();
    media.contentType = object.value(QStringLiteral("content_type")).toString();
    media.placeholder = object.value(QStringLiteral("placeholder")).toString();
    media.width = object.value(QStringLiteral("width")).toInt();
    media.height = object.value(QStringLiteral("height")).toInt();
    media.flags = object.value(QStringLiteral("flags")).toInt();
    media.duration = object.value(QStringLiteral("duration")).toInt();
    media.valid = !media.url.isEmpty();
    return media;
}

Embed Embed::fromJson(const QJsonObject &object)
{
    Embed embed;
    embed.type = object.value(QStringLiteral("type")).toString();
    embed.title = object.value(QStringLiteral("title")).toString();
    embed.description = object.value(QStringLiteral("description")).toString();
    embed.url = object.value(QStringLiteral("url")).toString();
    embed.timestamp = parseTimestamp(object.value(QStringLiteral("timestamp")));
    if (object.contains(QStringLiteral("color")))
        embed.color = object.value(QStringLiteral("color")).toInt(-1);

    const QJsonObject author = object.value(QStringLiteral("author")).toObject();
    embed.authorName = author.value(QStringLiteral("name")).toString();
    embed.authorUrl = author.value(QStringLiteral("url")).toString();
    embed.authorIconUrl = author.value(QStringLiteral("proxy_icon_url")).toString();
    if (embed.authorIconUrl.isEmpty())
        embed.authorIconUrl = author.value(QStringLiteral("icon_url")).toString();

    const QJsonObject provider = object.value(QStringLiteral("provider")).toObject();
    embed.providerName = provider.value(QStringLiteral("name")).toString();
    embed.providerUrl = provider.value(QStringLiteral("url")).toString();

    const QJsonObject footer = object.value(QStringLiteral("footer")).toObject();
    embed.footerText = footer.value(QStringLiteral("text")).toString();
    embed.footerIconUrl = footer.value(QStringLiteral("proxy_icon_url")).toString();
    if (embed.footerIconUrl.isEmpty())
        embed.footerIconUrl = footer.value(QStringLiteral("icon_url")).toString();

    embed.thumbnail = EmbedMedia::fromJson(object.value(QStringLiteral("thumbnail")).toObject());
    embed.image = EmbedMedia::fromJson(object.value(QStringLiteral("image")).toObject());
    embed.video = EmbedMedia::fromJson(object.value(QStringLiteral("video")).toObject());
    embed.audio = EmbedMedia::fromJson(object.value(QStringLiteral("audio")).toObject());

    const QJsonArray fields = object.value(QStringLiteral("fields")).toArray();
    for (const QJsonValue &value : fields) {
        const QJsonObject fieldObject = value.toObject();
        EmbedField field;
        field.name = fieldObject.value(QStringLiteral("name")).toString();
        field.value = fieldObject.value(QStringLiteral("value")).toString();
        field.inlineField = fieldObject.value(QStringLiteral("inline")).toBool();
        embed.fields.append(field);
    }

    embed.nsfw = object.value(QStringLiteral("nsfw")).toBool();
    return embed;
}

Reaction Reaction::fromJson(const QJsonObject &object)
{
    Reaction reaction;
    const QJsonObject emoji = object.value(QStringLiteral("emoji")).toObject();
    reaction.emojiId = emoji.value(QStringLiteral("id")).toString();
    reaction.emojiName = emoji.value(QStringLiteral("name")).toString();
    reaction.animated = emoji.value(QStringLiteral("animated")).toBool();
    reaction.count = object.value(QStringLiteral("count")).toInt();
    reaction.me = object.value(QStringLiteral("me")).toBool();
    return reaction;
}

QString Reaction::pathValue() const
{
    if (isCustom())
        return emojiName + QLatin1Char(':') + emojiId;
    return emojiName;
}

QString Reaction::key() const
{
    return isCustom() ? QStringLiteral(":") + emojiName + QLatin1Char(':') + emojiId
                      : emojiName;
}

QString Reaction::display() const
{
    if (isCustom())
        return QLatin1Char(':') + emojiName + QLatin1Char(':');
    return emojiName;
}

MessageReference MessageReference::fromJson(const QJsonObject &object)
{
    MessageReference reference;
    reference.channelId = object.value(QStringLiteral("channel_id")).toString();
    reference.messageId = object.value(QStringLiteral("message_id")).toString();
    reference.guildId = object.value(QStringLiteral("guild_id")).toString();
    reference.type = object.value(QStringLiteral("type")).toInt();
    return reference;
}

MessageSnapshot MessageSnapshot::fromJson(const QJsonObject &object)
{
    MessageSnapshot snapshot;
    snapshot.content = object.value(QStringLiteral("content")).toString();
    snapshot.timestamp = parseTimestamp(object.value(QStringLiteral("timestamp")));
    snapshot.editedTimestamp = parseTimestamp(object.value(QStringLiteral("edited_timestamp")));
    snapshot.type = object.value(QStringLiteral("type")).toInt();
    snapshot.flags = object.value(QStringLiteral("flags")).toInt();

    const QJsonArray embeds = object.value(QStringLiteral("embeds")).toArray();
    for (const QJsonValue &value : embeds)
        snapshot.embeds.append(Embed::fromJson(value.toObject()));

    const QJsonArray attachments = object.value(QStringLiteral("attachments")).toArray();
    for (const QJsonValue &value : attachments)
        snapshot.attachments.append(Attachment::fromJson(value.toObject()));

    return snapshot;
}

Message Message::fromJson(const QJsonObject &object)
{
    Message message;
    message.id = object.value(QStringLiteral("id")).toString();
    message.channelId = object.value(QStringLiteral("channel_id")).toString();
    message.author = User::fromJson(object.value(QStringLiteral("author")).toObject());
    message.type = object.value(QStringLiteral("type")).toInt();
    message.flags = object.value(QStringLiteral("flags")).toInt();
    message.content = object.value(QStringLiteral("content")).toString();
    message.timestamp = parseTimestamp(object.value(QStringLiteral("timestamp")));
    message.editedTimestamp = parseTimestamp(object.value(QStringLiteral("edited_timestamp")));
    message.pinned = object.value(QStringLiteral("pinned")).toBool();
    message.mentionEveryone = object.value(QStringLiteral("mention_everyone")).toBool();

    const QJsonArray mentions = object.value(QStringLiteral("mentions")).toArray();
    for (const QJsonValue &value : mentions)
        message.mentions.append(User::fromJson(value.toObject()));

    const QJsonArray embeds = object.value(QStringLiteral("embeds")).toArray();
    for (const QJsonValue &value : embeds)
        message.embeds.append(Embed::fromJson(value.toObject()));

    const QJsonArray attachments = object.value(QStringLiteral("attachments")).toArray();
    for (const QJsonValue &value : attachments)
        message.attachments.append(Attachment::fromJson(value.toObject()));

    const QJsonArray reactions = object.value(QStringLiteral("reactions")).toArray();
    for (const QJsonValue &value : reactions)
        message.reactions.append(Reaction::fromJson(value.toObject()));

    if (object.contains(QStringLiteral("message_reference"))) {
        message.hasReference = true;
        message.reference =
            MessageReference::fromJson(object.value(QStringLiteral("message_reference")).toObject());
    }

    const QJsonArray snapshots = object.value(QStringLiteral("message_snapshots")).toArray();
    for (const QJsonValue &value : snapshots)
        message.snapshots.append(MessageSnapshot::fromJson(value.toObject()));

    if (object.contains(QStringLiteral("referenced_message"))) {
        const QJsonValue referenced = object.value(QStringLiteral("referenced_message"));
        message.hasReferencedMessage = !referenced.isNull() && referenced.isObject();
        if (message.hasReferencedMessage)
            message.referencedMessage =
                std::make_shared<Message>(Message::fromJson(referenced.toObject()));
    }

    return message;
}
