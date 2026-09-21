#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <cmath>

#include "sources/metadatasourcetaglib.h"
#include "sources/soundsourceproxy.h"
#include "stems/stemconversionjob.h"
#include "stems/stemconversionsettings.h"
#include "stems/stemtrackcopy.h"
#include "test/mixxxtest.h"
#include "track/beats.h"
#include "track/cue.h"
#include "track/steminfoimporter.h"
#include "track/track.h"

namespace {

constexpr int kTestSampleRate = 44100;
constexpr int kTestFrameCount = 22050;

/// Write a short stereo sine as a 16 bit WAV file.
bool writeSineWav(const QString& filePath, int sampleRate, int frameCount) {
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
    writeU32(static_cast<quint32>(sampleRate));
    writeU32(static_cast<quint32>(sampleRate) * 4);
    writeU16(4);
    writeU16(16);
    file.write("data", 4);
    writeU32(dataSize);
    for (int frame = 0; frame < frameCount; ++frame) {
        const auto sample = static_cast<qint16>(
                12000 * std::sin(2.0 * M_PI * 440.0 * frame / sampleRate));
        writeU16(static_cast<quint16>(sample));
        writeU16(static_cast<quint16>(sample));
    }
    file.close();
    return true;
}

/// Write a stereo WAV that is silent except for a short click.
bool writeClickWav(const QString& filePath,
        int sampleRate,
        int frameCount,
        int clickFrame) {
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
    writeU32(static_cast<quint32>(sampleRate));
    writeU32(static_cast<quint32>(sampleRate) * 4);
    writeU16(4);
    writeU16(16);
    file.write("data", 4);
    writeU32(dataSize);
    for (int frame = 0; frame < frameCount; ++frame) {
        const bool inClick = frame >= clickFrame && frame < clickFrame + 4;
        const auto sample = static_cast<qint16>(inClick ? 30000 : 0);
        writeU16(static_cast<quint16>(sample));
        writeU16(static_cast<quint16>(sample));
    }
    file.close();
    return true;
}

QString muxerPathOrEmpty() {
    return QStandardPaths::findExecutable(QStringLiteral("MP4Box"));
}

QString encoderPathOrEmpty() {
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

/// The first frame of a stereo buffer that reaches half of its peak. The
/// threshold follows the peak, thus a lossy codec does not move the result.
int findOnsetFrame(const mixxx::SampleBuffer& buffer) {
    CSAMPLE peak = 0;
    for (SINT sample = 0; sample < buffer.size(); sample += 2) {
        peak = std::max(peak, std::abs(buffer[sample]));
    }
    if (peak <= 0.01f) {
        return -1;
    }
    for (SINT sample = 0; sample < buffer.size(); sample += 2) {
        if (std::abs(buffer[sample]) >= peak * 0.5f) {
            return static_cast<int>(sample / 2);
        }
    }
    return -1;
}

class StemConversionTest : public MixxxTest {
  protected:
    QString fakeSeparatorCommand(const QString& mode) const {
        const QString scriptPath =
                getTestDir().filePath(QStringLiteral("stems/fake_separator.sh"));
        return QStringLiteral("/bin/sh \"%1\" %2 $MODEL $OUTPUT_DIR \"$INPUT\"")
                .arg(scriptPath, mode);
    }

    /// A separator command that copies the four files of a directory.
    QString copyingSeparatorCommand(const QString& stemDirPath) const {
        return QStringLiteral("%1 \"%2\"")
                .arg(fakeSeparatorCommand(QStringLiteral("from")), stemDirPath);
    }

    /// Run the event loop until the job reaches the state.
    static bool waitForState(const mixxx::StemConversionJob& job,
            mixxx::StemConversionJob::State state,
            int timeoutMillis = 30000) {
        QElapsedTimer timer;
        timer.start();
        while (job.state() != state && timer.elapsed() < timeoutMillis) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
            QThread::msleep(5);
        }
        return job.state() == state;
    }

    mixxx::StemConversionSettings makeSettings(const QString& mode,
            const QString& outputDirPath) const {
        mixxx::StemConversionSettings settings;
        settings.setSeparatorCommand(fakeSeparatorCommand(mode));
        settings.setModel(QStringLiteral("testmodel"));
        // An empty encode command puts the WAV files in the stem file as is.
        settings.setEncoderCommand(QString());
        settings.setMuxerPath(muxerPathOrEmpty());
        settings.setOutputMode(mixxx::StemOutputMode::CustomDirectory);
        settings.setOutputDirectory(outputDirPath);
        return settings;
    }

    TrackPointer makeSourceTrack(const QString& filePath) const {
        EXPECT_TRUE(writeSineWav(filePath, kTestSampleRate, kTestFrameCount));
        TrackPointer pTrack = Track::newTemporary(filePath);
        pTrack->setAudioProperties(mixxx::audio::ChannelCount(2),
                mixxx::audio::SampleRate(kTestSampleRate),
                mixxx::audio::Bitrate(),
                mixxx::Duration::fromSeconds(
                        static_cast<double>(kTestFrameCount) / kTestSampleRate));
        return pTrack;
    }
};

} // anonymous namespace

