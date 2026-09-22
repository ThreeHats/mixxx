#include "muxic/relatedtracks/relateddeckwatcher.h"

#include <gtest/gtest.h>

#include <QSignalSpy>

#include "control/controlobject.h"
#include "mixer/playerinfo.h"
#include "mixer/playermanager.h"
#include "muxic/relatedtracks/trackrelation.h"
#include "muxic/relatedtracks/trackrelationstorage.h"
#include "test/librarytest.h"
#include "track/track.h"

using muxic::DeckTrackList;
using muxic::RelatedDeckWatcher;
using muxic::TrackRelationStorage;

namespace {

// The test drives the timer, thus it waits a short time only.
constexpr int kSettleDelayMillis = 5;
constexpr int kWaitMillis = 200;

/// Reads the decks for the related tracks panel.
class RelatedDeckWatcherTest : public LibraryTest {
  protected:
    void SetUp() override {
        m_pNumDecks = std::make_unique<ControlObject>(
                ConfigKey(QStringLiteral("[App]"), QStringLiteral("num_decks")));
        m_pNumDecks->set(2.0);
        PlayerInfo::create();
    }

    void TearDown() override {
        PlayerInfo::destroy();
        m_pNumDecks.reset();
    }

    TrackPointer addTrack(const QString& fileSuffix) {
        const TrackPointer pTrack = getOrAddTrackByLocation(getTestFile(fileSuffix));
        if (pTrack) {
            trackCollectionManager()->saveTrack(pTrack);
        }
        return pTrack;
    }

    void loadDeck(int deckNumber, const TrackPointer& pTrack) {
        PlayerInfo::instance().setTrackInfo(
                PlayerManager::groupForDeck(deckNumber - 1), pTrack);
    }

    TrackRelationStorage& storage() const {
        return internalCollection()->trackRelations();
    }

    /// Waits for the signal. Returns the count of the signals that came.
    int waitForUpdate(QSignalSpy* pSpy) {
        pSpy->wait(kWaitMillis);
        return pSpy->count();
    }

    std::unique_ptr<ControlObject> m_pNumDecks;
};

TEST_F(RelatedDeckWatcherTest, onlyADeckWithATrackCounts) {
    RelatedDeckWatcher watcher(nullptr, &storage(), kSettleDelayMillis);
    EXPECT_TRUE(watcher.deckTracks().isEmpty());

    const TrackPointer pTrack = addTrack(QStringLiteral("-png.mp3"));
    ASSERT_TRUE(pTrack);
    loadDeck(2, pTrack);

    const DeckTrackList deckTracks = watcher.deckTracks();
    ASSERT_EQ(1, deckTracks.size());
    EXPECT_EQ(2, deckTracks.at(0).deckNumber);
    EXPECT_EQ(pTrack->getId(), deckTracks.at(0).trackId);

    // An eject takes the deck out.
    loadDeck(2, TrackPointer());
    EXPECT_TRUE(watcher.deckTracks().isEmpty());
}

TEST_F(RelatedDeckWatcherTest, aDeckAboveTheDeckCountStaysOut) {
    const TrackPointer pTrack = addTrack(QStringLiteral("-png.mp3"));
    ASSERT_TRUE(pTrack);
    loadDeck(2, pTrack);

    RelatedDeckWatcher watcher(nullptr, &storage(), kSettleDelayMillis);
    ASSERT_EQ(1, watcher.deckTracks().size());

    m_pNumDecks->set(1.0);
    EXPECT_TRUE(watcher.deckTracks().isEmpty());
}

TEST_F(RelatedDeckWatcherTest, aWatcherThatIsNotActiveReportsNothing) {
    RelatedDeckWatcher watcher(nullptr, &storage(), kSettleDelayMillis);
    QSignalSpy spy(&watcher, &RelatedDeckWatcher::updateNeeded);
    ASSERT_TRUE(spy.isValid());
    ASSERT_FALSE(watcher.isActive());

    watcher.requestUpdate();
    EXPECT_EQ(0, waitForUpdate(&spy));

    // The panel comes back. It reads the table without a wait.
    watcher.setActive(true);
    EXPECT_EQ(1, spy.count());

    // A second time it has nothing to catch up on.
    watcher.setActive(false);
    watcher.setActive(true);
    EXPECT_EQ(1, spy.count());
}

TEST_F(RelatedDeckWatcherTest, aDeckLoadAndARelationAskForOneRead) {
    RelatedDeckWatcher watcher(nullptr, &storage(), kSettleDelayMillis);
    watcher.setActive(true);
    QSignalSpy spy(&watcher, &RelatedDeckWatcher::updateNeeded);
    ASSERT_TRUE(spy.isValid());

    const TrackPointer pTrack1 = addTrack(QStringLiteral("-png.mp3"));
    const TrackPointer pTrack2 = addTrack(QStringLiteral("-jpg.mp3"));
    ASSERT_TRUE(pTrack2);
    // Two loads that follow each other take one read of the table.
    loadDeck(1, pTrack1);
    loadDeck(2, pTrack2);
    EXPECT_EQ(1, waitForUpdate(&spy));

    spy.clear();
    ASSERT_TRUE(storage().saveRelation(
            muxic::TrackRelation(pTrack1->getId(), pTrack2->getId())));
    EXPECT_EQ(1, waitForUpdate(&spy));
}

} // namespace
