#include "main_window.hpp"
#include "./ui_main_window.h"

#include "src/account_wizard.hpp"
#include "src/account_manager.hpp"
#include "src/api_client.hpp"

#include <AeroQt/usericon.h>
#include <QAction>


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    auto *accounts = new AccountManager(this);
    auto *api = new ApiClient(AccountWizard::officialInstanceUrl());

    connect(accounts, &AccountManager::accountLoaded, this, [this, accounts,api](const Account &a) {
        if (a.isValid())
            api->setToken(a.token);
        else {
            auto *wizard = new AccountWizard(this);
            connect(wizard, &AccountWizard::accountReady, this,
                    [accounts, api](const Account &a) {
                accounts->setAccount(a);
                api->setToken(a.token);
            });
            wizard->open();
        }
    });
    accounts->loadAccount();


    auto *userIcon = new Aero::UserIcon(Aero::UserIcon::Size_48, ui->centralwidget);
    userIcon->setIcon(QIcon::fromTheme("avatar-default"));
    ui->userInfoLayout->insertWidget(0, userIcon);

    connect(ui->actionAdd_Account, &QAction::triggered, this, [this, accounts, api] {
        auto *wizard = new AccountWizard(this);
        connect(wizard, &AccountWizard::accountReady, this,
                [accounts, api](const Account &a) {
            accounts->setAccount(a);
            api->setToken(a.token);
        });
        wizard->show();
    });

    connect(ui->actionQuit, &QAction::triggered, qApp, &QApplication::quit);
}

MainWindow::~MainWindow()
{
    delete ui;
}
