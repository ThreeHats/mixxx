#pragma once

#include <QString>
#include <QVector>
#include <functional>
#include <vector>

#include "audio/frame.h"
#include "audio/types.h"
#include "muxic/downbeats/downbeatdetector.h"
#include "preferences/usersettings.h"

namespace mixxx {

/// Which detector looks for the first beat of the bar.
enum class DownbeatDetectorChoice {
    BuiltIn,
    ExternalCommand,
};

/// The user settings of the external downbeat command.
class ExternalDownbeatSettings {
  public:
    static const QString kConfigGroup;
    static const QString kCommandItem;
    static const QString kTimeoutItem;
    static const QString kDetectorItem;

    /// The seconds that the command may run before the detector kills it.
    static constexpr int kDefaultTimeoutSeconds = 120;
    static constexpr int kMinTimeoutSeconds = 5;
    static constexpr int kMaxTimeoutSeconds = 3600;

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

  private:
    QString m_command = defaultCommand();
    int m_timeoutSeconds = kDefaultTimeoutSeconds;
    DownbeatDetectorChoice m_detector = DownbeatDetectorChoice::BuiltIn;
};

/// The instant of each downbeat that the program reported, in seconds from
/// the track start. The program writes one line for each beat, with the time
/// and the place of the beat in its bar, and the first beat of a bar has the
/// place 1. A line that does not follow that form is skipped.
std::vector<double> parseDownbeatTimes(const QString& output);

/// The place in `beatPositions` of the beat nearest to each time of `times`.
/// A time before the first beat or after the last one is dropped. The list
/// comes back sorted, and it can hold the same place twice.
std::vector<int> mapTimesToBeats(const std::vector<double>& times,
        const QVector<audio::FramePos>& beatPositions,
        audio::SampleRate sampleRate);

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

  private:
    /// The detector asks this often whether the caller cancelled the work.
    static constexpr int kPollMilliseconds = 200;
    /// The program must start in this time.
    static constexpr int kStartMilliseconds = 10000;

    bool runCommand(const QString& trackFilePath, QString* pOutput);

    const ExternalDownbeatSettings m_settings;
    const int m_beatsPerBar;
    std::function<bool()> m_cancelCheck;
    QString m_errorMessage;
    int m_downbeatCount;
};

} // namespace mixxx
