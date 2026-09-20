#include "stems/stemconversionjob.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cmath>

#include "moc_stemconversionjob.cpp"
#include "track/steminfoimporter.h"
#include "track/track.h"
#include "util/assert.h"
#include "util/logger.h"

namespace {

const mixxx::Logger kLogger("StemConversionJob");

// The audio file name suffixes that a separator is expected to write.
const QStringList kAudioSuffixes = {
        QStringLiteral("wav"),
        QStringLiteral("flac"),
        QStringLiteral("aiff"),
        QStringLiteral("aif"),
        QStringLiteral("m4a"),
        QStringLiteral("mp4"),
        QStringLiteral("mp3"),
        QStringLiteral("ogg"),
        QStringLiteral("opus"),
};

const QStringList kStemKeywords[] = {
        {QStringLiteral("drums"), QStringLiteral("drum")},
        {QStringLiteral("bass")},
        {QStringLiteral("other"), QStringLiteral("instrumental"), QStringLiteral("synths")},
        {QStringLiteral("vocals"), QStringLiteral("vocal"), QStringLiteral("voice")},
};

constexpr int kStandardErrorTailChars = 4000;
constexpr mixxx::audio::SampleRate::value_t kFallbackSampleRate = 44100;

const QString kSeparatorLabel = QStringLiteral("SeparatorCommand");
const QString kEncoderLabel = QStringLiteral("EncoderCommand");
const QString kMuxerLabel = QStringLiteral("MuxerPath");

QStringList fileNameTokens(const QString& completeBaseName) {
    static const QRegularExpression separatorRegex(QStringLiteral("[^a-z0-9]+"));
    return completeBaseName.toLower().split(separatorRegex, Qt::SkipEmptyParts);
}

QString lastLines(const QString& text, int maxChars) {
    if (text.size() <= maxChars) {
        return text;
    }
    return text.right(maxChars);
}

} // anonymous namespace

namespace mixxx {

StemConversionJob::StemConversionJob(const StemConversionSettings& settings,
        TrackPointer pSourceTrack,
        QObject* pParent)
        : QObject(pParent),
          m_settings(settings),
          m_pSourceTrack(std::move(pSourceTrack)),
          m_sourceFilePath(m_pSourceTrack ? m_pSourceTrack->getLocation() : QString()),
          m_pProcess(make_parented<QProcess>(this)),
          m_encodeCount(0),
          m_state(State::Queued),
          m_progress(0.0),
          m_cancelRequested(false) {
    m_pProcess->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_pProcess,
            &QProcess::readyReadStandardError,
            this,
            &StemConversionJob::onStandardErrorReady);
    connect(m_pProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                onProcessFinished(
                        exitStatus == QProcess::CrashExit ? -1 : exitCode);
            });
}

StemConversionJob::~StemConversionJob() {
    if (m_pProcess->state() != QProcess::NotRunning) {
        m_pProcess->kill();
        m_pProcess->waitForFinished(1000);
    }
}

TrackId StemConversionJob::sourceTrackId() const {
    return m_pSourceTrack ? m_pSourceTrack->getId() : TrackId();
}

QString StemConversionJob::sourceTitle() const {
    if (!m_pSourceTrack) {
        return QString();
    }
    const QString info = m_pSourceTrack->getInfo();
    return info.isEmpty() ? QFileInfo(m_sourceFilePath).fileName() : info;
}

bool StemConversionJob::isDone() const {
    return m_state == State::Succeeded || m_state == State::Failed ||
            m_state == State::Cancelled;
}

// static
QString StemConversionJob::stateText(State state) {
    switch (state) {
    case State::Queued:
        return tr("Queued");
    case State::Separating:
        return tr("Separating");
    case State::Encoding:
        return tr("Encoding");
    case State::Muxing:
        return tr("Muxing");
    case State::Tagging:
        return tr("Writing the stem manifest");
    case State::Succeeded:
        return tr("Done");
    case State::Failed:
        return tr("Failed");
    case State::Cancelled:
        return tr("Cancelled");
    }
    DEBUG_ASSERT(!"unreachable");
    return QString();
}

