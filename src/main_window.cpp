#include "main_window.hpp"
#include "./ui_main_window.h"

#include "src/account_wizard.hpp"
#include "src/account_manager.hpp"
#include "src/api_client.hpp"
#include "src/channel_tree_model.hpp"
#include "src/chat_window.hpp"
#include "src/gateway.hpp"

#include <AeroQt/usericon.h>
#include <QAction>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QListView>
#include <QStatusBar>
#include <QTreeView>

#include <functional>
#include <utility>

namespace {

QString statusLabel(const QString &status)
{
    if (status == QLatin1String("online"))
        return QObject::tr("Online");
    if (status == QLatin1String("idle"))
        return QObject::tr("Idle");
    if (status == QLatin1String("dnd"))
        return QObject::tr("Do Not Disturb");
    if (status == QLatin1String("invisible"))
        return QObject::tr("Invisible");
    if (status == QLatin1String("offline"))
        return QObject::tr("Offline");
    return status;
}

bool customStatusActive(const QJsonObject &customStatus)
{
    const QJsonValue expires = customStatus.value(QStringLiteral("expires_at"));
    if (expires.isNull() || expires.isUndefined())
        return true;

    QDateTime expiry;
    if (expires.isString())
        expiry = QDateTime::fromString(expires.toString(), Qt::ISODate);
    else if (expires.isDouble())
        expiry = QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(expires.toDouble()));

    return !expiry.isValid() || expiry > QDateTime::currentDateTimeUtc();
}

const QLatin1String kGuildChannelsPrefix("guild-channels:");