TEST_F(StemConversionTest, ExpandCommandTemplateSubstitutes) {
    QMap<QString, QString> placeholders;
    placeholders.insert(QStringLiteral("MODEL"), QStringLiteral("htdemucs"));
    placeholders.insert(QStringLiteral("OUTPUT_DIR"), QStringLiteral("/tmp/out"));
    placeholders.insert(QStringLiteral("INPUT"), QStringLiteral("/music/a b.mp3"));

    QString errorMessage;
    const QStringList command = mixxx::expandCommandTemplate(
            QStringLiteral("demucs -n $MODEL -o ${OUTPUT_DIR} \"$INPUT\""),
            placeholders,
            &errorMessage);

    EXPECT_TRUE(errorMessage.isEmpty());
    ASSERT_EQ(6, command.size());
    EXPECT_QSTRING_EQ(QStringLiteral("demucs"), command.at(0));
    EXPECT_QSTRING_EQ(QStringLiteral("-n"), command.at(1));
    EXPECT_QSTRING_EQ(QStringLiteral("htdemucs"), command.at(2));
    EXPECT_QSTRING_EQ(QStringLiteral("-o"), command.at(3));
    EXPECT_QSTRING_EQ(QStringLiteral("/tmp/out"), command.at(4));
    EXPECT_QSTRING_EQ(QStringLiteral("/music/a b.mp3"), command.at(5));
}

TEST_F(StemConversionTest, ExpandCommandTemplateRejectsBadInput) {
    QMap<QString, QString> placeholders;
    placeholders.insert(QStringLiteral("INPUT"), QStringLiteral("/music/a.mp3"));

    QString errorMessage;
    EXPECT_TRUE(mixxx::expandCommandTemplate(
            QStringLiteral("tool $NOPE"), placeholders, &errorMessage)
                        .isEmpty());
    EXPECT_TRUE(errorMessage.contains(QStringLiteral("NOPE")));

    EXPECT_TRUE(mixxx::expandCommandTemplate(
            QStringLiteral("tool \"$INPUT"), placeholders, &errorMessage)
                        .isEmpty());
    EXPECT_FALSE(errorMessage.isEmpty());

    EXPECT_TRUE(mixxx::expandCommandTemplate(
            QStringLiteral("   "), placeholders, &errorMessage)
                        .isEmpty());
    EXPECT_FALSE(errorMessage.isEmpty());
}

TEST_F(StemConversionTest, OutputFilePathFollowsTheMode) {
    mixxx::StemConversionSettings settings;
    settings.setOutputMode(mixxx::StemOutputMode::SourceDirectory);
    EXPECT_QSTRING_EQ(QStringLiteral("/music/set/a track.stem.mp4"),
            settings.outputFilePathFor(QStringLiteral("/music/set/a track.flac")));

    settings.setOutputMode(mixxx::StemOutputMode::CustomDirectory);
    settings.setOutputDirectory(QStringLiteral("/stems"));
    EXPECT_QSTRING_EQ(QStringLiteral("/stems/a track.stem.mp4"),
            settings.outputFilePathFor(QStringLiteral("/music/set/a track.flac")));

    // A stem file must not get a second suffix.
    EXPECT_QSTRING_EQ(QStringLiteral("/stems/a track.stem.mp4"),
            settings.outputFilePathFor(QStringLiteral("/music/a track.stem.mp4")));
}