// static
QStringList StemConversionJob::findStemFiles(const QString& directoryPath,
        QString* pErrorMessage) {
    const auto fail = [pErrorMessage](const QString& message) {
        if (pErrorMessage) {
            *pErrorMessage = message;
        }
        return QStringList();
    };

    QStringList found;
    for (int stemIndex = 0; stemIndex < StemConversionSettings::kStemCount; ++stemIndex) {
        found.append(QString());
    }

    QDirIterator iterator(directoryPath,
            QDir::Files | QDir::Readable,
            QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString filePath = iterator.next();
        const QFileInfo fileInfo(filePath);
        if (!kAudioSuffixes.contains(fileInfo.suffix().toLower())) {
            continue;
        }
        const QStringList tokens = fileNameTokens(fileInfo.completeBaseName());
        for (int stemIndex = 0; stemIndex < StemConversionSettings::kStemCount; ++stemIndex) {
            bool matches = false;
            for (const QString& keyword : kStemKeywords[stemIndex]) {
                if (tokens.contains(keyword)) {
                    matches = true;
                    break;
                }
            }
            if (!matches) {
                continue;
            }
            if (!found.at(stemIndex).isEmpty()) {
                return fail(tr(
                        "The separator wrote two files for the stem \"%1\": "
                        "\"%2\" and \"%3\".")
                                    .arg(stemRoleName(static_cast<StemRole>(stemIndex)),
                                            QFileInfo(found.at(stemIndex)).fileName(),
                                            fileInfo.fileName()));
            }
            found[stemIndex] = filePath;
        }
    }

    for (int stemIndex = 0; stemIndex < StemConversionSettings::kStemCount; ++stemIndex) {
        if (found.at(stemIndex).isEmpty()) {
            return fail(tr("The separator wrote no file for the stem \"%1\" in \"%2\".")
                                .arg(stemRoleName(static_cast<StemRole>(stemIndex)),
                                        directoryPath));
        }
    }
    if (pErrorMessage) {
        pErrorMessage->clear();
    }
    return found;
}

void StemConversionJob::start() {
    VERIFY_OR_DEBUG_ASSERT(m_state == State::Queued) {
        return;
    }
    if (!m_pSourceTrack || m_sourceFilePath.isEmpty()) {
        fail(tr("The track has no file."));
        return;
    }
    if (!QFileInfo::exists(m_sourceFilePath)) {
        fail(tr("The file \"%1\" is missing.").arg(m_sourceFilePath));
        return;
    }
    if (StemInfoImporter::hasStemAtom(m_sourceFilePath)) {
        fail(tr("The track is a stem file already."));
        return;
    }

    m_outputFilePath = m_settings.outputFilePathFor(m_sourceFilePath);
    if (QFileInfo::exists(m_outputFilePath)) {
        fail(tr("The file \"%1\" exists already.").arg(m_outputFilePath));
        return;
    }
    const QDir outputDir = QFileInfo(m_outputFilePath).absoluteDir();
    if (!outputDir.exists() && !outputDir.mkpath(QStringLiteral("."))) {
        fail(tr("The directory \"%1\" cannot be made.").arg(outputDir.absolutePath()));
        return;
    }

    m_pWorkDir = std::make_unique<QTemporaryDir>();
    if (!m_pWorkDir->isValid()) {
        fail(tr("A work directory cannot be made: %1").arg(m_pWorkDir->errorString()));
        return;
    }

    runSeparator();
}

void StemConversionJob::cancel() {
    if (isDone()) {
        return;
    }
    m_cancelRequested = true;
    if (m_pProcess->state() != QProcess::NotRunning) {
        m_pProcess->kill();
        return;
    }
    setState(State::Cancelled);
    emit finished();
}

