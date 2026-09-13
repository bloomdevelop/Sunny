#include "account_wizard.hpp"
#include "api_client.hpp"
#include "ui_account_wizard.h"

#include <AeroQt/wizard.h>

#include <QComboBox>
#include <QCommandLinkButton>
#include <QJsonDocument>
#include <QLineEdit>
#include <QTimer>
#include <QUrl>
#include <QDebug>

LoginOptionsPage::LoginOptionsPage(QWidget *parent)
    : QWizardPage(parent)
{
}

void LoginOptionsPage::setEmailPageId(int id)
{
    m_emailPageId = id;
}

void LoginOptionsPage::setHandoffPageId(int id)
{
    m_handoffPageId = id;
}

void LoginOptionsPage::selectEmail()
{
    if (m_emailPageId < 0)
        return;
    m_selectedId = m_emailPageId;
    emit completeChanged();
}

void LoginOptionsPage::selectHandoff()
{
    if (m_handoffPageId < 0)
        return;
    m_selectedId = m_handoffPageId;
    emit completeChanged();
}

bool LoginOptionsPage::isComplete() const
{
    return m_selectedId != -1;
}

int LoginOptionsPage::nextId() const
{
    // Email -> _emailOption, Handoff -> handoffOption.
    return m_selectedId;
}

AccountWizard::AccountWizard(QWidget *parent)
    : Aero::Wizard(parent)
    , ui(new Ui::AccountWizard)
{
    // Destroy wizard when done, otherwise it can cause memory leak.
    setAttribute(Qt::WA_DeleteOnClose);
    ui->setupUi(this);

    auto *debounce = new QTimer(this);
    debounce->setSingleShot(true);
    debounce->setInterval(500); // ms

    auto const ids = pageIds();
    for (int id : ids) {
        if (page(id)->title() == QLatin1String("Instances")) {
            m_instancesPage = page(id);
            break;
        }
    }

    // God forbid whoever created this function, I don't really like how it being handle this.
    connect(
        ui->instanceOptionCombo,
        &QComboBox::currentTextChanged,
        this,
        [this, debounce](const QString &text) {
            m_isUsingCustomInstance = (text == QLatin1String("Custom") && m_customInstanceValid);
            ui->normalLogin->setEnabled(m_isUsingCustomInstance);

            const bool custom = (text == QLatin1String("Custom"));
            ui->customInstanceInput->setVisible(custom);
            if (custom && !ui->customInstanceInput->text().trimmed().isEmpty())
                debounce->start();
            else
                debounce->stop();
            updateNextButton();
        }
    );
    connect(
        ui->customInstanceInput,
        &QLineEdit::textChanged,
        debounce,
        qOverload<>(&QTimer::start)
    );
    connect(
        ui->customInstanceInput,
        &QLineEdit::textChanged,
        this,
        [this] {
            m_customInstanceValid = false;
            updateNextButton();
        }
    );

    connect(
        debounce,
        &QTimer::timeout,
        this,
        [this] {
            QUrl url = QUrl::fromUserInput(
                ui->customInstanceInput->text().trimmed());

            if (url.scheme() == QLatin1String("http"))
                url.setScheme(QStringLiteral("https"));

            if (!url.isValid() || url.host().isEmpty()) {
                m_customInstanceValid = false;
                updateNextButton();
                return;
            }

            validateViaWellKnown(url);
        }
    );

    connect(
        this,
        &QWizard::currentIdChanged,
        this,
        [this] { updateNextButton(); }
    );

    const auto idOf = [this, ids](QWizardPage *p) {
        for (int id : ids) {
            if (page(id) == p)
                return id;
        }
        return -1;
    };
    ui->wizardPage3->setEmailPageId(idOf(ui->wizardPage4_emailOption));
    ui->wizardPage3->setHandoffPageId(idOf(ui->wizardPage4_handoffOption));
    connect(
        ui->normalLogin,
        &QCommandLinkButton::clicked,
        this,
        [this] {
            ui->wizardPage3->selectEmail();
            next();
        }
    );
    connect(
        ui->handoffLogin,
        &QCommandLinkButton::clicked,
        this,
        [this] {
            ui->wizardPage3->selectHandoff();
            next();
        }
    );

    updateNextButton();
}

AccountWizard::~AccountWizard()
{
    delete ui;
}

QUrl AccountWizard::officialInstanceUrl()
{
    return QUrl(QStringLiteral("https://api.fluxer.app"));
}

QUrl AccountWizard::selectedInstanceUrl() const
{
    if (ui->instanceOptionCombo->currentText() == QLatin1String("Custom"))
        return QUrl::fromUserInput(ui->customInstanceInput->text().trimmed());
    return officialInstanceUrl();
}

void AccountWizard::validateViaWellKnown(const QUrl &url)
{
    const QString expected = url.toString();
    auto *api = new ApiClient(url, this);

    connect(api, &ApiClient::jsonReceived, this,
            [this, api, expected](const QJsonDocument &doc) {
        api->deleteLater();
        qDebug() << "Obtained .well-known:" << !doc.isNull();

        if (QUrl::fromUserInput(ui->customInstanceInput->text().trimmed()).toString() != expected)
            return;
        m_customInstanceValid = !doc.isNull();
        updateNextButton();
    });
    connect(api, &ApiClient::error, this,
            [this, api, expected](const QString &msg) {
        api->deleteLater();

        qDebug() << "Failed to obtain:" << msg;
        if (QUrl::fromUserInput(ui->customInstanceInput->text().trimmed()).toString() != expected)
            return;
        m_customInstanceValid = false;
        updateNextButton();
    });

    api->getJson(QStringLiteral("/.well-known/fluxer"));
}

void AccountWizard::updateNextButton()
{
    if (m_instancesPage && currentPage() != m_instancesPage)
        return; // other pages: let QWizard manage the buttons
    const bool custom =
        ui->instanceOptionCombo->currentText() == QLatin1String("Custom");
    button(QWizard::NextButton)->setEnabled(!custom || m_customInstanceValid);
}