TEST_F(StemConversionTest, StemManifestMatchesTheReader) {
    const QString manifest = mixxx::stemManifestJson();
    EXPECT_TRUE(manifest.contains(QStringLiteral("\"version\":1")));
    EXPECT_TRUE(manifest.contains(QStringLiteral("\"Drums\"")));
    EXPECT_TRUE(manifest.contains(QStringLiteral("\"Vocals\"")));
}

TEST_F(StemConversionTest, FindStemFilesOrdersByRole) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QDir modelDir(dir.filePath(QStringLiteral("htdemucs/track")));
    ASSERT_TRUE(QDir().mkpath(modelDir.absolutePath()));
    for (const QString& name : {QStringLiteral("drums"),
                 QStringLiteral("bass"),
                 QStringLiteral("other"),
                 QStringLiteral("vocals")}) {
        ASSERT_TRUE(writeSineWav(
                modelDir.absoluteFilePath(name + QStringLiteral(".wav")), 44100, 64));
    }

    QString errorMessage;
    const QStringList files = mixxx::StemConversionJob::findStemFiles(
            dir.path(), QString(), &errorMessage);
    ASSERT_EQ(4, files.size()) << errorMessage.toStdString();
    EXPECT_TRUE(files.at(0).endsWith(QStringLiteral("drums.wav")));
    EXPECT_TRUE(files.at(1).endsWith(QStringLiteral("bass.wav")));
    EXPECT_TRUE(files.at(2).endsWith(QStringLiteral("other.wav")));
    EXPECT_TRUE(files.at(3).endsWith(QStringLiteral("vocals.wav")));
}

TEST_F(StemConversionTest, FindStemFilesNamesTheMissingStem) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    ASSERT_TRUE(writeSineWav(dir.filePath(QStringLiteral("drums.wav")), 44100, 64));

    QString errorMessage;
    EXPECT_TRUE(mixxx::StemConversionJob::findStemFiles(dir.path(), QString(), &errorMessage)
                        .isEmpty());
    EXPECT_TRUE(errorMessage.contains(QStringLiteral("Bass")));
}

TEST_F(StemConversionTest, FindStemFilesRejectsTwoFilesForOneStem) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    for (const QString& name : {QStringLiteral("drums"),
                 QStringLiteral("bass"),
                 QStringLiteral("other"),
                 QStringLiteral("vocals"),
                 QStringLiteral("lead vocal")}) {
        ASSERT_TRUE(writeSineWav(
                dir.filePath(name + QStringLiteral(".wav")), 44100, 64));
    }

    QString errorMessage;
    EXPECT_TRUE(mixxx::StemConversionJob::findStemFiles(dir.path(), QString(), &errorMessage)
                        .isEmpty());
    EXPECT_TRUE(errorMessage.contains(QStringLiteral("Vocals")));
}

TEST_F(StemConversionTest, FindStemFilesIgnoresTheTrackNameInTheStemNames) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    // audio-separator keeps the track name and puts the stem in parentheses.
    const QString trackName =
            QStringLiteral("Drum and Bass Anthem (Instrumental Vocal Mix)");
    const QStringList stemNames = {QStringLiteral("Drums"),
            QStringLiteral("Bass"),
            QStringLiteral("Other"),
            QStringLiteral("Vocals")};
    for (const QString& stemName : stemNames) {
        ASSERT_TRUE(writeSineWav(dir.filePath(QStringLiteral("%1_(%2)_htdemucs.wav")
                                                      .arg(trackName, stemName)),
                44100,
                64));
    }

    QString errorMessage;
    const QStringList files = mixxx::StemConversionJob::findStemFiles(
            dir.path(), trackName, &errorMessage);
    ASSERT_EQ(4, files.size()) << errorMessage.toStdString();
    for (int stemIndex = 0; stemIndex < stemNames.size(); ++stemIndex) {
        EXPECT_TRUE(files.at(stemIndex).contains(
                QStringLiteral("(%1)").arg(stemNames.at(stemIndex))))
                << files.at(stemIndex).toStdString();
    }
}

