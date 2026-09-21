#include <gtest/gtest.h>

#include <QVector>

#include "control/controlobject.h"
#include "test/signalpathtest.h"
#include "track/beats.h"
#include "track/track.h"

namespace {

constexpr double kTrackBpm = 120.0;
constexpr int kBeatsPerBar = 4;

class DownbeatEngineTest : public BaseSignalPathTest {
  protected:
    TrackPointer loadTrack(int downbeatOffset, bool withPhase) {
        TrackPointer pTrack = m_pMixerDeck1->loadFakeTrack(false, kTrackBpm);
        ProcessBuffer();
        const mixxx::BeatsPointer pBeats = pTrack->getBeats();
        EXPECT_NE(nullptr, pBeats);
        if (pBeats && withPhase) {
            pTrack->trySetBeats(pBeats->withBarPhase(
                    mixxx::BarPhase(downbeatOffset, kBeatsPerBar)));
        }
        return pTrack;
    }

    int beatInBar() const {
        return static_cast<int>(ControlObject::get(
                ConfigKey(m_sGroup1, QStringLiteral("beat_in_bar"))));
    }

    void set(const QString& key, double value) {
        ControlObject::set(ConfigKey(m_sGroup1, key), value);
    }

    /// Plays `count` buffers and keeps each value of `beat_in_bar` that
    /// differs from the value before it.
    QVector<int> playAndCollect(int count) {
        QVector<int> values;
        values.append(beatInBar());
        for (int i = 0; i < count; i++) {
            ProcessBuffer();
            if (beatInBar() != values.last()) {
                values.append(beatInBar());
            }
        }
        return values;
    }
};

TEST_F(DownbeatEngineTest, AGridWithNoPhaseReportsZero) {
    loadTrack(0, false);
    set(QStringLiteral("play"), 1.0);
    for (int i = 0; i < 200; i++) {
        ProcessBuffer();
        ASSERT_EQ(0, beatInBar()) << "at the buffer " << i;
    }
}

TEST_F(DownbeatEngineTest, TheValueWalksThroughTheBar) {
    loadTrack(0, true);
    ProcessBuffer();
    EXPECT_EQ(1, beatInBar());

    set(QStringLiteral("play"), 1.0);
    const QVector<int> values = playAndCollect(400);
    ASSERT_GE(values.size(), 8);
    for (int i = 1; i < values.size(); i++) {
        EXPECT_EQ(values.at(i - 1) % kBeatsPerBar + 1, values.at(i))
                << "at the change " << i;
    }
}

TEST_F(DownbeatEngineTest, AnOffsetMovesTheFirstBeatOfTheBar) {
    loadTrack(1, true);
    ProcessBuffer();
    // The beat with the index 1 is the downbeat, thus the beat with the
    // index 0 is the last beat of the bar before it.
    EXPECT_EQ(kBeatsPerBar, beatInBar());
}

TEST_F(DownbeatEngineTest, AOneBeatLoopHoldsTheValue) {
    loadTrack(0, true);
    set(QStringLiteral("play"), 1.0);
    playAndCollect(60);
    set(QStringLiteral("beatloop_1_activate"), 1.0);
    ProcessBuffer();

    const int inLoop = beatInBar();
    ASSERT_NE(0, inLoop);
    // 400 buffers of 512 frames at 44100 Hz are 4.6 seconds, thus the loop
    // wraps about nine times.
    for (int i = 0; i < 400; i++) {
        ProcessBuffer();
        ASSERT_EQ(inLoop, beatInBar()) << "at the buffer " << i;
    }
}

TEST_F(DownbeatEngineTest, AFourBeatLoopWalksTheBar) {
    loadTrack(0, true);
    set(QStringLiteral("play"), 1.0);
    playAndCollect(60);
    set(QStringLiteral("beatloop_4_activate"), 1.0);
    ProcessBuffer();

    const QVector<int> values = playAndCollect(400);
    ASSERT_GE(values.size(), 8);
    for (int i = 1; i < values.size(); i++) {
        EXPECT_EQ(values.at(i - 1) % kBeatsPerBar + 1, values.at(i))
                << "at the change " << i;
    }
}

TEST_F(DownbeatEngineTest, ASeekKeepsTheValueOnTheGrid) {
    loadTrack(0, true);
    set(QStringLiteral("play"), 1.0);
    const QVector<int> values = playAndCollect(300);
    ASSERT_GE(values.size(), 4);
    set(QStringLiteral("play"), 0.0);
    ProcessBuffer();

    // The fake track holds ten beats of 120 beats per minute, thus the beat
    // with the index `beat` sits at the tenth `beat` of the track.
    for (const int beat : {5, 0, 7, 2, 9, 3}) {
        set(QStringLiteral("playposition"), beat / 10.0);
        ProcessBuffer();
        EXPECT_EQ(beat % kBeatsPerBar + 1, beatInBar())
                << "after a seek to the beat " << beat;
    }
}

TEST_F(DownbeatEngineTest, SetDownbeatMakesTheBeatAtThePositionTheFirstOne) {
    // The deck waits on the first beat of the track, and the bar starts one
    // beat later, thus the first beat is the last beat of a bar.
    TrackPointer pTrack = loadTrack(1, true);
    ProcessBuffer();
    ASSERT_EQ(kBeatsPerBar, beatInBar());

    set(QStringLiteral("beats_set_downbeat"), 1.0);
    ProcessBuffer();
    ASSERT_TRUE(pTrack->getBeats()->barPhase().has_value());
    EXPECT_EQ(0, pTrack->getBeats()->barPhase()->downbeatOffset());
    EXPECT_EQ(1, beatInBar());
}

TEST_F(DownbeatEngineTest, TheDownbeatButtonsMoveThePhase) {
    TrackPointer pTrack = loadTrack(0, true);
    ProcessBuffer();
    ASSERT_EQ(1, beatInBar());

    set(QStringLiteral("beats_downbeat_later"), 1.0);
    ProcessBuffer();
    EXPECT_EQ(1, pTrack->getBeats()->barPhase()->downbeatOffset());
    EXPECT_EQ(kBeatsPerBar, beatInBar());

    set(QStringLiteral("beats_downbeat_earlier"), 1.0);
    ProcessBuffer();
    EXPECT_EQ(0, pTrack->getBeats()->barPhase()->downbeatOffset());
    EXPECT_EQ(1, beatInBar());
}

TEST_F(DownbeatEngineTest, TheDownbeatButtonsDoNothingWithNoPhase) {
    TrackPointer pTrack = loadTrack(0, false);
    ProcessBuffer();
    ASSERT_FALSE(pTrack->getBeats()->barPhase().has_value());

    set(QStringLiteral("beats_downbeat_later"), 1.0);
    ProcessBuffer();
    EXPECT_FALSE(pTrack->getBeats()->barPhase().has_value());
    EXPECT_EQ(0, beatInBar());
}

} // namespace
