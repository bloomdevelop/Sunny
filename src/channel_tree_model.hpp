#pragma once

#include <QAbstractItemModel>
#include <QJsonObject>
#include <QList>
#include <QString>

struct Channel
{
    // https://docs.fluxer.app/http-api/channels/#channel-types
    enum Type {
        GuildText = 0,
        Dm = 1,
        GuildVoice = 2,
        GroupDm = 3,
        GuildCategory = 4,
        GuildLink = 998,
        DmPersonalNotes = 999,
    };

    QString id;
    int type = -1;
    QString guildId;
    QString parentId;
    int position = 0;
    QString name;
    QString displayName;
    QString avatarHash;
    QString lastMessageId;

    static Channel fromJson(const QJsonObject &object);

    bool isPrivate() const
    {
        return type == Dm || type == GroupDm || type == DmPersonalNotes;
    }

    bool isGuild() const
    {
        return type == GuildText || type == GuildVoice || type == GuildCategory
               || type == GuildLink;
    }
};

struct Guild
{
    QString id;
    QString name;
    QString iconHash;
    QList<Channel> channels;
    bool channelsLoaded = false;

    static Guild fromJson(const QJsonObject &object);
};

class ChannelTreeModel : public QAbstractItemModel
{
    Q_OBJECT
public:
    enum Role {
        ChannelIdRole = Qt::UserRole + 1,
        ChannelTypeRole,
        DisplayNameRole,
        AvatarHashRole,
        LastMessageIdRole,
        GuildIdRole,
        ItemKindRole,
        ItemKeyRole,
    };

    enum ItemKind {
        CategoryItem,
        ChannelItem,
        GuildItem,
    };

    explicit ChannelTreeModel(QObject *parent = nullptr);
    ~ChannelTreeModel() override;

    QModelIndex index(int row, int column,
                      const QModelIndex &parent = QModelIndex()) const override;
    QModelIndex parent(const QModelIndex &child) const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    // Rebuilds the whole tree: private categories, then the server list with
    // each guild's channels nested under it. The Personal Notes channel is not
    // returned by the API, so it is recreated from the account ID.
    void setContent(const QList<Channel> &privateChannels,
                    const QString &currentUserId,
                    const QList<Guild> &guilds);

private:
    struct Node
    {
        enum Kind { Category, Entry, Server };

        Kind kind = Category;
        int position = 0;
        QString id;
        QString label;
        QString iconName;
        Channel channel;
        Node *parent = nullptr;
        QList<Node *> children;

        Node() = default;
        Node(const Node &) = delete;
        Node &operator=(const Node &) = delete;

        ~Node() { qDeleteAll(children); }
    };

    Node *appendCategory(Node *parent, const QString &label);
    Node *appendEntry(Node *parent, const Channel &channel);
    void buildGuild(Node *serversCategory, const Guild &guild);
    static void sortChildren(Node *node);

    Node *nodeForIndex(const QModelIndex &index) const;
    QModelIndex indexForNode(const Node *node) const;

    Node *m_root = nullptr;
};
