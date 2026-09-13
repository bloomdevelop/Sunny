#pragma once

#include <QHash>
#include <QString>

struct MentionResolver
{
    QHash<QString, QString> userNames;
    QHash<QString, QString> channelNames;
    QHash<QString, QString> roleNames;
};

namespace Markdown {

// Renders Fluxer-flavoured markdown to HTML suitable for QTextDocument.
QString toHtml(const QString &text, const MentionResolver &resolver = {});

// Renders the same source to a compact plain-text form (for previews).
QString toPlainText(const QString &text);

// Resolves ":shortcode:" names (without the colons) to a Unicode emoji.
QString shortcodeToUnicode(const QString &name);

} // namespace Markdown
