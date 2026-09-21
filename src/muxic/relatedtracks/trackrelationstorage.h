#pragma once

#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QSqlDatabase>
#include <QStringList>

#include "muxic/relatedtracks/trackrelation.h"
#include "track/trackid.h"
#include "util/db/sqlstorage.h"
#include "util/thread_affinity.h"

namespace muxic {

/// A pair of tracks, in the order that the caller gives.
using TrackIdPair = QPair<TrackId, TrackId>;

/// Holds the relations between tracks that mix well.
///
/// This class makes its table when the database opens. Both ends of a
/// relation are `library.id` values. A pair of tracks has at most one row,
/// which a unique index on the unordered pair enforces.
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

    /// Writes one relation. A save on the reverse pair sets that row to
    /// both ways. A relation of a track with itself fails.
    bool saveRelation(const TrackRelation& relation);

    /// Writes the relations in one transaction and reports one change.
    /// Returns the number of relations that it wrote.
    int saveRelations(const QList<TrackRelation>& relations);

    /// Writes the type, the rating and the note of a relation that exists.
    /// Reports nothing: the caller holds the row that it edits.
    bool updateRelationFields(const TrackRelation& relation);

    /// Removes the relation of the two tracks, in either order.
    bool removeRelation(TrackId trackId1, TrackId trackId2);

    /// Removes the relations of the pairs in one transaction and reports
    /// one change. Returns the number of rows that it removed.
    int removeRelations(const QList<TrackIdPair>& pairs);

    /// Removes each relation with one of the tracks at an end.
    /// Returns the number of rows that it removed.
    int removeAllRelationsOfTracks(const QList<TrackId>& trackIds);

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

    /// The relation types that the table holds, without the empty type.
    QStringList readRelationTypes() const;

    uint countRelations() const;
    uint countRelationsOfTrack(TrackId trackId) const;

    /// True if one of the tracks has a relation. One query.
    bool anyTrackHasRelation(const QList<TrackId>& trackIds) const;

    /// The number of relations that have one of the tracks at an end.
    uint countRelationsOfTracks(const QList<TrackId>& trackIds) const;

    /// A subselect for the ids of all tracks with a relation.
    static QString formatSubselectQueryForAllRelatedTrackIds();

  signals:
    /// The table changed. A view reads it again.
    void relationsChanged();

  private:
    /// Makes the table and its indices. Folds a duplicate pair first,
    /// which the unique index of the pair refuses.
    static void createTableAndIndices(const QSqlDatabase& database);

    /// Writes one relation, without a transaction and without a signal.
    /// Sets pChanged when the row that it leaves is a new row.
    bool saveRelationWorker(const TrackRelation& relation, bool* pChanged);
    int removeRelationsWorker(const QList<TrackIdPair>& pairs);
    int removeRelationsOfTracksWorker(const QList<TrackId>& trackIds);

    QSqlDatabase m_database;
};

} // namespace muxic
