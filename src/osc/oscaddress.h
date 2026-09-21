#pragma once

#include <QString>

#include "preferences/configobject.h"

/// The OSC address space of Mixxx. One control has one address, built from its
/// group and its key: `[Channel1]` and `play` give `/mixxx/Channel1/play`.
namespace mixxx {
namespace osc {

/// True when the text is safe in one part of an OSC address.
bool isValidAddressPart(const QString& part);

/// The address of a control. The result is empty when the group or the key
/// holds a character that OSC keeps for its pattern syntax.
QString pathForControl(const ConfigKey& key);

/// The control of an address. The result is an invalid ConfigKey when the
/// address is not a control address.
ConfigKey controlForPath(const QString& path);

/// The address of a message that is not a control, for example `beat`.
QString pathForGroupMessage(const QString& group, const QString& name);

} // namespace osc
} // namespace mixxx
