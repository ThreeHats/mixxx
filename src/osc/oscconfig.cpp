#include "osc/oscconfig.h"

#include <QDir>
#include <QFile>
#include <QTextStream>

#include "mixer/playermanager.h"
#include "util/logger.h"

namespace {

const mixxx::Logger kLogger("OscConfig");

const QString kConfigGroup = QStringLiteral("[Osc]");
const QString kPublishFileName = QStringLiteral("osc-publish.conf");
const QString kDeckToken = QStringLiteral("[ChannelN]");

const ConfigKey kEnabledKey(kConfigGroup, QStringLiteral("Enabled"));
const ConfigKey kListenHostKey(kConfigGroup, QStringLiteral("ListenHost"));
const ConfigKey kListenPortKey(kConfigGroup, QStringLiteral("ListenPort"));
const ConfigKey kTargetsKey(kConfigGroup, QStringLiteral("Targets"));
const ConfigKey kSnapshotKey(kConfigGroup, QStringLiteral("SnapshotIntervalSeconds"));
const ConfigKey kAllowAllKey(kConfigGroup, QStringLiteral("AllowAllControls"));
const ConfigKey kAllowedKeysKey(kConfigGroup, QStringLiteral("AllowedKeys"));
const ConfigKey kAllowAnyHostKey(kConfigGroup, QStringLiteral("AllowRequestFromAnyHost"));
const ConfigKey kSamplerBeatsKey(kConfigGroup, QStringLiteral("SamplerBeats"));

} // namespace