QString guildChannelsTag(const QString &guildId)
{
    return QStringLiteral("guild-channels:") + guildId;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    auto *accounts = new AccountManager(this);
    m_api = new ApiClient(AccountWizard::officialInstanceUrl(), this);

    m_channelModel = new ChannelTreeModel(this);
    ui->treeView->setModel(m_channelModel);

    m_gateway = new Gateway(this);
    connect(m_gateway, &Gateway::connected, this, [this] {
        ui->statusBar->showMessage(tr("Connecting"));
    });
    connect(m_gateway, &Gateway::ready, this, [this] {
        ui->statusBar->showMessage(tr("Connected"));
        for (const QPointer<ChatWindow> &chat : std::as_const(m_chatWindows)) {
            if (chat)
                chat->syncSessionState(m_gateway->sessionId());
        }
    });
    connect(m_gateway, &Gateway::disconnected, this, [this] {
        ui->statusBar->showMessage(tr("Disconnected"));
    });
    connect(m_gateway, &Gateway::errorOccurred, this, [this](const QString &message) {
        ui->statusBar->showMessage(message);
    });
    connect(m_gateway, &Gateway::dispatch, this, &MainWindow::onDispatch);

    connect(m_api, &ApiClient::taggedJsonReceived, this, &MainWindow::onJsonReceived);

    const auto openFromIndex = [this](const QModelIndex &index) {
        if (!index.isValid()
            || index.data(ChannelTreeModel::ItemKindRole).toInt()
                   != ChannelTreeModel::ChannelItem)
            return;
        openChannel(index.data(ChannelTreeModel::ChannelIdRole).toString());
    };
    connect(ui->treeView, &QTreeView::activated, this, openFromIndex);
    connect(ui->treeView, &QTreeView::doubleClicked, this, openFromIndex);

    connect(accounts, &AccountManager::accountLoaded, this, [this, accounts](const Account &a) {
        if (a.isValid()) {
            startWithToken(a.token);
        } else {
            auto *wizard = new AccountWizard(this);
            connect(wizard, &AccountWizard::accountReady, this,
                    [this, accounts](const Account &a) {
                accounts->setAccount(a);
                startWithToken(a.token);
            });
            wizard->open();
        }
    });
    accounts->loadAccount();

    auto *userIcon = new Aero::UserIcon(Aero::UserIcon::Size_48, ui->centralwidget);
    userIcon->setIcon(QIcon::fromTheme(QStringLiteral("avatar-default")));
    ui->userInfoLayout->insertWidget(0, userIcon);

    connect(ui->actionAdd_Account, &QAction::triggered, this, [this, accounts] {
        auto *wizard = new AccountWizard(this);
        connect(wizard, &AccountWizard::accountReady, this,
                [this, accounts](const Account &a) {
            accounts->setAccount(a);
            startWithToken(a.token);
        });
        wizard->show();
    });

    connect(ui->actionQuit, &QAction::triggered, qApp, &QApplication::quit);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::startWithToken(const QString &token)
{
    m_token = token;
    m_api->setToken(token);
    loadAccountData();
}

void MainWindow::loadAccountData()
{
    m_api->getJson(QStringLiteral("/.well-known/fluxer"), QStringLiteral("discovery"));
    m_api->getJson(QStringLiteral("/v1/users/@me"), QStringLiteral("user"));
    m_api->getJson(QStringLiteral("/v1/users/@me/settings"), QStringLiteral("settings"));
    m_api->getJson(QStringLiteral("/v1/users/@me/channels"), QStringLiteral("channels"));
    m_api->getJson(QStringLiteral("/v1/users/@me/guilds"), QStringLiteral("guilds"));
}

void MainWindow::onJsonReceived(const QString &tag, const QJsonDocument &doc)
{
    if (tag == QLatin1String("channels") && doc.isArray()) {
        m_privateChannels.clear();
        const QJsonArray array = doc.array();
        for (const QJsonValue &value : array) {
            const Channel channel = Channel::fromJson(value.toObject());
            if (channel.type == Channel::Dm || channel.type == Channel::GroupDm)
                m_privateChannels.append(channel);
        }
        rebuildChannelTree();
    } else if (tag == QLatin1String("user") && doc.isObject()) {
        updateUserInfo(doc.object());
        m_userId = doc.object().value(QStringLiteral("id")).toString();
        rebuildChannelTree();
    } else if (tag == QLatin1String("settings") && doc.isObject()) {
        updateUserSettings(doc.object());
    } else if (tag == QLatin1String("discovery") && doc.isObject()) {
        const QString gatewayUrl = doc.object()
                                       .value(QStringLiteral("endpoints"))
                                       .toObject()
                                       .value(QStringLiteral("gateway"))
                                       .toString();
        if (!gatewayUrl.isEmpty() && !m_token.isEmpty())
            m_gateway->connectTo(QUrl(gatewayUrl), m_token);
    } else if (tag == QLatin1String("guilds") && doc.isArray()) {
        m_guilds.clear();
        const QJsonArray array = doc.array();
        for (const QJsonValue &value : array)
            m_guilds.append(Guild::fromJson(value.toObject()));
        rebuildChannelTree();

        for (const Guild &guild : std::as_const(m_guilds)) {
            m_api->getJson(
                QStringLiteral("/v1/guilds/") + guild.id + QStringLiteral("/channels"),
                guildChannelsTag(guild.id));
        }
    } else if (tag.startsWith(kGuildChannelsPrefix) && doc.isArray()) {
        const QString guildId = tag.mid(kGuildChannelsPrefix.size());
        const QJsonArray array = doc.array();
        for (Guild &guild : m_guilds) {
            if (guild.id != guildId)
                continue;

            guild.channels.clear();
            for (const QJsonValue &value : array) {
                const Channel channel = Channel::fromJson(value.toObject());
                if (channel.isGuild())
                    guild.channels.append(channel);
            }
            guild.channelsLoaded = true;
            break;
        }
        rebuildChannelTree();
    }
}

void MainWindow::rebuildChannelTree()
{
    captureExpansionState();
    m_channelModel->setContent(m_privateChannels, m_userId, m_guilds);
    applyExpansionState();
}

void MainWindow::captureExpansionState()
{
    m_knownKeys.clear();
    m_expandedKeys.clear();

    std::function<void(const QModelIndex &)> walk = [&](const QModelIndex &parent) {
        const int rows = m_channelModel->rowCount(parent);
        for (int row = 0; row < rows; ++row) {
            const QModelIndex index = m_channelModel->index(row, 0, parent);
            const QString key = index.data(ChannelTreeModel::ItemKeyRole).toString();
            if (!key.isEmpty()) {
                m_knownKeys.insert(key);
                if (ui->treeView->isExpanded(index))
                    m_expandedKeys.insert(key);
            }
            walk(index);
        }
    };
    walk(QModelIndex());
}

void MainWindow::applyExpansionState()
{
    std::function<void(const QModelIndex &)> walk = [&](const QModelIndex &parent) {
        const int rows = m_channelModel->rowCount(parent);
        for (int row = 0; row < rows; ++row) {
            const QModelIndex index = m_channelModel->index(row, 0, parent);
            const QString key = index.data(ChannelTreeModel::ItemKeyRole).toString();
            if (!key.isEmpty()) {
                // Known nodes keep their state; new nodes take the default.
                const bool expanded =
                    m_knownKeys.contains(key) ? m_expandedKeys.contains(key)
                                              : defaultExpanded(index);
                ui->treeView->setExpanded(index, expanded);

                m_knownKeys.insert(key);
                if (expanded)
                    m_expandedKeys.insert(key);
                else
                    m_expandedKeys.remove(key);
            }
            walk(index);
        }
    };
    walk(QModelIndex());
}

bool MainWindow::defaultExpanded(const QModelIndex &index) const
{
    if (index.data(ChannelTreeModel::ItemKindRole).toInt()
        != ChannelTreeModel::CategoryItem)
        return false;

    // A nested category belongs to a guild; only the top level defaults open.
    if (index.parent().isValid())
        return false;

    return index.data(ChannelTreeModel::DisplayNameRole).toString() != tr("Servers");
}

void MainWindow::updateUserInfo(const QJsonObject &user)
{
    const QString username = user.value(QStringLiteral("username")).toString();
    const QString globalName = user.value(QStringLiteral("global_name")).toString();

    QString display = globalName;
    if (display.isEmpty()) {
        QString tag = user.value(QStringLiteral("discriminator")).toVariant().toString();
        if (!tag.isEmpty())
            tag = tag.rightJustified(4, QLatin1Char('0'));
        display = tag.isEmpty() ? username
                                : username + QLatin1Char('#') + tag;
    }

    if (!display.isEmpty()) {
        m_userDisplayName = display;
        ui->titleWidget->setText(display);
        for (const QPointer<ChatWindow> &chat : std::as_const(m_chatWindows)) {
            if (chat)
                chat->setCurrentUserName(display);
        }
    }
}

void MainWindow::onDispatch(const QString &type, const QJsonObject &data)
{
    if (type == QLatin1String("USER_SETTINGS_UPDATE")) {
        updateUserSettings(data);
    } else if (type == QLatin1String("USER_UPDATE")) {
        updateUserInfo(data);
        const QString id = data.value(QStringLiteral("id")).toString();
        if (!id.isEmpty())
            m_userId = id;
    } else if (type == QLatin1String("PRESENCE_UPDATE")) {
        const QString userId = data.value(QStringLiteral("user"))
                                   .toObject()
                                   .value(QStringLiteral("id"))
                                   .toString();
        const bool accountScoped = !data.contains(QStringLiteral("guild_id"));
        if (accountScoped && (m_userId.isEmpty() || userId == m_userId))
            applyStatus(data);
    }

    routeDispatch(type, data);
}

void MainWindow::openChannel(const QString &channelId)
{
    if (channelId.isEmpty())
        return;

    Channel channel;
    if (!findChannel(channelId, &channel))
        return;

    openChatWindow(channel);
}

void MainWindow::openChatWindow(const Channel &channel)
{
    QPointer<ChatWindow> existing = m_chatWindows.value(channel.id);
    if (existing) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }

    auto *chat = new ChatWindow(channel, m_userId, m_api, this);
    chat->setCurrentUserName(m_userDisplayName);
    chat->setForwardTargets(forwardTargets());
    connect(chat, &ChatWindow::channelActivated, this, &MainWindow::openChannel);
    m_chatWindows.insert(channel.id, chat);
    chat->show();
}