TEST_F(StemConversionTest, FindStemFilesStripsTheTrackNameWithoutParentheses) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString trackName = QStringLiteral("Vocal Drum Other Bass Thing");
    for (const QString& stemName : {QStringLiteral("drums"),
                 QStringLiteral("bass"),
                 QStringLiteral("other"),
                 QStringLiteral("vocals")}) {
        ASSERT_TRUE(writeSineWav(
                dir.filePath(QStringLiteral("%1 - %2.wav").arg(trackName, stemName)),
                44100,
                64));
    }

    QString errorMessage;
    const QStringList files = mixxx::StemConversionJob::findStemFiles(
            dir.path(), trackName, &errorMessage);
    ASSERT_EQ(4, files.size()) << errorMessage.toStdString();
    EXPECT_TRUE(files.at(0).endsWith(QStringLiteral("drums.wav")));
    EXPECT_TRUE(files.at(3).endsWith(QStringLiteral("vocals.wav")));
}

TEST_F(StemConversionTest, RescaleBeatsKeepsTheTime) {
    const auto pBeats = mixxx::Beats::fromConstTempo(
            mixxx::audio::SampleRate(44100),
            mixxx::audio::FramePos(44100),
            mixxx::Bpm(120));
    const auto pRescaled = mixxx::rescaleBeats(
            pBeats, mixxx::audio::SampleRate(48000));
    ASSERT_NE(nullptr, pRescaled);
    EXPECT_EQ(48000, pRescaled->getSampleRate());
    // One second stays one second: 44100 frames become 48000 frames.
    EXPECT_NEAR(48000.0, pRescaled->getLastMarkerPosition().value(), 1.0);
    EXPECT_NEAR(120.0, pRescaled->getLastMarkerBpm().value(), 0.001);
}

TEST_F(StemConversionTest, CopyTrackToStemCarriesTheCuesAndTheGrid) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    TrackPointer pSource = makeSourceTrack(dir.filePath(QStringLiteral("source.wav")));
    pSource->setTitle(QStringLiteral("A Title"));
    pSource->setArtist(QStringLiteral("An Artist"));
    pSource->setComment(QStringLiteral("A comment"));
    pSource->setRating(4);
    pSource->setColor(mixxx::RgbColor(0x112233));
    pSource->setKeyText(QStringLiteral("Am"));
    pSource->setReplayGain(mixxx::ReplayGain(0.5, 0.0));
    pSource->trySetBeats(mixxx::Beats::fromConstTempo(
            mixxx::audio::SampleRate(kTestSampleRate),
            mixxx::audio::FramePos(0),
            mixxx::Bpm(128)));
    pSource->createAndAddCue(mixxx::CueType::HotCue,
            0,
            mixxx::audio::FramePos(11025),
            mixxx::audio::FramePos());
    pSource->createAndAddCue(mixxx::CueType::Loop,
            Cue::kNoHotCue,
            mixxx::audio::FramePos(4410),
            mixxx::audio::FramePos(8820));
    pSource->setBpmLocked(true);

    TrackPointer pStem = Track::newTemporary(dir.filePath(QStringLiteral("a.stem.mp4")));
    pStem->setAudioProperties(mixxx::audio::ChannelCount(2),
            mixxx::audio::SampleRate(kTestSampleRate),
            mixxx::audio::Bitrate(),
            mixxx::Duration::fromSeconds(0.5));

    mixxx::copyTrackToStem(*pSource, pStem.get());

    EXPECT_QSTRING_EQ(QStringLiteral("A Title"), pStem->getTitle());
    EXPECT_QSTRING_EQ(QStringLiteral("An Artist"), pStem->getArtist());
    EXPECT_QSTRING_EQ(QStringLiteral("A comment"), pStem->getComment());
    EXPECT_EQ(4, pStem->getRating());
    EXPECT_EQ(pSource->getColor(), pStem->getColor());
    EXPECT_QSTRING_EQ(pSource->getKeyText(), pStem->getKeyText());
    EXPECT_EQ(pSource->getReplayGain().getRatio(), pStem->getReplayGain().getRatio());
    EXPECT_TRUE(pStem->isBpmLocked());
    ASSERT_NE(nullptr, pStem->getBeats());
    EXPECT_NEAR(128.0, pStem->getBpm(), 0.001);
    EXPECT_EQ(2, pStem->getCuePoints().size());

    const CuePointer pHotcue = pStem->findHotcueByIndex(0);
    ASSERT_NE(nullptr, pHotcue);
    EXPECT_NEAR(11025.0, pHotcue->getPosition().value(), 1.0);
}

