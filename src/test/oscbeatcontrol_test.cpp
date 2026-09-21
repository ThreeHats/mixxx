#include <chrono>

#include "control/controlobject.h"
#include "engine/channels/enginedeck.h"
#include "osc/oscbeatfeed.h"
#include "test/signalpathtest.h"
#include "util/performancetimer.h"
#include "waveform/visualplayposition.h"

namespace {

using mixxx::osc::BeatEvent;
using mixxx::osc::BeatFeed;

constexpr double kSampleRate = 44100.0;
constexpr double kTrackBpm = 120.0;
/// What a sound card of this rig adds between the audio callback and the
/// first frame that leaves the outputs.
constexpr double kDacDelaySecs = 0.021;

qint64 monotonicNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
}

/// One beat with the time of the audio callback that reported it.
struct StampedBeat {
    BeatEvent event;
    int bufferIndex;
    qint64 callbackEntryNs;
};

class OscBeatControlTest : public BaseSignalPathTest {
  protected:
    void SetUp() override {
        BaseSignalPathTest::SetUp();
        ControlObject::set(ConfigKey(QStringLiteral("[App]"),
                                   QStringLiteral("samplerate")),
                kSampleRate);
        BeatFeed::clear();
        BeatFeed::setEnabled(true);
    }

    void TearDown() override {
        BeatFeed::setEnabled(false);
        BeatFeed::clear();
        BaseSignalPathTest::TearDown();
    }

    /// Runs the engine and keeps each beat that it reports.
    QVector<StampedBeat> playBuffers(int count) {
        QVector<StampedBeat> beats;
        for (int index = 0; index < count; index++) {
            PerformanceTimer entry;
            entry.start();
            const qint64 entryNs = monotonicNowNs();
            VisualPlayPosition::setCallbackEntryToDacSecs(kDacDelaySecs, entry);

            ProcessBuffer();

            BeatEvent events[8];
            const int found = BeatFeed::pop(events, 8);
            for (int i = 0; i < found; i++) {
                beats.append(StampedBeat{events[i], index, entryNs});
            }
        }
        return beats;
    }

    double bufferSecs() const {
        return (kProcessBufferSize / mixxx::kEngineChannelOutputCount) / kSampleRate;
    }
};

TEST_F(OscBeatControlTest, AStampSitsInsideTheBufferThatCarriesItsBeat) {
    m_pMixerDeck1->loadFakeTrack(false, kTrackBpm);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, QStringLiteral("play")), 1.0);

    const QVector<StampedBeat> beats = playBuffers(200);
    ASSERT_GE(beats.size(), 3);

    for (const StampedBeat& beat : beats) {
        EXPECT_QSTRING_EQ(m_sGroup1, BeatFeed::readGroup(beat.event));
        const double offsetSecs =
                (beat.event.stampNs - beat.callbackEntryNs) / 1e9 - kDacDelaySecs;
        // The instant of the beat is inside the buffer that carries it. The
        // slack covers the two reads of the clock.
        constexpr double kClockSlackSecs = 1e-6;
        EXPECT_GE(offsetSecs, -kClockSlackSecs);
        EXPECT_LE(offsetSecs, bufferSecs() + kClockSlackSecs);
        EXPECT_NEAR(kTrackBpm, beat.event.bpm, 0.01);
    }
}

TEST_F(OscBeatControlTest, TheStampsAreOneBeatApart) {
    m_pMixerDeck1->loadFakeTrack(false, kTrackBpm);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, QStringLiteral("play")), 1.0);

    const QVector<StampedBeat> beats = playBuffers(400);
    ASSERT_GE(beats.size(), 3);

    // A test runs faster than real time, thus the wall clock says nothing.
    // The period is the buffers between two beats plus the move in the buffer.
    const double expectedBeatSecs = 60.0 / kTrackBpm;
    const double frameSecs = 1.0 / kSampleRate;
    for (int i = 1; i < beats.size(); i++) {
        const StampedBeat& previous = beats.at(i - 1);
        const StampedBeat& current = beats.at(i);
        const double previousOffset =
                (previous.event.stampNs - previous.callbackEntryNs) / 1e9;
        const double currentOffset =
                (current.event.stampNs - current.callbackEntryNs) / 1e9;
        const double measured =
                (current.bufferIndex - previous.bufferIndex) * bufferSecs() +
                currentOffset - previousOffset;
        EXPECT_NEAR(expectedBeatSecs, measured, 2 * frameSecs);
        EXPECT_EQ(previous.event.trackBeat + 1, current.event.trackBeat);
        EXPECT_EQ(previous.event.seq + 1, current.event.seq);
    }
}

