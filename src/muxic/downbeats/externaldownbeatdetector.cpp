#include "muxic/downbeats/externaldownbeatdetector.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QMutex>
#include <QMutexLocker>
#include <QObject>
#include <QRegularExpression>
#include <QSemaphore>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

#include "track/beats.h"
#include "util/assert.h"
#include "util/commandtemplate.h"

namespace {

const QString kInputPlaceholder = QStringLiteral("INPUT");
const QString kOutputPlaceholder = QStringLiteral("OUTPUT");
const QString kOutputFileName = QStringLiteral("downbeats.txt");
// The end of the error output that a failure report carries.
constexpr int kErrorTailBytes = 400;
// A beat list of a twenty minute track is near 50 kB. A program that writes
// more than this writes something else.
constexpr qint64 kMaxOutputBytes = 16 * 1024 * 1024;

/// The slots that hold the count of commands that run at one time.
///
/// The default gives one model on one card. The count comes from the
/// settings. It can change, and a slot that the settings dropped then goes
/// away at the end of its run.
class CommandSlots {
  public:
    /// Take a slot. The call waits while all slots are busy, and it returns
    /// false when `cancelCheck` said to stop.
    bool acquire(int jobs, const std::function<bool()>& cancelCheck, int pollMilliseconds) {
        {
            const QMutexLocker locked(&m_mutex);
            m_jobs = jobs;
            if (m_permits < jobs) {
                m_slots.release(jobs - m_permits);
                m_permits = jobs;
            }
        }
        while (!m_slots.tryAcquire(1, pollMilliseconds)) {
            if (cancelCheck && cancelCheck()) {
                return false;
            }
        }
        return true;
    }

    void release() {
        const QMutexLocker locked(&m_mutex);
        if (m_permits > m_jobs) {
            m_permits--;
            return;
        }
        m_slots.release(1);
    }

  private:
    QMutex m_mutex;
    QSemaphore m_slots{1};
    int m_permits = 1;
    int m_jobs = 1;
};

/// One set of slots for the whole program.
CommandSlots& commandSlots() {
    static CommandSlots gate;
    return gate;
}

/// Gives the slot back on every way out of a run.
class SlotRelease {
  public:
    ~SlotRelease() {
        commandSlots().release();
    }
};

} // anonymous namespace

