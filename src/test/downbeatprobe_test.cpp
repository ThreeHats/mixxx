#include <gtest/gtest.h>

#include <QDir>
#include <QFileInfo>
#include <cstdio>
#include <dsp/tempotracking/DownBeat.h>
#include <vector>

#include "analyzer/plugins/analyzerqueenmarybeats.h"
#include "muxic/downbeats/downbeatdetector.h"
#include "sources/soundsourceproxy.h"
#include "test/mixxxtest.h"
#include "track/track.h"
#include "util/samplebuffer.h"

namespace {

// A probe on real audio files: MUXIC_DOWNBEAT_PROBE names a directory of
// files. The test skips when the variable is absent.
class DownbeatProbeTest : public MixxxTest {};

TEST_F(DownbeatProbeTest, RealTracks) {
    const QString dir = qEnvironmentVariable("MUXIC_DOWNBEAT_PROBE");
    if (dir.isEmpty()) {
        GTEST_SKIP() << "MUXIC_DOWNBEAT_PROBE is not set";
    }
    SoundSourceProxy::registerProviders();
    const QStringList files = QDir(dir).entryList(QDir::Files, QDir::Name);
    for (const QString& name : files) {
        const QString path = QDir(dir).filePath(name);
        TrackPointer pTrack = Track::newTemporary(path);
        mixxx::AudioSource::OpenParams params;
        params.setChannelCount(mixxx::audio::ChannelCount(2));
        const auto pSource = SoundSourceProxy(pTrack).openAudioSource(params);
        if (!pSource) {
            std::printf("%s: cannot open\n", qPrintable(name));
            continue;
        }
        const auto sampleRate = pSource->getSignalInfo().getSampleRate();
        mixxx::SampleBuffer buffer(pSource->frameLength() * 2);
        const auto readable = pSource->readSampleFrames(
                mixxx::WritableSampleFrames(pSource->frameIndexRange(),
                        mixxx::SampleBuffer::WritableSlice(
                                buffer.data(), buffer.size())));
        const CSAMPLE* pData = readable.readableData();
        const SINT length = readable.readableLength();

        mixxx::AnalyzerQueenMaryBeats beats;
        ASSERT_TRUE(beats.initialize(sampleRate));
        mixxx::DownbeatDetector detector(sampleRate, 4);
        constexpr SINT kChunk = 4096 * 2;
        for (SINT offset = 0; offset < length; offset += kChunk) {
            const SINT count = std::min(kChunk, length - offset);
            beats.processSamples(pData + offset, count);
            detector.processSamples(pData + offset, count, 2);
        }
        beats.finalize();
        const QVector<mixxx::audio::FramePos> positions = beats.getBeats();

        // The tracker's own answer.
        std::vector<double> beatBlocks;
        for (const auto& p : positions) {
            beatBlocks.push_back(p.value() / 1024.0);
        }
        DownBeat tracker(static_cast<float>(sampleRate.value()), 16, 1024);
        tracker.setBeatsPerBar(4);
        std::vector<float> mono;
        mono.reserve(length / 2);
        for (SINT i = 0; i + 1 < length; i += 2) {
            mono.push_back((pData[i] + pData[i + 1]) * 0.5f);
        }
        std::vector<float> block(1024, 0.0f);
        for (size_t i = 0; i + 1024 <= mono.size(); i += 1024) {
            std::copy(mono.begin() + i, mono.begin() + i + 1024, block.begin());
            tracker.pushAudioBlock(block.data());
        }
        size_t audioLength = 0;
        const float* pAudio = tracker.getBufferedAudio(audioLength);
        std::vector<int> downbeats;
        tracker.findDownBeats(pAudio, audioLength, beatBlocks, downbeats);
        int histogram[4] = {0, 0, 0, 0};
        for (int index : downbeats) {
            histogram[index % 4]++;
        }
        std::vector<double> beatSd;
        tracker.getBeatSD(beatSd);
        double sdByPhase[4] = {0, 0, 0, 0};
        int sdCount[4] = {0, 0, 0, 0};
        for (size_t i = 0; i < beatSd.size(); i++) {
            sdByPhase[(i + 1) % 4] += beatSd[i];
            sdCount[(i + 1) % 4]++;
        }

        // Stability: the winner of the per-phase mean beatSD in each quarter.
        {
            const int n = static_cast<int>(beatSd.size());
            std::printf("  quarter winners:");
            for (int q = 0; q < 4; q++) {
                double m[4] = {0, 0, 0, 0};
                int c[4] = {0, 0, 0, 0};
                for (int i = q * n / 4; i < (q + 1) * n / 4; i++) {
                    m[(i + 1) % 4] += beatSd[i];
                    c[(i + 1) % 4]++;
                }
                int best = 0;
                for (int k = 1; k < 4; k++) {
                    if (m[k] / std::max(1, c[k]) > m[best] / std::max(1, c[best])) {
                        best = k;
                    }
                }
                std::printf(" %d(%.3f/%.3f)", best, m[best] / std::max(1, c[best]),
                        (m[0] + m[1] + m[2] + m[3]) / std::max(1, n / 4));
            }
            std::printf("\n");
        }
        const mixxx::DownbeatPhase phase = detector.finalize(positions);
        std::printf("%s: %d beats at %.0f Hz\n",
                qPrintable(name),
                static_cast<int>(positions.size()),
                static_cast<double>(sampleRate.value()));
        std::printf("  tracker downbeats mod 4: %d %d %d %d (of %d)\n",
                histogram[0],
                histogram[1],
                histogram[2],
                histogram[3],
                static_cast<int>(downbeats.size()));
        std::printf("  mean beatSD into phase: %.3f %.3f %.3f %.3f\n",
                sdByPhase[0] / std::max(1, sdCount[0]),
                sdByPhase[1] / std::max(1, sdCount[1]),
                sdByPhase[2] / std::max(1, sdCount[2]),
                sdByPhase[3] / std::max(1, sdCount[3]));
        std::printf("  fork vote: accepted %d phase %d confidence %.3f\n",
                phase.accepted ? 1 : 0,
                phase.phase,
                phase.confidence);
        // Low band energy in the 60 ms after each beat, per phase.
        double low[4] = {0, 0, 0, 0};
        int lowCount[4] = {0, 0, 0, 0};
        const SINT window = static_cast<SINT>(sampleRate.value() * 0.06);
        for (int b = 0; b < positions.size(); b++) {
            const SINT start = static_cast<SINT>(positions[b].value());
            if (start + window >= static_cast<SINT>(mono.size())) {
                break;
            }
            // A crude low pass: the mean of 64 frame sums, squared.
            double energy = 0;
            for (SINT i = start; i + 64 < start + window; i += 64) {
                double sum = 0;
                for (SINT k = 0; k < 64; k++) {
                    sum += mono[i + k];
                }
                energy += sum * sum;
            }
            low[b % 4] += energy;
            lowCount[b % 4]++;
        }
        std::printf("  low band after beat by phase: %.3g %.3g %.3g %.3g\n",
                low[0] / std::max(1, lowCount[0]),
                low[1] / std::max(1, lowCount[1]),
                low[2] / std::max(1, lowCount[2]),
                low[3] / std::max(1, lowCount[3]));
        // Energy step contrast at 32-beat boundaries per phase.
        std::vector<double> beatEnergy;
        for (int b = 0; b + 1 < positions.size(); b++) {
            const SINT s = static_cast<SINT>(positions[b].value());
            const SINT e = std::min(static_cast<SINT>(positions[b + 1].value()),
                    static_cast<SINT>(mono.size()));
            double energy = 0;
            for (SINT i = s; i < e; i++) {
                energy += mono[i] * mono[i];
            }
            beatEnergy.push_back(energy / std::max<SINT>(1, e - s));
        }
        double step[4] = {0, 0, 0, 0};
        for (size_t b = 32; b < beatEnergy.size(); b++) {
            step[b % 4] += std::abs(beatEnergy[b] - beatEnergy[b - 1]);
        }
        std::printf("  energy step at beat by phase: %.3g %.3g %.3g %.3g\n",
                step[0], step[1], step[2], step[3]);
    }
}

} // namespace
