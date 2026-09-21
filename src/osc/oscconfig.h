#pragma once

#include <QHostAddress>
#include <QList>
#include <QString>
#include <QStringList>

#include "preferences/usersettings.h"

namespace mixxx {
namespace osc {

/// One place that gets the state of Mixxx.
struct Target {
    QHostAddress host;
    quint16 port = 0;

    bool isValid() const {
        return !host.isNull() && port != 0;
    }
    QString toString() const;
    bool operator==(const Target& other) const {
        return host == other.host && port == other.port;
    }
};

/// One control that the module sends when it changes.
struct PublishRule {
    QString group;
    QString key;
    /// The shortest time between two messages of this control, in
    /// milliseconds. 0 sends each change at once.
    int minIntervalMs = 0;

    bool operator==(const PublishRule& other) const {
        return group == other.group && key == other.key &&
                minIntervalMs == other.minIntervalMs;
    }
};

/// The settings of the OSC module. `[Osc]` in `mixxx.cfg` holds them, except
/// the publish list, which is a file of its own in the settings directory.
struct Config {
    bool enabled = false;
    QString listenHost = QStringLiteral("127.0.0.1");
    quint16 listenPort = 9000;
    QList<Target> targets;
    /// The time between two full snapshots, in seconds. 0 sends none.
    int snapshotIntervalSeconds = 10;
    /// True lets a message write any control. It opens the whole of Mixxx to
    /// the network, thus it is off by default.
    bool allowAllControls = false;
    /// The control keys that a message may write. Each entry is a glob, for
    /// example `hotcue_*_activate`.
    QStringList allowedKeys;
    QList<PublishRule> publishRules;
    /// True answers a state request from any host. False answers the loopback
    /// and the targets only. A snapshot is about 70 datagrams and the source
    /// address of a request can be false, thus an open server can flood
    /// another computer.
    bool allowRequestFromAnyHost = false;
    /// True lets a sampler report its beats too.
    bool samplerBeats = false;

    /// Read the settings and the publish list. Writes the publish list with
    /// its defaults when the file is not there yet.
    static Config load(const UserSettingsPointer& pConfig);
    /// Write the settings. It does not write the publish list.
    void save(const UserSettingsPointer& pConfig) const;

    /// The path of the publish list.
    static QString publishFilePath(const UserSettingsPointer& pConfig);

    /// The sampler setting alone. The engine reads it while it builds a deck,
    /// long before the service starts.
    static bool samplerBeatsEnabled(const UserSettingsPointer& pConfig);
};

/// True when a group reports its beats. A deck always does, a sampler only on
/// request, and a preview deck never, because it plays to the headphones.
bool groupSendsBeats(const QString& group, bool includeSamplers);

/// True when the module answers a state request from this host.
bool isTrustedSource(const Target& source, const QList<Target>& targets, bool allowAnyHost);

/// The readers that state requests may add. It bounds the traffic that one
/// stranger can start.
constexpr int kMaxSubscribers = 8;

/// The shortest time between two snapshots for one reader, in milliseconds.
constexpr int kSnapshotRequestIntervalMs = 1000;

/// `127.0.0.1:9001, [::1]:9002` gives two targets. A part that does not parse
/// is dropped.
QList<Target> parseTargets(const QString& text);
QString targetsToString(const QList<Target>& targets);

/// Read a publish list. `[ChannelN]` stands for each deck, thus one line
/// covers all of them. A line is `<group> <key> [interval in ms]`, `#` starts
/// a comment. Bad lines go to `pErrors` and are dropped.
QList<PublishRule> parsePublishRules(const QString& text, QStringList* pErrors);

/// The publish list that covers the state which the muxic rig needs.
QString defaultPublishRulesText();

/// The control keys that a message may write when the settings name none.
QStringList defaultAllowedKeys();

/// Replace each `[ChannelN]` rule by one rule per deck group.
QList<PublishRule> expandPublishRules(const QList<PublishRule>& rules, int deckCount);

/// The group that stands for every deck in a publish list.
QString deckGroupToken();

} // namespace osc
} // namespace mixxx
