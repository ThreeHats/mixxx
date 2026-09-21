#include <QSet>

#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "muxic/relatedtracks/relatedtrackstablemodel.h"
#include "muxic/relatedtracks/trackrelation.h"
#include "muxic/relatedtracks/trackrelationstorage.h"
#include "test/librarytest.h"
#include "track/keyutils.h"
#include "track/track.h"

using muxic::RelatedTracksTableModel;
using muxic::TrackRelation;
using muxic::TrackRelationStorage;

namespace {

constexpr double kReferenceBpm = 128.0;

} // anonymous namespace

/// Reads the SQL of the model against a small library.
class RelatedTracksModelTest : public LibraryTest {
  protected:
    RelatedTracksModelTest()
            : m_model(nullptr, trackCollectionManager()) {
    }

    TrackRelationStorage& storage() const {
        return internalCollection()->trackRelations();
    }

    TrackId addTrack(const QString& fileSuffix,
            double bpm,
            mixxx::track::io::key::ChromaticKey key) {
        const TrackPointer pTrack = getOrAddTrackByLocation(getTestFile(fileSuffix));
        if (!pTrack) {
            return TrackId();
        }
        if (bpm > 0.0) {
            pTrack->trySetBpm(bpm);
        }
        if (key != mixxx::track::io::key::INVALID) {
            pTrack->setKey(key, mixxx::track::io::key::USER);
        }
        trackCollectionManager()->saveTrack(pTrack);
        return pTrack->getId();
    }

    QSet<TrackId> rowTrackIds() {
        QSet<TrackId> trackIds;
        for (int row = 0; row < m_model.rowCount(); ++row) {
            trackIds.insert(m_model.getTrackId(m_model.index(row, 0)));
        }
        return trackIds;
    }

    RelatedTracksTableModel m_model;
};