namespace mixxx {

// static
QString ExternalDownbeatSettings::defaultCommand() {
    return QString::fromLatin1(BeatDetectionSettings::kDownbeatCommandDefault);
}

void ExternalDownbeatSettings::setTimeoutSeconds(int seconds) {
    m_timeoutSeconds = std::clamp(seconds, kMinTimeoutSeconds, kMaxTimeoutSeconds);
}

void ExternalDownbeatSettings::setJobs(int jobs) {
    m_jobs = std::clamp(jobs, kMinJobs, kMaxJobs);
}

// static
ExternalDownbeatSettings ExternalDownbeatSettings::readFrom(
        const UserSettingsPointer& pConfig) {
    ExternalDownbeatSettings settings;
    if (!pConfig) {
        return settings;
    }
    const BeatDetectionSettings bpmSettings(pConfig);
    settings.m_command = bpmSettings.getDownbeatCommand();
    settings.setTimeoutSeconds(bpmSettings.getDownbeatTimeoutSeconds());
    settings.setJobs(bpmSettings.getDownbeatCommandJobs());
    settings.m_detector = bpmSettings.getDownbeatDetectorName() ==
                    QString::fromLatin1(BeatDetectionSettings::kDownbeatDetectorExternal)
            ? DownbeatDetectorChoice::ExternalCommand
            : DownbeatDetectorChoice::BuiltIn;
    return settings;
}

void ExternalDownbeatSettings::writeTo(const UserSettingsPointer& pConfig) const {
    VERIFY_OR_DEBUG_ASSERT(pConfig) {
        return;
    }
    BeatDetectionSettings bpmSettings(pConfig);
    bpmSettings.setDownbeatCommand(m_command);
    bpmSettings.setDownbeatTimeoutSeconds(m_timeoutSeconds);
    bpmSettings.setDownbeatCommandJobs(m_jobs);
    bpmSettings.setDownbeatDetectorName(QString::fromLatin1(
            m_detector == DownbeatDetectorChoice::ExternalCommand
                    ? BeatDetectionSettings::kDownbeatDetectorExternal
                    : BeatDetectionSettings::kDownbeatDetectorBuiltIn));
}

std::vector<double> parseDownbeatTimes(const QString& output) {
    static const QRegularExpression fieldSeparator(QStringLiteral("[\\t ,;]+"));
    std::vector<double> times;
    const QStringList lines = output.split(QChar('\n'), Qt::SkipEmptyParts);
    for (const QString& rawLine : lines) {
        const QStringList fields =
                rawLine.trimmed().split(fieldSeparator, Qt::SkipEmptyParts);
        if (fields.size() < 2) {
            continue;
        }
        bool timeOk = false;
        const double time = fields.at(0).toDouble(&timeOk);
        bool placeOk = false;
        const int place = fields.at(1).toInt(&placeOk);
        if (!timeOk || !placeOk || !std::isfinite(time) || time < 0.0) {
            continue;
        }
        if (place == 1) {
            times.push_back(time);
        }
    }
    std::sort(times.begin(), times.end());
    return times;
}

std::vector<int> mapTimesToBeats(const std::vector<double>& times,
        const QVector<audio::FramePos>& beatPositions,
        audio::SampleRate sampleRate,
        double tolerance) {
    std::vector<int> indices;
    if (beatPositions.size() < 2 || !sampleRate.isValid()) {
        return indices;
    }
    indices.reserve(times.size());
    for (const double time : times) {
        const double frame = time * sampleRate.value();
        if (frame < beatPositions.first().value() ||
                frame > beatPositions.last().value()) {
            // A downbeat outside the grid has no beat to belong to.
            continue;
        }
        // The beat list rises. A binary search finds the place.
        const auto after = std::lower_bound(beatPositions.cbegin(),
                beatPositions.cend(),
                frame,
                [](const audio::FramePos& position, double value) {
                    return position.value() < value;
                });
        int index = static_cast<int>(after - beatPositions.cbegin());
        if (index > 0) {
            const double before = beatPositions.at(index - 1).value();
            if (frame - before <= beatPositions.at(index).value() - frame) {
                index--;
            }
        }
        // A downbeat between two beats belongs to another grid. Its vote
        // would go to a place of the bar that it never marked.
        const int neighbour = index > 0 ? index - 1 : index + 1;
        const double period = std::abs(
                beatPositions.at(neighbour).value() - beatPositions.at(index).value());
        if (period <= 0.0 ||
                std::abs(frame - beatPositions.at(index).value()) > tolerance * period) {
            continue;
        }
        indices.push_back(index);
    }
    return indices;
}

std::vector<int> barPhaseVotes(const std::vector<int>& beatIndices, int beatsPerBar) {
    if (beatsPerBar < 2) {
        return std::vector<int>();
    }
    std::vector<int> votes(beatsPerBar, 0);
    for (const int index : beatIndices) {
        if (index < 0) {
            continue;
        }
        votes[index % beatsPerBar]++;
    }
    return votes;
}

ExternalDownbeatDetector::ExternalDownbeatDetector(
        const ExternalDownbeatSettings& settings, int beatsPerBar)
        : m_settings(settings),
          m_beatsPerBar(std::max(2, beatsPerBar)),
          m_downbeatCount(0) {
}

void ExternalDownbeatDetector::setCancelCheck(std::function<bool()> cancelCheck) {
    m_cancelCheck = std::move(cancelCheck);
}

DownbeatPhase ExternalDownbeatDetector::detect(const QString& trackFilePath,
        const QVector<audio::FramePos>& beatPositions,
        audio::SampleRate sampleRate) {
    m_errorMessage.clear();
    m_downbeatCount = 0;
    m_votes.clear();
    if (beatPositions.size() < DownbeatDetector::kMinBeats) {
        m_errorMessage = QObject::tr("The track has too few beats.");
        return DownbeatPhase();
    }
    QString output;
    if (!runCommand(trackFilePath, &output)) {
        return DownbeatPhase();
    }
    const std::vector<double> times = parseDownbeatTimes(output);
    m_downbeatCount = static_cast<int>(times.size());
    if (times.empty()) {
        m_errorMessage = QObject::tr("The command reported no downbeat.");
        return DownbeatPhase();
    }
    // A program that counts another tempo reports far more downbeats than
    // the track has bars. Its votes are then not one trial for each bar.
    const int trackBars = beatPositions.size() / m_beatsPerBar;
    if (m_downbeatCount > kMaxDownbeatsPerBar * trackBars) {
        m_errorMessage = QObject::tr(
                "The command reported %1 downbeats for %2 bars.")
                                 .arg(m_downbeatCount)
                                 .arg(trackBars);
        return DownbeatPhase();
    }
    const std::vector<int> indices =
            mapTimesToBeats(times, beatPositions, sampleRate);
    m_votes = barPhaseVotes(indices, m_beatsPerBar);
    const DownbeatPhase phase = DownbeatDetector::scoreVotes(m_votes, -1, trackBars);
    if (!phase.accepted) {
        m_errorMessage = QObject::tr(
                "The downbeats of the command do not fit the beat grid.");
    }
    return phase;
}

DownbeatResult runDownbeatDetectors(const ExternalDownbeatSettings& settings,
        const QString& trackFilePath,
        const QVector<audio::FramePos>& beatPositions,
        audio::SampleRate sampleRate,
        const std::function<bool()>& cancelCheck,
        const std::function<DownbeatPhase()>& builtIn) {
    DownbeatResult result;
    if (settings.detector() == DownbeatDetectorChoice::ExternalCommand) {
        ExternalDownbeatDetector detector(settings, BarPhase::kDefaultBeatsPerBar);
        detector.setCancelCheck(cancelCheck);
        result.phase = detector.detect(trackFilePath, beatPositions, sampleRate);
        result.source = QStringLiteral("external");
        result.message = detector.errorMessage();
        if (result.phase.accepted) {
            return result;
        }
    }
    if (builtIn) {
        result.phase = builtIn();
        result.source = QStringLiteral("built in");
    }
    return result;
}

bool ExternalDownbeatDetector::waitForStart(QProcess* pProcess) {
    // waitForStarted blocks for its whole time. The wait runs in slices,
    // thus the cancel check gets a turn between them.
    QElapsedTimer startTimer;
    startTimer.start();
    while (!pProcess->waitForStarted(kPollMilliseconds)) {
        if (pProcess->state() != QProcess::Starting) {
            return false;
        }
        if (cancelled() || startTimer.elapsed() >= kStartMilliseconds) {
            return false;
        }
    }
    return true;
}

void ExternalDownbeatDetector::killProcessGroup(QProcess* pProcess) {
    const qint64 processId = pProcess->processId();
#ifdef Q_OS_UNIX
    // The child runs in a session of its own, thus the whole group goes.
    // Without this, a wrapper script leaves its python behind.
    if (processId > 0) {
        ::kill(static_cast<pid_t>(-processId), SIGTERM);
        QElapsedTimer killTimer;
        killTimer.start();
        while (pProcess->state() != QProcess::NotRunning &&
                killTimer.elapsed() < kKillMilliseconds) {
            pProcess->waitForFinished(kPollMilliseconds);
        }
        ::kill(static_cast<pid_t>(-processId), SIGKILL);
    }
#endif
    pProcess->kill();
    QElapsedTimer reapTimer;
    reapTimer.start();
    while (pProcess->state() != QProcess::NotRunning &&
            reapTimer.elapsed() < kStartMilliseconds) {
        pProcess->waitForFinished(kPollMilliseconds);
    }
}

bool ExternalDownbeatDetector::runCommand(const QString& trackFilePath, QString* pOutput) {
    if (m_settings.command().trimmed().isEmpty()) {
        m_errorMessage = QObject::tr("No downbeat command is set.");
        return false;
    }
    // A path that starts with a dash reads as an option of the program.
    // Mixxx holds the absolute path of every track.
    VERIFY_OR_DEBUG_ASSERT(QFileInfo(trackFilePath).isAbsolute()) {
        m_errorMessage = QObject::tr("The track path is not absolute.");
        return false;
    }
    QTemporaryDir workDirectory;
    if (!workDirectory.isValid()) {
        m_errorMessage = QObject::tr("The downbeat command got no work directory.");
        return false;
    }
    const QString outputFilePath = QDir(workDirectory.path()).filePath(kOutputFileName);

    QMap<QString, QString> placeholders;
    placeholders.insert(kInputPlaceholder, trackFilePath);
    placeholders.insert(kOutputPlaceholder, outputFilePath);
    QString templateError;
    const QStringList command = expandCommandTemplate(
            m_settings.command(), placeholders, &templateError);
    if (command.isEmpty()) {
        m_errorMessage = templateError;
        return false;
    }

    const QString program = command.first();
    const QString resolved = QStandardPaths::findExecutable(program);
    if (resolved.isEmpty() && !QFileInfo(program).isExecutable()) {
        m_errorMessage = QObject::tr("The program \"%1\" was not found.").arg(program);
        return false;
    }

    // Each analyzer thread holds a detector of its own. Without this gate a
    // library analysis starts one program for each thread, and each program
    // loads a model onto the same card.
    if (!commandSlots().acquire(m_settings.jobs(), m_cancelCheck, kPollMilliseconds)) {
        m_errorMessage = QObject::tr("The analysis of the track stopped.");
        return false;
    }
    const SlotRelease releaseSlot;

    QProcess process;
    process.setProgram(resolved.isEmpty() ? program : resolved);
    process.setArguments(command.mid(1));
    process.setWorkingDirectory(workDirectory.path());
#ifdef Q_OS_UNIX
    process.setChildProcessModifier([]() {
        ::setsid();
    });
#endif
    process.start(QIODevice::ReadOnly);
    if (!waitForStart(&process)) {
        killProcessGroup(&process);
        m_errorMessage = cancelled()
                ? QObject::tr("The analysis of the track stopped.")
                : QObject::tr("The program \"%1\" did not start: %2")
                          .arg(program, process.errorString());
        return false;
    }

    // A pipe that nobody reads holds the program at about 64 kB. The wait
    // reads both channels each time around.
    QByteArray standardOutput;
    QByteArray errorTail;
    bool tooMuchOutput = false;
    const auto readChannels = [&]() {
        standardOutput.append(process.readAllStandardOutput());
        if (standardOutput.size() > kMaxOutputBytes) {
            tooMuchOutput = true;
            standardOutput.truncate(kMaxOutputBytes);
        }
        errorTail.append(process.readAllStandardError());
        if (errorTail.size() > kErrorTailBytes) {
            errorTail = errorTail.right(kErrorTailBytes);
        }
    };

    QElapsedTimer runTimer;
    runTimer.start();
    const qint64 timeoutMilliseconds =
            static_cast<qint64>(m_settings.timeoutSeconds()) * 1000;
    bool stopped = false;
    bool ranTooLong = false;
    while (!process.waitForFinished(kPollMilliseconds)) {
        readChannels();
        if (process.state() == QProcess::NotRunning) {
            break;
        }
        stopped = cancelled();
        ranTooLong = runTimer.elapsed() >= timeoutMilliseconds;
        if (!stopped && !tooMuchOutput && !ranTooLong) {
            continue;
        }
        killProcessGroup(&process);
        readChannels();
        break;
    }
    readChannels();

    if (stopped) {
        m_errorMessage = QObject::tr("The analysis of the track stopped.");
        return false;
    }
    if (tooMuchOutput) {
        m_errorMessage = QObject::tr("The program \"%1\" wrote too much.").arg(program);
        return false;
    }
    if (ranTooLong) {
        m_errorMessage = QObject::tr("The program \"%1\" ran longer than %2 seconds.")
                                 .arg(program)
                                 .arg(m_settings.timeoutSeconds());
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        m_errorMessage = QObject::tr("\"%1\" stopped with the code %2. %3")
                                 .arg(program,
                                         QString::number(process.exitCode()),
                                         QString::fromUtf8(errorTail).trimmed());
        return false;
    }

    // The command writes the beats to the output file when the template uses
    // $OUTPUT, and to its standard output when it does not.
    QFile outputFile(outputFilePath);
    if (outputFile.exists() && outputFile.size() > 0) {
        if (outputFile.size() > kMaxOutputBytes) {
            m_errorMessage = QObject::tr("The program \"%1\" wrote too much.").arg(program);
            return false;
        }
        if (outputFile.open(QIODevice::ReadOnly)) {
            *pOutput = QString::fromUtf8(outputFile.readAll());
            return true;
        }
    }
    *pOutput = QString::fromUtf8(standardOutput);
    if (pOutput->trimmed().isEmpty()) {
        m_errorMessage = QObject::tr("The program \"%1\" wrote nothing.").arg(program);
        return false;
    }
    return true;
}

} // namespace mixxx
