#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

namespace mixxx {

/// Split a command template into a program and its arguments, and put the
/// value of each placeholder in place. Returns an empty list on an error.
QStringList expandCommandTemplate(const QString& commandTemplate,
        const QMap<QString, QString>& placeholders,
        QString* pErrorMessage);

} // namespace mixxx