void StemConversionJob::runSeparator() {
    const QString separatedDirPath =
            QDir(m_pWorkDir->path()).absoluteFilePath(QStringLiteral("separated"));
    if (!QDir().mkpath(separatedDirPath)) {
        fail(tr("The directory \"%1\" cannot be made.").arg(separatedDirPath));
        return;
    }

    QMap<QString, QString> placeholders;
    placeholders.insert(QStringLiteral("INPUT"), m_sourceFilePath);
    placeholders.insert(QStringLiteral("OUTPUT_DIR"), separatedDirPath);
    placeholders.insert(QStringLiteral("MODEL"), m_settings.model());

    QString errorMessage;
    const QStringList command = expandCommandTemplate(
            m_settings.separatorCommand(), placeholders, &errorMessage);
    if (command.isEmpty()) {
        fail(tr("The separation command is wrong: %1").arg(errorMessage));
        return;
    }
    setProgress(0.05);
    startProcess(command, State::Separating);
}

void StemConversionJob::runNextEncode() {
    if (m_encodeQueue.isEmpty()) {
        runMux();
        return;
    }

    const QString inputPath = m_encodeQueue.takeFirst();
    const int index = m_encodeCount - m_encodeQueue.size() - 1;
    const QString outputPath = QDir(m_pWorkDir->path())
                                       .absoluteFilePath(QStringLiteral("stream%1.m4a")
                                                                 .arg(index));
    auto sampleRate = m_pSourceTrack->getSampleRate();
    if (!sampleRate.isValid()) {
        sampleRate = audio::SampleRate(kFallbackSampleRate);
    }

    QMap<QString, QString> placeholders;
    placeholders.insert(QStringLiteral("INPUT"), inputPath);
    placeholders.insert(QStringLiteral("OUTPUT"), outputPath);
    placeholders.insert(QStringLiteral("SAMPLE_RATE"), QString::number(sampleRate.value()));

    QString errorMessage;
    const QStringList command = expandCommandTemplate(
            m_settings.encoderCommand(), placeholders, &errorMessage);
    if (command.isEmpty()) {
        fail(tr("The encode command is wrong: %1").arg(errorMessage));
        return;
    }
    m_muxInputs.append(outputPath);
    setProgress(0.7 + 0.15 * (index + 1) / m_encodeCount);
    startProcess(command, State::Encoding);
}

void StemConversionJob::runMux() {
    QStringList command;
    command.append(m_settings.muxerPath());
    command.append(QStringLiteral("-quiet"));
    for (const QString& inputPath : std::as_const(m_muxInputs)) {
        command.append(QStringLiteral("-add"));
        command.append(inputPath);
    }
    command.append(QStringLiteral("-new"));
    command.append(m_outputFilePath);
    setProgress(0.88);
    startProcess(command, State::Muxing);
}

void StemConversionJob::runTag() {
    const QString manifestPath =
            QDir(m_pWorkDir->path()).absoluteFilePath(QStringLiteral("stem.json"));
    QFile manifestFile(manifestPath);
    if (!manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fail(tr("The stem manifest cannot be written to \"%1\".").arg(manifestPath));
        return;
    }
    manifestFile.write(stemManifestJson().toUtf8());
    manifestFile.close();

    const QStringList command = {
            m_settings.muxerPath(),
            QStringLiteral("-quiet"),
            QStringLiteral("-udta"),
            QStringLiteral("0:type=stem:src=%1").arg(manifestPath),
            m_outputFilePath,
    };
    setProgress(0.95);
    startProcess(command, State::Tagging);
}

void StemConversionJob::complete() {
    if (!StemInfoImporter::hasStemAtom(m_outputFilePath)) {
        fail(tr("The file \"%1\" has no stem manifest. The muxer did not "
                "write it.")
                        .arg(m_outputFilePath));
        return;
    }
    m_pWorkDir.reset();
    setProgress(1.0);
    setState(State::Succeeded);
    emit finished();
}

