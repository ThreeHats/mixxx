#include <gtest/gtest.h>

#include <QByteArray>
#include <QVector>

#include "audio/types.h"
#include "track/beats.h"
#include "track/bpm.h"

using namespace mixxx;

namespace {

constexpr auto kBpm = Bpm(120.0);
constexpr auto kSampleRate = audio::SampleRate(48000);
constexpr auto kStartPosition = audio::FramePos(4800);

/// The frames that one beat of `kBpm` carries.
const double kBeatFrames = 60.0 * kSampleRate.value() / kBpm.value();

BeatsPointer makeConstTempoBeats() {
    return Beats::fromConstTempo(kSampleRate, kStartPosition, kBpm);
}

/// A grid of 32 beats where the beats 8 to 15 are half as fast, thus the grid
/// holds tempo markers and serializes as a beat map.
BeatsPointer makeVariableTempoBeats() {
    QVector<audio::FramePos> positions;
    audio::FramePos position = kStartPosition;
    for (int i = 0; i < 32; i++) {
        positions.append(position);
        position += (i >= 8 && i < 16) ? 2 * kBeatFrames : kBeatFrames;
    }
    return Beats::fromBeatPositions(kSampleRate, positions);
}

BeatsPointer roundTrip(const BeatsPointer& pBeats) {
    return Beats::fromByteArray(kSampleRate,
            pBeats->getVersion(),
            pBeats->getSubVersion(),
            pBeats->toByteArray());
}

TEST(BarPhaseTest, TheOffsetWrapsIntoTheBar) {
    EXPECT_EQ(0, BarPhase(0, 4).downbeatOffset());
    EXPECT_EQ(3, BarPhase(3, 4).downbeatOffset());
    EXPECT_EQ(0, BarPhase(4, 4).downbeatOffset());
    EXPECT_EQ(1, BarPhase(9, 4).downbeatOffset());
    EXPECT_EQ(3, BarPhase(-1, 4).downbeatOffset());
    EXPECT_EQ(1, BarPhase(-7, 4).downbeatOffset());
}

TEST(BarPhaseTest, TheBeatInBarCountsFromOne) {
    const auto phase = BarPhase(2, 4);
    EXPECT_EQ(3, phase.beatInBar(0));
    EXPECT_EQ(4, phase.beatInBar(1));
    EXPECT_EQ(1, phase.beatInBar(2));
    EXPECT_EQ(2, phase.beatInBar(3));
    EXPECT_EQ(1, phase.beatInBar(6));
    // A beat before the anchor gets a negative index.
    EXPECT_EQ(1, phase.beatInBar(-2));
    EXPECT_EQ(4, phase.beatInBar(-3));
    EXPECT_TRUE(phase.isDownbeat(2));
    EXPECT_FALSE(phase.isDownbeat(3));
}

TEST(BarPhaseTest, ABeatsObjectHasNoPhaseByDefault) {
    const auto pBeats = makeConstTempoBeats();
    ASSERT_NE(nullptr, pBeats);
    EXPECT_FALSE(pBeats->barPhase().has_value());
    EXPECT_EQ(0, pBeats->beatInBar(0));
    EXPECT_EQ(0, pBeats->beatInBarAt(kStartPosition));
    EXPECT_FALSE(pBeats->tryShiftBarPhase(1).has_value());
}

TEST(BarPhaseTest, SetDownbeatNearTakesTheClosestBeat) {
    const auto pBeats = makeConstTempoBeats();
    // A position just after the beat with the index 5.
    const auto position = kStartPosition + 5 * kBeatFrames + 10;
    const auto pWithPhase = pBeats->trySetDownbeatNear(position);
    ASSERT_TRUE(pWithPhase.has_value());
    ASSERT_TRUE((*pWithPhase)->barPhase().has_value());
    EXPECT_EQ(1, (*pWithPhase)->barPhase()->downbeatOffset());
    EXPECT_EQ(4, (*pWithPhase)->barPhase()->beatsPerBar());
    EXPECT_EQ(1, (*pWithPhase)->beatInBarAt(position));
    EXPECT_EQ(2, (*pWithPhase)->beatInBarAt(position + kBeatFrames));
    EXPECT_EQ(4, (*pWithPhase)->beatInBarAt(position - kBeatFrames));
}

TEST(BarPhaseTest, ShiftMovesThePhaseAndWraps) {
    const auto pBeats = makeConstTempoBeats()->withBarPhase(BarPhase(0, 4));
    const auto pLater = pBeats->tryShiftBarPhase(1);
    ASSERT_TRUE(pLater.has_value());
    EXPECT_EQ(1, (*pLater)->barPhase()->downbeatOffset());

    const auto pEarlier = pBeats->tryShiftBarPhase(-1);
    ASSERT_TRUE(pEarlier.has_value());
    EXPECT_EQ(3, (*pEarlier)->barPhase()->downbeatOffset());
}

TEST(BarPhaseTest, ABeatInBarFollowsTheGrid) {
    const auto pBeats = makeConstTempoBeats()->withBarPhase(BarPhase(2, 4));
    for (int i = -8; i < 16; i++) {
        const auto position = kStartPosition + i * kBeatFrames;
        EXPECT_EQ(pBeats->beatInBar(i), pBeats->beatInBarAt(position))
                << "at the beat " << i;
        // A position inside the beat reports the same place in the bar.
        EXPECT_EQ(pBeats->beatInBar(i), pBeats->beatInBarAt(position + kBeatFrames / 2))
                << "inside the beat " << i;
    }
}

TEST(BarPhaseTest, AConstTempoGridKeepsThePhaseOverASaveAndALoad) {
    const auto pBeats = makeConstTempoBeats()->withBarPhase(BarPhase(3, 4));
    ASSERT_EQ(QStringLiteral(BEAT_GRID_2_VERSION), pBeats->getVersion());

    const auto pLoaded = roundTrip(pBeats);
    ASSERT_NE(nullptr, pLoaded);
    ASSERT_TRUE(pLoaded->barPhase().has_value());
    EXPECT_EQ(3, pLoaded->barPhase()->downbeatOffset());
    EXPECT_EQ(4, pLoaded->barPhase()->beatsPerBar());
    EXPECT_EQ(*pBeats, *pLoaded);
}

TEST(BarPhaseTest, AVariableTempoGridKeepsThePhaseOverASaveAndALoad) {
    const auto pBeats = makeVariableTempoBeats();
    ASSERT_NE(nullptr, pBeats);
    ASSERT_FALSE(pBeats->hasConstantTempo());
    ASSERT_EQ(QStringLiteral(BEAT_MAP_VERSION), pBeats->getVersion());

    const auto pWithPhase = pBeats->withBarPhase(BarPhase(2, 4));
    const auto pLoaded = roundTrip(pWithPhase);
    ASSERT_NE(nullptr, pLoaded);
    ASSERT_TRUE(pLoaded->barPhase().has_value());
    EXPECT_EQ(2, pLoaded->barPhase()->downbeatOffset());
    // The anchor beat survives the round trip, thus the downbeats stay on the
    // same beats of the grid.
    for (int i = 0; i < 24; i++) {
        EXPECT_EQ(pWithPhase->beatInBar(i), pLoaded->beatInBar(i)) << "at the beat " << i;
    }
}

TEST(BarPhaseTest, AGridWithNoPhaseWritesNoPhase) {
    const auto pBeats = makeConstTempoBeats();
    const auto pLoaded = roundTrip(pBeats);
    ASSERT_NE(nullptr, pLoaded);
    EXPECT_FALSE(pLoaded->barPhase().has_value());
}

TEST(BarPhaseTest, ABlobOfAnOlderMixxxReadsWithNoPhase) {
    // A blob that a Mixxx without this feature wrote holds no phase field.
    const auto pBeats = makeConstTempoBeats();
    const QByteArray oldBlob = pBeats->toByteArray();
    const auto pWithPhase = pBeats->withBarPhase(BarPhase(1, 4));
    const QByteArray newBlob = pWithPhase->toByteArray();
    EXPECT_GT(newBlob.size(), oldBlob.size());

    const auto pLoaded = Beats::fromByteArray(
            kSampleRate, pBeats->getVersion(), QString(), oldBlob);
    ASSERT_NE(nullptr, pLoaded);
    EXPECT_FALSE(pLoaded->barPhase().has_value());
}

TEST(BarPhaseTest, ATranslateKeepsThePhase) {
    const auto pBeats = makeConstTempoBeats()->withBarPhase(BarPhase(1, 4));
    const auto pMoved = pBeats->tryTranslate(kBeatFrames / 4);
    ASSERT_TRUE(pMoved.has_value());
    ASSERT_TRUE((*pMoved)->barPhase().has_value());
    EXPECT_EQ(1, (*pMoved)->barPhase()->downbeatOffset());
    // The downbeat sits on the same beat of the grid, thus it moved with it.
    const auto downbeat = kStartPosition + kBeatFrames + kBeatFrames / 4;
    EXPECT_EQ(1, (*pMoved)->beatInBarAt(downbeat));
}

TEST(BarPhaseTest, ATranslateByWholeBeatsKeepsThePhase) {
    const auto pBeats = makeConstTempoBeats()->withBarPhase(BarPhase(1, 4));
    const auto pMoved = pBeats->tryTranslateBeats(2.0);
    ASSERT_TRUE(pMoved.has_value());
    ASSERT_TRUE((*pMoved)->barPhase().has_value());
    EXPECT_EQ(1, (*pMoved)->barPhase()->downbeatOffset());
}

TEST(BarPhaseTest, ABpmSetKeepsThePhase) {
    const auto pBeats = makeConstTempoBeats()->withBarPhase(BarPhase(2, 4));
    const auto pChanged = pBeats->trySetBpm(Bpm(128.0));
    ASSERT_TRUE(pChanged.has_value());
    ASSERT_TRUE((*pChanged)->barPhase().has_value());
    EXPECT_EQ(2, (*pChanged)->barPhase()->downbeatOffset());
}

TEST(BarPhaseTest, ADoubleAndAHalveMoveThePhaseWithTheGrid) {
    // Doubling the BPM makes two beats out of each beat, thus the beat with
    // the index 1 becomes the beat with the index 2.
    const auto pBeats = makeConstTempoBeats()->withBarPhase(BarPhase(1, 4));
    const auto pDouble = pBeats->tryScale(Beats::BpmScale::Double);
    ASSERT_TRUE(pDouble.has_value());
    ASSERT_TRUE((*pDouble)->barPhase().has_value());
    EXPECT_EQ(2, (*pDouble)->barPhase()->downbeatOffset());
    EXPECT_EQ(1, (*pDouble)->beatInBarAt(kStartPosition + kBeatFrames));

    const auto pHalve = (*pDouble)->tryScale(Beats::BpmScale::Halve);
    ASSERT_TRUE(pHalve.has_value());
    ASSERT_TRUE((*pHalve)->barPhase().has_value());
    EXPECT_EQ(1, (*pHalve)->barPhase()->downbeatOffset());
    EXPECT_EQ(1, (*pHalve)->beatInBarAt(kStartPosition + kBeatFrames));
}

TEST(BarPhaseTest, AScaleWithNoWholeBeatsRoundsThePhase) {
    // Three beats become four, thus the beat with the index 3 becomes the
    // beat with the index 4, which is a downbeat again.
    const auto pBeats = makeConstTempoBeats()->withBarPhase(BarPhase(3, 4));
    const auto pScaled = pBeats->tryScale(Beats::BpmScale::FourThirds);
    ASSERT_TRUE(pScaled.has_value());
    ASSERT_TRUE((*pScaled)->barPhase().has_value());
    EXPECT_EQ(0, (*pScaled)->barPhase()->downbeatOffset());
}

TEST(BarPhaseTest, AGridWithNoPhaseStaysWithNoPhaseAfterAScale) {
    const auto pBeats = makeConstTempoBeats();
    const auto pScaled = pBeats->tryScale(Beats::BpmScale::Double);
    ASSERT_TRUE(pScaled.has_value());
    EXPECT_FALSE((*pScaled)->barPhase().has_value());
}

} // namespace
