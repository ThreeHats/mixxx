#include "osc/oscservice.h"

#include <QTimer>
#include <QUdpSocket>

#include "control/controlproxy.h"
#include "moc_oscservice.cpp"
#include "osc/oscaddress.h"
#include "osc/oscbeatfeed.h"
#include "track/keyutils.h"
#include "util/logger.h"

namespace {

const mixxx::Logger kLogger("OscService");

/// The pump empties the beat queue and sends what the rate limit held back.
/// The instant of a beat is in its message, thus this period adds no error.
constexpr int kPumpIntervalMs = 5;

/// A reader that asks for the state keeps it for this time. It must ask again
/// before the time is over.
constexpr int kSubscriptionMs = 60000;

constexpr int kMaxBeatsPerPump = 16;

/// The shortest time between two warnings about a message that came in. It
/// keeps a flood of bad messages out of the log.
constexpr int kWarningIntervalMs = 5000;

const QString kBeatName = QStringLiteral("beat");
const QString kKeyTextName = QStringLiteral("key_text");
const QString kSnapshotPath = QStringLiteral("/mixxx/snapshot");
const QString kSnapshotEndPath = QStringLiteral("/mixxx/snapshot_end");
const QString kSubscribePath = QStringLiteral("/mixxx/subscribe");
const QString kUnsubscribePath = QStringLiteral("/mixxx/unsubscribe");

const QString kKeyControl = QStringLiteral("key");

struct MetadataField {
    const char* name;
    QString mixxx::osc::TrackInfo::*member;
};

const MetadataField kMetadataFields[] = {
        {"artist", &mixxx::osc::TrackInfo::artist},
        {"title", &mixxx::osc::TrackInfo::title},
        {"album", &mixxx::osc::TrackInfo::album},
        {"year", &mixxx::osc::TrackInfo::year},
        {"genre", &mixxx::osc::TrackInfo::genre},
        {"location", &mixxx::osc::TrackInfo::location},
};

} // namespace