TEST_F(OscBeatControlTest, ADeckThatDoesNotPlayReportsNoBeat) {
    m_pMixerDeck1->loadFakeTrack(false, kTrackBpm);
    ProcessBuffer();
    BeatFeed::clear();

    EXPECT_TRUE(playBuffers(100).isEmpty());
}

TEST_F(OscBeatControlTest, ALoopReportsABeatOnEachPass) {
    m_pMixerDeck1->loadFakeTrack(false, kTrackBpm);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, QStringLiteral("play")), 1.0);
    playBuffers(60);
    ControlObject::set(ConfigKey(m_sGroup1, QStringLiteral("beatloop_1_activate")), 1.0);

    // 400 buffers of 512 frames at 44100 Hz is 4.6 seconds, thus a loop of one
    // beat at 120 beats per minute passes its beat about nine times.
    const QVector<StampedBeat> beats = playBuffers(400);
    EXPECT_GE(beats.size(), 6);
    for (int i = 1; i < beats.size(); i++) {
        EXPECT_EQ(beats.at(i - 1).event.seq + 1, beats.at(i).event.seq);
    }
}

TEST_F(OscBeatControlTest, AFourBeatLoopReportsEachBeat) {
    m_pMixerDeck1->loadFakeTrack(false, kTrackBpm);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, QStringLiteral("play")), 1.0);
    playBuffers(60);
    ControlObject::set(ConfigKey(m_sGroup1, QStringLiteral("beatloop_4_activate")), 1.0);

    const QVector<StampedBeat> beats = playBuffers(400);
    EXPECT_GE(beats.size(), 6);
    const double expectedBeatSecs = 60.0 / kTrackBpm;
    for (int i = 1; i < beats.size(); i++) {
        const StampedBeat& previous = beats.at(i - 1);
        const StampedBeat& current = beats.at(i);
        const double measured =
                (current.bufferIndex - previous.bufferIndex) * bufferSecs() +
                (current.event.stampNs - current.callbackEntryNs) / 1e9 -
                (previous.event.stampNs - previous.callbackEntryNs) / 1e9;
        // Inside the loop the beats follow each other, and the wrap of the
        // loop skips back four beats. Both are a whole number of beats.
        const double beatsBetween = measured / expectedBeatSecs;
        EXPECT_NEAR(beatsBetween, std::round(beatsBetween), 0.01);
        EXPECT_EQ(previous.event.seq + 1, current.event.seq);
    }
}

TEST_F(OscBeatControlTest, ALoopThatHoldsNoBeatReportsNothing) {
    m_pMixerDeck1->loadFakeTrack(false, kTrackBpm);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, QStringLiteral("quantize")), 0.0);
    ControlObject::set(ConfigKey(m_sGroup1, QStringLiteral("play")), 1.0);
    // Close a half beat loop between two beats of the grid.
    playBuffers(10);
    ControlObject::set(ConfigKey(m_sGroup1, QStringLiteral("beatloop_0.5_activate")), 1.0);
    playBuffers(2);

    const double beatFrames = 60.0 * kSampleRate / kTrackBpm;
    const double startFrames = ControlObject::get(ConfigKey(m_sGroup1,
                                       QStringLiteral("loop_start_position"))) /
            mixxx::kEngineChannelOutputCount;
    const double endFrames = ControlObject::get(ConfigKey(m_sGroup1,
                                     QStringLiteral("loop_end_position"))) /
            mixxx::kEngineChannelOutputCount;
    ASSERT_DOUBLE_EQ(1.0,
            ControlObject::get(ConfigKey(m_sGroup1, QStringLiteral("loop_enabled"))));
    ASSERT_GT(startFrames, 0);
    ASSERT_GT(endFrames, startFrames);
    ASSERT_LT(endFrames, beatFrames);

    playBuffers(10);
    BeatFeed::clear();
    EXPECT_TRUE(playBuffers(200).isEmpty());
}

TEST_F(OscBeatControlTest, AStampSitsInsideItsBufferWhenABeatMeetsTheEdge) {
    // 22016 frames is 43 buffers of 512 frames, thus each beat falls on the
    // first frame of a buffer.
    const double bpm = 60.0 * kSampleRate / (43.0 * kProcessBufferSize / mixxx::kEngineChannelOutputCount);
    m_pMixerDeck1->loadFakeTrack(false, bpm);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, QStringLiteral("play")), 1.0);

    const QVector<StampedBeat> beats = playBuffers(400);
    ASSERT_GE(beats.size(), 5);
    for (int i = 1; i < beats.size(); i++) {
        EXPECT_EQ(beats.at(i - 1).event.trackBeat + 1, beats.at(i).event.trackBeat);
        const double offsetSecs =
                (beats.at(i).event.stampNs - beats.at(i).callbackEntryNs) / 1e9 -
                kDacDelaySecs;
        EXPECT_GE(offsetSecs, -1e-6);
        EXPECT_LE(offsetSecs, bufferSecs() + 1e-6);
    }
}