bool MainWindow::findChannel(const QString &channelId, Channel *channel) const
{
    for (const Channel &candidate : m_privateChannels) {
        if (candidate.id == channelId) {
            *channel = candidate;
            return true;
        }
    }
    for (const Guild &guild : m_guilds) {
        for (const Channel &candidate : guild.channels) {
            if (candidate.id == channelId) {
                *channel = candidate;
                return true;
            }
        }
    }
    if (!m_userId.isEmpty() && channelId == m_userId) {
        Channel notes;
        notes.id = m_userId;
        notes.type = Channel::DmPersonalNotes;
        notes.displayName = tr("Personal Notes");
        *channel = notes;
        return true;
    }
    return false;
}

QList<QPair<QString, QString>> MainWindow::forwardTargets() const
{
    QList<QPair<QString, QString>> targets;
    for (const Channel &channel : m_privateChannels) {
        if (channel.type == Channel::Dm || channel.type == Channel::GroupDm)
            targets.append({ channel.id, channel.displayName });
    }
    for (const Guild &guild : m_guilds) {
        for (const Channel &channel : guild.channels) {
            if (channel.type == Channel::GuildText || channel.type == Channel::GuildVoice)
                targets.append({ channel.id,
                                 QStringLiteral("#%1 — %2")
                                     .arg(channel.displayName,
                                          guild.name.isEmpty() ? tr("Unnamed server")
                                                               : guild.name) });
        }
    }
    return targets;
}