void StemConversionJob::startProcess(const QStringList& command, State state) {
    DEBUG_ASSERT(!command.isEmpty());
    const QString program = command.first();
    const QString resolved = QStandardPaths::findExecutable(program);
    if (resolved.isEmpty() && !QFileInfo(program).isExecutable()) {
        QString preference = kSeparatorLabel;
        if (state == State::Encoding) {
            preference = kEncoderLabel;
        } else if (state == State::Muxing || state == State::Tagging) {
            preference = kMuxerLabel;
        }
        fail(tr("The program \"%1\" was not found. Install it, or set "
                "\"%2\" in Preferences > Stems.")
                        .arg(program, preference));
        return;
    }

    m_standardErrorTail.clear();
    setState(state);
    m_pProcess->setProgram(resolved.isEmpty() ? program : resolved);
    m_pProcess->setArguments(command.mid(1));
    m_pProcess->start(QIODevice::ReadOnly);
}

void StemConversionJob::onProcessFinished(int exitCode) {
    if (m_cancelRequested) {
        m_pWorkDir.reset();
        QFile::remove(m_outputFilePath);
        setState(State::Cancelled);
        emit finished();
        return;
    }
    if (exitCode != 0) {
        fail(tr("\"%1\" stopped with the code %2.\n%3")
                        .arg(m_pProcess->program(),
                                QString::number(exitCode),
                                m_standardErrorTail));
        return;
    }

    switch (m_state) {
    case State::Separating: {
        QString errorMessage;
        const QStringList stemFiles = findStemFiles(
                QDir(m_pWorkDir->path()).absoluteFilePath(QStringLiteral("separated")),
                &errorMessage);
        if (stemFiles.isEmpty()) {
            fail(errorMessage);
            return;
        }
        QStringList rawInputs;
        rawInputs.append(m_sourceFilePath);
        rawInputs.append(stemFiles);
        setProgress(0.7);
        if (m_settings.encoderCommand().trimmed().isEmpty()) {
            m_muxInputs = rawInputs;
            runMux();
        } else {
            m_encodeQueue = rawInputs;
            m_encodeCount = rawInputs.size();
            m_muxInputs.clear();
            runNextEncode();
        }
        return;
    }
    case State::Encoding:
        runNextEncode();
        return;
    case State::Muxing:
        runTag();
        return;
    case State::Tagging:
        complete();
        return;
    default:
        DEBUG_ASSERT(!"unexpected state");
        return;
    }
}

void StemConversionJob::onStandardErrorReady() {
    const QString chunk = QString::fromUtf8(m_pProcess->readAllStandardError());
    m_standardErrorTail = lastLines(m_standardErrorTail + chunk, kStandardErrorTailChars);
    if (m_state != State::Separating) {
        return;
    }
    // Many separators print a percentage. Show it while the model runs.
    static const QRegularExpression percentRegex(QStringLiteral("(\\d{1,3})%"));
    int lastPercent = -1;
    auto matches = percentRegex.globalMatch(chunk);
    while (matches.hasNext()) {
        lastPercent = matches.next().captured(1).toInt();
    }
    if (lastPercent >= 0 && lastPercent <= 100) {
        setProgress(0.05 + 0.65 * lastPercent / 100.0);
    }
}

void StemConversionJob::fail(const QString& message) {
    if (isDone()) {
        return;
    }
    m_errorMessage = message;
    kLogger.warning() << "Stem conversion of" << m_sourceFilePath << "failed:" << message;
    m_pWorkDir.reset();
    if (!m_outputFilePath.isEmpty()) {
        QFile::remove(m_outputFilePath);
    }
    setState(State::Failed);
    emit finished();
}

void StemConversionJob::setState(State state) {
    if (m_state == state) {
        return;
    }
    m_state = state;
    emit changed();
}

void StemConversionJob::setProgress(double progress) {
    if (progress < 1.0 && std::abs(progress - m_progress) < 0.005) {
        return;
    }
    m_progress = progress;
    emit changed();
}

} // namespace mixxx
