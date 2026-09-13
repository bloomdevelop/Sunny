#include "channel_tree_model.hpp"

#include <QHash>
#include <QIcon>
#include <QJsonArray>
#include <QJsonValue>
#include <QStringList>

#include <algorithm>

namespace {

QString channelIconName(int type)
{
    switch (type) {
    case Channel::GuildText:
        return QStringLiteral("mail-send-receive");
    case Channel::GuildVoice:
        return QStringLiteral("audio-speakers");
    case Channel::GuildLink:
        return QStringLiteral("emblem-symbolic-link");
    case Channel::GroupDm:
        return QStringLiteral("system-users");
    case Channel::Dm:
        return QStringLiteral("avatar-default");
    case Channel::DmPersonalNotes:
        return QStringLiteral("document-edit");
    default:
        return {};
    }
}

QString userDisplayName(const QJsonObject &user)
{
    const QString global = user.value(QStringLiteral("global_name")).toString();
    if (!global.isEmpty())
        return global;
    return user.value(QStringLiteral("username")).toString();
}

QString recipientNames(const QJsonArray &recipients)
{
    QStringList names;
    names.reserve(recipients.size());
    for (const QJsonValue &value : recipients)
        names.append(userDisplayName(value.toObject()));
    return names.join(QStringLiteral(", "));
}

} // namespace

Channel Channel::fromJson(const QJsonObject &object)
{
    Channel channel;
    channel.id = object.value(QStringLiteral("id")).toString();
    channel.type = object.value(QStringLiteral("type")).toInt(-1);
    channel.guildId = object.value(QStringLiteral("guild_id")).toString();
    channel.parentId = object.value(QStringLiteral("parent_id")).toString();
    channel.position = object.value(QStringLiteral("position")).toInt();
    channel.name = object.value(QStringLiteral("name")).toString();
    channel.lastMessageId = object.value(QStringLiteral("last_message_id")).toString();

    const QJsonArray recipients = object.value(QStringLiteral("recipients")).toArray();

    switch (channel.type) {
    case Dm: {
        if (!recipients.isEmpty()) {
            const QJsonObject user = recipients.first().toObject();
            channel.displayName = userDisplayName(user);
            channel.avatarHash = user.value(QStringLiteral("avatar")).toString();
        }
        break;
    }
    case GroupDm:
        channel.avatarHash = object.value(QStringLiteral("icon")).toString();
        channel.displayName = channel.name.isEmpty() ? recipientNames(recipients) : channel.name;
        break;
    case DmPersonalNotes:
        channel.displayName = QObject::tr("Personal Notes");
        break;
    default:
        // Guild channel types own a name; categories and voice channels included.
        channel.displayName = channel.name;
        break;
    }

    if (channel.displayName.isEmpty())
        channel.displayName = QObject::tr("Unnamed channel");

    return channel;
}

Guild Guild::fromJson(const QJsonObject &object)
{
    Guild guild;
    guild.id = object.value(QStringLiteral("id")).toString();
    guild.name = object.value(QStringLiteral("name")).toString();
    guild.iconHash = object.value(QStringLiteral("icon")).toString();
    return guild;
}

ChannelTreeModel::ChannelTreeModel(QObject *parent)
    : QAbstractItemModel(parent)
    , m_root(new Node)
{
}

ChannelTreeModel::~ChannelTreeModel()
{
    delete m_root;
}

QModelIndex ChannelTreeModel::index(int row, int column, const QModelIndex &parent) const
{
    if (column != 0 || row < 0)
        return {};

    const Node *parentNode = nodeForIndex(parent);
    if (!parentNode || row >= parentNode->children.size())
        return {};

    return createIndex(row, column, parentNode->children.at(row));
}

QModelIndex ChannelTreeModel::parent(const QModelIndex &child) const
{
    if (!child.isValid())
        return {};

    const Node *node = nodeForIndex(child);
    if (!node || !node->parent || node->parent == m_root)
        return {};

    return indexForNode(node->parent);
}

int ChannelTreeModel::rowCount(const QModelIndex &parent) const
{
    if (parent.column() > 0)
        return 0;

    const Node *node = nodeForIndex(parent);
    return node ? node->children.size() : 0;
}

int ChannelTreeModel::columnCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return 1;
}

