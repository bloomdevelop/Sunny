#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <qt6keychain/keychain.h>

struct Account
{
    QUrl instance;
    QString token;
    QString userId;

    bool isValid() const {
        return instance.isValid() && !token.isEmpty();
    }
};

Q_DECLARE_METATYPE(Account)

class AccountManager : public QObject
{
    Q_OBJECT
public:
    explicit AccountManager(QObject *parent = nullptr);
    void loadAccount();
    void setAccount(const Account &a);
    void clearAccount();

    const Account &account() const {
        return m_account;
    }
    bool hasAccount() const {
        return m_account.isValid();
    }

signals:
    void accountLoaded(const Account &account);
    void accountStored();
    void accountCleared();
    void errorOccurred(const QString &message);

private:
    void onReadFinished(QKeychain::Job *job);
    void onWriteFinished(QKeychain::Job *job);
    void onDeleteFinished(QKeychain::Job *job);

    Account m_account;
};
