#pragma once

#include <QList>
#include <QObject>
#include <QSet>
#include <QSqlDatabase>

#include "muxic/relatedtracks/trackrelation.h"
#include "track/trackid.h"
#include "util/db/sqlstorage.h"
#include "util/thread_affinity.h"

namespace muxic {

/// Holds the relations between tracks that mix well.
///
/// This class makes its table when the database opens. Both ends of a
/// relation are `library.id` values.
class TrackRelationStorage : public QObject, public virtual /*implements*/ SqlStorage {
    Q_OBJECT

  public:
    explicit TrackRelationStorage(QObject* pParent = nullptr);
    ~TrackRelationStorage() override = default;

    void repairDatabase(
            const QSqlDatabase& database) override;

    void connectDatabase(
            const QSqlDatabase& database) override;
    void disconnectDatabase() override;

    /////////////////////////////////////////////////////////////////////////
    // Write operations
    /////////////////////////////////////////////////////////////////////////

    /// Writes the relation and reports the change.
    ///
    /// A pair of tracks has one row. A save on the reverse pair sets that
    /// row to both ways. A relation of a track with itself fails.
    bool saveRelation(const TrackRelation& relation);

    /// Removes the relation of the two tracks, in either order.
    bool removeRelation(TrackId trackId1, TrackId trackId2);

    /// Removes each relation that has one of the tracks at an end.
    bool removeAllRelationsOfTracks(const QList<TrackId>& trackIds);

    /// TrackCollection calls this in the purge transaction.
    bool onPurgingTracks(const QList<TrackId>& trackIds);
    /// TrackCollection calls this after it commits the purge.
    void afterPurgingTracks();

    /////////////////////////////////////////////////////////////////////////
    // Read operations (read-only, const)
    /////////////////////////////////////////////////////////////////////////

    /// Reads the relation of the two tracks, in either order. Omit
    /// pRelation to test for a relation only.
    bool readRelation(
            TrackId trackId1,
            TrackId trackId2,
            TrackRelation* pRelation = nullptr) const;

    /// The relations that lead away from the track.
    QList<TrackRelation> readRelationsFrom(TrackId trackId) const;

    uint countRelations() const;
    uint countRelationsOfTrack(TrackId trackId) const;

    /// The tracks of the list that have a relation.
    QSet<TrackId> collectRelatedTrackIds(const QList<TrackId>& trackIds) const;

    /// A subselect for the ids at the far end of a relation of this track.
    /// No database access.
    static QString formatSubselectQueryForRelatedTrackIds(TrackId trackId);

    /// A subselect for the ids of all tracks with a relation.
    static QString formatSubselectQueryForAllRelatedTrackIds();

  signals:
    /// The table changed. A view reads it again.
    void relationsChanged();

  private:
    bool removeRelationWithoutSignal(TrackId trackId1, TrackId trackId2);

    QSqlDatabase m_database;
};

} // namespace muxic
