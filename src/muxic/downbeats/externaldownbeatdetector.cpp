#include "muxic/downbeats/externaldownbeatdetector.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QObject>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>

#include "track/beats.h"
#include "util/assert.h"
#include "util/commandtemplate.h"

namespace {

const QString kBuiltInValue = QStringLiteral("builtin");
const QString kExternalValue = QStringLiteral("external");
const QString kInputPlaceholder = QStringLiteral("INPUT");
const QString kOutputPlaceholder = QStringLiteral("OUTPUT");
const QString kOutputFileName = QStringLiteral("downbeats.txt");
// The end of the error output that a failure report carries.
constexpr int kErrorTailChars = 400;

} // anonymous namespace

namespace mixxx {

const QString ExternalDownbeatSettings::kConfigGroup =
        QStringLiteral("[BeatDetection]");
const QString ExternalDownbeatSettings::kCommandItem =
        QStringLiteral("DownbeatCommand");
const QString ExternalDownbeatSettings::kTimeoutItem =
        QStringLiteral("DownbeatTimeoutSeconds");
const QString ExternalDownbeatSettings::kDetectorItem =
        QStringLiteral("DownbeatDetector");

// static
QString ExternalDownbeatSettings::defaultCommand() {
    return QStringLiteral("beat_this --gpu 0 -o \"$OUTPUT\" \"$INPUT\"");
}

void ExternalDownbeatSettings::setTimeoutSeconds(int seconds) {
    m_timeoutSeconds = std::clamp(seconds, kMinTimeoutSeconds, kMaxTimeoutSeconds);
}

// static
ExternalDownbeatSettings ExternalDownbeatSettings::readFrom(
        const UserSettingsPointer& pConfig) {
    ExternalDownbeatSettings settings;
    if (!pConfig) {
        return settings;
    }
    settings.m_command = pConfig->getValue(
            ConfigKey(kConfigGroup, kCommandItem), defaultCommand());
    settings.setTimeoutSeconds(pConfig->getValue(
            ConfigKey(kConfigGroup, kTimeoutItem), kDefaultTimeoutSeconds));
    settings.m_detector = pConfig->getValue(
                                  ConfigKey(kConfigGroup, kDetectorItem),
                                  kBuiltInValue) == kExternalValue
            ? DownbeatDetectorChoice::ExternalCommand
            : DownbeatDetectorChoice::BuiltIn;
    return settings;
}

void ExternalDownbeatSettings::writeTo(const UserSettingsPointer& pConfig) const {
    VERIFY_OR_DEBUG_ASSERT(pConfig) {
        return;
    }
    pConfig->setValue(ConfigKey(kConfigGroup, kCommandItem), m_command);
    pConfig->setValue(ConfigKey(kConfigGroup, kTimeoutItem), m_timeoutSeconds);
    pConfig->setValue(ConfigKey(kConfigGroup, kDetectorItem),
            m_detector == DownbeatDetectorChoice::ExternalCommand
                    ? kExternalValue
                    : kBuiltInValue);
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
        audio::SampleRate sampleRate) {
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
        // The beat list rises, thus a binary search finds the place.
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
    const std::vector<int> indices =
            mapTimesToBeats(times, beatPositions, sampleRate);
    const std::vector<int> votes = barPhaseVotes(indices, m_beatsPerBar);
    const DownbeatPhase phase = DownbeatDetector::scoreVotes(votes);
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

bool ExternalDownbeatDetector::runCommand(const QString& trackFilePath, QString* pOutput) {
    if (m_settings.command().trimmed().isEmpty()) {
        m_errorMessage = QObject::tr("No downbeat command is set.");
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

    QProcess process;
    process.setProgram(resolved.isEmpty() ? program : resolved);
    process.setArguments(command.mid(1));
    process.setWorkingDirectory(workDirectory.path());
    process.start(QIODevice::ReadOnly);
    if (!process.waitForStarted(kStartMilliseconds)) {
        m_errorMessage = QObject::tr("The program \"%1\" did not start: %2")
                                 .arg(program, process.errorString());
        return false;
    }

    QElapsedTimer runTimer;
    runTimer.start();
    const qint64 timeoutMilliseconds =
            static_cast<qint64>(m_settings.timeoutSeconds()) * 1000;
    while (!process.waitForFinished(kPollMilliseconds)) {
        if (process.state() == QProcess::NotRunning) {
            break;
        }
        const bool cancelled = m_cancelCheck && m_cancelCheck();
        if (!cancelled && runTimer.elapsed() < timeoutMilliseconds) {
            continue;
        }
        process.kill();
        process.waitForFinished(kStartMilliseconds);
        m_errorMessage = cancelled
                ? QObject::tr("The analysis of the track stopped.")
                : QObject::tr("The program \"%1\" ran longer than %2 seconds.")
                          .arg(program)
                          .arg(m_settings.timeoutSeconds());
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        const QString errorTail =
                QString::fromUtf8(process.readAllStandardError()).right(kErrorTailChars);
        m_errorMessage = QObject::tr("\"%1\" stopped with the code %2. %3")
                                 .arg(program,
                                         QString::number(process.exitCode()),
                                         errorTail.trimmed());
        return false;
    }

    // The command writes the beats to the output file when the template uses
    // $OUTPUT, and to its standard output when it does not.
    QFile outputFile(outputFilePath);
    if (outputFile.exists() && outputFile.size() > 0 &&
            outputFile.open(QIODevice::ReadOnly)) {
        *pOutput = QString::fromUtf8(outputFile.readAll());
        return true;
    }
    *pOutput = QString::fromUtf8(process.readAllStandardOutput());
    if (pOutput->trimmed().isEmpty()) {
        m_errorMessage = QObject::tr("The program \"%1\" wrote nothing.").arg(program);
        return false;
    }
    return true;
}

} // namespace mixxx
