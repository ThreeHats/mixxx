#include "muxic/downbeats/downbeatdetector.h"

#include <gtest/gtest.h>

#include <QVector>
#include <algorithm>
#include <cmath>
#include <vector>

#include "audio/types.h"
#include "util/math.h"

using namespace mixxx;

namespace {

constexpr auto kSampleRate = audio::SampleRate(48000);
constexpr int kBeatsPerBar = 4;
constexpr int kBars = 14;

/// The bass note of each bar, in hertz. A bar line changes the note, thus the
/// spectrum of the beat after it differs from the spectrum before it.
constexpr double kBarNotes[] = {110.0, 146.83, 164.81, 130.81};

/// One track of test audio, with the beats that made it.
struct TestTrack {
    std::vector<CSAMPLE> samples;
    QVector<audio::FramePos> beats;
};

/// Build a click track of `bars` bars at `bpm`, in two channels. Each beat
/// carries a kick, the first beat of a bar carries a louder one, and a bass
/// note runs through the bar. `pickupBeats` beats come before the first
/// downbeat. With `withBars` false the note changes every beat in a pattern
/// that no bar line fits, thus the track has no bar structure.
TestTrack makeClickTrack(double bpm, int pickupBeats, bool withBars) {
    const double beatFrames = 60.0 * kSampleRate.value() / bpm;
    const int beatCount = pickupBeats + kBars * kBeatsPerBar;
    TestTrack track;
    track.samples.reserve(
            static_cast<std::size_t>(beatCount * beatFrames) * 2);

    double notePhase = 0.0;
    for (int beat = 0; beat < beatCount; beat++) {
        // The beat 0 of a bar is the downbeat. The pickup beats come before
        // the first bar, thus they count backwards.
        const int beatInBar = ((beat - pickupBeats) % kBeatsPerBar + kBeatsPerBar) % kBeatsPerBar;
        const int bar = (beat - pickupBeats) / kBeatsPerBar;
        const double noteHz = withBars
                ? kBarNotes[((bar % 4) + 4) % 4]
                : kBarNotes[(beat * 3 + beat / 5) % 4];
        const bool accent = withBars && beatInBar == 0;

        const int frames = static_cast<int>(
                std::llround((beat + 1) * beatFrames) -
                std::llround(beat * beatFrames));
        for (int i = 0; i < frames; i++) {
            const double t = i / static_cast<double>(kSampleRate.value());
            const double kickEnv = std::exp(-t * 40.0);
            const double kick = (accent ? 1.0 : 0.45) * kickEnv *
                    std::sin(2.0 * M_PI * 55.0 * t);
            notePhase += 2.0 * M_PI * noteHz / kSampleRate.value();
            const double note = 0.35 * std::sin(notePhase);
            const auto value = static_cast<CSAMPLE>(0.5 * (kick + note));
            track.samples.push_back(value);
            track.samples.push_back(value);
        }
        track.beats.append(audio::FramePos(std::llround(beat * beatFrames)));
    }
    return track;
}

DownbeatPhase detect(const TestTrack& track) {
    DownbeatDetector detector(kSampleRate, kBeatsPerBar);
    // Hand the audio over in blocks of an odd size, as the analyzer does.
    constexpr SINT kChunk = 4000;
    const auto total = static_cast<SINT>(track.samples.size());
    for (SINT offset = 0; offset < total; offset += kChunk) {
        const SINT count = std::min(kChunk, total - offset);
        detector.processSamples(track.samples.data() + offset, count, 2);
    }
    return detector.finalize(track.beats);
}

TEST(DownbeatDetectorTest, TheScoreTakesThePhaseWithTheLargestChange) {
    // The change into every fourth beat is large, thus the first downbeat is
    // the beat with the index 2.
    std::vector<double> beatSd;
    for (int i = 0; i < 40; i++) {
        beatSd.push_back((i + 1) % 4 == 2 ? 1.0 : 0.1);
    }
    const DownbeatPhase phase = DownbeatDetector::scorePhases(beatSd, 4);
    EXPECT_EQ(2, phase.phase);
    EXPECT_DOUBLE_EQ(1.0, phase.confidence);
}

TEST(DownbeatDetectorTest, AFlatScoreGivesNoConfidence) {
    // Every fourth transition is large, but they fall on no single phase.
    const std::vector<double> beatSd{
            1.0, 0.1, 0.1, 0.1, 0.1, 1.0, 0.1, 0.1, 0.1, 0.1, 1.0, 0.1,
            0.1, 1.0, 0.1, 0.1, 1.0, 0.1, 0.1, 0.1, 0.1, 0.1, 1.0, 0.1};
    const DownbeatPhase phase = DownbeatDetector::scorePhases(beatSd, 4);
    EXPECT_LT(phase.confidence, 0.5);
}

TEST(DownbeatDetectorTest, AShortListGivesNoPhase) {
    const std::vector<double> beatSd{1.0, 0.1, 0.1, 0.1};
    const DownbeatPhase phase = DownbeatDetector::scorePhases(beatSd, 4);
    EXPECT_DOUBLE_EQ(0.0, phase.confidence);
}

TEST(DownbeatDetectorTest, AShortTrackGivesNoPhase) {
    DownbeatDetector detector(kSampleRate, kBeatsPerBar);
    QVector<audio::FramePos> beats;
    for (int i = 0; i < 8; i++) {
        beats.append(audio::FramePos(i * 24000));
    }
    const DownbeatPhase phase = detector.finalize(beats);
    EXPECT_DOUBLE_EQ(0.0, phase.confidence);
}

class DownbeatPickupTest : public testing::TestWithParam<int> {};

TEST_P(DownbeatPickupTest, TheDetectorFindsThePhaseAfterAPickup) {
    const int pickupBeats = GetParam();
    for (const double bpm : {120.0, 128.0, 140.0, 174.0}) {
        const TestTrack track = makeClickTrack(bpm, pickupBeats, true);
        const DownbeatPhase phase = detect(track);
        EXPECT_EQ(pickupBeats, phase.phase)
                << "at " << bpm << " beats per minute with " << pickupBeats
                << " pickup beats";
        EXPECT_GE(phase.confidence, DownbeatDetector::kMinConfidence)
                << "at " << bpm << " beats per minute with " << pickupBeats
                << " pickup beats";
    }
}

INSTANTIATE_TEST_SUITE_P(AllPickups, DownbeatPickupTest, testing::Values(0, 1, 2, 3));

TEST(DownbeatDetectorTest, ATrackWithNoBarStructureGivesALowConfidence) {
    const TestTrack track = makeClickTrack(128.0, 0, false);
    const DownbeatPhase phase = detect(track);
    EXPECT_LT(phase.confidence, DownbeatDetector::kMinConfidence);
}

} // namespace
