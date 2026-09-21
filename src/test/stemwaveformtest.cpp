#include <gtest/gtest.h>

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <algorithm>
#include <array>
#include <cmath>

#include "analyzer/analyzerebur128.h"
#include "analyzer/analyzertrack.h"
#include "analyzer/analyzerwaveform.h"
#include "audio/types.h"
#include "sources/soundsourceproxy.h"
#include "stems/stemconversionjob.h"
#include "stems/stemconversionsettings.h"
#include "test/mixxxtest.h"
#include "track/track.h"
#include "util/samplebuffer.h"
#include "waveform/renderers/stemwaveformscale.h"
#include "waveform/waveform.h"

namespace {

constexpr int kSampleRate = 44100;
constexpr int kFrameCount = kSampleRate;
constexpr double kToneHz = 440.0;

// The four parts sum to the mix. The loudest part reaches 40 % of the mix,
// which is the level that a separation model gives to a drum stem.
constexpr double kMixAmplitude = 0.98;
const std::array<double, 4> kStemAmplitudeRatio{0.40, 0.30, 0.20, 0.10};

/// The peak of each band of a waveform, in the 0 to 255 scale of the analyzer.
struct WaveformPeaks {
    int all = 0;
    std::array<int, mixxx::kMaxSupportedStems> stems{};

    int loudestStem() const {
        return *std::max_element(stems.cbegin(), stems.cend());
    }
};

WaveformPeaks peaksOf(ConstWaveformPointer pWaveform) {
    WaveformPeaks peaks;
    if (pWaveform.isNull()) {
        return peaks;
    }
    for (int i = 0; i < pWaveform->getDataSize(); ++i) {
        const WaveformData& datum = pWaveform->get(i);
        peaks.all = std::max(peaks.all, static_cast<int>(datum.filtered.all));
        for (int stemIdx = 0; stemIdx < mixxx::kMaxSupportedStems; ++stemIdx) {
            peaks.stems[stemIdx] = std::max(peaks.stems[stemIdx],
                    static_cast<int>(datum.stems[stemIdx]));
        }
    }
    return peaks;
}

/// Write a stereo 16 bit WAV file that holds one tone.
bool writeToneWav(const QString& filePath, double amplitude, int frameCount) {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    const quint32 dataSize = static_cast<quint32>(frameCount) * 4;
    const auto writeU32 = [&file](quint32 value) {
        char bytes[4] = {static_cast<char>(value & 0xFF),
                static_cast<char>((value >> 8) & 0xFF),
                static_cast<char>((value >> 16) & 0xFF),
                static_cast<char>((value >> 24) & 0xFF)};
        file.write(bytes, 4);
    };
    const auto writeU16 = [&file](quint16 value) {
        char bytes[2] = {static_cast<char>(value & 0xFF),
                static_cast<char>((value >> 8) & 0xFF)};
        file.write(bytes, 2);
    };
    file.write("RIFF", 4);
    writeU32(36 + dataSize);
    file.write("WAVEfmt ", 8);
    writeU32(16);
    writeU16(1);
    writeU16(2);
    writeU32(static_cast<quint32>(kSampleRate));
    writeU32(static_cast<quint32>(kSampleRate) * 4);
    writeU16(4);
    writeU16(16);
    file.write("data", 4);
    writeU32(dataSize);
    for (int frame = 0; frame < frameCount; ++frame) {
        const auto sample = static_cast<qint16>(std::lround(amplitude * 32767.0 *
                std::sin(2.0 * M_PI * kToneHz * frame / kSampleRate)));
        writeU16(static_cast<quint16>(sample));
        writeU16(static_cast<quint16>(sample));
    }
    file.close();
    return true;
}

QString muxerPathOrEmpty() {
    return QStandardPaths::findExecutable(QStringLiteral("MP4Box"));
}

class StemWaveformTest : public MixxxTest {
  protected:
    /// Run the waveform analyzer over a buffer and give the peak of each band.
    WaveformPeaks analyzeBuffer(const std::vector<CSAMPLE>& buffer,
            mixxx::audio::ChannelCount channelCount) {
        TrackPointer pTrack = Track::newTemporary();
        const SINT frameCount = static_cast<SINT>(buffer.size()) / channelCount;
        pTrack->setAudioProperties(channelCount,
                mixxx::audio::SampleRate(kSampleRate),
                mixxx::audio::Bitrate(),
                mixxx::Duration::fromSeconds(
                        static_cast<double>(frameCount) / kSampleRate));
        AnalyzerWaveform analyzer(config(), QSqlDatabase());
        EXPECT_TRUE(analyzer.initialize(AnalyzerTrack(pTrack),
                mixxx::audio::SampleRate(kSampleRate),
                channelCount,
                frameCount));
        EXPECT_TRUE(analyzer.processSamples(
                buffer.data(), static_cast<SINT>(buffer.size())));
        analyzer.storeResults(pTrack);
        analyzer.cleanup();
        return peaksOf(pTrack->getWaveform());
    }

