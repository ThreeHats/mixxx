#include "muxic/relatedtracks/trackrelationstorage.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QtDebug>

#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "muxic/relatedtracks/trackrelation.h"
#include "muxic/relatedtracks/trackrelationschema.h"
#include "test/librarytest.h"
#include "track/track.h"

using muxic::TrackRelation;
using muxic::TrackRelationStorage;

class TrackRelationStorageTest : public LibraryTest {
  protected:
    TrackRelationStorageTest() {
        m_storage.connectDatabase(dbConnection());
    }

    TrackId addTrack(const QString& fileSuffix) {
        const TrackPointer pTrack = getOrAddTrackByLocation(getTestFile(fileSuffix));
        return pTrack ? pTrack->getId() : TrackId();
    }

    TrackRelationStorage m_storage;
};

TEST_F(TrackRelationStorageTest, createTableTwice) {
    // The table is made when the database opens. A second connect must
    // keep the rows.
    const TrackId trackId1 = addTrack(QStringLiteral("-png.mp3"));
    const TrackId trackId2 = addTrack(QStringLiteral("-jpg.mp3"));
    ASSERT_TRUE(trackId1.isValid());
    ASSERT_TRUE(trackId2.isValid());

    ASSERT_TRUE(m_storage.saveRelation(TrackRelation(trackId1, trackId2)));
    EXPECT_EQ(1u, m_storage.countRelations());

    m_storage.disconnectDatabase();
    m_storage.connectDatabase(dbConnection());
    EXPECT_EQ(1u, m_storage.countRelations());
}

TEST_F(TrackRelationStorageTest, addUpdateRemove) {
    const TrackId trackId1 = addTrack(QStringLiteral("-png.mp3"));
    const TrackId trackId2 = addTrack(QStringLiteral("-jpg.mp3"));
    ASSERT_TRUE(trackId1.isValid());
    ASSERT_TRUE(trackId2.isValid());

    TrackRelation relation(trackId1, trackId2);
    relation.setType(QStringLiteral("harmonic_blend"));
    relation.setRating(4);
    relation.setNotes(QStringLiteral("drop on bar 33"));
    ASSERT_TRUE(m_storage.saveRelation(relation));

    TrackRelation stored;
    ASSERT_TRUE(m_storage.readRelation(trackId1, trackId2, &stored));
    EXPECT_TRUE(stored.getId().isValid());
    EXPECT_EQ(trackId1, stored.getSourceTrackId());
    EXPECT_EQ(trackId2, stored.getTargetTrackId());
    EXPECT_FALSE(stored.isBidirectional());
    EXPECT_EQ(QStringLiteral("harmonic_blend"), stored.getType());
    EXPECT_EQ(4, stored.getRating());
    EXPECT_EQ(QStringLiteral("drop on bar 33"), stored.getNotes());

    // An update keeps one row.
    relation.setRating(2);
    relation.setNotes(QStringLiteral("too long"));
    ASSERT_TRUE(m_storage.saveRelation(relation));
    EXPECT_EQ(1u, m_storage.countRelations());
    ASSERT_TRUE(m_storage.readRelation(trackId1, trackId2, &stored));
    EXPECT_EQ(2, stored.getRating());
    EXPECT_EQ(QStringLiteral("too long"), stored.getNotes());

    ASSERT_TRUE(m_storage.removeRelation(trackId1, trackId2));
    EXPECT_FALSE(m_storage.readRelation(trackId1, trackId2));
    EXPECT_EQ(0u, m_storage.countRelations());
}

TEST_F(TrackRelationStorageTest, duplicatePairIsOneRow) {
    const TrackId trackId1 = addTrack(QStringLiteral("-png.mp3"));
    const TrackId trackId2 = addTrack(QStringLiteral("-jpg.mp3"));
    ASSERT_TRUE(trackId1.isValid());
    ASSERT_TRUE(trackId2.isValid());

    ASSERT_TRUE(m_storage.saveRelation(TrackRelation(trackId1, trackId2)));
    ASSERT_TRUE(m_storage.saveRelation(TrackRelation(trackId1, trackId2)));
    EXPECT_EQ(1u, m_storage.countRelations());

    // A save of the reverse pair keeps one row and makes it go both ways.
    ASSERT_TRUE(m_storage.saveRelation(TrackRelation(trackId2, trackId1)));
    EXPECT_EQ(1u, m_storage.countRelations());

    TrackRelation stored;
    ASSERT_TRUE(m_storage.readRelation(trackId1, trackId2, &stored));
    EXPECT_EQ(trackId1, stored.getSourceTrackId());
    EXPECT_TRUE(stored.isBidirectional());
}

