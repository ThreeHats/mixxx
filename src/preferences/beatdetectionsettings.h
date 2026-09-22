#pragma once

#include <QString>

#include "preferences/usersettings.h"

#define VAMP_CONFIG_KEY "[Vamp]"

// VAMP_CONFIG_KEY Preferences. WARNING: Do not fix the "analyser" spelling here
// since user config files contain these.
#define VAMP_ANALYZER_BEAT_LIBRARY "AnalyserBeatLibrary"
#define VAMP_ANALYZER_BEAT_PLUGIN_ID "AnalyserBeatPluginID"

#define BPM_CONFIG_KEY "[BPM]"

// BPM_CONFIG_KEY Preferences
#define BPM_DETECTION_ENABLED "BPMDetectionEnabled"
#define BPM_FIXED_TEMPO_ASSUMPTION "BeatDetectionFixedTempoAssumption"
#define BPM_REANALYZE_WHEN_SETTINGS_CHANGE "ReanalyzeWhenSettingsChange"
#define BPM_REANALYZE_IMPORTED "ReanalyzeImported"
#define BPM_FAST_ANALYSIS_ENABLED "FastAnalysisEnabled"
#define BPM_STEM_STRATEGY "stem_strategy"
#define BPM_DOWNBEAT_DETECTION_ENABLED "DownbeatDetectionEnabled"
#define BPM_DOWNBEAT_DETECTOR "DownbeatDetector"
#define BPM_DOWNBEAT_COMMAND "DownbeatCommand"
#define BPM_DOWNBEAT_TIMEOUT_SECONDS "DownbeatTimeoutSeconds"
#define BPM_DOWNBEAT_COMMAND_JOBS "DownbeatCommandJobs"

class BeatDetectionSettings {
  public:
    enum class StemStrategy {
        Disabled = 0,
        // TODO (#13466) - detect if the stem is using compliant labels for its
        // channels and use the first channel if so - to be implemented
        // Automatic
        Enforced = 2
    };

    /// The two values of the downbeat detector setting.
    static constexpr auto kDownbeatDetectorBuiltIn = "builtin";
    static constexpr auto kDownbeatDetectorExternal = "external";
    /// The program that finds the downbeats of one track. See
    /// `tools/muxic/docs/downbeats.md`.
    static constexpr auto kDownbeatCommandDefault =
            "beat_this --gpu 0 -o \"$OUTPUT\" -- \"$INPUT\"";

    BeatDetectionSettings(UserSettingsPointer pConfig) : m_pConfig(pConfig) {}

    DEFINE_PREFERENCE_HELPERS(BpmDetectionEnabled, bool,
                              BPM_CONFIG_KEY, BPM_DETECTION_ENABLED, true);

    DEFINE_PREFERENCE_HELPERS(FixedTempoAssumption, bool,
                              BPM_CONFIG_KEY, BPM_FIXED_TEMPO_ASSUMPTION, true);

    DEFINE_PREFERENCE_HELPERS(ReanalyzeWhenSettingsChange, bool,
                              BPM_CONFIG_KEY, BPM_REANALYZE_WHEN_SETTINGS_CHANGE, false);

    DEFINE_PREFERENCE_HELPERS(StemStrategy,
            StemStrategy,
            BPM_CONFIG_KEY,
            BPM_STEM_STRATEGY,
            StemStrategy::Disabled);

    DEFINE_PREFERENCE_HELPERS(ReanalyzeImported,
            bool,
            BPM_CONFIG_KEY,
            BPM_REANALYZE_IMPORTED,
            false);

    DEFINE_PREFERENCE_HELPERS(FastAnalysis, bool,
                              BPM_CONFIG_KEY, BPM_FAST_ANALYSIS_ENABLED, false);

    DEFINE_PREFERENCE_HELPERS(DownbeatDetectionEnabled,
            bool,
            BPM_CONFIG_KEY,
            BPM_DOWNBEAT_DETECTION_ENABLED,
            true);

    DEFINE_PREFERENCE_HELPERS(DownbeatDetectorName,
            QString,
            BPM_CONFIG_KEY,
            BPM_DOWNBEAT_DETECTOR,
            QString::fromLatin1(kDownbeatDetectorBuiltIn));

    DEFINE_PREFERENCE_HELPERS(DownbeatCommand,
            QString,
            BPM_CONFIG_KEY,
            BPM_DOWNBEAT_COMMAND,
            QString::fromLatin1(kDownbeatCommandDefault));

    DEFINE_PREFERENCE_HELPERS(DownbeatTimeoutSeconds,
            int,
            BPM_CONFIG_KEY,
            BPM_DOWNBEAT_TIMEOUT_SECONDS,
            120);

    DEFINE_PREFERENCE_HELPERS(DownbeatCommandJobs,
            int,
            BPM_CONFIG_KEY,
            BPM_DOWNBEAT_COMMAND_JOBS,
            1);

    QString getBeatPluginId() const {
        return m_pConfig->getValue<QString>(ConfigKey(
                VAMP_CONFIG_KEY, VAMP_ANALYZER_BEAT_PLUGIN_ID));
    }
    void setBeatPluginId(const QString& plugin_id) {
        m_pConfig->setValue(
                ConfigKey(VAMP_CONFIG_KEY, VAMP_ANALYZER_BEAT_PLUGIN_ID),
                plugin_id);
    }

  private:
    UserSettingsPointer m_pConfig;
};
