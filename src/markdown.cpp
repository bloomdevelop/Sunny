#include "markdown.hpp"

#include <QDateTime>
#include <QLocale>
#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

#include <algorithm>

namespace {

QString escape(const QString &text)
{
    QString out;
    out.reserve(text.size());
    for (const QChar &c : text) {
        switch (c.unicode()) {
        case u'&':
            out += QStringLiteral("&amp;");
            break;
        case u'<':
            out += QStringLiteral("&lt;");
            break;
        case u'>':
            out += QStringLiteral("&gt;");
            break;
        case u'"':
            out += QStringLiteral("&quot;");
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

const QHash<QString, QString> &emojiTable()
{
    static const QHash<QString, QString> table = {
        { QStringLiteral("+1"), QStringLiteral("👍") },
        { QStringLiteral("-1"), QStringLiteral("👎") },
        { QStringLiteral("100"), QStringLiteral("💯") },
        { QStringLiteral("angry"), QStringLiteral("😠") },
        { QStringLiteral("blush"), QStringLiteral("😊") },
        { QStringLiteral("check"), QStringLiteral("✅") },
        { QStringLiteral("clap"), QStringLiteral("👏") },
        { QStringLiteral("cry"), QStringLiteral("😢") },
        { QStringLiteral("eyes"), QStringLiteral("👀") },
        { QStringLiteral("fire"), QStringLiteral("🔥") },
        { QStringLiteral("grin"), QStringLiteral("😁") },
        { QStringLiteral("heart"), QStringLiteral("❤️") },
        { QStringLiteral("heart_eyes"), QStringLiteral("😍") },
        { QStringLiteral("joy"), QStringLiteral("😂") },
        { QStringLiteral("ok_hand"), QStringLiteral("👌") },
        { QStringLiteral("pray"), QStringLiteral("🙏") },
        { QStringLiteral("rocket"), QStringLiteral("🚀") },
        { QStringLiteral("rofl"), QStringLiteral("🤣") },
        { QStringLiteral("skull"), QStringLiteral("💀") },
        { QStringLiteral("slight_smile"), QStringLiteral("🙂") },
        { QStringLiteral("smile"), QStringLiteral("😄") },
        { QStringLiteral("sob"), QStringLiteral("😭") },
        { QStringLiteral("sparkles"), QStringLiteral("✨") },
        { QStringLiteral("star"), QStringLiteral("⭐") },
        { QStringLiteral("sunglasses"), QStringLiteral("😎") },
        { QStringLiteral("tada"), QStringLiteral("🎉") },
        { QStringLiteral("thinking"), QStringLiteral("🤔") },
        { QStringLiteral("thumbsup"), QStringLiteral("👍") },
        { QStringLiteral("thumbsdown"), QStringLiteral("👎") },
        { QStringLiteral("warning"), QStringLiteral("⚠️") },
        { QStringLiteral("wave"), QStringLiteral("👋") },
        { QStringLiteral("wink"), QStringLiteral("😉") },
        { QStringLiteral("x"), QStringLiteral("❌") },
    };
    return table;
}

QString formatTimestamp(qint64 seconds, QChar style)
{
    const QDateTime dt = QDateTime::fromSecsSinceEpoch(seconds);
    const QLocale locale;
    switch (style.unicode()) {
    case u't':
        return locale.toString(dt.time(), QLocale::ShortFormat);
    case u'T':
        return locale.toString(dt.time(), QLocale::LongFormat);
    case u'd':
        return locale.toString(dt.date(), QLocale::ShortFormat);
    case u'D':
        return locale.toString(dt.date(), QLocale::LongFormat);
    case u'f':
        return locale.toString(dt, QLocale::ShortFormat);
    case u'F':
        return locale.toString(dt, QLocale::LongFormat);
    case u'R': {
        const QDateTime now = QDateTime::currentDateTimeUtc();
        const qint64 delta = dt.secsTo(now);
        const qint64 abs = qAbs(delta);
        QString unit;
        qint64 amount = 0;
        if (abs < 60) {
            unit = QStringLiteral("second");
            amount = abs;
        } else if (abs < 3600) {
            unit = QStringLiteral("minute");
            amount = abs / 60;
        } else if (abs < 86400) {
            unit = QStringLiteral("hour");
            amount = abs / 3600;
        } else if (abs < 2592000) {
            unit = QStringLiteral("day");
            amount = abs / 86400;
        } else if (abs < 31536000) {
            unit = QStringLiteral("month");
            amount = abs / 2592000;
        } else {
            unit = QStringLiteral("year");
            amount = abs / 31536000;
        }
        const QString text = amount == 1 ? unit : unit + QLatin1Char('s');
        return delta >= 0 ? QStringLiteral("%1 %2 ago").arg(amount).arg(text)
                          : QStringLiteral("in %1 %2").arg(amount).arg(text);
    }
    default:
        return locale.toString(dt, QLocale::ShortFormat);
    }
}

QString mentionUser(const QString &id, const MentionResolver &resolver)
{
    const auto it = resolver.userNames.constFind(id);
    if (it != resolver.userNames.constEnd())
        return QStringLiteral("<a href=\"fluxer://user/") + id
               + QStringLiteral("\" style=\"color:#3b6ea5;background-color:#e6eef8;") + QStringLiteral("\">@") + escape(it.value()) + QStringLiteral("</a>");
    return QStringLiteral("<span style=\"color:#3b6ea5;background-color:#e6eef8;\">@unknown</span>");
}

QString mentionChannel(const QString &id, const MentionResolver &resolver)
{
    const auto it = resolver.channelNames.constFind(id);
    const QString name = it != resolver.channelNames.constEnd() ? it.value()
                                                                : QStringLiteral("unknown");
    return QStringLiteral("<a href=\"fluxer://channel/") + id
           + QStringLiteral("\" style=\"color:#3b6ea5;background-color:#e6eef8;") + QStringLiteral("\">#") + escape(name) + QStringLiteral("</a>");
}

QString mentionRole(const QString &id, const MentionResolver &resolver)
{
    const auto it = resolver.roleNames.constFind(id);
    const QString name = it != resolver.roleNames.constEnd() ? it.value()
                                                             : QStringLiteral("unknown");
    return QStringLiteral("<span style=\"color:#3b6ea5;background-color:#e6eef8;\">@") + escape(name)
           + QStringLiteral("</span>");
}

QString linkifyUrl(const QString &url)
{
    static const QRegularExpression trailing(
        QStringLiteral(R"([.,!?;:)\]}'"]+$)"));
    QString clean = url;
    QString trailingText;
    const QRegularExpressionMatch match = trailing.match(clean);
    if (match.hasMatch()) {
        trailingText = match.captured();
        clean.chop(trailingText.size());
    }
    return QStringLiteral("<a href=\"") + escape(clean) + QStringLiteral("\">") + escape(clean)
           + QStringLiteral("</a>") + escape(trailingText);
}

QString renderInline(const QString &text, const MentionResolver &resolver);

QString renderDelimited(const QString &text, const MentionResolver &resolver)
{
    return renderInline(text, resolver);
}

QString renderInline(const QString &text, const MentionResolver &resolver)
{
    QString out;
    int i = 0;
    const int n = text.size();

    while (i < n) {
        const QChar c = text.at(i);

        if (c == QLatin1Char('\\') && i + 1 < n) {
            out += escape(QString(text.at(i + 1)));
            i += 2;
            continue;
        }

        if (c == QLatin1Char('`')) {
            const int end = text.indexOf(QLatin1Char('`'), i + 1);
            if (end > i) {
                out += QStringLiteral("<code>") + escape(text.mid(i + 1, end - i - 1))
                       + QStringLiteral("</code>");
                i = end + 1;
                continue;
            }
        }

        const auto delimited = [&](const QString &marker, const QString &openTag,
                                   const QString &closeTag) -> bool {
            if (!text.mid(i).startsWith(marker, Qt::CaseInsensitive))
                return false;
            const int start = i + marker.size();
            const int end = text.indexOf(marker, start, Qt::CaseInsensitive);
            if (end <= start)
                return false;
            out += openTag + renderDelimited(text.mid(start, end - start), resolver) + closeTag;
            i = end + marker.size();
            return true;
        };

        if (c == QLatin1Char('*') && text.mid(i, 2) == QStringLiteral("**")) {
            if (delimited(QStringLiteral("**"), QStringLiteral("<b>"), QStringLiteral("</b>")))
                continue;
        }
        if (c == QLatin1Char('_') && text.mid(i, 2) == QStringLiteral("__")) {
            if (delimited(QStringLiteral("__"), QStringLiteral("<u>"), QStringLiteral("</u>")))
                continue;
        }
        if (c == QLatin1Char('~') && text.mid(i, 2) == QStringLiteral("~~")) {
            if (delimited(QStringLiteral("~~"), QStringLiteral("<s>"), QStringLiteral("</s>")))
                continue;
        }
        if (c == QLatin1Char('|') && text.mid(i, 2) == QStringLiteral("||")) {
            if (delimited(QStringLiteral("||"), QStringLiteral("<span style=\"background-color:#b8b8b8;color:#b8b8b8;\">"),
                          QStringLiteral("</span>")))
                continue;
        }
        if (c == QLatin1Char('*')) {
            if (delimited(QStringLiteral("*"), QStringLiteral("<i>"), QStringLiteral("</i>")))
                continue;
        }
        if (c == QLatin1Char('_')) {
            if (delimited(QStringLiteral("_"), QStringLiteral("<i>"), QStringLiteral("</i>")))
                continue;
        }

        if (c == QLatin1Char('[')) {
            static const QRegularExpression maskedLink(
                QStringLiteral(R"(^\[([^\]]+)\]\((\S+?)\))"));
            const QRegularExpressionMatch match = maskedLink.match(text.mid(i));
            if (match.hasMatch()) {
                const QString url = match.captured(2);
                if (url.startsWith(QStringLiteral("http://"))
                    || url.startsWith(QStringLiteral("https://"))) {
                    out += QStringLiteral("<a href=\"") + escape(url) + QStringLiteral("\">")
                           + renderDelimited(match.captured(1), resolver)
                           + QStringLiteral("</a>");
                    i += match.capturedLength();
                    continue;
                }
            }
        }

        if (c == QLatin1Char('<')) {
            const int end = text.indexOf(QLatin1Char('>'), i + 1);
            if (end > i) {
                const QString inner = text.mid(i + 1, end - i - 1);

                if (inner.startsWith(QLatin1Char('@'))) {
                    QString id = inner.mid(1);
                    if (id.startsWith(QLatin1Char('!')))
                        id = id.mid(1);
                    if (!id.isEmpty() && std::all_of(id.cbegin(), id.cend(), [](QChar ch) {
                            return ch.isDigit();
                        })) {
                        out += mentionUser(id, resolver);
                        i = end + 1;
                        continue;
                    }
                }
                if (inner.startsWith(QStringLiteral("@&"))) {
                    out += mentionRole(inner.mid(2), resolver);
                    i = end + 1;
                    continue;
                }
                if (inner.startsWith(QLatin1Char('#'))) {
                    out += mentionChannel(inner.mid(1), resolver);
                    i = end + 1;
                    continue;
                }
                if (inner.startsWith(QStringLiteral("t:"))) {
                    const QStringList parts =
                        inner.mid(2).split(QLatin1Char(':'), Qt::SkipEmptyParts);
                    if (!parts.isEmpty()) {
                        bool ok = false;
                        const qint64 secs = parts.first().toLongLong(&ok);
                        if (ok) {
                            const QChar style = parts.size() > 1 ? parts.at(1).at(0) : QLatin1Char('f');
                            out += QStringLiteral("<span style=\"color:#6b6b6b;\">")
                                   + escape(formatTimestamp(secs, style))
                                   + QStringLiteral("</span>");
                            i = end + 1;
                            continue;
                        }
                    }
                }
                if (inner.startsWith(QStringLiteral("http://"))
                    || inner.startsWith(QStringLiteral("https://"))) {
                    out += QStringLiteral("<a href=\"") + escape(inner) + QStringLiteral("\">")
                           + escape(inner) + QStringLiteral("</a>");
                    i = end + 1;
                    continue;
                }
                if (inner.startsWith(QStringLiteral("mailto:"))) {
                    out += QStringLiteral("<a href=\"") + escape(inner) + QStringLiteral("\">")
                           + escape(inner.mid(7)) + QStringLiteral("</a>");
                    i = end + 1;
                    continue;
                }
                if (inner.contains(QLatin1Char('@')) && !inner.contains(QLatin1Char(' '))
                    && !inner.startsWith(QStringLiteral("@"))
                    && !inner.startsWith(QStringLiteral("#"))) {
                    out += QStringLiteral("<a href=\"mailto:") + escape(inner)
                           + QStringLiteral("\">") + escape(inner) + QStringLiteral("</a>");
                    i = end + 1;
                    continue;
                }
                if (inner.startsWith(QStringLiteral("tel:"))
                    || inner.startsWith(QStringLiteral("sms:"))) {
                    out += QStringLiteral("<a href=\"") + escape(inner) + QStringLiteral("\">")
                           + escape(inner) + QStringLiteral("</a>");
                    i = end + 1;
                    continue;
                }
            }
        }

        if (c == QLatin1Char(':') && (i == 0 || !text.at(i - 1).isLetterOrNumber())) {
            const int end = text.indexOf(QLatin1Char(':'), i + 1);
            if (end > i + 1 && end - i <= 33) {
                const QString name = text.mid(i + 1, end - i - 1);
                const auto it = emojiTable().constFind(name);
                if (it != emojiTable().constEnd()) {
                    out += escape(it.value());
                    i = end + 1;
                    continue;
                }
            }
        }

        if ((c == QLatin1Char('h') || c == QLatin1Char('H'))
            && (text.mid(i).startsWith(QStringLiteral("http://"))
                || text.mid(i).startsWith(QStringLiteral("https://")))) {
            static const QRegularExpression urlRe(
                QStringLiteral(R"(^https?://[^\s<>]+)"));
            const QRegularExpressionMatch match = urlRe.match(text.mid(i));
            if (match.hasMatch()) {
                out += linkifyUrl(match.captured());
                i += match.capturedLength();
                continue;
            }
        }

        out += escape(QString(c));
        ++i;
    }

    return out;
}

QString renderCodeBlock(const QStringList &lines)
{
    QString code;
    for (int i = 0; i < lines.size(); ++i) {
        if (i)
            code += QLatin1Char('\n');
        code += lines.at(i);
    }
    return QStringLiteral("<pre class=\"code-block\">") + escape(code) + QStringLiteral("</pre>");
}

QString renderLines(const QStringList &lines, const MentionResolver &resolver);

QString calloutTitle(const QString &kind)
{
    return kind.at(0).toUpper() + kind.mid(1).toLower();
}

QString renderCallout(const QString &kind, const QStringList &body, const MentionResolver &resolver)
{
    struct Style
    {
        QString accent;
        QString background;
        QString icon;
    };
    const QHash<QString, Style> styles = {
        { QStringLiteral("note"),
          { QStringLiteral("#4a9eff"), QStringLiteral("#eef4fc"), QStringLiteral("ℹ️") } },
        { QStringLiteral("tip"),
          { QStringLiteral("#3fb950"), QStringLiteral("#eefbf0"), QStringLiteral("💡") } },
        { QStringLiteral("important"),
          { QStringLiteral("#a371f7"), QStringLiteral("#f5f0fe"), QStringLiteral("❗") } },
        { QStringLiteral("warning"),
          { QStringLiteral("#d29922"), QStringLiteral("#fff8e6"), QStringLiteral("⚠️") } },
        { QStringLiteral("caution"),
          { QStringLiteral("#f85149"), QStringLiteral("#fdeeee"), QStringLiteral("🛑") } },
    };
    const QString key = kind.toLower();
    const Style style = styles.value(
        key, { QStringLiteral("#888888"), QStringLiteral("#f2f2f2"), QStringLiteral("ℹ️") });

    QString html = QStringLiteral("<table class=\"callout\" width=\"100%\" cellspacing=\"0\" "
                                  "cellpadding=\"0\"><tr><td width=\"4\" bgcolor=\"%1\"></td>"
                                  "<td bgcolor=\"%2\" style=\"padding:8px;\">")
                       .arg(style.accent, style.background);
    html += QStringLiteral("<b>") + escape(style.icon + QLatin1Char(' ')) + calloutTitle(key)
            + QStringLiteral("</b>");
    html += QStringLiteral("<br/>") + renderLines(body, resolver);
    html += QStringLiteral("</td></tr></table>");
    return html;
}

QString renderLines(const QStringList &lines, const MentionResolver &resolver);

QString renderListItem(const QString &content, const QStringList &nested,
                       const MentionResolver &resolver)
{
    QString html = QStringLiteral("<li>") + renderInline(content, resolver);
    if (!nested.isEmpty())
        html += QStringLiteral("<br/>") + renderLines(nested, resolver);
    html += QStringLiteral("</li>");
    return html;
}

bool isBlockStart(const QString &line, int *listIndent = nullptr)
{
    static const QRegularExpression heading(QStringLiteral(R"(^#{1,4}\s+)"));
    static const QRegularExpression fence(QStringLiteral(R"(^\s*```)"));
    static const QRegularExpression quote(QStringLiteral(R"(^\s*>)"));
    static const QRegularExpression rule(QStringLiteral(R"(^\s*(-{3,}|\*{3,}|_{3,})\s*$)"));
    static const QRegularExpression subtext(QStringLiteral(R"(^\s*-#\s+)"));
    static const QRegularExpression list(QStringLiteral(R"(^(\s*)([-*+]|\d+[.)])\s+)"));
    static const QRegularExpression table(QStringLiteral(R"(^\s*\|)"));
    const QRegularExpressionMatch listMatch = list.match(line);
    if (listIndent)
        *listIndent = listMatch.hasMatch() ? listMatch.capturedLength(1) : 0;
    return heading.match(line).hasMatch() || fence.match(line).hasMatch()
           || quote.match(line).hasMatch() || rule.match(line).hasMatch()
           || subtext.match(line).hasMatch() || list.match(line).hasMatch()
           || table.match(line).hasMatch();
}

QString renderLines(const QStringList &lines, const MentionResolver &resolver)
{
    QString html;
    int i = 0;
    const int n = lines.size();

    while (i < n) {
        const QString line = lines.at(i);

        if (line.trimmed().isEmpty()) {
            ++i;
            continue;
        }

        static const QRegularExpression fenceRe(QStringLiteral(R"(^\s*```(\w*))"));
        const QRegularExpressionMatch fenceMatch = fenceRe.match(line);
        if (fenceMatch.hasMatch()) {
            QStringList code;
            ++i;
            while (i < n && !lines.at(i).trimmed().startsWith(QStringLiteral("```"))) {
                code.append(lines.at(i));
                ++i;
            }
            if (i < n)
                ++i;
            html += renderCodeBlock(code);
            continue;
        }

        static const QRegularExpression headingRe(QStringLiteral(R"(^(#{1,4})\s+(.*)$)"));
        const QRegularExpressionMatch heading = headingRe.match(line);
        if (heading.hasMatch()) {
            const int level = heading.captured(1).size();
            html += QStringLiteral("<h%1>").arg(level)
                    + renderInline(heading.captured(2), resolver)
                    + QStringLiteral("</h%1>").arg(level);
            ++i;
            continue;
        }

        static const QRegularExpression ruleRe(
            QStringLiteral(R"(^\s*(-{3,}|\*{3,}|_{3,})\s*$)"));
        if (ruleRe.match(line).hasMatch()) {
            html += QStringLiteral("<hr/>");
            ++i;
            continue;
        }

        static const QRegularExpression subtextRe(
            QStringLiteral(R"(^\s*-#\s+(.*)$)"));
        const QRegularExpressionMatch subtext = subtextRe.match(line);
        if (subtext.hasMatch()) {
            html += QStringLiteral("<p style=\"color:#767676;font-size:smaller;\">")
                    + renderInline(subtext.captured(1), resolver) + QStringLiteral("</p>");
            ++i;
            continue;
        }

        if (line.trimmed().startsWith(QStringLiteral(">>>"))) {
            QStringList quote;
            const QString first = line.trimmed().mid(3);
            const int start = line.indexOf(QStringLiteral(">>>"));
            if (!line.mid(start + 3).trimmed().isEmpty())
                quote.append(line.mid(start + 3));
            Q_UNUSED(first);
            ++i;
            while (i < n) {
                quote.append(lines.at(i));
                ++i;
            }
            html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">"
                                  "<tr><td width=\"3\" bgcolor=\"#c8c8c8\"></td>"
                                  "<td style=\"padding-left:8px;\">")
                    + renderLines(quote, resolver) + QStringLiteral("</td></tr></table>");
            continue;
        }

        static const QRegularExpression quoteMarker(QStringLiteral(R"(^\s*>)"));
        if (quoteMarker.match(line).hasMatch()) {
            QStringList quoteLines;
            while (i < n && quoteMarker.match(lines.at(i)).hasMatch()) {
                const QString current = lines.at(i);
                const int marker = current.indexOf(QLatin1Char('>'));
                QString content = current.mid(marker + 1);
                if (content.startsWith(QLatin1Char(' ')))
                    content = content.mid(1);
                quoteLines.append(content);
                ++i;
            }

            static const QRegularExpression alert(
                QStringLiteral(R"(^\s*\[!(NOTE|TIP|IMPORTANT|WARNING|CAUTION)\]\s*(.*)$)"),
                QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch alertMatch = alert.match(quoteLines.value(0));
            if (alertMatch.hasMatch()) {
                quoteLines.removeFirst();
                if (!alertMatch.captured(2).trimmed().isEmpty())
                    quoteLines.prepend(alertMatch.captured(2));
                html += renderCallout(alertMatch.captured(1), quoteLines, resolver);
            } else {
                html += QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"0\">"
                                      "<tr><td width=\"3\" bgcolor=\"#c8c8c8\"></td>"
                                      "<td style=\"padding-left:8px;\">")
                        + renderLines(quoteLines, resolver) + QStringLiteral("</td></tr></table>");
            }
            continue;
        }

        static const QRegularExpression tableRow(QStringLiteral(R"(^\s*\|(.+)\|\s*$)"));
        const QRegularExpressionMatch tableMatch = tableRow.match(line);
        const QRegularExpressionMatch nextLineMatch =
            i + 1 < n ? tableRow.match(lines.at(i + 1)) : QRegularExpressionMatch();
        if (tableMatch.hasMatch() && nextLineMatch.hasMatch()
            && lines.at(i + 1).contains(QLatin1Char('-'))) {
            const auto splitCells = [](const QString &row) {
                QStringList cells = row.trimmed().split(QLatin1Char('|'));
                if (!cells.isEmpty() && cells.first().trimmed().isEmpty())
                    cells.removeFirst();
                if (!cells.isEmpty() && cells.last().trimmed().isEmpty())
                    cells.removeLast();
                for (QString &cell : cells)
                    cell = cell.trimmed();
                return cells;
            };

            const QStringList headers = splitCells(tableMatch.captured(1));
            ++i; // header row
            ++i; // separator row

            QString table = QStringLiteral("<table width=\"100%\" cellspacing=\"0\" cellpadding=\"4\" "
                                          "border=\"1\"><tr>");
            for (const QString &header : headers)
                table += QStringLiteral("<td bgcolor=\"#f0f0f0\"><b>%1</b></td>")
                             .arg(renderInline(header, resolver));
            table += QStringLiteral("</tr>");

            while (i < n) {
                const QRegularExpressionMatch rowMatch = tableRow.match(lines.at(i));
                if (!rowMatch.hasMatch())
                    break;
                table += QStringLiteral("<tr>");
                const QStringList cells = splitCells(rowMatch.captured(1));
                for (int cellIndex = 0; cellIndex < headers.size(); ++cellIndex) {
                    const QString cell = cellIndex < cells.size() ? cells.at(cellIndex) : QString();
                    table += QStringLiteral("<td>") + renderInline(cell, resolver)
                             + QStringLiteral("</td>");
                }
                table += QStringLiteral("</tr>");
                ++i;
            }
            table += QStringLiteral("</table>");
            html += table;
            continue;
        }

        static const QRegularExpression listItem(
            QStringLiteral(R"(^(\s*)([-*+]|\d+[.)])\s+(.*)$)"));
        const QRegularExpressionMatch listMatch = listItem.match(line);
        if (listMatch.hasMatch()) {
            QString listHtml;
            const bool ordered =
                listMatch.captured(2).at(0).isDigit();
            listHtml += ordered ? QStringLiteral("<ol>") : QStringLiteral("<ul>");

            while (i < n) {
                const QRegularExpressionMatch itemMatch = listItem.match(lines.at(i));
                if (!itemMatch.hasMatch())
                    break;

                const QString content = itemMatch.captured(3);
                const int itemIndent = itemMatch.captured(1).size();
                ++i;

                QStringList nested;
                while (i < n) {
                    if (lines.at(i).trimmed().isEmpty()) {
                        if (i + 1 < n) {
                            const QRegularExpressionMatch next = listItem.match(lines.at(i + 1));
                            if (next.hasMatch() && next.captured(1).size() > itemIndent) {
                                nested.append(QString());
                                ++i;
                                continue;
                            }
                        }
                        break;
                    }
                    int indent = 0;
                    if (!isBlockStart(lines.at(i), &indent) || indent <= itemIndent)
                        break;
                    QString nestedLine = lines.at(i);
                    nestedLine.remove(0, std::min(static_cast<int>(nestedLine.size()), itemIndent + 2));
                    nested.append(nestedLine);
                    ++i;
                }

                listHtml += renderListItem(content, nested, resolver);
                if (i >= n)
                    break;
                const QRegularExpressionMatch nextItem = listItem.match(lines.at(i));
                if (!nextItem.hasMatch()
                    || nextItem.captured(2).at(0).isDigit() != ordered)
                    break;
            }

            listHtml += ordered ? QStringLiteral("</ol>") : QStringLiteral("</ul>");
            html += listHtml;
            continue;
        }

        QStringList paragraph;
        static const QRegularExpression multilineQuoteMarker(QStringLiteral(R"(^\s*>>>)"));
        while (i < n && !lines.at(i).trimmed().isEmpty() && !isBlockStart(lines.at(i))
               && !multilineQuoteMarker.match(lines.at(i)).hasMatch()) {
            paragraph.append(lines.at(i));
            ++i;
        }
        QString paragraphHtml;
        for (int j = 0; j < paragraph.size(); ++j) {
            if (j)
                paragraphHtml += QStringLiteral("<br/>");
            paragraphHtml += renderInline(paragraph.at(j), resolver);
        }
        html += QStringLiteral("<p>") + paragraphHtml + QStringLiteral("</p>");
    }

    return html;
}

} // namespace

QString Markdown::toHtml(const QString &text, const MentionResolver &resolver)
{
    if (text.isEmpty())
        return {};
    return renderLines(text.split(QLatin1Char('\n')), resolver);
}

QString Markdown::shortcodeToUnicode(const QString &name)
{
    return emojiTable().value(name.trimmed().toLower());
}

QString Markdown::toPlainText(const QString &text)
{
    static const QRegularExpression codeBlockRe(QStringLiteral(R"(```[^\n]*\n([\s\S]*?)```)"));
    static const QRegularExpression headingRe(QStringLiteral(R"(^#{1,4}\s+)"),
                                              QRegularExpression::MultilineOption);
    static const QRegularExpression quoteRe(QStringLiteral(R"(^\s*>\s?)"),
                                            QRegularExpression::MultilineOption);
    static const QRegularExpression subtextRe(QStringLiteral(R"(^\s*-#\s+)"),
                                              QRegularExpression::MultilineOption);
    static const QRegularExpression inlineCodeRe(QStringLiteral(R"(\*\*|__|~~|\*|_)"));
    static const QRegularExpression spoilerRe(QStringLiteral(R"(\|\|)"));
    static const QRegularExpression maskedLinkRe(QStringLiteral(R"(\[([^\]]+)\]\([^)]+\))"));
    static const QRegularExpression timestampRe(QStringLiteral(R"(<t:(\d+)(?::[a-zA-Z])?>)"));
    static const QRegularExpression mentionRe(QStringLiteral(R"(<[@#][!&]?[^>]*>)"));
    static const QRegularExpression tableSeparatorRe(
        QStringLiteral(R"(^\s*\|?[\s:|-]+\|[\s:|-]*$)"), QRegularExpression::MultilineOption);
    static const QRegularExpression tableStartRe(QStringLiteral(R"(^\s*\|)"),
                                                 QRegularExpression::MultilineOption);
    QString plain = text;
    plain.replace(codeBlockRe, QStringLiteral("\\1"));
    plain.remove(headingRe);
    plain.remove(quoteRe);
    plain.remove(subtextRe);
    plain.remove(inlineCodeRe);
    plain.remove(spoilerRe);
    plain.remove(QLatin1Char('`'));
    plain.remove(maskedLinkRe);
    plain.replace(timestampRe, QStringLiteral("<timestamp>"));
    plain.remove(mentionRe);
    plain.remove(tableSeparatorRe);
    plain.remove(tableStartRe);
    plain.remove(QLatin1Char('|'));
    plain = plain.trimmed();
    if (plain.size() > 160)
        plain = plain.left(157) + QStringLiteral("...");
    return plain;
}
