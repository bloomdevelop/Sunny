#include "account_manager.hpp"

#include <qt6keychain/keychain.h>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

using namespace QKeychain;

namespace {

constexpr QLatin1String kService{"io.github.bloomdevelop.Sunny"};
constexpr QLatin1String kKey{"Account"};
constexpr int kFormatVersion = 1;

QByteArray toJson(const Account &account)
{
    QJsonObject object{
        {QStringLiteral("version"), kFormatVersion},
            {QStringLiteral("instance"), account.instance.toString()},
            {QStringLiteral("token"), account.token},
        };
    if (!account.userId.isEmpty())
        object.insert(QStringLiteral("userId"), account.userId);
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

Account fromJson(const QString &data)
{
    Account account;
    const QJsonDocument doc = QJsonDocument::fromJson(data.toUtf8());
    if (!doc.isObject())
        return account;

    const QJsonObject object = doc.object();
    if (object.value(QStringLiteral("version")).toInt() != kFormatVersion)
        return account;

    account.instance = QUrl(object.value(QStringLiteral("instance")).toString());
    account.token = object.value(QStringLiteral("token")).toString();

    return account;
}

} // namespace

AccountManager::AccountManager(QObject *parent)
    : QObject{parent}
{}

void AccountManager::loadAccount()
{
    auto *job = new ReadPasswordJob(kService, this);
    job->setKey(kKey);
    job->setInsecureFallback(true);

    connect(job, &Job::finished, this, &AccountManager::onReadFinished);
    job->start();
}

void AccountManager::setAccount(const Account &account)
{
    m_account = account;

    auto *job = new WritePasswordJob(kService, this);
    job->setKey(kKey);
    job->setInsecureFallback(true);
    job->setTextData(QString::fromUtf8(toJson(account)));

    connect(job, &Job::finished, this, &AccountManager::onWriteFinished);
    job->start();
}

void AccountManager::clearAccount()
{
    m_account = {};

    auto *job = new DeletePasswordJob(kService, this);
    job->setKey(kKey);
    job->setInsecureFallback(true);

    connect(job, &Job::finished, this, &AccountManager::onDeleteFinished);
    job->start();
}

void AccountManager::onReadFinished(QKeychain::Job *job)
{
    switch (job->error())
    {
    case NoError:
    {
        const auto *readJob = static_cast<ReadPasswordJob *>(job);
        m_account = fromJson(readJob->textData());
        emit accountLoaded(m_account);
        break;
    }
    case EntryNotFound:
    {
        m_account = {};
        emit accountLoaded({});
        break;
    }
    default:
    {
        emit errorOccurred(job->errorString());
        break;
    }
    }
}

void AccountManager::onWriteFinished(QKeychain::Job *job)
{
    if (job->error() == NoError)
        emit accountStored();
    else
        emit errorOccurred(job->errorString());
}

void AccountManager::onDeleteFinished(QKeychain::Job *job)
{
    if (job->error() == NoError || job->error() == EntryNotFound)
        emit accountCleared();
    else
        emit errorOccurred(job->errorString());
}