#include "util/commandtemplate.h"

#include <QObject>
#include <QRegularExpression>

namespace mixxx {

QStringList expandCommandTemplate(const QString& commandTemplate,
        const QMap<QString, QString>& placeholders,
        QString* pErrorMessage) {
    const auto fail = [pErrorMessage](const QString& message) {
        if (pErrorMessage) {
            *pErrorMessage = message;
        }
        return QStringList();
    };

    QStringList tokens;
    QString token;
    bool hasToken = false;
    QChar quote;
    for (const QChar character : commandTemplate) {
        if (!quote.isNull()) {
            if (character == quote) {
                quote = QChar();
            } else {
                token.append(character);
            }
            continue;
        }
        if (character == '"' || character == '\'') {
            quote = character;
            hasToken = true;
            continue;
        }
        if (character.isSpace()) {
            if (hasToken) {
                tokens.append(token);
                token.clear();
                hasToken = false;
            }
            continue;
        }
        token.append(character);
        hasToken = true;
    }
    if (!quote.isNull()) {
        return fail(QObject::tr("The command has an unclosed quote."));
    }
    if (hasToken) {
        tokens.append(token);
    }
    if (tokens.isEmpty()) {
        return fail(QObject::tr("The command is empty."));
    }

    // A name in any case matches, thus a name in the wrong case gives an
    // error and does not reach the program as plain text.
    static const QRegularExpression placeholderRegex(QStringLiteral(
            "\\$\\{([A-Za-z_][A-Za-z0-9_]*)\\}|\\$([A-Za-z_][A-Za-z0-9_]*)"));
    QStringList expandedTokens;
    expandedTokens.reserve(tokens.size());
    for (const QString& rawToken : std::as_const(tokens)) {
        QString expanded;
        int copiedUpTo = 0;
        auto matches = placeholderRegex.globalMatch(rawToken);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            const QString name = match.captured(1).isEmpty()
                    ? match.captured(2)
                    : match.captured(1);
            if (!placeholders.contains(name)) {
                return fail(QObject::tr("The command uses the unknown placeholder $%1.")
                                    .arg(name));
            }
            if (placeholders.value(name).isEmpty()) {
                return fail(QObject::tr(
                        "The placeholder $%1 of the command has "
                        "no value.")
                                    .arg(name));
            }
            expanded.append(rawToken.mid(copiedUpTo, match.capturedStart() - copiedUpTo));
            expanded.append(placeholders.value(name));
            copiedUpTo = match.capturedEnd();
        }
        expanded.append(rawToken.mid(copiedUpTo));
        expandedTokens.append(expanded);
    }
    if (expandedTokens.first().isEmpty()) {
        return fail(QObject::tr("The command has no program name."));
    }
    if (pErrorMessage) {
        pErrorMessage->clear();
    }
    return expandedTokens;
}

} // namespace mixxx
