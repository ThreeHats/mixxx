#include "muxic/relatedtracks/trackrelationstorage.h"

#include <QtDebug>

#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "muxic/relatedtracks/trackrelation.h"
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

TEST_F(TrackRelationStorageTest, bothWaysLookupFromEitherEnd) {
    const TrackId trackId1 = addTrack(QStringLiteral("-png.mp3"));
    const TrackId trackId2 = addTrack(QStringLiteral("-jpg.mp3"));
    ASSERT_TRUE(trackId1.isValid());
    ASSERT_TRUE(trackId2.isValid());

    TrackRelation relation(trackId1, trackId2);
    relation.setBidirectional(true);
    ASSERT_TRUE(m_storage.saveRelation(relation));

    const QList<TrackRelation> fromSource = m_storage.readRelationsFrom(trackId1);
    ASSERT_EQ(1, static_cast<int>(fromSource.size()));
    EXPECT_EQ(trackId2, fromSource.first().otherTrackId(trackId1));

    const QList<TrackRelation> fromTarget = m_storage.readRelationsFrom(trackId2);
    ASSERT_EQ(1, static_cast<int>(fromTarget.size()));
    EXPECT_EQ(trackId1, fromTarget.first().otherTrackId(trackId2));
}

TEST_F(TrackRelationStorageTest, oneWayLookupFromSourceOnly) {
    const TrackId trackId1 = addTrack(QStringLiteral("-png.mp3"));
    const TrackId trackId2 = addTrack(QStringLiteral("-jpg.mp3"));
    ASSERT_TRUE(trackId1.isValid());
    ASSERT_TRUE(trackId2.isValid());

    ASSERT_TRUE(m_storage.saveRelation(TrackRelation(trackId1, trackId2)));

    EXPECT_EQ(1, static_cast<int>(m_storage.readRelationsFrom(trackId1).size()));
    EXPECT_EQ(0, static_cast<int>(m_storage.readRelationsFrom(trackId2).size()));
    // Both ends count the relation.
    EXPECT_EQ(1u, m_storage.countRelationsOfTrack(trackId1));
    EXPECT_EQ(1u, m_storage.countRelationsOfTrack(trackId2));
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
