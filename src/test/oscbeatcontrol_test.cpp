#include <chrono>

#include "control/controlobject.h"
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
        // The beat is inside the buffer that the engine has just made, thus
        // its instant is between the first and the last frame of that buffer.
        // The slack covers the two reads of the clock, one in the test and
        // one in the engine.
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

    // The engine runs faster than real time in a test, thus the wall clock
    // says nothing. Each buffer is one buffer period of playing time, so the
    // period between two beats is the buffers between them plus the move of
    // the stamp inside its own buffer.
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

} // namespace