TEST_F(StemConversionTest, CopyTrackToStemConvertsAnotherSampleRate) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    TrackPointer pSource = makeSourceTrack(dir.filePath(QStringLiteral("source.wav")));
    pSource->trySetBeats(mixxx::Beats::fromConstTempo(
            mixxx::audio::SampleRate(kTestSampleRate),
            mixxx::audio::FramePos(44100),
            mixxx::Bpm(120)));
    pSource->createAndAddCue(mixxx::CueType::HotCue,
            0,
            mixxx::audio::FramePos(44100),
            mixxx::audio::FramePos());

    TrackPointer pStem = Track::newTemporary(dir.filePath(QStringLiteral("a.stem.mp4")));
    pStem->setAudioProperties(mixxx::audio::ChannelCount(2),
            mixxx::audio::SampleRate(48000),
            mixxx::audio::Bitrate(),
            mixxx::Duration::fromSeconds(0.5));

    mixxx::copyTrackToStem(*pSource, pStem.get());

    const CuePointer pHotcue = pStem->findHotcueByIndex(0);
    ASSERT_NE(nullptr, pHotcue);
    // One second stays one second: 44100 frames become 48000 frames.
    EXPECT_NEAR(48000.0, pHotcue->getPosition().value(), 1.0);
    ASSERT_NE(nullptr, pStem->getBeats());
    EXPECT_EQ(48000, pStem->getBeats()->getSampleRate());
}

#ifndef Q_OS_WIN
TEST_F(StemConversionTest, JobReportsAMissingTool) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    TrackPointer pSource = makeSourceTrack(dir.filePath(QStringLiteral("source.wav")));

    mixxx::StemConversionSettings settings = makeSettings(
            QStringLiteral("ok"), dir.path());
    settings.setSeparatorCommand(QStringLiteral("mixxx-no-such-separator $INPUT"));

    mixxx::StemConversionJob job(settings, pSource);
    QSignalSpy spy(&job, &mixxx::StemConversionJob::finished);
    job.start();
    ASSERT_TRUE(spy.count() == 1 || spy.wait(30000));
    EXPECT_EQ(mixxx::StemConversionJob::State::Failed, job.state());
    EXPECT_TRUE(job.errorMessage().contains(
            QStringLiteral("mixxx-no-such-separator")));
    EXPECT_TRUE(job.errorMessage().contains(QStringLiteral("SeparatorCommand")));
}

TEST_F(StemConversionTest, JobReportsAProgramThatCannotRun) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    TrackPointer pSource = makeSourceTrack(dir.filePath(QStringLiteral("source.wav")));

    // The file is executable but its interpreter is missing, thus the check
    // of the job passes and the start of the process fails.
    const QString scriptPath =
            getTestDir().filePath(QStringLiteral("stems/broken_separator.sh"));
    ASSERT_TRUE(QFileInfo(scriptPath).isExecutable());
    mixxx::StemConversionSettings settings = makeSettings(
            QStringLiteral("ok"), dir.path());
    settings.setSeparatorCommand(
            QStringLiteral("\"%1\" $MODEL $OUTPUT_DIR \"$INPUT\"").arg(scriptPath));

    mixxx::StemConversionJob job(settings, pSource);
    QSignalSpy spy(&job, &mixxx::StemConversionJob::finished);
    job.start();
    ASSERT_TRUE(spy.count() == 1 || spy.wait(30000))
            << "the job never ended, state "
            << static_cast<int>(job.state());
    EXPECT_EQ(mixxx::StemConversionJob::State::Failed, job.state());
    EXPECT_TRUE(job.errorMessage().contains(QStringLiteral("broken_separator.sh")))
            << job.errorMessage().toStdString();
}

TEST_F(StemConversionTest, JobReportsASeparatorError) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QDir outputDir(dir.filePath(QStringLiteral("out")));
    ASSERT_TRUE(QDir().mkpath(outputDir.absolutePath()));
    TrackPointer pSource = makeSourceTrack(dir.filePath(QStringLiteral("source.wav")));

    mixxx::StemConversionJob job(
            makeSettings(QStringLiteral("fail"), outputDir.absolutePath()), pSource);
    QSignalSpy spy(&job, &mixxx::StemConversionJob::finished);
    job.start();
    ASSERT_TRUE(spy.count() == 1 || spy.wait(30000));
    EXPECT_EQ(mixxx::StemConversionJob::State::Failed, job.state());
    EXPECT_TRUE(job.errorMessage().contains(QStringLiteral("did not load")))
            << job.errorMessage().toStdString();
    // No half written file stays behind for a library scan to pick up.
    EXPECT_TRUE(outputDir.entryList(QDir::Files | QDir::Hidden).isEmpty());
}