TEST_F(TrackRelationStorageTest, selfRelationRefused) {
    const TrackId trackId = addTrack(QStringLiteral("-png.mp3"));
    ASSERT_TRUE(trackId.isValid());

    EXPECT_FALSE(m_storage.saveRelation(TrackRelation(trackId, trackId)));
    EXPECT_FALSE(m_storage.saveRelation(TrackRelation(trackId, TrackId())));
    EXPECT_EQ(0u, m_storage.countRelations());
}

TEST_F(TrackRelationStorageTest, aPairHasOneRowInEitherOrder) {
    const TrackId trackId1 = addTrack(QStringLiteral("-png.mp3"));
    const TrackId trackId2 = addTrack(QStringLiteral("-jpg.mp3"));
    ASSERT_TRUE(trackId1.isValid());
    ASSERT_TRUE(trackId2.isValid());

    TrackRelation relation(trackId1, trackId2);
    relation.setBidirectional(true);
    ASSERT_TRUE(m_storage.saveRelation(relation));

    // The unique index of the unordered pair refuses the reverse row.
    QSqlQuery insert(dbConnection());
    insert.prepare(QStringLiteral("INSERT INTO " TRACK_RELATIONS_TABLE
                                  " (source_track_id, target_track_id) VALUES (?, ?)"));
    insert.addBindValue(trackId2.toVariant());
    insert.addBindValue(trackId1.toVariant());
    EXPECT_FALSE(insert.exec());
    EXPECT_EQ(1u, m_storage.countRelations());

    // Either order finds the one row.
    TrackRelation stored;
    ASSERT_TRUE(m_storage.readRelation(trackId1, trackId2, &stored));
    EXPECT_TRUE(stored.isBidirectional());
    ASSERT_TRUE(m_storage.readRelation(trackId2, trackId1, &stored));
    EXPECT_TRUE(stored.isBidirectional());
}

TEST_F(TrackRelationStorageTest, purgeHook) {
    const TrackId trackId1 = addTrack(QStringLiteral("-png.mp3"));
    const TrackId trackId2 = addTrack(QStringLiteral("-jpg.mp3"));
    const TrackId trackId3 = addTrack(QStringLiteral("-vbr.mp3"));
    ASSERT_TRUE(trackId1.isValid());
    ASSERT_TRUE(trackId2.isValid());
    ASSERT_TRUE(trackId3.isValid());

    ASSERT_TRUE(m_storage.saveRelation(TrackRelation(trackId1, trackId2)));
    ASSERT_TRUE(m_storage.saveRelation(TrackRelation(trackId2, trackId3)));
    EXPECT_EQ(2u, m_storage.countRelations());

    ASSERT_TRUE(m_storage.onPurgingTracks(QList<TrackId>{trackId2}));
    EXPECT_EQ(0u, m_storage.countRelations());
}

TEST_F(TrackRelationStorageTest, removeAllRelationsOfTracks) {
    const TrackId trackId1 = addTrack(QStringLiteral("-png.mp3"));
    const TrackId trackId2 = addTrack(QStringLiteral("-jpg.mp3"));
    const TrackId trackId3 = addTrack(QStringLiteral("-vbr.mp3"));
    ASSERT_TRUE(trackId1.isValid());
    ASSERT_TRUE(trackId2.isValid());
    ASSERT_TRUE(trackId3.isValid());

    ASSERT_TRUE(m_storage.saveRelation(TrackRelation(trackId1, trackId2)));
    ASSERT_TRUE(m_storage.saveRelation(TrackRelation(trackId1, trackId3)));
    ASSERT_TRUE(m_storage.saveRelation(TrackRelation(trackId2, trackId3)));

    ASSERT_TRUE(m_storage.removeAllRelationsOfTracks(QList<TrackId>{trackId1}));
    EXPECT_EQ(1u, m_storage.countRelations());
    EXPECT_TRUE(m_storage.readRelation(trackId2, trackId3));
}