    /// Decode a file, run the waveform analyzer and give the peak of each band.
    WaveformPeaks analyzeFile(const QString& filePath,
            mixxx::audio::ChannelCount channelCount) {
        TrackPointer pTrack = Track::newTemporary(filePath);
        mixxx::AudioSource::OpenParams params;
        params.setChannelCount(channelCount);
        const auto pSource = SoundSourceProxy(pTrack).openAudioSource(params);
        EXPECT_NE(nullptr, pSource);
        if (!pSource) {
            return WaveformPeaks();
        }
        EXPECT_EQ(channelCount, pSource->getSignalInfo().getChannelCount());
        mixxx::SampleBuffer buffer(pSource->frameLength() * channelCount);
        const auto readable = pSource->readSampleFrames(
                mixxx::WritableSampleFrames(pSource->frameIndexRange(),
                        mixxx::SampleBuffer::WritableSlice(
                                buffer.data(), buffer.size())));
        std::vector<CSAMPLE> samples(readable.readableData(),
                readable.readableData() + readable.readableLength());
        return analyzeBuffer(samples, channelCount);
    }

    /// Decode a file, run the EBU R128 analyzer and give the ReplayGain ratio.
    double replayGainOfFile(const QString& filePath,
            mixxx::audio::ChannelCount channelCount) {
        TrackPointer pTrack = Track::newTemporary(filePath);
        mixxx::AudioSource::OpenParams params;
        params.setChannelCount(channelCount);
        const auto pSource = SoundSourceProxy(pTrack).openAudioSource(params);
        EXPECT_NE(nullptr, pSource);
        if (!pSource) {
            return 0.0;
        }
        mixxx::SampleBuffer buffer(pSource->frameLength() * channelCount);
        const auto readable = pSource->readSampleFrames(
                mixxx::WritableSampleFrames(pSource->frameIndexRange(),
                        mixxx::SampleBuffer::WritableSlice(
                                buffer.data(), buffer.size())));
        AnalyzerEbur128 analyzer(config());
        EXPECT_TRUE(analyzer.initialize(AnalyzerTrack(pTrack),
                mixxx::audio::SampleRate(kSampleRate),
                channelCount,
                pSource->frameLength()));
        EXPECT_TRUE(analyzer.processSamples(
                readable.readableData(), readable.readableLength()));
        analyzer.storeResults(pTrack);
        analyzer.cleanup();
        return pTrack->getReplayGain().getRatio();
    }

