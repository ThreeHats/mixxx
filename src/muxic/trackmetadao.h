#pragma once

#include <QObject>
#include <QSet>
#include <QSqlDatabase>
#include <QStringList>
#include <optional>

#include "muxic/trackmeta.h"
#include "track/trackid.h"
#include "util/class.h"

class QTimer;

namespace muxic {

/// Reads and writes the fork table muxic_track_meta. The table is outside the
/// schema of upstream, thus this class makes it when the database opens.
///
/// The muxic hub writes the same table from Python while Mixxx runs. A poll
/// reads the rows that changed and tells the library cache about them.
class TrackMetaDao : public QObject {
    Q_OBJECT
  public:
    explicit TrackMetaDao(QObject* parent = nullptr);
    ~TrackMetaDao() override;

    /// Makes the table if it is absent and keeps the connection. A poll
    /// interval of 0 ms stops the poll.
    void initialize(const QSqlDatabase& database, int pollMillis = 0);
    void finish();

    TrackMeta read(TrackId trackId) const;

    bool setEnergy(TrackId trackId, std::optional<int> energy);
    bool setDanceability(TrackId trackId, std::optional<double> danceability);
    bool setTags(TrackId trackId, const QStringList& tags);

    /// Reads the rows that changed since the last read and reports them.
    void reloadChanged();

    /// The instance of the track collection, or nullptr before the database
    /// opens. The track properties dialog has no path to the collection.
    static TrackMetaDao* instance();

  public slots:
    /// Removes the rows of tracks that left the library.
    void purgeTracks(const QSet<TrackId>& trackIds);

  signals:
    /// The values of these tracks are new. The library cache reads them again.
    void tracksChanged(const QSet<TrackId>& trackIds);

  private:
    bool write(TrackId trackId, const QString& column, const QVariant& value);

    QSqlDatabase m_database;
    QTimer* m_pPollTimer;
    qint64 m_lastUpdatedAt;

    DISALLOW_COPY_AND_ASSIGN(TrackMetaDao);
};

} // namespace muxic