TEST_F(OscBeatControlTest, AReverseDeckReportsNothing) {
    m_pMixerDeck1->loadFakeTrack(false, kTrackBpm);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, QStringLiteral("play")), 1.0);
    playBuffers(200);
    ControlObject::set(ConfigKey(m_sGroup1, QStringLiteral("reverse")), 1.0);
    playBuffers(10);
    BeatFeed::clear();

    EXPECT_TRUE(playBuffers(200).isEmpty());
}

TEST_F(OscBeatControlTest, ASeekReportsTheNextBeatOnlyOnce) {
    m_pMixerDeck1->loadFakeTrack(false, kTrackBpm);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, QStringLiteral("play")), 1.0);
    const QVector<StampedBeat> before = playBuffers(200);
    ASSERT_GE(before.size(), 2);

    // Go back to the start. The beat that the deck reported last comes again.
    ControlObject::set(ConfigKey(m_sGroup1, QStringLiteral("playposition")), 0.0);
    const QVector<StampedBeat> after = playBuffers(200);
    ASSERT_GE(after.size(), 2);
    for (int i = 1; i < after.size(); i++) {
        EXPECT_EQ(after.at(i - 1).event.trackBeat + 1, after.at(i).event.trackBeat);
    }
}

#ifdef __STEM__
/// A stem deck gives the engine four times as many samples per buffer, thus
/// the frame count must come from the deck and not from a stereo buffer.
class OscStemBeatControlTest : public OscBeatControlTest {
  protected:
    void SetUp() override {
        OscBeatControlTest::SetUp();
        for (int i = 1; i <= 4; i++) {
            const QString stemGroup = m_sGroup1.chopped(1) +
                    QStringLiteral("_Stem") + QChar('0' + i) + QChar(']');
            ChannelHandleAndGroup handle = m_pEngineMixer->registerChannelGroup(stemGroup);
            m_pChannel1->addStemHandle(handle);
            m_pEffectsManager->addStem(handle);
        }
    }
};

TEST_F(OscStemBeatControlTest, TheStampsOfAStemDeckAreOneBeatApart) {
    const QString location = getTestDir().filePath(
            QStringLiteral("stems/sin_AAC_256kbps_VBR.stem.mp4"));
    TrackPointer pTrack(Track::newTemporary(location));
    loadTrack(m_pMixerDeck1.get(), pTrack);
    ASSERT_GT(m_pChannel1->getEngineBuffer()->getChannelCount(),
            mixxx::kEngineChannelOutputCount);
    pTrack->trySetBpm(kTrackBpm);
    ProcessBuffer();
    ControlObject::set(ConfigKey(m_sGroup1, QStringLiteral("play")), 1.0);

    const QVector<StampedBeat> beats = playBuffers(400);
    ASSERT_GE(beats.size(), 3);
    const double expectedBeatSecs = 60.0 / kTrackBpm;
    const double frameSecs = 1.0 / kSampleRate;
    for (int i = 0; i < beats.size(); i++) {
        // A wrong frame count moves each stamp by whole buffers, which cancels
        // between two beats. Thus measure the stamp inside its own buffer.
        const double offsetSecs =
                (beats.at(i).event.stampNs - beats.at(i).callbackEntryNs) / 1e9 -
                kDacDelaySecs;
        EXPECT_GE(offsetSecs, -1e-6);
        EXPECT_LE(offsetSecs, bufferSecs() + 1e-6);
    }
    for (int i = 1; i < beats.size(); i++) {
        const StampedBeat& previous = beats.at(i - 1);
        const StampedBeat& current = beats.at(i);
        const double measured =
                (current.bufferIndex - previous.bufferIndex) * bufferSecs() +
                (current.event.stampNs - current.callbackEntryNs) / 1e9 -
                (previous.event.stampNs - previous.callbackEntryNs) / 1e9;
        EXPECT_NEAR(expectedBeatSecs, measured, 4 * frameSecs);
        EXPECT_EQ(previous.event.trackBeat + 1, current.event.trackBeat);
    }
}
#endif

} // namespace