TEST_F(StemConversionTest, JobKeepsTheStemFileOfAnEarlierRun) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    TrackPointer pSource = makeSourceTrack(dir.filePath(QStringLiteral("source.wav")));

    const QString stemFilePath = dir.filePath(QStringLiteral("source.stem.mp4"));
    const QByteArray earlierContent("the stem file of the first run");
    QFile earlierFile(stemFilePath);
    ASSERT_TRUE(earlierFile.open(QIODevice::WriteOnly));
    earlierFile.write(earlierContent);
    earlierFile.close();

    mixxx::StemConversionJob job(
            makeSettings(QStringLiteral("ok"), dir.path()), pSource);
    QSignalSpy spy(&job, &mixxx::StemConversionJob::finished);
    job.start();
    ASSERT_TRUE(spy.count() == 1 || spy.wait(30000));
    EXPECT_EQ(mixxx::StemConversionJob::State::Failed, job.state());
    EXPECT_TRUE(job.errorMessage().contains(QStringLiteral("exists already")))
            << job.errorMessage().toStdString();

    // The job owns no file that it did not make.
    ASSERT_TRUE(QFile::exists(stemFilePath));
    QFile keptFile(stemFilePath);
    ASSERT_TRUE(keptFile.open(QIODevice::ReadOnly));
    EXPECT_EQ(earlierContent, keptFile.readAll());
}

TEST_F(StemConversionTest, JobCancels) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    TrackPointer pSource = makeSourceTrack(dir.filePath(QStringLiteral("source.wav")));

    mixxx::StemConversionJob job(
            makeSettings(QStringLiteral("hang"), dir.path()), pSource);
    QSignalSpy spy(&job, &mixxx::StemConversionJob::finished);
    job.start();
    ASSERT_TRUE(waitForState(job, mixxx::StemConversionJob::State::Separating));
    job.cancel();
    ASSERT_TRUE(spy.count() == 1 || spy.wait(30000));
    EXPECT_EQ(mixxx::StemConversionJob::State::Cancelled, job.state());
    EXPECT_FALSE(QFile::exists(
            dir.filePath(QStringLiteral("source.stem.mp4"))));
}

TEST_F(StemConversionTest, JobWritesAStemFileThatMixxxOpens) {
    if (muxerPathOrEmpty().isEmpty()) {
        GTEST_SKIP() << "MP4Box is not installed";
    }
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    TrackPointer pSource = makeSourceTrack(dir.filePath(QStringLiteral("source.wav")));

    mixxx::StemConversionJob job(
            makeSettings(QStringLiteral("ok"), dir.path()), pSource);
    QSignalSpy spy(&job, &mixxx::StemConversionJob::finished);
    job.start();
    ASSERT_TRUE(spy.count() == 1 || spy.wait(60000));
    ASSERT_EQ(mixxx::StemConversionJob::State::Succeeded, job.state())
            << job.errorMessage().toStdString();

    const QString stemFilePath = dir.filePath(QStringLiteral("source.stem.mp4"));
    ASSERT_TRUE(QFile::exists(stemFilePath));
    EXPECT_TRUE(mixxx::StemInfoImporter::hasStemAtom(stemFilePath));

    const QList<StemInfo> stemInfos =
            mixxx::StemInfoImporter::importStemInfos(stemFilePath);
    ASSERT_EQ(4, stemInfos.size());
    EXPECT_QSTRING_EQ(QStringLiteral("Drums"), stemInfos.at(0).getLabel());
    EXPECT_QSTRING_EQ(QStringLiteral("Vocals"), stemInfos.at(3).getLabel());

    ASSERT_TRUE(SoundSourceProxy::isFileTypeSupported(QStringLiteral("stem.mp4")) ||
            SoundSourceProxy::registerProviders());
    TrackPointer pStemTrack(Track::newTemporary(stemFilePath));
    mixxx::AudioSource::OpenParams params;
    params.setChannelCount(mixxx::audio::ChannelCount::stem());
    const auto pAudioSource = SoundSourceProxy(pStemTrack).openAudioSource(params);
    ASSERT_NE(nullptr, pAudioSource);
    EXPECT_EQ(mixxx::audio::ChannelCount::stem(),
            pAudioSource->getSignalInfo().getChannelCount());
    EXPECT_EQ(kTestSampleRate, pAudioSource->getSignalInfo().getSampleRate());
}

