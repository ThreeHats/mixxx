#include "osc/oscaddress.h"

#include <QStringList>

namespace {

const QString kRoot = QStringLiteral("/mixxx");

/// OSC keeps these characters for its pattern syntax, thus an address part
/// must not hold one. See the OSC 1.0 specification, "OSC Address Patterns".
const QString kReserved = QStringLiteral(" #*,/?[]{}");

/// `[Channel1]` gives `Channel1`. A group without the brackets gives an empty
/// string, because every Mixxx group has them.
QString groupToPart(const QString& group) {
    if (group.size() < 3 || !group.startsWith('[') || !group.endsWith(']')) {
        return QString();
    }
    return group.mid(1, group.size() - 2);
}

} // namespace

namespace mixxx {
namespace osc {

QString addressRoot() {
    return kRoot;
}

bool isValidAddressPart(const QString& part) {
    if (part.isEmpty()) {
        return false;
    }
    for (const QChar c : part) {
        if (c < ' ' || c > '~' || kReserved.contains(c)) {
            return false;
        }
    }
    return true;
}

QString pathForControl(const ConfigKey& key) {
    return pathForGroupMessage(key.group, key.item);
}

QString pathForGroupMessage(const QString& group, const QString& name) {
    const QString part = groupToPart(group);
    if (!isValidAddressPart(part) || !isValidAddressPart(name)) {
        return QString();
    }
    return kRoot + QChar('/') + part + QChar('/') + name;
}

ConfigKey controlForPath(const QString& path) {
    if (!path.startsWith(kRoot + QChar('/'))) {
        return ConfigKey();
    }
    const QStringList parts = path.mid(kRoot.size() + 1).split(QChar('/'));
    if (parts.size() != 2) {
        return ConfigKey();
    }
    if (!isValidAddressPart(parts.at(0)) || !isValidAddressPart(parts.at(1))) {
        return ConfigKey();
    }
    return ConfigKey(QChar('[') + parts.at(0) + QChar(']'), parts.at(1));
}

} // namespace osc
} // namespace mixxx