    /// Run the conversion job over a tone and give the path of the stem file.
    /// Each stem holds the same tone at its share of the level of the source.
    QString makeStemFile(const QString& workDirPath) {
        const QString sourceFilePath =
                QDir(workDirPath).filePath(QStringLiteral("tone.wav"));
        EXPECT_TRUE(writeToneWav(sourceFilePath, kMixAmplitude, kFrameCount));
        TrackPointer pSource = Track::newTemporary(sourceFilePath);
        pSource->setAudioProperties(mixxx::audio::ChannelCount::stereo(),
                mixxx::audio::SampleRate(kSampleRate),
                mixxx::audio::Bitrate(),
                mixxx::Duration::fromSeconds(1));

        const QDir stemDir(QDir(workDirPath).filePath(QStringLiteral("stems")));
        EXPECT_TRUE(QDir().mkpath(stemDir.absolutePath()));
        const QStringList stemNames{QStringLiteral("drums"),
                QStringLiteral("bass"),
                QStringLiteral("other"),
                QStringLiteral("vocals")};
        for (int stemIdx = 0; stemIdx < stemNames.size(); ++stemIdx) {
            EXPECT_TRUE(writeToneWav(
                    stemDir.absoluteFilePath(stemNames.at(stemIdx) +
                            QStringLiteral(".wav")),
                    kMixAmplitude * kStemAmplitudeRatio[stemIdx],
                    kFrameCount));
        }

        const QString separatorScript =
                getTestDir().filePath(QStringLiteral("stems/fake_separator.sh"));
        mixxx::StemConversionSettings settings;
        settings.setSeparatorCommand(
                QStringLiteral("/bin/sh \"%1\" from $MODEL $OUTPUT_DIR "
                               "\"$INPUT\" \"%2\"")
                        .arg(separatorScript, stemDir.absolutePath()));
        settings.setModel(QStringLiteral("testmodel"));
        settings.setEncoderCommand(QString());
        settings.setMuxerPath(muxerPathOrEmpty());
        settings.setOutputMode(mixxx::StemOutputMode::CustomDirectory);
        settings.setOutputDirectory(workDirPath);

        mixxx::StemConversionJob job(settings, pSource);
        QSignalSpy spy(&job, &mixxx::StemConversionJob::finished);
        job.start();
        EXPECT_TRUE(spy.count() == 1 || spy.wait(120000));
        EXPECT_EQ(mixxx::StemConversionJob::State::Succeeded, job.state())
                << job.errorMessage().toStdString();

        const QString stemFilePath =
                QDir(workDirPath).filePath(QStringLiteral("tone.stem.mp4"));
        EXPECT_TRUE(QFile::exists(stemFilePath));
        EXPECT_TRUE(SoundSourceProxy::isFileTypeSupported(
                            QStringLiteral("stem.mp4")) ||
                SoundSourceProxy::registerProviders());
        return stemFilePath;
    }
};

} // anonymous namespace

// The all band of a stem track holds the level of the mix, because the
// analyzer mixes the eight channels down. The stem bands hold one part.
TEST_F(StemWaveformTest, AnalyzerGivesTheStemTrackTheLevelOfTheMix) {
    constexpr int kStereoSamples = kFrameCount * 2;
    std::vector<CSAMPLE> mix(kStereoSamples);
    std::vector<CSAMPLE> stems(kFrameCount * mixxx::audio::ChannelCount::stem());
    for (int frame = 0; frame < kFrameCount; ++frame) {
        const auto tone = static_cast<CSAMPLE>(kMixAmplitude *
                std::sin(2.0 * M_PI * kToneHz * frame / kSampleRate));
        mix[frame * 2] = tone;
        mix[frame * 2 + 1] = tone;
        for (int stemIdx = 0; stemIdx < 4; ++stemIdx) {
            const auto part = static_cast<CSAMPLE>(
                    kStemAmplitudeRatio[stemIdx] * tone);
            stems[frame * 8 + stemIdx * 2] = part;
            stems[frame * 8 + stemIdx * 2 + 1] = part;
        }
    }

    const WaveformPeaks mixPeaks = analyzeBuffer(
            mix, mixxx::audio::ChannelCount::stereo());
    const WaveformPeaks stemPeaks = analyzeBuffer(
            stems, mixxx::audio::ChannelCount::stem());

    // The conversion loses nothing: the all band is the same in both files.
    EXPECT_NEAR(mixPeaks.all, stemPeaks.all, 1);
    EXPECT_EQ(0, mixPeaks.loudestStem());
    // The loudest stem holds 40 % of the mix, thus a renderer that draws only
    // the stem bands draws the track 2.5 times too small.
    EXPECT_NEAR(stemPeaks.all * kStemAmplitudeRatio[0],
            stemPeaks.stems[0],
            2);
    EXPECT_LT(stemPeaks.loudestStem(), stemPeaks.all / 2);
}

// The scale makes the tallest of the overlaid stems as tall as the waveform
// of the same music in a file with no stems.
TEST_F(StemWaveformTest, StemOverlayScaleLiftsTheLoudestStemToTheMix) {
    EXPECT_FLOAT_EQ(2.5f, mixxx::stemOverlayScale(255, 102));
    EXPECT_FLOAT_EQ(1.0f, mixxx::stemOverlayScale(255, 255));
    // A mix that cancels its own parts must not grow.
    EXPECT_FLOAT_EQ(0.5f, mixxx::stemOverlayScale(100, 200));
    // Silence must not divide by zero.
    EXPECT_FLOAT_EQ(1.0f, mixxx::stemOverlayScale(0, 0));
    EXPECT_FLOAT_EQ(1.0f, mixxx::stemOverlayScale(200, 0));
}

