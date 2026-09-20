#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QString>
#include <memory>
#include <vector>

#include "osc/oscconfig.h"
#include "osc/oscmessage.h"
#include "osc/oscpolicy.h"

class ControlProxy;
class QTimer;
class QUdpSocket;

namespace mixxx {
namespace osc {

/// What a deck says about the track it holds.
struct TrackInfo {
    QString artist;
    QString title;
    QString album;
    QString year;
    QString genre;
    QString location;
};

/// Sends the state of Mixxx as OSC and takes OSC commands back. It lives on a
/// thread of its own with a Qt event loop. Each method here runs on that
/// thread; `Controller` moves the calls of the main thread over.
class Service : public QObject {
    Q_OBJECT

  public:
    explicit Service(QObject* pParent = nullptr);
    ~Service() override;

    /// Start with these settings, or stop when they say so. A second call
    /// replaces the first one.
    void applyConfig(const Config& config, int deckCount);

    /// Close the socket and drop the controls that it watches.
    void stop();

    /// The track of one deck. An empty `TrackInfo` means that the deck is
    /// empty.
    void setTrackInfo(const QString& group, const TrackInfo& info);

    /// The port that the socket holds, or 0. A test reads it after it asked
    /// for port 0.
    quint16 boundPort() const;

  private slots:
    void slotDatagramReady();
    void slotPump();
    void slotSnapshot();

  private:
    struct PublishEntry {
        ConfigKey key;
        QString path;
        std::unique_ptr<ControlProxy> pProxy;
        RateLimiter limiter;
    };

    struct Subscriber {
        Target target;
        qint64 expiresMs = 0;
    };

    void buildPublishEntries(const Config& config, int deckCount);
    void onControlChanged(PublishEntry* pEntry, double value);
    void sendControl(const PublishEntry& entry, double value);
    void sendControlTo(const QList<Target>& targets,
            const PublishEntry& entry,
            double value);
    void sendTrackInfoTo(const QList<Target>& targets,
            const QString& group,
            const TrackInfo& info);
    void sendSnapshotTo(const QList<Target>& targets);
    void handleMessage(const IncomingMessage& message, const Target& source);
    void handleControlWrite(const IncomingMessage& message);
    void addSubscriber(const Target& target);
    void removeSubscriber(const Target& target);
    void expireSubscribers();
    QList<Target> allTargets() const;
    void send(const QList<Target>& targets, const QString& path, const Message& message);
    qint64 nowMs() const;

    std::unique_ptr<QUdpSocket> m_pSocket;
    std::unique_ptr<QTimer> m_pPumpTimer;
    std::unique_ptr<QTimer> m_pSnapshotTimer;
    std::vector<std::unique_ptr<PublishEntry>> m_publishEntries;
    QHash<QString, TrackInfo> m_trackInfo;
    QList<Target> m_targets;
    QList<Subscriber> m_subscribers;
    ControlFilter m_filter;
    std::vector<std::unique_ptr<ControlProxy>> m_writeProxyStore;
    QHash<ConfigKey, ControlProxy*> m_writeProxies;
    QElapsedTimer m_clock;
    bool m_running;
};

} // namespace osc
} // namespace mixxx
