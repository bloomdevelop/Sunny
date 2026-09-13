#include "main_window.hpp"
#include "account_wizard.hpp"
#include "./ui_main_window.h"

#include <AeroQt/usericon.h>
#include <QAction>


MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
{
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