// WaveformRendererStem draws heightFactor * peak * scale and
// WaveformRendererRGB draws heightFactor * all. Both must give one height.
TEST_F(StemWaveformTest, StemStripReachesTheHeightOfTheNormalWaveform) {
    // One strip of the measured values: the mix reaches 250 and the four
    // stems reach 100, 75, 50 and 25.
    constexpr int kStemCount = 4;
    std::array<WaveformData, 2> strip{};
    for (WaveformData& datum : strip) {
        datum.filtered.all = 250;
        datum.stems[0] = 100;
        datum.stems[1] = 75;
        datum.stems[2] = 50;
        datum.stems[3] = 25;
    }

    const mixxx::StemStripPeaks peaks = mixxx::stemStripPeaks(
            strip.data(), 0, 2, kStemCount);
    EXPECT_EQ(250, peaks.all);
    EXPECT_EQ(100, peaks.loudestStem(kStemCount));

    // The height factor of every signal renderer, for a widget of 80 pixels.
    constexpr float kHeightFactor = 40.0f / 255.0f;
    const float normalHeight = kHeightFactor * static_cast<float>(peaks.all);
    const float scale = mixxx::stemOverlayScale(
            peaks.all, peaks.loudestStem(kStemCount));
    const float stemHeight = kHeightFactor *
            static_cast<float>(peaks.loudestStem(kStemCount)) * scale;
    EXPECT_FLOAT_EQ(normalHeight, stemHeight);

    // The stems keep their size relative to each other.
    const float bassHeight = kHeightFactor *
            static_cast<float>(peaks.stems[1]) * scale;
    EXPECT_FLOAT_EQ(0.75f, bassHeight / stemHeight);
}

// The whole conversion pipeline, with the real ffmpeg and the real MP4Box.
// The encode and the mux must keep the level of every stream.
TEST_F(StemWaveformTest, ConvertedStemFileKeepsTheLevelOfTheSource) {
    if (muxerPathOrEmpty().isEmpty()) {
        GTEST_SKIP() << "MP4Box is not installed";
    }
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString stemFilePath = makeStemFile(dir.path());
    const QString sourceFilePath = dir.filePath(QStringLiteral("tone.wav"));

    const WaveformPeaks sourcePeaks = analyzeFile(
            sourceFilePath, mixxx::audio::ChannelCount::stereo());
    const WaveformPeaks stemPeaks = analyzeFile(
            stemFilePath, mixxx::audio::ChannelCount::stem());

    // The mux keeps the level: the all band of the stem file is the level of
    // the source file.
    EXPECT_NEAR(sourcePeaks.all, stemPeaks.all, 3);
    // The stem bands hold one part alone, thus they are much smaller.
    EXPECT_NEAR(stemPeaks.all * kStemAmplitudeRatio[0], stemPeaks.stems[0], 4);
    EXPECT_LT(stemPeaks.loudestStem(), stemPeaks.all / 2);
}

// libebur128 reads more than two channels as a surround signal. Without a
// mix down it drops the vocals and gives other a weight of 1.41.
TEST_F(StemWaveformTest, ReplayGainOfAStemFileIsTheReplayGainOfTheSource) {
    if (muxerPathOrEmpty().isEmpty()) {
        GTEST_SKIP() << "MP4Box is not installed";
    }
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString stemFilePath = makeStemFile(dir.path());
    const QString sourceFilePath = dir.filePath(QStringLiteral("tone.wav"));

    const double sourceRatio = replayGainOfFile(
            sourceFilePath, mixxx::audio::ChannelCount::stereo());
    const double stemRatio = replayGainOfFile(
            stemFilePath, mixxx::audio::ChannelCount::stem());

    ASSERT_GT(sourceRatio, 0.0);
    ASSERT_GT(stemRatio, 0.0);
    const double dbOff = 20.0 * std::log10(stemRatio / sourceRatio);
    EXPECT_LT(std::abs(dbOff), 0.5) << "the stem file gets a ReplayGain that "
                                    << "is " << dbOff << " dB off the source";
}