TEST_F(StemConversionTest, JobEncodesWithTheDefaultTemplateAndKeepsTheFrames) {
    if (muxerPathOrEmpty().isEmpty() || encoderPathOrEmpty().isEmpty()) {
        GTEST_SKIP() << "MP4Box or ffmpeg is not installed";
    }
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    // The rig runs at 48 kHz and htdemucs writes 44100 Hz. The click is at
    // the same second in the source and in the stems.
    constexpr int kSourceRate = 48000;
    constexpr int kSeparatorRate = 44100;
    constexpr int kSourceClickFrame = kSourceRate;
    const QString sourceFilePath = dir.filePath(QStringLiteral("clicktrack.wav"));
    ASSERT_TRUE(writeClickWav(
            sourceFilePath, kSourceRate, kSourceRate * 2, kSourceClickFrame));
    TrackPointer pSource = Track::newTemporary(sourceFilePath);
    pSource->setAudioProperties(mixxx::audio::ChannelCount(2),
            mixxx::audio::SampleRate(kSourceRate),
            mixxx::audio::Bitrate(),
            mixxx::Duration::fromSeconds(2));

    const QDir stemDir(dir.filePath(QStringLiteral("stems")));
    ASSERT_TRUE(QDir().mkpath(stemDir.absolutePath()));
    for (const QString& name : {QStringLiteral("drums"),
                 QStringLiteral("bass"),
                 QStringLiteral("other"),
                 QStringLiteral("vocals")}) {
        ASSERT_TRUE(writeClickWav(
                stemDir.absoluteFilePath(name + QStringLiteral(".wav")),
                kSeparatorRate,
                kSeparatorRate * 2,
                kSeparatorRate));
    }

    mixxx::StemConversionSettings settings = makeSettings(
            QStringLiteral("from"), dir.path());
    settings.setSeparatorCommand(copyingSeparatorCommand(stemDir.absolutePath()));
    settings.setEncoderCommand(
            mixxx::StemConversionSettings::defaultEncoderCommand());

    mixxx::StemConversionJob job(settings, pSource);
    QSignalSpy spy(&job, &mixxx::StemConversionJob::finished);
    job.start();
    ASSERT_TRUE(spy.count() == 1 || spy.wait(120000));
    ASSERT_EQ(mixxx::StemConversionJob::State::Succeeded, job.state())
            << job.errorMessage().toStdString();

    const QString stemFilePath = dir.filePath(QStringLiteral("clicktrack.stem.mp4"));
    ASSERT_TRUE(QFile::exists(stemFilePath));
    ASSERT_EQ(4, mixxx::StemInfoImporter::importStemInfos(stemFilePath).size());

    ASSERT_TRUE(SoundSourceProxy::isFileTypeSupported(QStringLiteral("stem.mp4")) ||
            SoundSourceProxy::registerProviders());
    TrackPointer pStemTrack(Track::newTemporary(stemFilePath));
    mixxx::AudioSource::OpenParams stemParams;
    stemParams.setChannelCount(mixxx::audio::ChannelCount::stem());
    const auto pStemSource = SoundSourceProxy(pStemTrack).openAudioSource(stemParams);
    ASSERT_NE(nullptr, pStemSource);
    EXPECT_EQ(mixxx::audio::ChannelCount::stem(),
            pStemSource->getSignalInfo().getChannelCount());
    // The encode command resamples to the rate of the source track.
    EXPECT_EQ(kSourceRate, pStemSource->getSignalInfo().getSampleRate());

    // Read the mix of the four stems and find the click.
    TrackPointer pMixTrack(Track::newTemporary(stemFilePath));
    mixxx::AudioSource::OpenParams mixParams;
    mixParams.setChannelCount(mixxx::audio::ChannelCount::stereo());
    const auto pMixSource = SoundSourceProxy(pMixTrack).openAudioSource(mixParams);
    ASSERT_NE(nullptr, pMixSource);

    constexpr SINT kReadFrames = 4000;
    constexpr SINT kReadStart = kSourceClickFrame - 2000;
    mixxx::SampleBuffer buffer(kReadFrames * 2);
    const auto readFrames = pMixSource->readSampleFrames(
            mixxx::WritableSampleFrames(
                    mixxx::IndexRange::forward(kReadStart, kReadFrames),
                    mixxx::SampleBuffer::WritableSlice(
                            buffer.data(), buffer.size())));
    ASSERT_EQ(kReadFrames * 2, readFrames.readableLength());

    const int clickFrame = findOnsetFrame(buffer);
    ASSERT_GE(clickFrame, 0) << "no click found in the stem file";
    const int offsetFrames =
            static_cast<int>(kReadStart) + clickFrame - kSourceClickFrame;
    // The encoder delay must not move the audio, otherwise each cue lands
    // late by one AAC frame, 1024 samples or more.
    EXPECT_EQ(0, offsetFrames) << "the stem audio starts " << offsetFrames
                               << " frames off the source";
}

