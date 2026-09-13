#include "media_utils.hpp"

#include "message.hpp"

#include <QDesktopServices>
#include <QGlobalStatic>
#include <QImageReader>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrlQuery>

namespace {

Q_GLOBAL_STATIC(QUrl, s_mediaBase, QStringLiteral("https://fluxerusercontent.com"))
Q_GLOBAL_STATIC(QUrl, s_staticBase, QStringLiteral("https://fluxerstatic.com"))

// Decoded image data the pixmap cache is allowed to hold, in kibibytes.
constexpr qsizetype kImageCacheBudgetKb = 32 * 1024;

} // namespace

QUrl Media::mediaBase()
{
    return *s_mediaBase;
}

QUrl Media::staticBase()
{
    return *s_staticBase;
}

void Media::setEndpoints(const QUrl &mediaBase, const QUrl &staticBase)
{
    if (mediaBase.isValid())
        *s_mediaBase = mediaBase;
    if (staticBase.isValid())
        *s_staticBase = staticBase;
}

QUrl Media::avatarUrl(const QString &userId, const QString &avatarHash, int size)
{
    if (userId.isEmpty())
        return {};

    if (avatarHash.isEmpty()) {
        bool ok = false;
        const qulonglong id = userId.toULongLong(&ok);
        const int index = ok ? static_cast<int>(id % 6) : 0;
        return QUrl(s_staticBase->toString() + QStringLiteral("/avatars/%1.png?v=1").arg(index));
    }

    const bool animated = avatarHash.startsWith(QLatin1String("a_"));
    QUrl url(s_mediaBase->toString() + QStringLiteral("/avatars/%1/%2.webp").arg(userId, avatarHash));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("size"), QString::number(size));
    if (animated)
        query.addQueryItem(QStringLiteral("animated"), QStringLiteral("true"));
    url.setQuery(query);
    return url;
}

QUrl Media::avatarUrl(const User &user, int size)
{
    return avatarUrl(user.id, user.avatar, size);
}

QUrl Media::guildIconUrl(const QString &id, const QString &iconHash, int size)
{
    if (id.isEmpty() || iconHash.isEmpty())
        return {};

    const bool animated = iconHash.startsWith(QLatin1String("a_"));
    QUrl url(s_mediaBase->toString() + QStringLiteral("/icons/%1/%2.webp").arg(id, iconHash));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("size"), QString::number(size));
    if (animated)
        query.addQueryItem(QStringLiteral("animated"), QStringLiteral("true"));
    url.setQuery(query);
    return url;
}

QUrl Media::emojiUrl(const QString &emojiId, bool animated, int size)
{
    if (emojiId.isEmpty())
        return {};

    QUrl url(s_mediaBase->toString() + QStringLiteral("/emojis/%1.webp").arg(emojiId));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("size"), QString::number(size));
    if (animated)
        query.addQueryItem(QStringLiteral("animated"), QStringLiteral("true"));
    url.setQuery(query);
    return url;
}

QDateTime Media::snowflakeTime(const QString &id)
{
    bool ok = false;
    const qulonglong snowflake = id.toULongLong(&ok);
    if (!ok)
        return {};

    constexpr qint64 epoch = 1420070400000LL;
    return QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(snowflake >> 22) + epoch);
}

QString Media::formatFileSize(qint64 bytes)
{
    constexpr double kb = 1024.0;
    constexpr double mb = kb * 1024.0;
    constexpr double gb = mb * 1024.0;

    if (bytes >= gb)
        return QStringLiteral("%1 GB").arg(bytes / gb, 0, 'f', 1);
    if (bytes >= mb)
        return QStringLiteral("%1 MB").arg(bytes / mb, 0, 'f', 1);
    if (bytes >= kb)
        return QStringLiteral("%1 KB").arg(bytes / kb, 0, 'f', 1);
    return QStringLiteral("%1 B").arg(bytes);
}

QString Media::formatDuration(int seconds)
{
    const int minutes = seconds / 60;
    const int remainder = seconds % 60;
    return QStringLiteral("%1:%2").arg(minutes).arg(remainder, 2, 10, QLatin1Char('0'));
}