namespace mixxx {
namespace osc {

QString Target::toString() const {
    if (host.protocol() == QAbstractSocket::IPv6Protocol) {
        return QStringLiteral("[%1]:%2").arg(host.toString()).arg(port);
    }
    return QStringLiteral("%1:%2").arg(host.toString()).arg(port);
}

QString deckGroupToken() {
    return kDeckToken;
}

bool groupSendsBeats(const QString& group, bool includeSamplers) {
    if (group.startsWith(QStringLiteral("[Channel"))) {
        return true;
    }
    return includeSamplers && group.startsWith(QStringLiteral("[Sampler"));
}

bool isTrustedSource(const Target& source, const QList<Target>& targets, bool allowAnyHost) {
    if (!source.isValid()) {
        return false;
    }
    if (allowAnyHost) {
        return true;
    }
    if (source.host.isLoopback()) {
        return true;
    }
    for (const Target& target : targets) {
        if (target.host == source.host) {
            return true;
        }
    }
    return false;
}

QList<Target> parseTargets(const QString& text) {
    QList<Target> targets;
    const QStringList parts = text.split(QChar(','), Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString trimmed = part.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        const int colon = trimmed.lastIndexOf(QChar(':'));
        if (colon <= 0) {
            kLogger.warning() << "target without a port:" << trimmed;
            continue;
        }
        QString hostText = trimmed.left(colon).trimmed();
        if (hostText.startsWith(QChar('[')) && hostText.endsWith(QChar(']'))) {
            hostText = hostText.mid(1, hostText.size() - 2);
        }
        bool ok = false;
        const uint port = trimmed.mid(colon + 1).trimmed().toUInt(&ok);
        Target target;
        if (!ok || port == 0 || port > 65535 || !target.host.setAddress(hostText)) {
            kLogger.warning() << "target that does not parse:" << trimmed;
            continue;
        }
        target.port = static_cast<quint16>(port);
        if (!targets.contains(target)) {
            targets.append(target);
        }
    }
    return targets;
}

QString targetsToString(const QList<Target>& targets) {
    QStringList parts;
    parts.reserve(targets.size());
    for (const Target& target : targets) {
        parts.append(target.toString());
    }
    return parts.join(QStringLiteral(", "));
}

QList<PublishRule> parsePublishRules(const QString& text, QStringList* pErrors) {
    QList<PublishRule> rules;
    const QStringList lines = text.split(QChar('\n'));
    int lineNumber = 0;
    for (const QString& rawLine : lines) {
        lineNumber++;
        QString line = rawLine;
        const int comment = line.indexOf(QChar('#'));
        if (comment >= 0) {
            line = line.left(comment);
        }
        line = line.simplified();
        if (line.isEmpty()) {
            continue;
        }
        const QStringList fields = line.split(QChar(' '), Qt::SkipEmptyParts);
        if (fields.size() < 2 || fields.size() > 3) {
            if (pErrors) {
                pErrors->append(QStringLiteral("line %1: expected <group> <key> [ms]")
                                        .arg(lineNumber));
            }
            continue;
        }
        PublishRule rule;
        rule.group = fields.at(0);
        rule.key = fields.at(1);
        if (!rule.group.startsWith(QChar('[')) || !rule.group.endsWith(QChar(']'))) {
            if (pErrors) {
                pErrors->append(QStringLiteral("line %1: a group needs brackets")
                                        .arg(lineNumber));
            }
            continue;
        }
        if (fields.size() == 3) {
            bool ok = false;
            rule.minIntervalMs = fields.at(2).toInt(&ok);
            if (!ok || rule.minIntervalMs < 0) {
                if (pErrors) {
                    pErrors->append(QStringLiteral("line %1: the interval is not a number")
                                            .arg(lineNumber));
                }
                continue;
            }
        }
        rules.append(rule);
    }
    return rules;
}

QList<PublishRule> expandPublishRules(const QList<PublishRule>& rules, int deckCount) {
    QList<PublishRule> expanded;
    for (const PublishRule& rule : rules) {
        if (rule.group != kDeckToken) {
            expanded.append(rule);
            continue;
        }
        for (int deck = 0; deck < deckCount; deck++) {
            PublishRule deckRule = rule;
            deckRule.group = PlayerManager::groupForDeck(deck);
            expanded.append(deckRule);
        }
    }
    return expanded;
}

QString defaultPublishRulesText() {
    return QStringLiteral(
            "# Which Mixxx controls go out as OSC messages.\n"
            "#\n"
            "#   <group> <key> [shortest time between two messages, in ms]\n"
            "#\n"
            "# [ChannelN] stands for each deck. The address of a control is\n"
            "# /mixxx/<group without the brackets>/<key>, for example\n"
            "# /mixxx/Channel1/play. See tools/muxic/docs/osc.md.\n"
            "\n"
            "[ChannelN] play\n"
            "[ChannelN] track_loaded\n"
            "[ChannelN] bpm 50\n"
            "[ChannelN] file_bpm\n"
            "[ChannelN] rate 50\n"
            "[ChannelN] beat_distance 100\n"
            "[ChannelN] beat_in_bar\n"
            "[ChannelN] volume 30\n"
            "[ChannelN] mute\n"
            "[ChannelN] pregain 30\n"
            "[ChannelN] orientation\n"
            "[ChannelN] key\n"
            "[ChannelN] duration\n"
            "[ChannelN] playposition 100\n"
            "[ChannelN] sync_leader\n"
            "[ChannelN] sync_enabled\n"
            "\n"
            "[Master] crossfader 30\n"
            "[Master] headMix 30\n"
            "[Master] headGain 30\n"
            "[Master] duckStrength 30\n");
}

QStringList defaultAllowedKeys() {
    return QStringList{
            QStringLiteral("play"),
            QStringLiteral("play_stutter"),
            QStringLiteral("start_play"),
            QStringLiteral("start_stop"),
            QStringLiteral("stop"),
            QStringLiteral("cue_default"),
            QStringLiteral("cue_gotoandplay"),
            QStringLiteral("cue_gotoandstop"),
            QStringLiteral("cue_set"),
            QStringLiteral("beatjump"),
            QStringLiteral("beatjump_size"),
            QStringLiteral("beatjump_forward"),
            QStringLiteral("beatjump_backward"),
            QStringLiteral("beatjump_*"),
            QStringLiteral("beatloop_*"),
            QStringLiteral("beatlooproll_*"),
            QStringLiteral("loop_in"),
            QStringLiteral("loop_out"),
            QStringLiteral("loop_double"),
            QStringLiteral("loop_halve"),
            QStringLiteral("reloop_toggle"),
            QStringLiteral("reloop_andstop"),
            QStringLiteral("hotcue_*"),
            QStringLiteral("sync_enabled"),
            QStringLiteral("sync_leader"),
            QStringLiteral("sync_mode"),
            QStringLiteral("quantize"),
            QStringLiteral("keylock"),
            QStringLiteral("rate"),
            QStringLiteral("rate_temp_up"),
            QStringLiteral("rate_temp_down"),
            QStringLiteral("rate_perm_up"),
            QStringLiteral("rate_perm_down"),
    };
}

// static
QString Config::publishFilePath(const UserSettingsPointer& pConfig) {
    return QDir(pConfig->getSettingsPath()).filePath(kPublishFileName);
}

// static
bool Config::samplerBeatsEnabled(const UserSettingsPointer& pConfig) {
    return pConfig && pConfig->getValue<bool>(kSamplerBeatsKey, false);
}

// static
Config Config::load(const UserSettingsPointer& pConfig) {
    Config config;
    config.enabled = pConfig->getValue<bool>(kEnabledKey, false);
    config.listenHost = pConfig->getValue(kListenHostKey, QStringLiteral("127.0.0.1"));
    const int port = pConfig->getValue<int>(kListenPortKey, 9000);
    config.listenPort = (port > 0 && port <= 65535) ? static_cast<quint16>(port) : 9000;
    config.targets = parseTargets(pConfig->getValue(kTargetsKey, QString()));
    config.snapshotIntervalSeconds = std::max(0, pConfig->getValue<int>(kSnapshotKey, 10));
    config.allowAllControls = pConfig->getValue<bool>(kAllowAllKey, false);
    config.allowRequestFromAnyHost = pConfig->getValue<bool>(kAllowAnyHostKey, false);
    config.samplerBeats = samplerBeatsEnabled(pConfig);
    const QString allowedKeys = pConfig->getValue(kAllowedKeysKey, QString()).trimmed();
    config.allowedKeys = allowedKeys.isEmpty()
            ? defaultAllowedKeys()
            : allowedKeys.split(QChar(','), Qt::SkipEmptyParts);
    for (QString& key : config.allowedKeys) {
        key = key.trimmed();
    }

    if (pConfig->getSettingsPath().isEmpty()) {
        config.publishRules = parsePublishRules(defaultPublishRulesText(), nullptr);
        return config;
    }
    const QString path = publishFilePath(pConfig);
    QFile file(path);
    if (!file.exists()) {
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            stream << defaultPublishRulesText();
            file.close();
        } else {
            kLogger.warning() << "cannot write the publish list" << path;
        }
        config.publishRules = parsePublishRules(defaultPublishRulesText(), nullptr);
        return config;
    }
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        kLogger.warning() << "cannot read the publish list" << path;
        config.publishRules = parsePublishRules(defaultPublishRulesText(), nullptr);
        return config;
    }
    QStringList errors;
    config.publishRules = parsePublishRules(QString::fromUtf8(file.readAll()), &errors);
    for (const QString& error : errors) {
        kLogger.warning() << path << error;
    }
    return config;
}

void Config::save(const UserSettingsPointer& pConfig) const {
    pConfig->setValue(kEnabledKey, enabled);
    pConfig->setValue(kListenHostKey, listenHost);
    pConfig->setValue(kListenPortKey, static_cast<int>(listenPort));
    pConfig->setValue(kTargetsKey, targetsToString(targets));
    pConfig->setValue(kSnapshotKey, snapshotIntervalSeconds);
    pConfig->setValue(kAllowAllKey, allowAllControls);
    pConfig->setValue(kAllowAnyHostKey, allowRequestFromAnyHost);
}

} // namespace osc
} // namespace mixxx