TEST_F(RelatedTracksModelTest, relatedToShowsOutgoingAndBothWays) {
    const TrackId reference = addTrack(QStringLiteral("-png.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    const TrackId outgoing = addTrack(QStringLiteral("-jpg.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    const TrackId incoming = addTrack(QStringLiteral("-vbr.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    ASSERT_TRUE(reference.isValid());
    ASSERT_TRUE(outgoing.isValid());
    ASSERT_TRUE(incoming.isValid());

    ASSERT_TRUE(storage().saveRelation(TrackRelation(reference, outgoing)));
    // A one-way relation that points at the reference stays out of the view.
    ASSERT_TRUE(storage().saveRelation(TrackRelation(incoming, reference)));

    m_model.selectRelatedTo(reference);
    EXPECT_EQ(1, m_model.rowCount());
    EXPECT_TRUE(rowTrackIds().contains(outgoing));
    EXPECT_FALSE(rowTrackIds().contains(incoming));

    // The same relation both ways shows under both tracks.
    TrackRelation bothWays(incoming, reference);
    bothWays.setBidirectional(true);
    ASSERT_TRUE(storage().saveRelation(bothWays));
    m_model.selectRelatedTo(reference);
    EXPECT_EQ(2, m_model.rowCount());
    EXPECT_TRUE(rowTrackIds().contains(incoming));
}

TEST_F(RelatedTracksModelTest, suggestionsMatchTheTempo) {
    const TrackId reference = addTrack(QStringLiteral("-png.mp3"),
            kReferenceBpm,
            mixxx::track::io::key::INVALID);
    const TrackId sameTempo = addTrack(QStringLiteral("-jpg.mp3"),
            kReferenceBpm * 1.02,
            mixxx::track::io::key::INVALID);
    const TrackId halfTempo = addTrack(QStringLiteral("-vbr.mp3"),
            kReferenceBpm / 2.0,
            mixxx::track::io::key::INVALID);
    const TrackId farTempo = addTrack(QStringLiteral(".flac"),
            kReferenceBpm * 1.2,
            mixxx::track::io::key::INVALID);
    ASSERT_TRUE(farTempo.isValid());

    m_model.selectSuggestedFor(reference);
    const QSet<TrackId> trackIds = rowTrackIds();
    EXPECT_TRUE(trackIds.contains(sameTempo));
    EXPECT_TRUE(trackIds.contains(halfTempo));
    EXPECT_FALSE(trackIds.contains(farTempo));
    EXPECT_FALSE(trackIds.contains(reference));
}

TEST_F(RelatedTracksModelTest, suggestionsMatchTheDoubleTempo) {
    const TrackId reference = addTrack(QStringLiteral("-png.mp3"),
            87.0,
            mixxx::track::io::key::INVALID);
    const TrackId doubleTempo = addTrack(QStringLiteral("-jpg.mp3"),
            174.0,
            mixxx::track::io::key::INVALID);

    m_model.selectSuggestedFor(reference);
    EXPECT_TRUE(rowTrackIds().contains(doubleTempo));
}

TEST_F(RelatedTracksModelTest, suggestionsMatchTheKeyWheel) {
    using namespace mixxx::track::io::key;
    const TrackId reference = addTrack(QStringLiteral("-png.mp3"), 0.0, C_MAJOR);
    // The relative minor of C major.
    const TrackId relative = addTrack(QStringLiteral("-jpg.mp3"), 0.0, A_MINOR);
    // One step on the wheel: G major.
    const TrackId neighbour = addTrack(QStringLiteral("-vbr.mp3"), 0.0, G_MAJOR);
    // Two steps on the wheel: D major.
    const TrackId farKey = addTrack(QStringLiteral(".flac"), 0.0, D_MAJOR);
    ASSERT_TRUE(farKey.isValid());

    m_model.selectSuggestedFor(reference);
    const QSet<TrackId> trackIds = rowTrackIds();
    EXPECT_TRUE(trackIds.contains(relative));
    EXPECT_TRUE(trackIds.contains(neighbour));
    EXPECT_FALSE(trackIds.contains(farKey));
}

TEST_F(RelatedTracksModelTest, suggestionsNeedTheTempoAndTheKey) {
    using namespace mixxx::track::io::key;
    const TrackId reference = addTrack(QStringLiteral("-png.mp3"), kReferenceBpm, C_MAJOR);
    // Both filters hold.
    const TrackId bothFit = addTrack(QStringLiteral("-jpg.mp3"), kReferenceBpm, A_MINOR);
    // The key holds, the tempo does not.
    const TrackId keyOnly = addTrack(QStringLiteral("-vbr.mp3"), kReferenceBpm * 1.2, A_MINOR);
    // The tempo holds, the key does not.
    const TrackId tempoOnly = addTrack(QStringLiteral(".flac"), kReferenceBpm, D_MAJOR);
    ASSERT_TRUE(tempoOnly.isValid());

    m_model.selectSuggestedFor(reference);
    const QSet<TrackId> trackIds = rowTrackIds();
    EXPECT_TRUE(trackIds.contains(bothFit));
    EXPECT_FALSE(trackIds.contains(keyOnly));
    EXPECT_FALSE(trackIds.contains(tempoOnly));
}

TEST_F(RelatedTracksModelTest, aReferenceWithoutTempoAndKeyGivesNothing) {
    const TrackId reference = addTrack(QStringLiteral("-png.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    addTrack(QStringLiteral("-jpg.mp3"), kReferenceBpm, mixxx::track::io::key::C_MAJOR);

    m_model.selectSuggestedFor(reference);
    EXPECT_EQ(0, m_model.rowCount());
}

TEST_F(RelatedTracksModelTest, removeTracksKeepsTheRightRelations) {
    const TrackId reference = addTrack(QStringLiteral("-png.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    const TrackId first = addTrack(QStringLiteral("-jpg.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    const TrackId second = addTrack(QStringLiteral("-vbr.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    const TrackId third = addTrack(QStringLiteral(".flac"),
            0.0,
            mixxx::track::io::key::INVALID);
    ASSERT_TRUE(third.isValid());

    ASSERT_TRUE(storage().saveRelation(TrackRelation(reference, first)));
    ASSERT_TRUE(storage().saveRelation(TrackRelation(reference, second)));
    ASSERT_TRUE(storage().saveRelation(TrackRelation(reference, third)));

    m_model.selectRelatedTo(reference);
    ASSERT_EQ(3, m_model.rowCount());

    // The feature reads the table again on each report of a change. This
    // connection puts that behaviour under the test.
    QObject::connect(&storage(),
            &TrackRelationStorage::relationsChanged,
            &m_model,
            [this, reference] {
                m_model.selectRelatedTo(reference);
            });

    // Remove the first two rows in one call. A model that reads its table
    // again after each write gives the second index to another track.
    QModelIndexList indices;
    indices.append(m_model.index(0, 0));
    indices.append(m_model.index(1, 0));
    const TrackId keptTrackId = m_model.getTrackId(m_model.index(2, 0));
    const TrackId firstRemoved = m_model.getTrackId(indices.at(0));
    const TrackId secondRemoved = m_model.getTrackId(indices.at(1));
    m_model.removeTracks(indices);

    EXPECT_EQ(1u, storage().countRelations());
    EXPECT_TRUE(storage().readRelation(reference, keptTrackId));
    EXPECT_FALSE(storage().readRelation(reference, firstRemoved));
    EXPECT_FALSE(storage().readRelation(reference, secondRemoved));
}

TEST_F(RelatedTracksModelTest, setRelationsBidirectionalKeepsTheRightRelations) {
    const TrackId reference = addTrack(QStringLiteral("-png.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    const QList<TrackId> others{
            addTrack(QStringLiteral("-jpg.mp3"), 0.0, mixxx::track::io::key::INVALID),
            addTrack(QStringLiteral("-vbr.mp3"), 0.0, mixxx::track::io::key::INVALID),
            addTrack(QStringLiteral(".flac"), 0.0, mixxx::track::io::key::INVALID)};
    for (const auto& other : others) {
        ASSERT_TRUE(other.isValid());
        // The relation starts at the other track and goes both ways, thus
        // the view holds it. One way would take the row out of the view.
        TrackRelation relation(other, reference);
        relation.setBidirectional(true);
        ASSERT_TRUE(storage().saveRelation(relation));
    }

    m_model.selectRelatedTo(reference);
    ASSERT_EQ(3, m_model.rowCount());

    QObject::connect(&storage(),
            &TrackRelationStorage::relationsChanged,
            &m_model,
            [this, reference] {
                m_model.selectRelatedTo(reference);
            });

    QModelIndexList indices;
    indices.append(m_model.index(0, 0));
    indices.append(m_model.index(1, 0));
    indices.append(m_model.index(2, 0));
    // Before the fix the first write took its row out of the view, thus the
    // rest of the list named other rows or no row at all.
    EXPECT_EQ(3, m_model.setRelationsBidirectional(indices, false));

    for (const auto& other : others) {
        TrackRelation relation;
        ASSERT_TRUE(storage().readRelation(reference, other, &relation));
        EXPECT_FALSE(relation.isBidirectional());
    }
}

TEST_F(RelatedTracksModelTest, theRootNodeOffersNoRemove) {
    m_model.selectAllRelated();
    EXPECT_FALSE(m_model.hasCapabilities(TrackModel::Capability::Remove));

    const TrackId reference = addTrack(QStringLiteral("-png.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    m_model.selectRelatedTo(reference);
    EXPECT_TRUE(m_model.hasCapabilities(TrackModel::Capability::Remove));
}

namespace {

/// One row of a deck view.
struct DeckRow {
    int deckNumber;
    TrackId trackId;

    bool operator==(const DeckRow& other) const {
        return deckNumber == other.deckNumber && trackId == other.trackId;
    }
};

} // anonymous namespace

/// Reads the deck views of the related tracks panel.
class RelatedDecksModelTest : public RelatedTracksModelTest {
  protected:
    QList<DeckRow> deckRows() {
        const int deckColumn = m_model.fieldIndex(QStringLiteral("deck_number"));
        EXPECT_GE(deckColumn, 0);
        QList<DeckRow> rows;
        for (int row = 0; row < m_model.rowCount(); ++row) {
            rows.append(DeckRow{
                    m_model.data(m_model.index(row, deckColumn)).toInt(),
                    m_model.getTrackId(m_model.index(row, 0))});
        }
        return rows;
    }

    void relate(TrackId source, TrackId target, int rating) {
        muxic::TrackRelation relation(source, target);
        relation.setRating(rating);
        ASSERT_TRUE(storage().saveRelation(relation));
    }
};

TEST_F(RelatedDecksModelTest, eachDeckKeepsItsOwnRows) {
    const TrackId deck1 = addTrack(QStringLiteral("-png.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    const TrackId deck2 = addTrack(QStringLiteral("-jpg.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    const TrackId ofDeck1 = addTrack(QStringLiteral("-vbr.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    const TrackId ofDeck2 = addTrack(QStringLiteral(".flac"),
            0.0,
            mixxx::track::io::key::INVALID);
    ASSERT_TRUE(ofDeck2.isValid());
    relate(deck1, ofDeck1, 0);
    relate(deck2, ofDeck2, 0);

    m_model.selectRelatedToDecks(
            muxic::DeckTrackList{muxic::DeckTrack{1, deck1}, muxic::DeckTrack{2, deck2}});

    const QList<DeckRow> rows = deckRows();
    ASSERT_EQ(2, rows.size());
    // The rows come in the order of the decks.
    EXPECT_EQ(1, rows.at(0).deckNumber);
    EXPECT_EQ(ofDeck1, rows.at(0).trackId);
    EXPECT_EQ(2, rows.at(1).deckNumber);
    EXPECT_EQ(ofDeck2, rows.at(1).trackId);
}

TEST_F(RelatedDecksModelTest, aTrackOfTwoDecksHasTwoRows) {
    const TrackId deck1 = addTrack(QStringLiteral("-png.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    const TrackId deck2 = addTrack(QStringLiteral("-jpg.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    const TrackId shared = addTrack(QStringLiteral("-vbr.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    ASSERT_TRUE(shared.isValid());
    relate(deck1, shared, 0);
    relate(deck2, shared, 0);

    m_model.selectRelatedToDecks(
            muxic::DeckTrackList{muxic::DeckTrack{1, deck1}, muxic::DeckTrack{2, deck2}});

    const QList<DeckRow> rows = deckRows();
    ASSERT_EQ(2, rows.size());
    EXPECT_EQ(shared, rows.at(0).trackId);
    EXPECT_EQ(shared, rows.at(1).trackId);
    EXPECT_EQ(1, rows.at(0).deckNumber);
    EXPECT_EQ(2, rows.at(1).deckNumber);
}

TEST_F(RelatedDecksModelTest, theBestRelationOfADeckComesFirst) {
    const TrackId deck1 = addTrack(QStringLiteral("-png.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    const TrackId low = addTrack(QStringLiteral("-jpg.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    const TrackId high = addTrack(QStringLiteral("-vbr.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    ASSERT_TRUE(high.isValid());
    relate(deck1, low, 1);
    relate(deck1, high, 5);

    m_model.selectRelatedToDecks(muxic::DeckTrackList{muxic::DeckTrack{1, deck1}});

    const QList<DeckRow> rows = deckRows();
    ASSERT_EQ(2, rows.size());
    EXPECT_EQ(high, rows.at(0).trackId);
    EXPECT_EQ(low, rows.at(1).trackId);
}

TEST_F(RelatedDecksModelTest, aDeckThatHoldsNoTrackDropsOut) {
    const TrackId deck1 = addTrack(QStringLiteral("-png.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    const TrackId deck2 = addTrack(QStringLiteral("-jpg.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    const TrackId ofDeck1 = addTrack(QStringLiteral("-vbr.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    const TrackId ofDeck2 = addTrack(QStringLiteral(".flac"),
            0.0,
            mixxx::track::io::key::INVALID);
    ASSERT_TRUE(ofDeck2.isValid());
    relate(deck1, ofDeck1, 0);
    relate(deck2, ofDeck2, 0);

    // Deck 1 gives its track back, thus only deck 2 is left.
    m_model.selectRelatedToDecks(muxic::DeckTrackList{muxic::DeckTrack{2, deck2}});
    QList<DeckRow> rows = deckRows();
    ASSERT_EQ(1, rows.size());
    EXPECT_EQ(2, rows.at(0).deckNumber);
    EXPECT_EQ(ofDeck2, rows.at(0).trackId);

    // No deck holds a track. The view keeps its columns and holds no row.
    m_model.selectRelatedToDecks(muxic::DeckTrackList());
    EXPECT_EQ(0, m_model.rowCount());
    EXPECT_GE(m_model.fieldIndex(QStringLiteral("deck_number")), 0);
}

TEST_F(RelatedDecksModelTest, aRelationAndAPurgeChangeTheRows) {
    const TrackId deck1 = addTrack(QStringLiteral("-png.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    const TrackId other = addTrack(QStringLiteral("-jpg.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    ASSERT_TRUE(other.isValid());

    m_model.selectRelatedToDecks(muxic::DeckTrackList{muxic::DeckTrack{1, deck1}});
    EXPECT_EQ(0, m_model.rowCount());

    relate(deck1, other, 3);
    m_model.refresh();
    ASSERT_EQ(1, m_model.rowCount());
    EXPECT_EQ(other, m_model.getTrackId(m_model.index(0, 0)));

    trackCollectionManager()->purgeTracks(QList<TrackRef>{TrackRef::fromFilePath(
            getTestFile(QStringLiteral("-jpg.mp3")), other)});
    m_model.refresh();
    EXPECT_EQ(0, m_model.rowCount());
    EXPECT_EQ(0u, storage().countRelations());
}

TEST_F(RelatedDecksModelTest, aDeckViewTakesNoSortColumnOfTheLibrary) {
    const TrackId deck1 = addTrack(QStringLiteral("-png.mp3"),
            0.0,
            mixxx::track::io::key::INVALID);
    ASSERT_TRUE(deck1.isValid());

    // The Preview column of the view carries a sort column id. The test
    // has no track source, thus the columns of the library carry none.
    m_model.selectRelatedTo(deck1);
    EXPECT_TRUE(m_model.hasCapabilities(TrackModel::Capability::Sorting));
    const int previewColumn =
            m_model.columnIndexFromSortColumnId(TrackModel::SortColumnId::Preview);
    ASSERT_GE(previewColumn, 0);
    EXPECT_EQ(TrackModel::SortColumnId::Preview,
            m_model.sortColumnIdFromColumnIndex(previewColumn));

    m_model.selectRelatedToDecks(muxic::DeckTrackList{muxic::DeckTrack{1, deck1}});
    EXPECT_FALSE(m_model.hasCapabilities(TrackModel::Capability::Sorting));
    // The table view of the panel keeps its own order.
    EXPECT_EQ(-1, m_model.columnIndexFromSortColumnId(TrackModel::SortColumnId::Preview));
    EXPECT_EQ(TrackModel::SortColumnId::Invalid,
            m_model.sortColumnIdFromColumnIndex(previewColumn));
}

TEST_F(RelatedDecksModelTest, theSuggestionsOfEachDeckCarryItsDeckNumber) {
    const TrackId deck1 = addTrack(QStringLiteral("-png.mp3"),
            kReferenceBpm,
            mixxx::track::io::key::INVALID);
    const TrackId deck2 = addTrack(QStringLiteral("-jpg.mp3"),
            kReferenceBpm * 1.5,
            mixxx::track::io::key::INVALID);
    const TrackId ofDeck1 = addTrack(QStringLiteral("-vbr.mp3"),
            kReferenceBpm,
            mixxx::track::io::key::INVALID);
    const TrackId ofDeck2 = addTrack(QStringLiteral(".flac"),
            kReferenceBpm * 1.5,
            mixxx::track::io::key::INVALID);
    ASSERT_TRUE(ofDeck2.isValid());

    m_model.selectSuggestedForDecks(
            muxic::DeckTrackList{muxic::DeckTrack{1, deck1}, muxic::DeckTrack{2, deck2}});

    const QList<DeckRow> rows = deckRows();
    ASSERT_FALSE(rows.isEmpty());
    for (const DeckRow& row : rows) {
        if (row.deckNumber == 1) {
            EXPECT_NE(ofDeck2, row.trackId);
        } else {
            EXPECT_EQ(2, row.deckNumber);
            EXPECT_NE(ofDeck1, row.trackId);
        }
    }
    // Each deck brings the track that has its tempo.
    EXPECT_TRUE(rows.contains(DeckRow{1, ofDeck1}));
    EXPECT_TRUE(rows.contains(DeckRow{2, ofDeck2}));
}
