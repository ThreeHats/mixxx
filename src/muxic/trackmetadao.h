#pragma once

#include <QList>
#include <QObject>
#include <QSet>
#include <QSqlDatabase>
#include <QStringList>
#include <optional>

#include "muxic/trackmeta.h"
#include "track/trackid.h"
#include "util/class.h"
#include "util/db/dbconnectionpool.h"

class QThread;
class RelocatedTrack;

namespace muxic {

class TrackMetaPoller;

/// Reads and writes the fork table muxic_track_meta. The table is outside the
/// schema of upstream, thus this class makes it when the database opens.
///
/// A write of Mixxx goes over the connection of the main thread, like every
/// other write of Mixxx. A write of the muxic hub comes from another process,
/// and a poll thread finds it, see TrackMetaPoller.
class TrackMetaDao : public QObject {
    Q_OBJECT
  public:
    explicit TrackMetaDao(QObject* parent = nullptr);
    ~TrackMetaDao() override;

    /// Makes the table if it is absent and keeps the connection.
    void initialize(const QSqlDatabase& database);
    void finish();

    /// Starts the poll thread and remembers how, so that a second
    /// initialize() starts it again. Without this call the object only reads
    /// and writes on demand, which is what a test wants.
    void startPolling(mixxx::DbConnectionPoolPtr pDbConnectionPool, int pollMillis);
    void stopPolling();
    bool isPolling() const {
        return m_pPollThread != nullptr;
    }

    TrackMeta read(TrackId trackId) const;

    bool setEnergy(TrackId trackId, std::optional<int> energy);
    bool setDanceability(TrackId trackId, std::optional<double> danceability);
    bool setTags(TrackId trackId, const QStringList& tags);

    /// Removes the rows of tracks that leave the library. Call it inside the
    /// purge transaction, in the idiom of CrateStorage.
    bool onPurgingTracks(const QList<TrackId>& trackIds);

    /// Moves the values of a track that a merge removed to the track that
    /// stays, when the track that stays has no values.
    void relocateTracks(const QList<RelocatedTrack>& relocatedTracks);

    /// Removes the rows of track ids that the library does not have. The hub
    /// can purge a track while Mixxx is closed, and SQLite does not enforce
    /// the REFERENCES clause. Returns the number of rows.
    int deleteOrphanedRows();

    /// The instance that holds the open database, or nullptr. The track
    /// properties dialog has no path to the track collection.
    static TrackMetaDao* instance();

  signals:
    /// The values of these tracks are new. The library cache reads them again.
    void tracksChanged(const QSet<TrackId>& trackIds);

  private:
    bool write(TrackId trackId, const QString& column, const QVariant& value);
    void warnAboutForeignTableShape();

    QSqlDatabase m_database;
    mixxx::DbConnectionPoolPtr m_pDbConnectionPool;
    int m_pollMillis;
    QThread* m_pPollThread;
    TrackMetaPoller* m_pPoller;

    DISALLOW_COPY_AND_ASSIGN(TrackMetaDao);
};

} // namespace muxic
