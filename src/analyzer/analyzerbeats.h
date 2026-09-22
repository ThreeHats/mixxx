#pragma once

#include <QHash>
#include <QList>
#include <QVector>
#include <functional>
#include <memory>

#include "analyzer/analyzer.h"
#include "analyzer/plugins/analyzerplugin.h"
#include "muxic/downbeats/downbeatdetector.h"
#include "muxic/downbeats/externaldownbeatdetector.h"
#include "preferences/beatdetectionsettings.h"
#include "preferences/usersettings.h"
#include "track/beats.h"

class AnalyzerBeats : public Analyzer {
  public:
    explicit AnalyzerBeats(
            UserSettingsPointer pConfig,
            bool enforceBpmDetection = false);
    ~AnalyzerBeats() override = default;

    static QList<mixxx::AnalyzerPluginInfo> availablePlugins();
    static mixxx::AnalyzerPluginInfo defaultPlugin();

    /// Stop the external downbeat command as soon as `cancelCheck` returns
    /// true. The check runs on the analyzer thread.
    void setCancelCheck(std::function<bool()> cancelCheck);

    bool initialize(const AnalyzerTrack& track,
            mixxx::audio::SampleRate sampleRate,
            mixxx::audio::ChannelCount channelCount,
            SINT frameLength) override;
    bool processSamples(const CSAMPLE* pIn, SINT count) override;
    void storeResults(TrackPointer tio) override;
    void cleanup() override;

  private:
    bool shouldAnalyze(TrackPointer pTrack) const;
    void storeDownbeatOnly(const TrackPointer& pTrack);
    /// Find the bar phase and put it in `pBeats`. Returns the grid with the
    /// phase, or `pBeats` when no detector found one.
    mixxx::BeatsPointer detectDownbeat(const TrackPointer& pTrack,
            const mixxx::BeatsPointer& pBeats,
            const QVector<mixxx::audio::FramePos>& beatPositions);
    static QHash<QString, QString> getExtraVersionInfo(
            const QString& pluginId, bool bPreferencesFastAnalysis);

    BeatDetectionSettings m_bpmSettings;
    mixxx::ExternalDownbeatSettings m_downbeatSettings;
    std::unique_ptr<mixxx::AnalyzerBeatsPlugin> m_pPlugin;
    std::unique_ptr<mixxx::DownbeatDetector> m_pDownbeatDetector;
    std::function<bool()> m_cancelCheck;
    const bool m_enforceBpmDetection;
    QString m_pluginId;
    bool m_bPreferencesReanalyzeOldBpm;
    bool m_bPreferencesReanalyzeImported;
    bool m_bPreferencesFixedTempo;
    bool m_bPreferencesFastAnalysis;
    bool m_downbeatOnly;

    mixxx::audio::SampleRate m_sampleRate;
    mixxx::audio::ChannelCount m_channelCount;
    SINT m_maxFramesToProcess;
    SINT m_currentFrame;
};