namespace mixxx {
namespace osc {

Service::Service(QObject* pParent)
        : QObject(pParent),
          m_lastWarningMs(0),
          m_allowRequestFromAnyHost(false),
          m_running(false) {
    m_clock.start();
}

Service::~Service() {
    stop();
}

quint16 Service::boundPort() const {
    return m_pSocket ? m_pSocket->localPort() : 0;
}

qint64 Service::nowMs() const {
    return m_clock.elapsed();
}

void Service::stop() {
    m_running = false;
    BeatFeed::setEnabled(false);
    m_pPumpTimer.reset();
    m_pSnapshotTimer.reset();
    m_publishEntries.clear();
    qDeleteAll(m_writeProxies);
    m_writeProxies.clear();
    m_pSocket.reset();
    m_subscribers.clear();
    m_targets.clear();
    m_allTargets.clear();
}

void Service::applyConfig(const Config& config, int deckCount) {
    stop();
    if (!config.enabled) {
        return;
    }

    QHostAddress bindAddress;
    if (!bindAddress.setAddress(config.listenHost)) {
        kLogger.warning() << "the bind address does not parse:" << config.listenHost;
        bindAddress = QHostAddress(QHostAddress::LocalHost);
    }
    m_pSocket = std::make_unique<QUdpSocket>();
    if (!m_pSocket->bind(bindAddress, config.listenPort)) {
        kLogger.warning() << "cannot listen on" << config.listenHost << config.listenPort
                          << m_pSocket->errorString();
        m_pSocket.reset();
        return;
    }
    connect(m_pSocket.get(),
            &QUdpSocket::readyRead,
            this,
            &Service::slotDatagramReady);

    m_targets = config.targets;
    m_allowRequestFromAnyHost = config.allowRequestFromAnyHost;
    rebuildTargets();
    m_filter.setAllowAll(config.allowAllControls);
    m_filter.setAllowedKeys(config.allowedKeys);
    buildPublishEntries(config, deckCount);

    BeatFeed::clear();
    BeatFeed::setEnabled(true);
    m_running = true;

    m_pPumpTimer = std::make_unique<QTimer>();
    m_pPumpTimer->setTimerType(Qt::PreciseTimer);
    connect(m_pPumpTimer.get(), &QTimer::timeout, this, &Service::slotPump);
    m_pPumpTimer->start(kPumpIntervalMs);

    if (config.snapshotIntervalSeconds > 0) {
        m_pSnapshotTimer = std::make_unique<QTimer>();
        connect(m_pSnapshotTimer.get(), &QTimer::timeout, this, &Service::slotSnapshot);
        m_pSnapshotTimer->start(config.snapshotIntervalSeconds * 1000);
    }

    kLogger.info() << "listens on" << config.listenHost << m_pSocket->localPort()
                   << "and watches" << static_cast<int>(m_publishEntries.size())
                   << "controls";
    sendSnapshotTo(m_targets);
}

void Service::buildPublishEntries(const Config& config, int deckCount) {
    const QList<PublishRule> rules = expandPublishRules(config.publishRules, deckCount);
    m_publishEntries.reserve(rules.size());
    for (const PublishRule& rule : rules) {
        const ConfigKey key(rule.group, rule.key);
        const QString path = pathForControl(key);
        if (path.isEmpty()) {
            kLogger.warning() << "no address for" << rule.group << rule.key;
            continue;
        }
        auto pEntry = std::make_unique<PublishEntry>();
        pEntry->key = key;
        pEntry->path = path;
        pEntry->limiter = RateLimiter(rule.minIntervalMs);
        pEntry->pProxy = std::make_unique<ControlProxy>(
                key, nullptr, ControlFlag::NoWarnIfMissing);
        if (!pEntry->pProxy->valid()) {
            kLogger.debug() << "no such control:" << rule.group << rule.key;
            continue;
        }
        PublishEntry* pRaw = pEntry.get();
        pEntry->pProxy->connectValueChanged(this, [this, pRaw](double value) {
            onControlChanged(pRaw, value);
        });
        m_publishEntries.push_back(std::move(pEntry));
    }
}

void Service::onControlChanged(PublishEntry* pEntry, double value) {
    if (!m_running) {
        return;
    }
    if (pEntry->limiter.offer(value, nowMs())) {
        sendControl(*pEntry, value);
    }
}

void Service::sendControl(const PublishEntry& entry, double value) {
    sendControlTo(m_allTargets, entry, value);
}

int Service::sendControlTo(const QList<Target>& targets,
        const PublishEntry& entry,
        double value) {
    if (targets.isEmpty()) {
        return 0;
    }
    Message message;
    message.addFloat(static_cast<float>(value));
    send(targets, entry.path, message);

    if (entry.key.item != kKeyControl) {
        return 1;
    }
    // The number of a key says nothing to a reader that shows text, thus the
    // module also sends the name in the notation of the user.
    Message text;
    text.addString(KeyUtils::keyToString(KeyUtils::keyFromNumericValue(value)));
    send(targets, pathForGroupMessage(entry.key.group, kKeyTextName), text);
    return 2;
}

void Service::setTrackInfo(const QString& group, const TrackInfo& info) {
    m_trackInfo.insert(group, info);
    if (m_running) {
        sendTrackInfoTo(m_allTargets, group, info);
    }
}

int Service::sendTrackInfoTo(const QList<Target>& targets,
        const QString& group,
        const TrackInfo& info) {
    if (targets.isEmpty()) {
        return 0;
    }
    int count = 0;
    for (const MetadataField& field : kMetadataFields) {
        Message message;
        message.addString(info.*(field.member));
        send(targets, pathForGroupMessage(group, QString::fromLatin1(field.name)), message);
        count++;
    }
    return count;
}

void Service::sendSnapshotTo(const QList<Target>& targets) {
    if (targets.isEmpty()) {
        return;
    }
    int count = 0;
    for (const auto& pEntry : m_publishEntries) {
        count += sendControlTo(targets, *pEntry, pEntry->pProxy->get());
    }
    for (auto it = m_trackInfo.constBegin(); it != m_trackInfo.constEnd(); ++it) {
        count += sendTrackInfoTo(targets, it.key(), it.value());
    }
    Message message;
    message.addInt32(count);
    send(targets, kSnapshotEndPath, message);
}

void Service::send(const QList<Target>& targets, const QString& path, const Message& message) {
    if (!m_pSocket || path.isEmpty()) {
        return;
    }
    const QByteArray datagram = message.serialise(path);
    if (datagram.isEmpty()) {
        return;
    }
    for (const Target& target : targets) {
        m_pSocket->writeDatagram(datagram, target.host, target.port);
    }
}

void Service::rebuildTargets() {
    m_allTargets = m_targets;
    for (const Subscriber& subscriber : m_subscribers) {
        if (!m_allTargets.contains(subscriber.target)) {
            m_allTargets.append(subscriber.target);
        }
    }
}

void Service::addSubscriber(const Target& target) {
    const qint64 now = nowMs();
    const qint64 expires = now + kSubscriptionMs;
    for (Subscriber& subscriber : m_subscribers) {
        if (subscriber.target == target) {
            subscriber.expiresMs = expires;
            if (now - subscriber.lastSnapshotMs >= kSnapshotRequestIntervalMs) {
                subscriber.lastSnapshotMs = now;
                sendSnapshotTo({target});
            }
            return;
        }
    }
    expireSubscribers();
    if (m_subscribers.size() >= kMaxSubscribers) {
        warnOnce(QStringLiteral("no room for another reader: ") + target.toString());
        return;
    }
    m_subscribers.append(Subscriber{target, expires, now});
    rebuildTargets();
    kLogger.debug() << "a reader asked for the state:" << target.toString();
    sendSnapshotTo({target});
}

void Service::removeSubscriber(const Target& target) {
    for (int i = 0; i < m_subscribers.size(); i++) {
        if (m_subscribers.at(i).target == target) {
            m_subscribers.removeAt(i);
            rebuildTargets();
            return;
        }
    }
}

void Service::expireSubscribers() {
    const qint64 now = nowMs();
    bool changed = false;
    for (int i = m_subscribers.size() - 1; i >= 0; i--) {
        if (m_subscribers.at(i).expiresMs <= now) {
            m_subscribers.removeAt(i);
            changed = true;
        }
    }
    if (changed) {
        rebuildTargets();
    }
}

void Service::warnOnce(const QString& text) {
    const qint64 now = nowMs();
    if (now - m_lastWarningMs < kWarningIntervalMs) {
        return;
    }
    m_lastWarningMs = now;
    kLogger.warning() << text;
}

void Service::slotDatagramReady() {
    while (m_pSocket && m_pSocket->hasPendingDatagrams()) {
        QByteArray datagram(static_cast<int>(m_pSocket->pendingDatagramSize()), '\0');
        QHostAddress host;
        quint16 port = 0;
        const qint64 read = m_pSocket->readDatagram(
                datagram.data(), datagram.size(), &host, &port);
        if (read < 0) {
            return;
        }
        datagram.resize(static_cast<int>(read));
        IncomingMessage message;
        if (!parseMessage(datagram, &message)) {
            continue;
        }
        Target source;
        source.host = host;
        source.port = port;
        handleMessage(message, source);
    }
}

void Service::handleMessage(const IncomingMessage& message, const Target& source) {
    const bool isRequest = message.path == kSnapshotPath ||
            message.path == kSubscribePath || message.path == kUnsubscribePath;
    if (isRequest && !isTrustedSource(source, m_targets, m_allowRequestFromAnyHost)) {
        // A state request answers with about 70 datagrams, thus a stranger
        // with a false source address could flood another computer.
        warnOnce(QStringLiteral("a state request from a host that is not "
                                "trusted: ") +
                source.toString());
        return;
    }

    // A reader that listens on another port than the one it sends from names
    // that port in the message.
    Target target = source;
    double port = 0;
    if (message.firstNumber(&port) && port > 0 && port <= 65535) {
        target.port = static_cast<quint16>(port);
    }

    if (message.path == kSnapshotPath) {
        sendSnapshotTo({target});
        return;
    }
    if (message.path == kSubscribePath) {
        addSubscriber(target);
        return;
    }
    if (message.path == kUnsubscribePath) {
        removeSubscriber(target);
        return;
    }
    handleControlWrite(message);
}

void Service::handleControlWrite(const IncomingMessage& message) {
    const ConfigKey key = controlForPath(message.path);
    if (!key.isValid()) {
        return;
    }
    if (!m_filter.isAllowed(key)) {
        kLogger.warning() << "the allow list holds back" << message.path;
        return;
    }
    double value = 0;
    if (!message.firstNumber(&value)) {
        warnOnce(QStringLiteral("no number in ") + message.path +
                QStringLiteral(" of type ") + message.types);
        return;
    }
    ControlProxy* pProxy = m_writeProxies.value(key, nullptr);
    if (pProxy == nullptr) {
        auto pOwned = std::make_unique<ControlProxy>(
                key, nullptr, ControlFlag::NoWarnIfMissing);
        if (!pOwned->valid()) {
            warnOnce(QStringLiteral("no such control: ") + message.path);
            return;
        }
        pProxy = pOwned.release();
        m_writeProxies.insert(key, pProxy);
    }
    pProxy->set(value);
}

void Service::slotPump() {
    if (!m_running) {
        return;
    }
    const qint64 now = nowMs();

    expireSubscribers();

    BeatEvent events[kMaxBeatsPerPump];
    const int count = BeatFeed::pop(events, kMaxBeatsPerPump);
    const QList<Target>& targets = m_allTargets;
    for (int i = 0; i < count; i++) {
        const BeatEvent& event = events[i];
        Message message;
        message.addInt64(event.stampNs);
        message.addInt32(event.trackBeat);
        message.addFloat(event.bpm);
        message.addInt32(event.seq);
        send(targets, pathForGroupMessage(BeatFeed::readGroup(event), kBeatName), message);
    }

    for (const auto& pEntry : m_publishEntries) {
        double value = 0;
        if (pEntry->limiter.takeDue(now, &value)) {
            sendControlTo(targets, *pEntry, value);
        }
    }
}

void Service::slotSnapshot() {
    if (m_running) {
        sendSnapshotTo(m_allTargets);
    }
}

} // namespace osc
} // namespace mixxx