TEST_F(TrackRelationStorageTest, ratingIsClamped) {
    const TrackId trackId1 = addTrack(QStringLiteral("-png.mp3"));
    const TrackId trackId2 = addTrack(QStringLiteral("-jpg.mp3"));
    ASSERT_TRUE(trackId1.isValid());
    ASSERT_TRUE(trackId2.isValid());

    TrackRelation relation(trackId1, trackId2);
    relation.setRating(42);
    ASSERT_TRUE(m_storage.saveRelation(relation));

    TrackRelation stored;
    ASSERT_TRUE(m_storage.readRelation(trackId1, trackId2, &stored));
    EXPECT_EQ(TrackRelation::kMaxRating, stored.getRating());
}

// The muxic hub runs these statements when it merges two library rows.
// tools/muxic/docs/related-tracks.md shows the same statements.
namespace {

const char* kMergeStatements[] = {
        // 1. Fold the fields of each row of the loser into the row of the
        // winner that holds the same other track.
        "UPDATE " TRACK_RELATIONS_TABLE
        " AS w SET"
        " bidirectional = max(w.bidirectional, coalesce(("
        "   SELECT CASE WHEN l.bidirectional<>0"
        "     OR (l.source_track_id=:loser)<>(w.source_track_id=:winner)"
        "     THEN 1 ELSE 0 END FROM " TRACK_RELATIONS_TABLE
        " l"
        "   WHERE (l.source_track_id=:loser AND l.target_track_id<>:winner"
        "          AND l.target_track_id IN (w.source_track_id, w.target_track_id))"
        "      OR (l.target_track_id=:loser AND l.source_track_id<>:winner"
        "          AND l.source_track_id IN (w.source_track_id, w.target_track_id))), 0)),"
        " relation_type = CASE WHEN w.relation_type<>'' THEN w.relation_type ELSE coalesce(("
        "   SELECT l.relation_type FROM " TRACK_RELATIONS_TABLE
        " l"
        "   WHERE (l.source_track_id=:loser AND l.target_track_id<>:winner"
        "          AND l.target_track_id IN (w.source_track_id, w.target_track_id))"
        "      OR (l.target_track_id=:loser AND l.source_track_id<>:winner"
        "          AND l.source_track_id IN (w.source_track_id, w.target_track_id))), '') END,"
        " rating = CASE WHEN w.rating<>0 THEN w.rating ELSE coalesce(("
        "   SELECT l.rating FROM " TRACK_RELATIONS_TABLE
        " l"
        "   WHERE (l.source_track_id=:loser AND l.target_track_id<>:winner"
        "          AND l.target_track_id IN (w.source_track_id, w.target_track_id))"
        "      OR (l.target_track_id=:loser AND l.source_track_id<>:winner"
        "          AND l.source_track_id IN (w.source_track_id, w.target_track_id))), 0) END,"
        " notes = CASE WHEN w.notes<>'' THEN w.notes ELSE coalesce(("
        "   SELECT l.notes FROM " TRACK_RELATIONS_TABLE
        " l"
        "   WHERE (l.source_track_id=:loser AND l.target_track_id<>:winner"
        "          AND l.target_track_id IN (w.source_track_id, w.target_track_id))"
        "      OR (l.target_track_id=:loser AND l.source_track_id<>:winner"
        "          AND l.source_track_id IN (w.source_track_id, w.target_track_id))), '') END"
        " WHERE w.source_track_id=:winner OR w.target_track_id=:winner",
        // 2. Move the rows that the winner does not hold. The unique index
        // of the unordered pair refuses the rest.
        "UPDATE OR IGNORE " TRACK_RELATIONS_TABLE
        " SET source_track_id = :winner WHERE source_track_id = :loser",
        "UPDATE OR IGNORE " TRACK_RELATIONS_TABLE
        " SET target_track_id = :winner WHERE target_track_id = :loser",
        // 3. Drop what is left on the loser and any self relation.
        "DELETE FROM " TRACK_RELATIONS_TABLE
        " WHERE source_track_id = :loser OR target_track_id = :loser",
        "DELETE FROM " TRACK_RELATIONS_TABLE
        " WHERE source_track_id = target_track_id",
};

} // anonymous namespace

