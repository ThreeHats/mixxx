#include "stems/stemconversionjob.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QtConcurrentRun>
#include <cmath>

#include "moc_stemconversionjob.cpp"
#include "sources/metadatasourcetaglib.h"
#include "track/steminfoimporter.h"
#include "track/track.h"
#include "util/assert.h"
#include "util/commandtemplate.h"
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

constexpr int kNoStemRole = -1;
constexpr int kSeveralStemRoles = -2;
constexpr int kOutputTailChars = 4000;
constexpr mixxx::audio::SampleRate::value_t kFallbackSampleRate = 44100;

// The file type that gives TagLib the MP4 reader for a stem file.
const QString kStemFileType = QStringLiteral("stem.mp4");

const QString kSeparatorLabel = QStringLiteral("SeparatorCommand");
const QString kEncoderLabel = QStringLiteral("EncoderCommand");
const QString kMuxerLabel = QStringLiteral("MuxerPath");

QStringList fileNameTokens(const QString& name) {
    static const QRegularExpression separatorRegex(QStringLiteral("[^a-z0-9]+"));
    return name.toLower().split(separatorRegex, Qt::SkipEmptyParts);
}

/// The stem that a name asks for, or kNoStemRole, or kSeveralStemRoles when
/// the name holds the word of more than one stem.
int stemRoleOfName(const QString& name) {
    const QStringList tokens = fileNameTokens(name);
    int role = kNoStemRole;
    for (int stemIndex = 0; stemIndex < mixxx::StemConversionSettings::kStemCount;
            ++stemIndex) {
        for (const QString& keyword : kStemKeywords[stemIndex]) {
            if (!tokens.contains(keyword)) {
                continue;
            }
            if (role != kNoStemRole) {
                return kSeveralStemRoles;
            }
            role = stemIndex;
            break;
        }
    }
    return role;
}

/// The stem that a separator output file asks for. The text of the last
/// parentheses wins, because audio-separator writes "Title_(Vocals)_model".
int stemRoleOfFileName(const QString& completeBaseName, const QString& sourceBaseName) {
    static const QRegularExpression lastParenthesesRegex(
            QStringLiteral("\\(([^()]*)\\)[^()]*$"));
    const QRegularExpressionMatch match = lastParenthesesRegex.match(completeBaseName);
    if (match.hasMatch()) {
        const int role = stemRoleOfName(match.captured(1));
        if (role >= 0) {
            return role;
        }
    }
    QString strippedName = completeBaseName;
    if (!sourceBaseName.isEmpty()) {
        strippedName.remove(sourceBaseName, Qt::CaseInsensitive);
    }
    return stemRoleOfName(strippedName);
}

QString lastLines(const QString& text, int maxChars) {
    if (text.size() <= maxChars) {
        return text;
    }
    return text.right(maxChars);
}

QString absolutePathOf(const QString& filePath) {
    if (filePath.isEmpty()) {
        return filePath;
    }
    return QFileInfo(filePath).absoluteFilePath();
}

} // anonymous namespace

