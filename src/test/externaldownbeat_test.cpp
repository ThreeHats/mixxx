#include <gtest/gtest.h>

#include <QDir>
#include <QElapsedTimer>
#include <QString>
#include <QStringLiteral>
#include <QVector>
#include <vector>

#include "audio/types.h"
#include "muxic/downbeats/externaldownbeatdetector.h"
#include "test/mixxxtest.h"
#include "track/beats.h"

using namespace mixxx;

namespace {

constexpr auto kSampleRate = audio::SampleRate(48000);
constexpr int kBeatsPerBar = 4;
constexpr double kBpm = 128.0;
/// 128 beats per minute give this many frames for each beat at 48 kHz.
constexpr double kBeatFrames = 60.0 * 48000.0 / kBpm;
/// The fake detector writes a list of this length.
constexpr int kFakeBeats = 200;

QString fakeDetectorPath() {
    return MixxxTest::getOrInitTestDir().filePath(
            QStringLiteral("downbeat-test-data/fake-detector.sh"));
}

/// A command template that runs the fake detector in `mode`.
QString fakeCommand(const QString& mode, bool withOutputFile = true) {
    return QStringLiteral("\"%1\" %2 \"$INPUT\"%3")
            .arg(fakeDetectorPath(),
                    mode,
                    withOutputFile ? QStringLiteral(" \"$OUTPUT\"") : QString());
}

/// A beat grid of `beats` beats at 128 beats per minute, from the frame 0.
QVector<audio::FramePos> makeGrid(int beats) {
    QVector<audio::FramePos> positions;
    positions.reserve(beats);
    for (int beat = 0; beat < beats; beat++) {
        positions.append(audio::FramePos(beat * kBeatFrames));
    }
    return positions;
}

ExternalDownbeatSettings externalSettings(const QString& command, int timeoutSeconds = 30) {
    ExternalDownbeatSettings settings;
    settings.setDetector(DownbeatDetectorChoice::ExternalCommand);
    settings.setCommand(command);
    settings.setTimeoutSeconds(timeoutSeconds);
    return settings;
}

TEST(ExternalDownbeatParseTest, ReadsTheBeatThisFormat) {
    const QString output = QStringLiteral(
            "0.02\t1\n0.48\t2\n0.94\t3\n1.42\t4\n1.88\t1\n2.36\t2\n");
    const std::vector<double> times = parseDownbeatTimes(output);
    ASSERT_EQ(2u, times.size());
    EXPECT_DOUBLE_EQ(0.02, times[0]);
    EXPECT_DOUBLE_EQ(1.88, times[1]);
}

TEST(ExternalDownbeatParseTest, SkipsAMalformedLine) {
    const QString output = QStringLiteral(
            "0.5\t1\n"
            "this is not a beat\n"
            "\n"
            "1.5\n"
            "x\t1\n"
            "2.5\tnine\n"
            "-1.0\t1\n"
            "3.5\t1\n");
    const std::vector<double> times = parseDownbeatTimes(output);
    ASSERT_EQ(2u, times.size());
    EXPECT_DOUBLE_EQ(0.5, times[0]);
    EXPECT_DOUBLE_EQ(3.5, times[1]);
}

TEST(ExternalDownbeatParseTest, AnEmptyOutputGivesNoTime) {
    EXPECT_TRUE(parseDownbeatTimes(QString()).empty());
    EXPECT_TRUE(parseDownbeatTimes(QStringLiteral("\n\n  \n")).empty());
}

TEST(ExternalDownbeatParseTest, ReadsSpacesAndCommasAsWell) {
    const QString output = QStringLiteral("0.5 1\n1.0,2\n1.5;1\n");
    const std::vector<double> times = parseDownbeatTimes(output);
    ASSERT_EQ(2u, times.size());
    EXPECT_DOUBLE_EQ(0.5, times[0]);
    EXPECT_DOUBLE_EQ(1.5, times[1]);
}

TEST(ExternalDownbeatMapTest, ATimeBetweenTwoBeatsGoesToTheNearest) {
    const QVector<audio::FramePos> grid = makeGrid(8);
    const double beatSeconds = kBeatFrames / kSampleRate.value();
    // Just after the beat 3, just before the beat 5, and on the beat 6.
    const std::vector<double> times{
            3 * beatSeconds + 0.01,
            5 * beatSeconds - 0.01,
            6 * beatSeconds};
    const std::vector<int> indices = mapTimesToBeats(times, grid, kSampleRate);
    ASSERT_EQ(3u, indices.size());
    EXPECT_EQ(3, indices[0]);
    EXPECT_EQ(5, indices[1]);
    EXPECT_EQ(6, indices[2]);
}

TEST(ExternalDownbeatMapTest, ATimeInTheMiddleGoesToTheBeatBefore) {
    const QVector<audio::FramePos> grid = makeGrid(8);
    const double beatSeconds = kBeatFrames / kSampleRate.value();
    const std::vector<double> times{2.5 * beatSeconds};
    const std::vector<int> indices = mapTimesToBeats(times, grid, kSampleRate);
    ASSERT_EQ(1u, indices.size());
    EXPECT_EQ(2, indices[0]);
}

TEST(ExternalDownbeatMapTest, ATimeOutsideTheGridIsDropped) {
    QVector<audio::FramePos> grid = makeGrid(8);
    // Move the grid one second into the track.
    for (int beat = 0; beat < grid.size(); beat++) {
        grid[beat] = audio::FramePos(grid.at(beat).value() + kSampleRate.value());
    }
    const double lastSeconds = grid.last().value() / kSampleRate.value();
    const std::vector<double> times{0.1, 0.9, 1.0, lastSeconds, lastSeconds + 0.5};
    const std::vector<int> indices = mapTimesToBeats(times, grid, kSampleRate);
    ASSERT_EQ(2u, indices.size());
    EXPECT_EQ(0, indices[0]);
    EXPECT_EQ(grid.size() - 1, indices[1]);
}

TEST(ExternalDownbeatMapTest, AGridOfLessThanTwoBeatsMapsNothing) {
    EXPECT_TRUE(mapTimesToBeats({1.0}, makeGrid(1), kSampleRate).empty());
    EXPECT_TRUE(mapTimesToBeats({1.0}, QVector<audio::FramePos>(), kSampleRate).empty());
}

TEST(ExternalDownbeatVoteTest, ACleanListTakesThePhase) {
    std::vector<int> beatIndices;
    for (int bar = 0; bar < 40; bar++) {
        beatIndices.push_back(2 + bar * kBeatsPerBar);
    }
    const std::vector<int> votes = barPhaseVotes(beatIndices, kBeatsPerBar);
    ASSERT_EQ(4u, votes.size());
    EXPECT_EQ(40, votes[2]);
    const DownbeatPhase phase = DownbeatDetector::scoreVotes(votes);
    EXPECT_TRUE(phase.accepted);
    EXPECT_EQ(2, phase.phase);
    EXPECT_DOUBLE_EQ(1.0, phase.confidence);
}

TEST(ExternalDownbeatVoteTest, AScatteredListTakesNoPhase) {
    // A downbeat every five beats walks through all four places of the bar.
    std::vector<int> beatIndices;
    for (int downbeat = 0; downbeat < 40; downbeat++) {
        beatIndices.push_back(downbeat * 5);
    }
    const std::vector<int> votes = barPhaseVotes(beatIndices, kBeatsPerBar);
    const DownbeatPhase phase = DownbeatDetector::scoreVotes(votes);
    EXPECT_FALSE(phase.accepted);
}

TEST(ExternalDownbeatVoteTest, AFewVotesTakeNoPhase) {
    // Sixteen bars are the fewest that give a phase, thus fifteen clean bars
    // still give none.
    EXPECT_FALSE(DownbeatDetector::scoreVotes({15, 0, 0, 0}).accepted);
    EXPECT_TRUE(DownbeatDetector::scoreVotes({16, 0, 0, 0}).accepted);
    EXPECT_FALSE(DownbeatDetector::scoreVotes({0, 0, 0, 0}).accepted);
}

TEST(ExternalDownbeatVoteTest, TheGivenPhaseWinsOverTheLargestCount) {
    const std::vector<int> votes{5, 40, 5, 5};
    EXPECT_EQ(1, DownbeatDetector::scoreVotes(votes).phase);
    EXPECT_EQ(2, DownbeatDetector::scoreVotes(votes, 2).phase);
    EXPECT_FALSE(DownbeatDetector::scoreVotes(votes, 2).accepted);
}

TEST(ExternalDownbeatSettingsTest, TheDefaultsKeepTheBuiltInDetector) {
    const ExternalDownbeatSettings settings;
    EXPECT_EQ(DownbeatDetectorChoice::BuiltIn, settings.detector());
    EXPECT_EQ(ExternalDownbeatSettings::kDefaultTimeoutSeconds,
            settings.timeoutSeconds());
    EXPECT_TRUE(settings.command().contains(QStringLiteral("$INPUT")));
    EXPECT_TRUE(settings.command().contains(QStringLiteral("$OUTPUT")));
}

TEST(ExternalDownbeatSettingsTest, TheTimeoutStaysInItsRange) {
    ExternalDownbeatSettings settings;
    settings.setTimeoutSeconds(0);
    EXPECT_EQ(ExternalDownbeatSettings::kMinTimeoutSeconds, settings.timeoutSeconds());
    settings.setTimeoutSeconds(100000);
    EXPECT_EQ(ExternalDownbeatSettings::kMaxTimeoutSeconds, settings.timeoutSeconds());
}

class ExternalDownbeatRunTest : public MixxxTest {};

TEST_F(ExternalDownbeatRunTest, TheSettingsSurviveTheConfigFile) {
    ExternalDownbeatSettings settings;
    settings.setDetector(DownbeatDetectorChoice::ExternalCommand);
    settings.setCommand(QStringLiteral("detector \"$INPUT\""));
    settings.setTimeoutSeconds(42);
    settings.writeTo(config());

    const ExternalDownbeatSettings read = ExternalDownbeatSettings::readFrom(config());
    EXPECT_EQ(DownbeatDetectorChoice::ExternalCommand, read.detector());
    EXPECT_EQ(QStringLiteral("detector \"$INPUT\""), read.command());
    EXPECT_EQ(42, read.timeoutSeconds());
}

TEST_F(ExternalDownbeatRunTest, TheFakeDetectorFindsThePhase) {
    ExternalDownbeatDetector detector(
            externalSettings(fakeCommand(QStringLiteral("good"))), kBeatsPerBar);
    const DownbeatPhase phase = detector.detect(
            QStringLiteral("/nonexistent/track.flac"), makeGrid(kFakeBeats), kSampleRate);
    EXPECT_TRUE(phase.accepted) << detector.errorMessage().toStdString();
    EXPECT_EQ(2, phase.phase);
    EXPECT_DOUBLE_EQ(1.0, phase.confidence);
    EXPECT_EQ(kFakeBeats / kBeatsPerBar, detector.downbeatCount());
}

TEST_F(ExternalDownbeatRunTest, TheDetectorReadsTheStandardOutput) {
    ExternalDownbeatDetector detector(
            externalSettings(fakeCommand(QStringLiteral("good"), false)), kBeatsPerBar);
    const DownbeatPhase phase = detector.detect(
            QStringLiteral("/nonexistent/track.flac"), makeGrid(kFakeBeats), kSampleRate);
    EXPECT_TRUE(phase.accepted) << detector.errorMessage().toStdString();
    EXPECT_EQ(2, phase.phase);
}

TEST_F(ExternalDownbeatRunTest, AShiftedListGivesThePhaseZero) {
    ExternalDownbeatDetector detector(
            externalSettings(fakeCommand(QStringLiteral("shifted"))), kBeatsPerBar);
    const DownbeatPhase phase = detector.detect(
            QStringLiteral("/nonexistent/track.flac"), makeGrid(kFakeBeats), kSampleRate);
    EXPECT_TRUE(phase.accepted) << detector.errorMessage().toStdString();
    EXPECT_EQ(0, phase.phase);
}

TEST_F(ExternalDownbeatRunTest, AScatteredListGivesNoPhase) {
    ExternalDownbeatDetector detector(
            externalSettings(fakeCommand(QStringLiteral("scatter"))), kBeatsPerBar);
    const DownbeatPhase phase = detector.detect(
            QStringLiteral("/nonexistent/track.flac"), makeGrid(kFakeBeats), kSampleRate);
    EXPECT_FALSE(phase.accepted);
    EXPECT_FALSE(detector.errorMessage().isEmpty());
}

TEST_F(ExternalDownbeatRunTest, AnEmptyOutputGivesNoPhase) {
    ExternalDownbeatDetector detector(
            externalSettings(fakeCommand(QStringLiteral("empty"))), kBeatsPerBar);
    EXPECT_FALSE(detector
                         .detect(QStringLiteral("/nonexistent/track.flac"),
                                 makeGrid(kFakeBeats),
                                 kSampleRate)
                         .accepted);
    EXPECT_FALSE(detector.errorMessage().isEmpty());
    EXPECT_EQ(0, detector.downbeatCount());
}

TEST_F(ExternalDownbeatRunTest, GarbageGivesNoPhase) {
    ExternalDownbeatDetector detector(
            externalSettings(fakeCommand(QStringLiteral("garbage"))), kBeatsPerBar);
    EXPECT_FALSE(detector
                         .detect(QStringLiteral("/nonexistent/track.flac"),
                                 makeGrid(kFakeBeats),
                                 kSampleRate)
                         .accepted);
    EXPECT_EQ(0, detector.downbeatCount());
}

TEST_F(ExternalDownbeatRunTest, AFailingCommandGivesAnError) {
    ExternalDownbeatDetector detector(
            externalSettings(fakeCommand(QStringLiteral("fail"))), kBeatsPerBar);
    EXPECT_FALSE(detector
                         .detect(QStringLiteral("/nonexistent/track.flac"),
                                 makeGrid(kFakeBeats),
                                 kSampleRate)
                         .accepted);
    EXPECT_TRUE(detector.errorMessage().contains(QStringLiteral("3")))
            << detector.errorMessage().toStdString();
}

TEST_F(ExternalDownbeatRunTest, AnUnknownProgramGivesAnError) {
    ExternalDownbeatDetector detector(
            externalSettings(QStringLiteral("muxic-no-such-program \"$INPUT\"")),
            kBeatsPerBar);
    EXPECT_FALSE(detector
                         .detect(QStringLiteral("/nonexistent/track.flac"),
                                 makeGrid(kFakeBeats),
                                 kSampleRate)
                         .accepted);
    EXPECT_TRUE(detector.errorMessage().contains(QStringLiteral("not found")))
            << detector.errorMessage().toStdString();
}

TEST_F(ExternalDownbeatRunTest, AnEmptyCommandGivesAnError) {
    ExternalDownbeatDetector detector(externalSettings(QString()), kBeatsPerBar);
    EXPECT_FALSE(detector
                         .detect(QStringLiteral("/nonexistent/track.flac"),
                                 makeGrid(kFakeBeats),
                                 kSampleRate)
                         .accepted);
    EXPECT_FALSE(detector.errorMessage().isEmpty());
}

TEST_F(ExternalDownbeatRunTest, AShortGridRunsNoCommand) {
    ExternalDownbeatDetector detector(
            externalSettings(fakeCommand(QStringLiteral("good"))), kBeatsPerBar);
    EXPECT_FALSE(detector
                         .detect(QStringLiteral("/nonexistent/track.flac"),
                                 makeGrid(DownbeatDetector::kMinBeats - 1),
                                 kSampleRate)
                         .accepted);
    EXPECT_EQ(0, detector.downbeatCount());
}

TEST_F(ExternalDownbeatRunTest, TheTimeoutStopsASlowCommand) {
    ExternalDownbeatDetector detector(
            externalSettings(fakeCommand(QStringLiteral("slow")), 1), kBeatsPerBar);
    QElapsedTimer timer;
    timer.start();
    EXPECT_FALSE(detector
                         .detect(QStringLiteral("/nonexistent/track.flac"),
                                 makeGrid(kFakeBeats),
                                 kSampleRate)
                         .accepted);
    EXPECT_LT(timer.elapsed(), 20000);
    EXPECT_TRUE(detector.errorMessage().contains(QStringLiteral("longer")))
            << detector.errorMessage().toStdString();
}

TEST_F(ExternalDownbeatRunTest, ACancelStopsASlowCommand) {
    ExternalDownbeatDetector detector(
            externalSettings(fakeCommand(QStringLiteral("slow")), 600), kBeatsPerBar);
    detector.setCancelCheck([]() {
        return true;
    });
    QElapsedTimer timer;
    timer.start();
    EXPECT_FALSE(detector
                         .detect(QStringLiteral("/nonexistent/track.flac"),
                                 makeGrid(kFakeBeats),
                                 kSampleRate)
                         .accepted);
    EXPECT_LT(timer.elapsed(), 20000);
    EXPECT_TRUE(detector.errorMessage().contains(QStringLiteral("stopped")))
            << detector.errorMessage().toStdString();
}

TEST_F(ExternalDownbeatRunTest, TheOrderTakesTheCommandFirst) {
    bool builtInRan = false;
    const auto builtIn = [&builtInRan]() {
        builtInRan = true;
        DownbeatPhase phase;
        phase.phase = 1;
        phase.accepted = true;
        return phase;
    };
    const DownbeatResult result = runDownbeatDetectors(
            externalSettings(fakeCommand(QStringLiteral("good"))),
            QStringLiteral("/nonexistent/track.flac"),
            makeGrid(kFakeBeats),
            kSampleRate,
            nullptr,
            builtIn);
    EXPECT_FALSE(builtInRan);
    EXPECT_EQ(QStringLiteral("external"), result.source);
    EXPECT_TRUE(result.phase.accepted);
    EXPECT_EQ(2, result.phase.phase);
}

TEST_F(ExternalDownbeatRunTest, TheOrderFallsBackToTheBuiltInDetector) {
    const auto builtIn = []() {
        DownbeatPhase phase;
        phase.phase = 1;
        phase.confidence = 0.8;
        phase.accepted = true;
        return phase;
    };
    const DownbeatResult result = runDownbeatDetectors(
            externalSettings(fakeCommand(QStringLiteral("fail"))),
            QStringLiteral("/nonexistent/track.flac"),
            makeGrid(kFakeBeats),
            kSampleRate,
            nullptr,
            builtIn);
    EXPECT_EQ(QStringLiteral("built in"), result.source);
    EXPECT_TRUE(result.phase.accepted);
    EXPECT_EQ(1, result.phase.phase);
    EXPECT_FALSE(result.message.isEmpty());
}

TEST_F(ExternalDownbeatRunTest, TheBuiltInChoiceRunsNoCommand) {
    bool builtInRan = false;
    ExternalDownbeatSettings settings =
            externalSettings(fakeCommand(QStringLiteral("good")));
    settings.setDetector(DownbeatDetectorChoice::BuiltIn);
    const DownbeatResult result = runDownbeatDetectors(settings,
            QStringLiteral("/nonexistent/track.flac"),
            makeGrid(kFakeBeats),
            kSampleRate,
            nullptr,
            [&builtInRan]() {
                builtInRan = true;
                return DownbeatPhase();
            });
    EXPECT_TRUE(builtInRan);
    EXPECT_EQ(QStringLiteral("built in"), result.source);
    EXPECT_TRUE(result.message.isEmpty());
}

TEST_F(ExternalDownbeatRunTest, NoDetectorLeavesTheSourceEmpty) {
    ExternalDownbeatSettings settings;
    settings.setDetector(DownbeatDetectorChoice::BuiltIn);
    const DownbeatResult result = runDownbeatDetectors(settings,
            QStringLiteral("/nonexistent/track.flac"),
            makeGrid(kFakeBeats),
            kSampleRate,
            nullptr,
            nullptr);
    EXPECT_TRUE(result.source.isEmpty());
    EXPECT_FALSE(result.phase.accepted);
}

TEST_F(ExternalDownbeatRunTest, TheGridOfATrackHoldsOnlyItsOwnBeats) {
    const auto pBeats = Beats::fromConstTempo(
            kSampleRate, audio::FramePos(kBeatFrames / 2), Bpm(kBpm));
    ASSERT_TRUE(pBeats);
    const audio::FramePos end{10 * kBeatFrames};
    const QVector<audio::FramePos> positions = gridBeatPositions(*pBeats, end);
    ASSERT_EQ(10, positions.size());
    EXPECT_NEAR(kBeatFrames / 2, positions.first().value(), 1.0);
    EXPECT_LE(positions.last().value(), end.value());
    for (int beat = 1; beat < positions.size(); beat++) {
        EXPECT_GT(positions.at(beat).value(), positions.at(beat - 1).value());
    }
    EXPECT_TRUE(gridBeatPositions(*pBeats, audio::kStartFramePos).isEmpty());
}

} // namespace
