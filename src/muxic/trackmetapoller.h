#pragma once

#include <QObject>
#include <QSet>
#include <QSqlDatabase>
#include <optional>

#include "track/trackid.h"
#include "util/class.h"
#include "util/db/dbconnectionpool.h"
#include "util/db/dbconnectionpooler.h"

class QTimer;

namespace muxic {

/// Watches muxic_track_meta for writes that come from outside of Mixxx.
///
/// The object runs in a thread of its own with a connection of its own from
/// the pool of Mixxx, thus a long transaction of the muxic hub cannot stop the
/// user interface. It gives up at once when the database is busy and tries
/// again at the next tick.
class TrackMetaPoller : public QObject {
    Q_OBJECT
  public:
    /// How many track ids go into one report.
    static constexpr int kReportChunkSize = 500;
    /// How long the poll waits for a busy database, in milliseconds.
    static constexpr int kBusyTimeoutMillis = 20;

    TrackMetaPoller(mixxx::DbConnectionPoolPtr pDbConnectionPool, int pollMillis);
    ~TrackMetaPoller() override;

  public slots:
    /// Opens the connection and starts the timer. Runs in the poll thread. A
    /// poll interval of 0 ms makes no timer, which is what a test wants.
    void start();
    /// Stops the timer and closes the connection. Runs in the poll thread.
    void stop();
    /// Reads the rows that changed and reports them in chunks.
    void poll();

  signals:
    /// The values of these tracks are new in the database.
    void tracksChanged(const QSet<TrackId>& trackIds);

  private slots:
    void pollIfChanged();

  private:
    bool databaseChanged();
    void readWatermark();

    const mixxx::DbConnectionPoolPtr m_pDbConnectionPool;
    const int m_pollMillis;

    QTimer* m_pTimer;
    /// Keeps the connection of the poll thread in the pool. It is in the
    /// thread of the object, thus it never outlives the thread.
    std::optional<mixxx::DbConnectionPooler> m_pooler;
    QSqlDatabase m_database;

    /// The newest updated_at that a report covered, and the ids that the report
    /// covered at that stamp. Several rows can share one millisecond, thus the
    /// query asks for >= and this set removes the repeats.
    qint64 m_watermark;
    QSet<TrackId> m_reportedAtWatermark;

    /// The value of PRAGMA data_version of the last look. It changes when
    /// another connection commits.
    int m_dataVersion;

    DISALLOW_COPY_AND_ASSIGN(TrackMetaPoller);
};

} // namespace muxic
