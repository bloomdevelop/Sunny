#pragma once

#include <QCache>
#include <QDateTime>
#include <QHash>
#include <QLabel>
#include <QMetaType>
#include <QNetworkAccessManager>
#include <QPixmap>
#include <QSet>
#include <QUrl>

class User;

namespace Media {

// Endpoints of the official deployment. A self-hosted instance announces its
// own values through /.well-known/fluxer and can override them here.
QUrl mediaBase();
QUrl staticBase();
void setEndpoints(const QUrl &mediaBase, const QUrl &staticBase);

QUrl avatarUrl(const User &user, int size = 48);
QUrl avatarUrl(const QString &userId, const QString &avatarHash, int size = 48);
QUrl guildIconUrl(const QString &id, const QString &iconHash, int size = 48);
QUrl emojiUrl(const QString &emojiId, bool animated = false, int size = 48);
QDateTime snowflakeTime(const QString &id);

QString formatFileSize(qint64 bytes);
QString formatDuration(int seconds);

} // namespace Media

// Small process-wide pixmap cache backed by a QNetworkAccessManager. Widgets
// connect to imageLoaded() and re-read pixmap(). Images are scaled down to the
// requested display size while they are decoded, and the cache evicts the
// least recently used entries once its memory budget is exceeded.
class ImageCache : public QObject
{
    Q_OBJECT
public:
    static ImageCache *instance();

    // The returned pixmap is scaled to fit maximumSize; an empty size keeps
    // the original dimensions.
    QPixmap pixmap(const QUrl &url, const QSize &maximumSize = {}) const;
    void request(const QUrl &url, const QSize &maximumSize = {});

signals:
    void imageLoaded(const QUrl &url);

private:
    explicit ImageCache(QObject *parent = nullptr);

    static QString cacheKey(const QUrl &url, const QSize &maximumSize);

    QNetworkAccessManager m_manager;
    QCache<QString, QPixmap> m_cache;
    QSet<QString> m_pending;
};

// A QLabel that loads an image asynchronously and scales it down to a box.
class AsyncImageLabel : public QLabel
{
    Q_OBJECT
public:
    explicit AsyncImageLabel(QWidget *parent = nullptr);

    void setSourceUrl(const QUrl &url, QSize maximumSize);
    QUrl sourceUrl() const { return m_url; }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void applyPixmap(const QPixmap &pixmap);

    QUrl m_url;
    QSize m_maximumSize;
};
