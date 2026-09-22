#pragma once

#include <QProcess>
#include <QString>
#include <QVector>
#include <functional>
#include <vector>

#include "audio/frame.h"
#include "audio/types.h"
#include "muxic/downbeats/downbeatdetector.h"
#include "preferences/beatdetectionsettings.h"
#include "preferences/usersettings.h"

namespace mixxx {

/// Which detector looks for the first beat of the bar.
enum class DownbeatDetectorChoice {
    BuiltIn,
    ExternalCommand,
};

/// The user settings of the external downbeat command. They live in the
/// group `[BPM]`, next to the downbeat checkbox.
class ExternalDownbeatSettings {
  public:
    /// The seconds that the command may run before the detector kills it.
    static constexpr int kDefaultTimeoutSeconds = 120;
    static constexpr int kMinTimeoutSeconds = 1;
    static constexpr int kMaxTimeoutSeconds = 3600;
    /// The commands that run at one time. One card holds one model well.
    static constexpr int kDefaultJobs = 1;
    static constexpr int kMinJobs = 1;
    static constexpr int kMaxJobs = 32;

    static QString defaultCommand();

    static ExternalDownbeatSettings readFrom(const UserSettingsPointer& pConfig);
    void writeTo(const UserSettingsPointer& pConfig) const;

    const QString& command() const {
        return m_command;
    }
    void setCommand(const QString& command) {
        m_command = command;
    }

    int timeoutSeconds() const {
        return m_timeoutSeconds;
    }
    void setTimeoutSeconds(int seconds);

    DownbeatDetectorChoice detector() const {
        return m_detector;
    }
    void setDetector(DownbeatDetectorChoice detector) {
        m_detector = detector;
    }

    int jobs() const {
        return m_jobs;
    }
    void setJobs(int jobs);

  private:
    QString m_command = defaultCommand();
    int m_timeoutSeconds = kDefaultTimeoutSeconds;
    int m_jobs = kDefaultJobs;
    DownbeatDetectorChoice m_detector = DownbeatDetectorChoice::BuiltIn;
};

/// The instant of each downbeat that the program reported, in seconds from
/// the track start. The program writes one line for each beat, with the time
/// and the place of the beat in its bar, and the first beat of a bar has the
/// place 1. A line that does not follow that form is skipped.
std::vector<double> parseDownbeatTimes(const QString& output);

/// A downbeat may stand this part of the beat period away from the beat of
/// the grid. A downbeat farther away belongs to another grid.
constexpr double kBeatToleranceFraction = 0.25;

/// The place in `beatPositions` of the beat nearest to each time of `times`.
/// A time before the first beat, after the last one, or farther from its
/// nearest beat than `tolerance` of the beat period is dropped. The list
/// comes back sorted, and it can hold the same place twice.
std::vector<int> mapTimesToBeats(const std::vector<double>& times,
        const QVector<audio::FramePos>& beatPositions,
        audio::SampleRate sampleRate,
        double tolerance = kBeatToleranceFraction);

/// Count the beats of `beatIndices` in each place of the bar.
std::vector<int> barPhaseVotes(const std::vector<int>& beatIndices, int beatsPerBar);

/// Finds the first beat of the bar with an external program.
///
/// The detector runs the command of the preferences on the track file, reads
/// the downbeat times that the program writes, maps each one to the nearest
/// beat of the grid that Mixxx computed, and hands the vote histogram to the
/// significance test of `DownbeatDetector`. See
/// `tools/muxic/docs/downbeats.md`.
class ExternalDownbeatDetector {
  public:
    ExternalDownbeatDetector(const ExternalDownbeatSettings& settings, int beatsPerBar);

    /// Stop the command as soon as `cancelCheck` returns true. The check runs
    /// on the thread that calls `detect`.
    void setCancelCheck(std::function<bool()> cancelCheck);

    /// Run the command on `trackFilePath` and score the phase against
    /// `beatPositions`. The phase is a place in `beatPositions`, from 0 to
    /// the beats of a bar minus one.
    DownbeatPhase detect(const QString& trackFilePath,
            const QVector<audio::FramePos>& beatPositions,
            audio::SampleRate sampleRate);

    /// Why the last `detect` gave no phase. It is empty when the command ran
    /// and the vote decided.
    const QString& errorMessage() const {
        return m_errorMessage;
    }

    /// The downbeats that the last run of the command reported.
    int downbeatCount() const {
        return m_downbeatCount;
    }

    /// The vote histogram of the last run, one count for each place of the
    /// bar. It is empty when the command gave nothing.
    const std::vector<int>& votes() const {
        return m_votes;
    }

  private:
    /// The detector asks this often whether the caller cancelled the work.
    static constexpr int kPollMilliseconds = 200;
    /// The program must start in this time.
    static constexpr int kStartMilliseconds = 10000;
    /// A killed program gets this long to die before the kill is harder.
    static constexpr int kKillMilliseconds = 2000;
    /// A program that reports more downbeats than this part of the bars of
    /// the track counted another tempo.
    static constexpr double kMaxDownbeatsPerBar = 1.5;

    bool runCommand(const QString& trackFilePath, QString* pOutput);
    bool waitForStart(QProcess* pProcess);
    /// Stop the program and everything that it started.
    void killProcessGroup(QProcess* pProcess);
    bool cancelled() const {
        return m_cancelCheck && m_cancelCheck();
    }

    const ExternalDownbeatSettings m_settings;
    const int m_beatsPerBar;
    std::function<bool()> m_cancelCheck;
    QString m_errorMessage;
    int m_downbeatCount;
    std::vector<int> m_votes;
};

/// What the detectors of one track found.
struct DownbeatResult {
    DownbeatPhase phase;
    /// The detector that gave `phase`. It is empty when none ran.
    QString source;
    /// Why the external command gave no phase. It is empty when the command
    /// did not run or when it found one.
    QString message;
};

/// Run the detectors in their order: the external command first when the
/// settings choose it, then `builtIn`. An empty `builtIn` means that no
/// built in detector holds the audio of this track.
DownbeatResult runDownbeatDetectors(const ExternalDownbeatSettings& settings,
        const QString& trackFilePath,
        const QVector<audio::FramePos>& beatPositions,
        audio::SampleRate sampleRate,
        const std::function<bool()>& cancelCheck,
        const std::function<DownbeatPhase()>& builtIn);

} // namespace mixxx