TEST_F(StemConversionTest, JobWritesTheTagsAndKeepsTheStemManifest) {
    if (muxerPathOrEmpty().isEmpty()) {
        GTEST_SKIP() << "MP4Box is not installed";
    }
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    // This file carries an embedded cover image.
    const QString sourceFilePath = getTestFile(QStringLiteral("-jpg.mp3"));
    ASSERT_TRUE(QFile::exists(sourceFilePath));
    TrackPointer pSource = Track::newTemporary(sourceFilePath);
    pSource->setAudioProperties(mixxx::audio::ChannelCount(2),
            mixxx::audio::SampleRate(kTestSampleRate),
            mixxx::audio::Bitrate(),
            mixxx::Duration::fromSeconds(1));
    pSource->setTitle(QStringLiteral("Stem Title"));
    pSource->setArtist(QStringLiteral("Stem Artist"));
    pSource->setAlbum(QStringLiteral("Stem Album"));

    const QDir stemDir(dir.filePath(QStringLiteral("stems")));
    ASSERT_TRUE(QDir().mkpath(stemDir.absolutePath()));
    for (const QString& name : {QStringLiteral("drums"),
                 QStringLiteral("bass"),
                 QStringLiteral("other"),
                 QStringLiteral("vocals")}) {
        ASSERT_TRUE(writeSineWav(
                stemDir.absoluteFilePath(name + QStringLiteral(".wav")),
                kTestSampleRate,
                kTestFrameCount));
    }

    mixxx::StemConversionSettings settings = makeSettings(
            QStringLiteral("from"), dir.path());
    settings.setSeparatorCommand(copyingSeparatorCommand(stemDir.absolutePath()));

    mixxx::StemConversionJob job(settings, pSource);
    QSignalSpy spy(&job, &mixxx::StemConversionJob::finished);
    job.start();
    ASSERT_TRUE(spy.count() == 1 || spy.wait(120000));
    ASSERT_EQ(mixxx::StemConversionJob::State::Succeeded, job.state())
            << job.errorMessage().toStdString();

    const QString stemFilePath = job.outputFilePath();
    ASSERT_TRUE(QFile::exists(stemFilePath));

    // The tag writer must keep the stem manifest of the muxer.
    const QList<StemInfo> stemInfos =
            mixxx::StemInfoImporter::importStemInfos(stemFilePath);
    ASSERT_EQ(4, stemInfos.size());
    EXPECT_QSTRING_EQ(QStringLiteral("Drums"), stemInfos.at(0).getLabel());

    mixxx::TrackMetadata metadata;
    QImage coverImage;
    const auto result = mixxx::MetadataSourceTagLib(
            stemFilePath, QStringLiteral("stem.mp4"))
                                .importTrackMetadataAndCoverImage(
                                        &metadata, &coverImage, false);
    ASSERT_EQ(mixxx::MetadataSource::ImportResult::Succeeded, result.first);
    EXPECT_QSTRING_EQ(QStringLiteral("Stem Title"),
            metadata.getTrackInfo().getTitle());
    EXPECT_QSTRING_EQ(QStringLiteral("Stem Artist"),
            metadata.getTrackInfo().getArtist());
    EXPECT_QSTRING_EQ(QStringLiteral("Stem Album"),
            metadata.getAlbumInfo().getTitle());
    EXPECT_FALSE(coverImage.isNull()) << "the cover image is missing";
}

#endif // Q_OS_WIN
