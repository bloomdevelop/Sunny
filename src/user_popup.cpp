#include "user_popup.hpp"

#include "markdown.hpp"
#include "media_utils.hpp"

#include <AeroQt/infostrip.h>
#include <AeroQt/insetwindow.h>

#include <QFrame>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

UserInfoPopup::UserInfoPopup(const User &user, QWidget *parent)
    : QWidget(parent, Qt::Popup)
    , m_user(user)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setFixedWidth(340);
    buildUi();
}

UserInfoPopup::~UserInfoPopup() = default;

void UserInfoPopup::buildUi()
{
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 10);
    layout->setSpacing(6);

    m_header = new Aero::InfoStrip(content);
    m_header->setIcon(QIcon::fromTheme(QStringLiteral("avatar-default")));

    auto *name = new QLabel(content);
    QString nameHtml = QStringLiteral("<b style=\"font-size:14px;color:#1a3d6d;\">%1</b>")
                           .arg(m_user.displayName().toHtmlEscaped());
    if (m_user.bot) {
        nameHtml += QLatin1String(
            " <span style=\"border:1px solid #9a9a9a;border-radius:2px;padding:0px 3px;"
            "color:#5a5a5a;font-size:9px;\">BOT</span>");
    }
    if (m_user.system) {
        nameHtml += QLatin1String(
            " <span style=\"border:1px solid #9a9a9a;border-radius:2px;padding:0px 3px;"
            "color:#5a5a5a;font-size:9px;\">SYSTEM</span>");
    }
    name->setText(nameHtml);
    name->setTextFormat(Qt::RichText);

    auto *tag = new QLabel(m_user.tag(), content);
    tag->setStyleSheet(QStringLiteral("color:#7a7a7a;font-size:11px;"));

    auto *nameColumn = new QVBoxLayout;
    nameColumn->setContentsMargins(0, 0, 0, 0);
    nameColumn->setSpacing(0);
    nameColumn->addWidget(name);
    nameColumn->addWidget(tag);
    m_header->childLayout()->addLayout(nameColumn, 1);

    layout->addWidget(m_header);

    auto *body = new QWidget(content);
    auto *bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(14, 4, 14, 0);
    bodyLayout->setSpacing(5);

    m_bioTitle = new QLabel(tr("About me"), body);
    m_bioTitle->setStyleSheet(QStringLiteral("font-weight:bold;color:#3a3a3a;"));
    m_bioTitle->hide();
    bodyLayout->addWidget(m_bioTitle);

    m_bio = new QLabel(body);
    m_bio->setTextFormat(Qt::RichText);
    m_bio->setWordWrap(true);
    m_bio->setTextInteractionFlags(Qt::TextBrowserInteraction | Qt::TextSelectableByMouse);
    m_bio->hide();
    bodyLayout->addWidget(m_bio);

    m_pronouns = addField(bodyLayout, QString());
    m_created = addField(bodyLayout, QString());
    m_mutual = addField(bodyLayout, QString());
    m_premium = addField(bodyLayout, QString());
    m_premium->hide();

    auto *separator = new QFrame(body);
    separator->setFrameShape(QFrame::HLine);
    separator->setFrameShadow(QFrame::Sunken);
    bodyLayout->addWidget(separator);

    auto *actions = new QHBoxLayout;
    actions->setContentsMargins(0, 0, 0, 0);
    auto *messageButton = new QPushButton(tr("Send Message"), body);
    messageButton->setIcon(QIcon::fromTheme(QStringLiteral("mail-message-new")));
    connect(messageButton, &QPushButton::clicked, this, [this] {
        emit directMessageRequested(m_user);
        close();
    });
    actions->addWidget(messageButton);
    actions->addStretch();
    bodyLayout->addLayout(actions);

    layout->addWidget(body);

    if (Media::snowflakeTime(m_user.id).isValid()) {
        setFieldText(m_created,
                     tr("<b>Account created</b><br/>%1")
                         .arg(QLocale::system().toString(Media::snowflakeTime(m_user.id).toLocalTime(),
                                                         QLocale::LongFormat)
                                  .toHtmlEscaped()));
    } else {
        setFieldText(m_created,
                     tr("<b>User ID</b><br/>%1").arg(m_user.id.toHtmlEscaped()));
    }

    Aero::makeInsetWindow(this, content);
    applyAvatar();
}

QLabel *UserInfoPopup::addField(QVBoxLayout *layout, const QString &html)
{
    auto *label = new QLabel(html, layout->parentWidget());
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->hide();
    layout->addWidget(label);
    return label;
}

void UserInfoPopup::setFieldText(QLabel *label, const QString &html)
{
    if (!label)
        return;
    label->setText(html);
    label->setTextFormat(Qt::RichText);
    label->setVisible(!html.isEmpty());
}

void UserInfoPopup::applyAvatar()
{
    const QUrl url = Media::avatarUrl(m_user, 48);
    if (url.isEmpty())
        return;

    const QPixmap cached = ImageCache::instance()->pixmap(url);
    if (!cached.isNull()) {
        m_header->setIcon(QIcon(cached));
        return;
    }

    connect(ImageCache::instance(), &ImageCache::imageLoaded, m_header,
            [this, url](const QUrl &loaded) {
        if (loaded != url)
            return;
        const QPixmap pixmap = ImageCache::instance()->pixmap(url);
        if (!pixmap.isNull())
            m_header->setIcon(QIcon(pixmap));
    });
    ImageCache::instance()->request(url);
}

void UserInfoPopup::setProfile(const QJsonObject &profileObject)
{
    const QJsonObject profile = profileObject.value(QStringLiteral("user_profile")).toObject();

    QString bio = profile.value(QStringLiteral("bio")).toString();
    if (bio.isEmpty())
        bio = profileObject.value(QStringLiteral("user"))
                  .toObject()
                  .value(QStringLiteral("bio"))
                  .toString();
    if (!bio.isEmpty()) {
        m_bioTitle->show();
        m_bio->setText(Markdown::toHtml(bio));
        m_bio->show();
    }

    const QString pronouns = profile.value(QStringLiteral("pronouns")).toString();
    if (!pronouns.isEmpty())
        setFieldText(m_pronouns, tr("<b>Pronouns</b><br/>%1").arg(pronouns.toHtmlEscaped()));

    const int premium = profileObject.value(QStringLiteral("premium_type")).toInt();
    if (premium > 0)
        setFieldText(m_premium, tr("<b>Premium</b><br/>Yes"));

    const QJsonArray mutualGuilds = profileObject.value(QStringLiteral("mutual_guilds")).toArray();
    if (!mutualGuilds.isEmpty()) {
        setFieldText(m_mutual,
                     tr("<b>Mutual servers</b><br/>%1").arg(mutualGuilds.size()));
    }
}
