#pragma once

#include "account_manager.hpp"
#include "handoff_client.hpp"

#include <AeroQt/wizard.h>
#include <QWizardPage>

class QTimer;
class QNetworkAccessManager;
class QUrl;

QT_BEGIN_NAMESPACE
namespace Ui {
class AccountWizard;
}
QT_END_NAMESPACE

// Login options (branching) page: Email -> _emailOption, Handoff -> handoffOption.
class LoginOptionsPage : public QWizardPage
{
    Q_OBJECT

public:
    explicit LoginOptionsPage(QWidget *parent = nullptr);

    void setEmailPageId(int id);
    void setHandoffPageId(int id);
    void selectEmail();
    void selectHandoff();

    bool isComplete() const override;
    int nextId() const override;

private:
    int m_emailPageId = -1;
    int m_handoffPageId = -1;
    int m_selectedId = -1;
};

// Terminal page: never advances to another page (nextId() == -1).
class TerminalWizardPage : public QWizardPage
{
    Q_OBJECT

public:
    using QWizardPage::QWizardPage;
    int nextId() const override { return -1; }
};

QT_BEGIN_NAMESPACE
namespace Ui {
class AccountWizard;
}
QT_END_NAMESPACE

class AccountWizard : public Aero::Wizard
{
    Q_OBJECT

public:
    explicit AccountWizard(QWidget *parent = nullptr);
    ~AccountWizard() override;

    QUrl selectedInstanceUrl() const;
    static QUrl officialInstanceUrl();

signals:
    void accountReady(const Account &account);

protected:
    void done(int result) override;

private:
    void validateViaWellKnown(const QUrl &url);
    void updateNextButton();
    void startHandoff();

    HandoffClient *m_handoff = nullptr;
    Ui::AccountWizard *ui;
    QTimer *m_debounceTimer = nullptr;
    QNetworkAccessManager *m_nam = nullptr;
    QWizardPage *m_instancesPage = nullptr;
    bool m_customInstanceValid = false;
    bool m_isUsingCustomInstance = false;
};