QVariant ChannelTreeModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid())
        return {};

    const Node *node = nodeForIndex(index);
    if (!node)
        return {};

    if (role == Qt::DecorationRole) {
        return node->iconName.isEmpty() ? QVariant()
                                        : QVariant(QIcon::fromTheme(node->iconName));
    }

    // Stable identity used to persist expansion state across model rebuilds.
    if (role == ItemKeyRole) {
        switch (node->kind) {
        case Node::Server:
            return QStringLiteral("server:") + node->id;
        case Node::Entry:
            return QStringLiteral("channel:") + node->id;
        case Node::Category:
            if (node->parent && node->parent->kind == Node::Server) {
                return QStringLiteral("guild-category:") + node->parent->id
                       + QLatin1Char(':') + node->id;
            }
            return QStringLiteral("top-category:") + node->label;
        }
    }

    switch (node->kind) {
    case Node::Server:
        if (role == Qt::DisplayRole || role == DisplayNameRole)
            return node->label;
        if (role == GuildIdRole)
            return node->id;
        if (role == ItemKindRole)
            return GuildItem;
        return {};
    case Node::Category:
        if (role == Qt::DisplayRole || role == DisplayNameRole)
            return node->label;
        if (role == ItemKindRole)
            return CategoryItem;
        return {};
    case Node::Entry:
        break;
    }

    const Channel &channel = node->channel;
    switch (role) {
    case Qt::DisplayRole:
    case DisplayNameRole:
        return channel.displayName;
    case ChannelIdRole:
        return channel.id;
    case ChannelTypeRole:
        return channel.type;
    case AvatarHashRole:
        return channel.avatarHash;
    case LastMessageIdRole:
        return channel.lastMessageId;
    case GuildIdRole:
        return channel.guildId;
    case ItemKindRole:
        return ChannelItem;
    default:
        return {};
    }
}

void ChannelTreeModel::setContent(const QList<Channel> &privateChannels,
                                  const QString &currentUserId,
                                  const QList<Guild> &guilds)
{
    beginResetModel();

    delete m_root;
    m_root = new Node;

    Node *directMessages = appendCategory(m_root, tr("Direct Messages"));
    Node *groupDms = appendCategory(m_root, tr("Group DMs"));

    if (!currentUserId.isEmpty()) {
        Channel notes;
        notes.id = currentUserId;
        notes.type = Channel::DmPersonalNotes;
        notes.displayName = tr("Personal Notes");
        appendEntry(directMessages, notes);
    }

    for (const Channel &channel : privateChannels) {
        if (channel.type == Channel::Dm)
            appendEntry(directMessages, channel);
        else if (channel.type == Channel::GroupDm)
            appendEntry(groupDms, channel);
    }

    Node *servers = appendCategory(m_root, tr("Servers"));
    for (const Guild &guild : guilds)
        buildGuild(servers, guild);

    sortChildren(m_root);

    endResetModel();
}

ChannelTreeModel::Node *ChannelTreeModel::appendCategory(Node *parent, const QString &label)
{
    auto *node = new Node;
    node->kind = Node::Category;
    node->label = label;
    node->parent = parent;
    parent->children.append(node);
    return node;
}

ChannelTreeModel::Node *ChannelTreeModel::appendEntry(Node *parent, const Channel &channel)
{
    auto *node = new Node;
    node->kind = Node::Entry;
    node->position = channel.position;
    node->id = channel.id;
    node->iconName = channelIconName(channel.type);
    node->channel = channel;
    node->parent = parent;
    parent->children.append(node);
    return node;
}

void ChannelTreeModel::buildGuild(Node *serversCategory, const Guild &guild)
{
    auto *guildNode = new Node;
    guildNode->kind = Node::Server;
    guildNode->id = guild.id;
    guildNode->label = guild.name.isEmpty() ? tr("Unnamed server") : guild.name;
    guildNode->parent = serversCategory;
    serversCategory->children.append(guildNode);

    if (!guild.channelsLoaded)
        return;

    // Category channels (type 4) first so the rest can be parented to them.
    QHash<QString, Node *> categories;
    for (const Channel &channel : guild.channels) {
        if (channel.type != Channel::GuildCategory)
            continue;

        auto *category = new Node;
        category->kind = Node::Category;
        category->position = channel.position;
        category->id = channel.id;
        category->label = channel.displayName;
        category->iconName = QStringLiteral("folder");
        category->parent = guildNode;
        guildNode->children.append(category);
        categories.insert(channel.id, category);
    }

    for (const Channel &channel : guild.channels) {
        if (channel.type == Channel::GuildCategory)
            continue;

        Node *parent = guildNode;
        if (!channel.parentId.isEmpty()) {
            const auto it = categories.constFind(channel.parentId);
            if (it != categories.constEnd())
                parent = it.value();
        }
        appendEntry(parent, channel);
    }
}

void ChannelTreeModel::sortChildren(Node *node)
{
    std::stable_sort(node->children.begin(), node->children.end(),
                     [](const Node *a, const Node *b) { return a->position < b->position; });

    for (Node *child : std::as_const(node->children))
        sortChildren(child);
}

ChannelTreeModel::Node *ChannelTreeModel::nodeForIndex(const QModelIndex &index) const
{
    if (!index.isValid())
        return m_root;
    return static_cast<Node *>(index.internalPointer());
}

QModelIndex ChannelTreeModel::indexForNode(const Node *node) const
{
    if (!node || node == m_root || !node->parent)
        return {};

    const int row = node->parent->children.indexOf(const_cast<Node *>(node));
    if (row < 0)
        return {};

    return createIndex(row, 0, const_cast<Node *>(node));
}