ImageCache::ImageCache(QObject *parent)
    : QObject(parent)
{
    m_cache.setMaxCost(kImageCacheBudgetKb);
}

ImageCache *ImageCache::instance()
{
    static ImageCache cache;
    return &cache;
}

QString ImageCache::cacheKey(const QUrl &url, const QSize &maximumSize)
{
    return url.toString()
           + QStringLiteral("@%1x%2").arg(maximumSize.width()).arg(maximumSize.height());
}

QPixmap ImageCache::pixmap(const QUrl &url, const QSize &maximumSize) const
{
    const QPixmap *cached = m_cache.object(cacheKey(url, maximumSize));
    return cached ? *cached : QPixmap();
}

void ImageCache::request(const QUrl &url, const QSize &maximumSize)
{
    if (!url.isValid())
        return;

    const QString key = cacheKey(url, maximumSize);
    if (m_cache.contains(key) || m_pending.contains(key))
        return;

    m_pending.insert(key);
    QNetworkRequest request(url);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_manager.get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, maximumSize, key] {
        m_pending.remove(key);
        if (reply->error() == QNetworkReply::NoError) {
            // Ask the image handler to scale while decoding when the format
            // allows it, so full-resolution pictures never reach the cache.
            QImageReader reader(reply);
            const QSize sourceSize = reader.size();
            if (!maximumSize.isEmpty() && sourceSize.isValid()
                && (sourceSize.width() > maximumSize.width()
                    || sourceSize.height() > maximumSize.height())) {
                reader.setScaledSize(sourceSize.scaled(maximumSize, Qt::KeepAspectRatio));
            }

            QImage image = reader.read();
            if (!image.isNull() && !maximumSize.isEmpty()
                && (image.width() > maximumSize.width()
                    || image.height() > maximumSize.height())) {
                image = image.scaled(maximumSize, Qt::KeepAspectRatio,
                                     Qt::SmoothTransformation);
            }

            if (!image.isNull()) {
                const QPixmap pixmap = QPixmap::fromImage(image);
                const qsizetype cost = qMax<qsizetype>(
                    1, qsizetype(pixmap.width()) * pixmap.height() * pixmap.depth() / 8 / 1024);
                m_cache.insert(key, new QPixmap(pixmap), cost);
            }
        }
        reply->deleteLater();
        emit imageLoaded(url);
    });
}

AsyncImageLabel::AsyncImageLabel(QWidget *parent)
    : QLabel(parent)
{
    setAlignment(Qt::AlignLeft | Qt::AlignTop);
    setTextInteractionFlags(Qt::NoTextInteraction);
    setCursor(Qt::PointingHandCursor);
}

void AsyncImageLabel::setSourceUrl(const QUrl &url, QSize maximumSize)
{
    m_url = url;
    m_maximumSize = maximumSize;
    setText(QString());

    if (!url.isValid())
        return;

    const QPixmap cached = ImageCache::instance()->pixmap(url, maximumSize);
    if (!cached.isNull()) {
        applyPixmap(cached);
        return;
    }

    connect(ImageCache::instance(), &ImageCache::imageLoaded, this,
            [this](const QUrl &loaded) {
        if (loaded != m_url)
            return;
        const QPixmap pixmap = ImageCache::instance()->pixmap(m_url, m_maximumSize);
        if (!pixmap.isNull())
            applyPixmap(pixmap);
    });
    ImageCache::instance()->request(url, maximumSize);
}

void AsyncImageLabel::applyPixmap(const QPixmap &pixmap)
{
    // The cache already stores pixmaps at their display size, so the common
    // case shares the cached data instead of copying it into the label.
    QPixmap scaled = pixmap;
    if (!m_maximumSize.isEmpty()
        && (pixmap.width() > m_maximumSize.width()
            || pixmap.height() > m_maximumSize.height())) {
        scaled = pixmap.scaled(m_maximumSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }
    setFixedSize(scaled.size());
    setPixmap(scaled);
}

void AsyncImageLabel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_url.isValid())
        QDesktopServices::openUrl(m_url);
    QLabel::mouseReleaseEvent(event);
}