class TrackRelationMergeTest : public TrackRelationStorageTest {
  protected:
    bool mergeTracks(TrackId loser, TrackId winner) {
        for (const char* statement : kMergeStatements) {
            QSqlQuery query(dbConnection());
            if (!query.prepare(QString::fromLatin1(statement))) {
                qWarning() << "prepare failed" << query.lastError();
                return false;
            }
            query.bindValue(QStringLiteral(":loser"), loser.toVariant());
            query.bindValue(QStringLiteral(":winner"), winner.toVariant());
            if (!query.exec()) {
                qWarning() << "exec failed" << query.lastError();
                return false;
            }
        }
        return true;
    }
};

TEST_F(TrackRelationMergeTest, relationsMoveToTheWinner) {
    const TrackId loser = addTrack(QStringLiteral("-png.mp3"));
    const TrackId winner = addTrack(QStringLiteral("-jpg.mp3"));
    const TrackId other = addTrack(QStringLiteral("-vbr.mp3"));
    ASSERT_TRUE(loser.isValid());
    ASSERT_TRUE(winner.isValid());
    ASSERT_TRUE(other.isValid());

    TrackRelation relation(loser, other);
    relation.setType(QStringLiteral("mashup"));
    relation.setRating(4);
    ASSERT_TRUE(m_storage.saveRelation(relation));
    ASSERT_EQ(1u, m_storage.countRelations());

    ASSERT_TRUE(mergeTracks(loser, winner));

    EXPECT_EQ(1u, m_storage.countRelations());
    EXPECT_FALSE(m_storage.readRelation(loser, other));
    TrackRelation moved;
    ASSERT_TRUE(m_storage.readRelation(winner, other, &moved));
    EXPECT_EQ(winner, moved.getSourceTrackId());
    EXPECT_EQ(other, moved.getTargetTrackId());
    EXPECT_EQ(QStringLiteral("mashup"), moved.getType());
    EXPECT_EQ(4, moved.getRating());
}

TEST_F(TrackRelationMergeTest, aDuplicatePairIsDropped) {
    const TrackId loser = addTrack(QStringLiteral("-png.mp3"));
    const TrackId winner = addTrack(QStringLiteral("-jpg.mp3"));
    const TrackId other = addTrack(QStringLiteral("-vbr.mp3"));
    ASSERT_TRUE(loser.isValid());
    ASSERT_TRUE(winner.isValid());
    ASSERT_TRUE(other.isValid());

    // Both rows would become the same pair after the merge.
    ASSERT_TRUE(m_storage.saveRelation(TrackRelation(loser, other)));
    TrackRelation kept(winner, other);
    kept.setType(QStringLiteral("double_drop"));
    ASSERT_TRUE(m_storage.saveRelation(kept));
    ASSERT_EQ(2u, m_storage.countRelations());

    ASSERT_TRUE(mergeTracks(loser, winner));

    // The row of the winner stays, the row of the loser goes.
    EXPECT_EQ(1u, m_storage.countRelations());
    TrackRelation stored;
    ASSERT_TRUE(m_storage.readRelation(winner, other, &stored));
    EXPECT_EQ(QStringLiteral("double_drop"), stored.getType());
}

TEST_F(TrackRelationMergeTest, aRelationOfTheTwoMergedTracksGoes) {
    const TrackId loser = addTrack(QStringLiteral("-png.mp3"));
    const TrackId winner = addTrack(QStringLiteral("-jpg.mp3"));
    ASSERT_TRUE(loser.isValid());
    ASSERT_TRUE(winner.isValid());

    ASSERT_TRUE(m_storage.saveRelation(TrackRelation(loser, winner)));
    ASSERT_EQ(1u, m_storage.countRelations());

    ASSERT_TRUE(mergeTracks(loser, winner));

    // The merge would make a relation of the winner with itself.
    EXPECT_EQ(0u, m_storage.countRelations());
}