namespace mixxx {

StemConversionJob::StemConversionJob(const StemConversionSettings& settings,
        TrackPointer pSourceTrack,
        QObject* pParent)
        : QObject(pParent),
          m_settings(settings),
          m_pSourceTrack(std::move(pSourceTrack)),
          m_sourceFilePath(absolutePathOf(
                  m_pSourceTrack ? m_pSourceTrack->getLocation() : QString())),
          m_outputFilePath(m_sourceFilePath.isEmpty()
                          ? QString()
                          : settings.outputFilePathFor(m_sourceFilePath)),
          m_pProcess(make_parented<QProcess>(this)),
          m_madeOutputFile(false),
          m_encodeCount(0),
          m_state(State::Queued),
          m_progress(0.0),
          m_cancelRequested(false) {
    // Both channels go to one stream, thus neither pipe fills up and the
    // percentage of a separator is read whichever channel carries it.
    m_pProcess->setProcessChannelMode(QProcess::MergedChannels);
    connect(m_pProcess,
            &QProcess::readyReadStandardOutput,
            this,
            &StemConversionJob::onOutputReady);
    connect(m_pProcess,
            &QProcess::errorOccurred,
            this,
            &StemConversionJob::onProcessError);
    connect(m_pProcess,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this,
            [this](int exitCode, QProcess::ExitStatus exitStatus) {
                onProcessFinished(
                        exitStatus == QProcess::CrashExit ? -1 : exitCode);
            });
    connect(&m_watcher,
            &QFutureWatcher<QString>::finished,
            this,
            &StemConversionJob::onAsyncStepFinished);
}

StemConversionJob::~StemConversionJob() {
    m_pProcess->disconnect(this);
    if (m_pProcess->state() != QProcess::NotRunning) {
        m_pProcess->kill();
        m_pProcess->waitForFinished(1000);
    }
    m_watcher.disconnect(this);
    // The worker holds a pointer to this object, thus it must end first.
    m_watcher.waitForFinished();
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
    case State::Preparing:
        return tr("Preparing");
    case State::Separating:
        return tr("Separating");
    case State::Collecting:
        return tr("Reading the stem files");
    case State::Encoding:
        return tr("Encoding");
    case State::Muxing:
        return tr("Muxing");
    case State::Tagging:
        return tr("Writing the stem manifest");
    case State::Finishing:
        return tr("Writing the tags");
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
        const QString& sourceBaseName,
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
        const int stemIndex = stemRoleOfFileName(
                fileInfo.completeBaseName(), sourceBaseName);
        if (stemIndex < 0) {
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
    setProgress(0.02);
    runAsyncStep(&StemConversionJob::prepare, State::Preparing);
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
    if (m_watcher.isRunning()) {
        // The worker sees the request when its step ends.
        return;
    }
    cleanUp();
    setState(State::Cancelled);
    emit finished();
}

void StemConversionJob::runAsyncStep(QString (StemConversionJob::*step)(), State state) {
    setState(state);
    m_watcher.setFuture(QtConcurrent::run([this, step] {
        return (this->*step)();
    }));
}

void StemConversionJob::onAsyncStepFinished() {
    const QString errorMessage = m_watcher.result();
    if (m_cancelRequested) {
        cleanUp();
        if (m_madeOutputFile) {
            // The move went through just before the request came in.
            QFile::remove(m_outputFilePath);
            m_madeOutputFile = false;
        }
        setState(State::Cancelled);
        emit finished();
        return;
    }
    if (!errorMessage.isEmpty()) {
        fail(errorMessage);
        return;
    }
    switch (m_state) {
    case State::Preparing:
        runSeparator();
        return;
    case State::Collecting:
        setProgress(0.7);
        if (m_settings.encoderCommand().trimmed().isEmpty()) {
            runMux();
        } else {
            m_encodeQueue = m_muxInputs;
            m_encodeCount = m_encodeQueue.size();
            m_muxInputs.clear();
            runNextEncode();
        }
        return;
    case State::Finishing:
        succeed();
        return;
    default:
        DEBUG_ASSERT(!"unexpected state");
        return;
    }
}

QString StemConversionJob::separatedDirPath() const {
    return QDir(m_pWorkDir->path()).absoluteFilePath(QStringLiteral("separated"));
}

QString StemConversionJob::prepare() {
    if (!QFileInfo::exists(m_sourceFilePath)) {
        return tr("The file \"%1\" is missing.").arg(m_sourceFilePath);
    }
    if (StemInfoImporter::hasStemAtom(m_sourceFilePath)) {
        return tr("The track is a stem file already.");
    }
    if (QFileInfo::exists(m_outputFilePath)) {
        return tr("The file \"%1\" exists already.").arg(m_outputFilePath);
    }
    const QDir outputDir = QFileInfo(m_outputFilePath).absoluteDir();
    if (!outputDir.exists() && !outputDir.mkpath(QStringLiteral("."))) {
        return tr("The directory \"%1\" cannot be made.").arg(outputDir.absolutePath());
    }

    m_pWorkDir = std::make_unique<QTemporaryDir>();
    if (!m_pWorkDir->isValid()) {
        return tr("A work directory cannot be made: %1").arg(m_pWorkDir->errorString());
    }
    // The muxer splits a file list at a colon, thus such a path cannot pass.
    if (m_pWorkDir->path().contains(QChar(':'))) {
        return tr(
                "The work directory \"%1\" holds a colon. Set TMPDIR to a "
                "directory without one.")
                .arg(m_pWorkDir->path());
    }
    if (!QDir().mkpath(separatedDirPath())) {
        return tr("The directory \"%1\" cannot be made.").arg(separatedDirPath());
    }

    const QDir workDir(m_pWorkDir->path());
    m_workOutputFilePath = workDir.absoluteFilePath(QStringLiteral("out.stem.mp4"));
    m_manifestFilePath = workDir.absoluteFilePath(QStringLiteral("stem.json"));
    QFile manifestFile(m_manifestFilePath);
    if (!manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return tr("The stem manifest cannot be written to \"%1\".").arg(m_manifestFilePath);
    }
    manifestFile.write(stemManifestJson().toUtf8());
    manifestFile.close();

    m_coverImageFilePath = writeSourceCoverImage();
    return QString();
}

QString StemConversionJob::collectStemFiles() {
    QString errorMessage;
    const QStringList stemFiles = findStemFiles(separatedDirPath(),
            QFileInfo(m_sourceFilePath).completeBaseName(),
            &errorMessage);
    if (stemFiles.isEmpty()) {
        return errorMessage;
    }

    // The muxer takes neither a colon nor a leading dash in a file name,
    // thus every input gets a plain name in the work directory.
    QStringList rawInputs;
    rawInputs.append(m_sourceFilePath);
    rawInputs.append(stemFiles);
    m_muxInputs.clear();
    const QDir workDir(m_pWorkDir->path());
    for (int index = 0; index < rawInputs.size(); ++index) {
        const QString rawPath = rawInputs.at(index);
        const QString safePath = workDir.absoluteFilePath(
                QStringLiteral("in%1.%2").arg(QString::number(index),
                        QFileInfo(rawPath).suffix()));
        if (!QFile::link(rawPath, safePath) && !QFile::copy(rawPath, safePath)) {
            return tr("The file \"%1\" cannot be given to the muxer.").arg(rawPath);
        }
        m_muxInputs.append(safePath);
    }
    return QString();
}

QString StemConversionJob::writeSourceCoverImage() {
    const QString sourceFileType = QFileInfo(m_sourceFilePath).suffix();
    if (sourceFileType.isEmpty()) {
        return QString();
    }
    QImage coverImage;
    MetadataSourceTagLib(m_sourceFilePath, sourceFileType)
            .importTrackMetadataAndCoverImage(nullptr, &coverImage, false);
    if (coverImage.isNull()) {
        return QString();
    }
    const QString coverImagePath =
            QDir(m_pWorkDir->path()).absoluteFilePath(QStringLiteral("cover.jpg"));
    if (!coverImage.save(coverImagePath, "JPEG", 90)) {
        return QString();
    }
    return coverImagePath;
}

QString StemConversionJob::exportSourceTags() {
    const auto result = MetadataSourceTagLib(m_workOutputFilePath, kStemFileType)
                                .exportTrackMetadata(m_pSourceTrack->getMetadata());
    if (result.first != MetadataSource::ExportResult::Succeeded) {
        kLogger.warning() << "Failed to write the tags into" << m_workOutputFilePath;
    }
    return QString();
}

QString StemConversionJob::finish() {
    exportSourceTags();
    if (!StemInfoImporter::hasStemAtom(m_workOutputFilePath)) {
        return tr("The stem file has no stem manifest. The muxer did not write it.");
    }
    return moveIntoPlace();
}

QString StemConversionJob::moveIntoPlace() {
    const QFileInfo outputFileInfo(m_outputFilePath);
    const QString partFilePath = outputFileInfo.absoluteDir().absoluteFilePath(
            QStringLiteral(".%1.part").arg(outputFileInfo.fileName()));
    QFile::remove(partFilePath);
    m_partFilePath = partFilePath;
    // A rename fails across file systems, thus a copy is the fallback.
    if (!QFile::rename(m_workOutputFilePath, partFilePath) &&
            !QFile::copy(m_workOutputFilePath, partFilePath)) {
        cleanUp();
        return tr("The stem file cannot be moved to \"%1\".").arg(m_outputFilePath);
    }
    if (QFileInfo::exists(m_outputFilePath)) {
        cleanUp();
        return tr("The file \"%1\" exists already.").arg(m_outputFilePath);
    }
    if (!QFile::rename(partFilePath, m_outputFilePath)) {
        cleanUp();
        return tr("The stem file cannot be moved to \"%1\".").arg(m_outputFilePath);
    }
    m_partFilePath.clear();
    m_madeOutputFile = true;
    return QString();
}

void StemConversionJob::cleanUp() {
    if (!m_partFilePath.isEmpty()) {
        QFile::remove(m_partFilePath);
        m_partFilePath.clear();
    }
    m_pWorkDir.reset();
}

void StemConversionJob::runSeparator() {
    QMap<QString, QString> placeholders;
    placeholders.insert(QStringLiteral("INPUT"), m_sourceFilePath);
    placeholders.insert(QStringLiteral("OUTPUT_DIR"), separatedDirPath());
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
    command.append(m_workOutputFilePath);
    setProgress(0.88);
    startProcess(command, State::Muxing);
}

void StemConversionJob::runTag() {
    QStringList command = {m_settings.muxerPath(), QStringLiteral("-quiet")};
    if (!m_coverImageFilePath.isEmpty()) {
        command.append(QStringLiteral("-itags"));
        command.append(QStringLiteral("cover=%1").arg(m_coverImageFilePath));
    }
    command.append(QStringLiteral("-udta"));
    command.append(QStringLiteral("0:type=stem:src=%1").arg(m_manifestFilePath));
    command.append(m_workOutputFilePath);
    setProgress(0.93);
    startProcess(command, State::Tagging);
}

void StemConversionJob::succeed() {
    cleanUp();
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

    m_outputTail.clear();
    setState(state);
    m_pProcess->setProgram(resolved.isEmpty() ? program : resolved);
    m_pProcess->setArguments(command.mid(1));
    m_pProcess->start(QIODevice::ReadOnly);
}

void StemConversionJob::onProcessError(QProcess::ProcessError error) {
    if (error != QProcess::FailedToStart) {
        // A crash and the other errors come with finished().
        return;
    }
    fail(tr("The program \"%1\" did not start: %2")
                    .arg(m_pProcess->program(), m_pProcess->errorString()));
}

void StemConversionJob::onProcessFinished(int exitCode) {
    onOutputReady();
    if (m_cancelRequested) {
        cleanUp();
        setState(State::Cancelled);
        emit finished();
        return;
    }
    if (exitCode != 0) {
        fail(tr("\"%1\" stopped with the code %2.\n%3")
                        .arg(m_pProcess->program(),
                                QString::number(exitCode),
                                m_outputTail));
        return;
    }

    switch (m_state) {
    case State::Separating:
        runAsyncStep(&StemConversionJob::collectStemFiles, State::Collecting);
        return;
    case State::Encoding:
        runNextEncode();
        return;
    case State::Muxing:
        runTag();
        return;
    case State::Tagging:
        setProgress(0.96);
        runAsyncStep(&StemConversionJob::finish, State::Finishing);
        return;
    default:
        DEBUG_ASSERT(!"unexpected state");
        return;
    }
}

void StemConversionJob::onOutputReady() {
    const QString chunk = QString::fromUtf8(m_pProcess->readAllStandardOutput());
    if (chunk.isEmpty()) {
        return;
    }
    m_outputTail = lastLines(m_outputTail + chunk, kOutputTailChars);
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
    // The output file of another run is not the property of this job.
    cleanUp();
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
