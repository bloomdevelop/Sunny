#pragma once

#include "message.hpp"

#include <QJsonObject>
#include <QWidget>

class QLabel;
class QVBoxLayout;

namespace Aero {
class InfoStrip;
}

// A small Aero-styled flyout with a user's public information, shown when the
// user clicks an avatar or an author name.
class UserInfoPopup : public QWidget
{
    Q_OBJECT
public:
    explicit UserInfoPopup(const User &user, QWidget *parent = nullptr);
    ~UserInfoPopup() override;

    void setProfile(const QJsonObject &profileObject);

signals:
    void directMessageRequested(const User &user);

private:
    void buildUi();
    void applyAvatar();
    QLabel *addField(QVBoxLayout *layout, const QString &html);
    void setFieldText(QLabel *label, const QString &html);

    User m_user;
    Aero::InfoStrip *m_header = nullptr;
    QLabel *m_bioTitle = nullptr;
    QLabel *m_bio = nullptr;
    QLabel *m_pronouns = nullptr;
    QLabel *m_created = nullptr;
    QLabel *m_mutual = nullptr;
    QLabel *m_premium = nullptr;
};