void MainWindow::routeDispatch(const QString &type, const QJsonObject &data)
{
    const QString channelId = data.value(QStringLiteral("channel_id")).toString();
    QPointer<ChatWindow> chat = m_chatWindows.value(channelId);
    if (!chat)
        return;

    if (type == QLatin1String("MESSAGE_CREATE"))
        chat->handleIncomingMessage(data);
    else if (type == QLatin1String("MESSAGE_UPDATE"))
        chat->handleMessageUpdate(data);
    else if (type == QLatin1String("MESSAGE_DELETE"))
        chat->handleMessageDelete(data.value(QStringLiteral("id")).toString());
    else if (type == QLatin1String("MESSAGE_DELETE_BULK")) {
        QList<QString> ids;
        const QJsonArray array = data.value(QStringLiteral("ids")).toArray();
        ids.reserve(array.size());
        for (const QJsonValue &value : array)
            ids.append(value.toString());
        chat->handleMessageDeleteBulk(ids);
    } else if (type == QLatin1String("MESSAGE_REACTION_ADD")) {
        chat->handleReactionUpdate(data, true);
    } else if (type == QLatin1String("MESSAGE_REACTION_REMOVE")) {
        chat->handleReactionUpdate(data, false);
    } else if (type == QLatin1String("MESSAGE_REACTION_REMOVE_ALL")) {
        chat->handleReactionRemoveAll(data.value(QStringLiteral("message_id")).toString());
    } else if (type == QLatin1String("MESSAGE_REACTION_REMOVE_EMOJI")) {
        chat->handleReactionRemoveEmoji(data.value(QStringLiteral("message_id")).toString(),
                                        data.value(QStringLiteral("emoji")).toObject());
    }
}

void MainWindow::updateUserSettings(const QJsonObject &settings)
{
    applyStatus(settings);
}

void MainWindow::applyStatus(const QJsonObject &source)
{
    const QJsonObject customStatus =
        source.value(QStringLiteral("custom_status")).toObject();
    const QString customText = customStatus.value(QStringLiteral("text")).toString();

    // A custom status wins over the presence, matching the usual client behaviour.
    const QString status =
        (!customText.isEmpty() && customStatusActive(customStatus))
            ? customText
            : statusLabel(source.value(QStringLiteral("status")).toString());

    if (!status.isEmpty())
        ui->titleWidget->setComment(status);
}
