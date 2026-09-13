#include "main_window.hpp"
#include "./ui_main_window.h"

#include "src/account_wizard.hpp"
#include "src/account_manager.hpp"

#include <AeroQt/usericon.h>
#include <QAction>


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
    auto *accounts = new AccountManager(this);
    auto *api = new ApiClient(AccountWizard::officialInstanceUrl());

    connect(accounts, &AccountManager::accountLoaded, this, [this, api](const Account &a) {
        if (a.isValid())
            api->setToken(a.token);
        else {
            auto *wizard = new AccountWizard(this);
            connect(wizard, &QDialog::accepted, this, [accounts, wizard] {
               // TODO: actually store straight to keychain
            });
            wizard->open();
        }
    });

    ui->setupUi(this);

    auto *userIcon = new Aero::UserIcon(Aero::UserIcon::Size_48, ui->centralwidget);
    userIcon->setIcon(QIcon::fromTheme("avatar-default"));
    ui->userInfoLayout->insertWidget(0, userIcon);

    connect(ui->actionAdd_Account, &QAction::triggered, this, [this] {
        auto *wizard = new AccountWizard(this);
        wizard->show();
    });

    connect(ui->actionQuit, &QAction::triggered, qApp, &QApplication::quit);
}

MainWindow::~MainWindow()
{
    delete ui;
}