TEST_F(TrackRelationMergeTest, theWinnerTakesTheFieldsThatItLacks) {
    const TrackId loser = addTrack(QStringLiteral("-png.mp3"));
    const TrackId winner = addTrack(QStringLiteral("-jpg.mp3"));
    const TrackId other = addTrack(QStringLiteral("-vbr.mp3"));
    ASSERT_TRUE(other.isValid());

    // The winner has a type only, the loser has a rating and a note.
    TrackRelation winnerRelation(winner, other);
    winnerRelation.setType(QStringLiteral("mashup"));
    ASSERT_TRUE(m_storage.saveRelation(winnerRelation));
    TrackRelation loserRelation(loser, other);
    loserRelation.setType(QStringLiteral("double_drop"));
    loserRelation.setRating(4);
    loserRelation.setNotes(QStringLiteral("cut on the drop"));
    ASSERT_TRUE(m_storage.saveRelation(loserRelation));

    ASSERT_TRUE(mergeTracks(loser, winner));

    EXPECT_EQ(1u, m_storage.countRelations());
    TrackRelation stored;
    ASSERT_TRUE(m_storage.readRelation(winner, other, &stored));
    EXPECT_EQ(QStringLiteral("mashup"), stored.getType());
    EXPECT_EQ(4, stored.getRating());
    EXPECT_EQ(QStringLiteral("cut on the drop"), stored.getNotes());
}

TEST_F(TrackRelationMergeTest, aBothWaysLoserMakesTheWinnerBothWays) {
    const TrackId loser = addTrack(QStringLiteral("-png.mp3"));
    const TrackId winner = addTrack(QStringLiteral("-jpg.mp3"));
    const TrackId other = addTrack(QStringLiteral("-vbr.mp3"));
    ASSERT_TRUE(other.isValid());

    ASSERT_TRUE(m_storage.saveRelation(TrackRelation(winner, other)));
    TrackRelation loserRelation(loser, other);
    loserRelation.setBidirectional(true);
    ASSERT_TRUE(m_storage.saveRelation(loserRelation));

    ASSERT_TRUE(mergeTracks(loser, winner));

    EXPECT_EQ(1u, m_storage.countRelations());
    TrackRelation stored;
    ASSERT_TRUE(m_storage.readRelation(winner, other, &stored));
    EXPECT_TRUE(stored.isBidirectional());
}

TEST_F(TrackRelationMergeTest, oppositeDirectionsGiveBothWays) {
    const TrackId loser = addTrack(QStringLiteral("-png.mp3"));
    const TrackId winner = addTrack(QStringLiteral("-jpg.mp3"));
    const TrackId other = addTrack(QStringLiteral("-vbr.mp3"));
    ASSERT_TRUE(other.isValid());

    // The winner leads to the other track, the other track leads to the
    // loser. After the merge the two tracks point at each other.
    ASSERT_TRUE(m_storage.saveRelation(TrackRelation(winner, other)));
    ASSERT_TRUE(m_storage.saveRelation(TrackRelation(other, loser)));
    ASSERT_EQ(2u, m_storage.countRelations());

    ASSERT_TRUE(mergeTracks(loser, winner));

    EXPECT_EQ(1u, m_storage.countRelations());
    TrackRelation stored;
    ASSERT_TRUE(m_storage.readRelation(winner, other, &stored));
    EXPECT_TRUE(stored.isBidirectional());
}

TEST_F(TrackRelationMergeTest, theReverseOrderOfAPairIsOneRow) {
    const TrackId loser = addTrack(QStringLiteral("-png.mp3"));
    const TrackId winner = addTrack(QStringLiteral("-jpg.mp3"));
    const TrackId other = addTrack(QStringLiteral("-vbr.mp3"));
    ASSERT_TRUE(other.isValid());

    // The two rows hold the same pair after the merge, in reverse order.
    ASSERT_TRUE(m_storage.saveRelation(TrackRelation(loser, other)));
    ASSERT_TRUE(m_storage.saveRelation(TrackRelation(other, winner)));
    ASSERT_EQ(2u, m_storage.countRelations());

    ASSERT_TRUE(mergeTracks(loser, winner));

    EXPECT_EQ(1u, m_storage.countRelations());
}
