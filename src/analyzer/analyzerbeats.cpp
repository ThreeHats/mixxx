#include "analyzer/analyzerbeats.h"

#include <QHash>
#include <QString>
#include <QVector>
#include <QtDebug>

#include "analyzer/analyzertrack.h"
#include "analyzer/constants.h"
#include "analyzer/plugins/analyzerqueenmarybeats.h"
#include "analyzer/plugins/analyzersoundtouchbeats.h"
#include "library/rekordbox/rekordboxconstants.h"
#include "muxic/downbeats/downbeatdetector.h"
#include "track/beatfactory.h"
#include "track/track.h"

// static
QList<mixxx::AnalyzerPluginInfo> AnalyzerBeats::availablePlugins() {
    QList<mixxx::AnalyzerPluginInfo> plugins;
    // First one below is the default
    plugins.append(mixxx::AnalyzerQueenMaryBeats::pluginInfo());
    plugins.append(mixxx::AnalyzerSoundTouchBeats::pluginInfo());
    return plugins;
}

// static
mixxx::AnalyzerPluginInfo AnalyzerBeats::defaultPlugin() {
    const auto plugins = availablePlugins();
    DEBUG_ASSERT(!plugins.isEmpty());
    return plugins.at(0);
}

AnalyzerBeats::AnalyzerBeats(UserSettingsPointer pConfig, bool enforceBpmDetection)
        : m_pConfig(pConfig),
          m_bpmSettings(pConfig),
          m_enforceBpmDetection(enforceBpmDetection),
          m_bPreferencesReanalyzeOldBpm(false),
          m_bPreferencesReanalyzeImported(false),
          m_bPreferencesFixedTempo(true),
          m_bPreferencesFastAnalysis(false),
          m_downbeatOnly(false),
          m_maxFramesToProcess(0),
          m_currentFrame(0) {
}

void AnalyzerBeats::setCancelCheck(std::function<bool()> cancelCheck) {
    m_cancelCheck = std::move(cancelCheck);
}

bool AnalyzerBeats::initialize(const AnalyzerTrack& track,
        mixxx::audio::SampleRate sampleRate,
        mixxx::audio::ChannelCount channelCount,
        SINT frameLength) {
    if (frameLength <= 0) {
        return false;
    }

    bool bpmLock = track.getTrack()->isBpmLocked();
    if (bpmLock) {
        qDebug() << "Track is BpmLocked: Beat calculation will not start";
        return false;
    }

    m_downbeatOnly = track.getOptions().downbeatOnly;
    bool bPreferencesBeatDetectionEnabled = m_enforceBpmDetection ||
            m_downbeatOnly || m_bpmSettings.getBpmDetectionEnabled();
    if (!bPreferencesBeatDetectionEnabled) {
        qDebug() << "Beat calculation is deactivated";
        return false;
    }

    // The command template, the timeout and the jobs come from the settings
    // of this moment, thus a change reaches the next track of a running job.
    m_downbeatSettings = mixxx::ExternalDownbeatSettings::readFrom(m_pConfig);

    if (m_downbeatOnly) {
        // The grid stays. The detector works against the beats that the
        // track has, thus a track with no grid has nothing to work with.
        if (!track.getTrack()->getBeats()) {
            qDebug() << "Downbeat detection needs a beat grid: skipping"
                     << track.getTrack()->getLocation();
            return false;
        }
        m_sampleRate = sampleRate;
        m_channelCount = channelCount;
        m_maxFramesToProcess = frameLength;
        m_currentFrame = 0;
        // The command reads the track file, thus the built in detector, its
        // buffer of decimated audio and the mix to mono are work for nothing
        // while the command is the choice. There is no fallback then.
        if (m_downbeatSettings.detector() != mixxx::DownbeatDetectorChoice::ExternalCommand) {
            m_pDownbeatDetector = std::make_unique<mixxx::DownbeatDetector>(
                    m_sampleRate, mixxx::BarPhase::kDefaultBeatsPerBar);
        }
        return true;
    }

    m_bPreferencesFixedTempo = track.getOptions().useFixedTempo.value_or(
            m_bpmSettings.getFixedTempoAssumption());
    m_bPreferencesReanalyzeOldBpm = m_bpmSettings.getReanalyzeWhenSettingsChange();
    m_bPreferencesReanalyzeImported = m_bpmSettings.getReanalyzeImported();
    m_bPreferencesFastAnalysis = m_bpmSettings.getFastAnalysis();

    const auto plugins = availablePlugins();
    if (!plugins.isEmpty()) {
        m_pluginId = defaultPlugin().id();
        QString pluginId = m_bpmSettings.getBeatPluginId();
        for (const auto& info : plugins) {
            if (info.id() == pluginId) {
                m_pluginId = pluginId; // configured Plug-In available
                break;
            }
        }
    }

    qDebug() << "AnalyzerBeats preference settings:"
             << "\nPlugin:" << m_pluginId
             << "\nFixed tempo assumption:" << m_bPreferencesFixedTempo
             << "\nRe-analyze when settings change:" << m_bPreferencesReanalyzeOldBpm
             << "\nRe-analyze imported from other software:" << m_bPreferencesReanalyzeImported
             << "\nFast analysis:" << m_bPreferencesFastAnalysis;

    m_sampleRate = sampleRate;
    m_channelCount = channelCount;
    // In fast analysis mode, skip processing after
    // kFastAnalysisSecondsToAnalyze seconds are analyzed.
    if (m_bPreferencesFastAnalysis) {
        m_maxFramesToProcess =
                mixxx::kFastAnalysisSecondsToAnalyze * m_sampleRate;
    } else {
        m_maxFramesToProcess = frameLength;
    }
    m_currentFrame = 0;

    // if we can load a stored track don't reanalyze it
    bool bShouldAnalyze = shouldAnalyze(track.getTrack());

    DEBUG_ASSERT(!m_pPlugin);
    if (bShouldAnalyze) {
        if (m_pluginId == mixxx::AnalyzerQueenMaryBeats::pluginInfo().id()) {
            m_pPlugin = std::make_unique<mixxx::AnalyzerQueenMaryBeats>();
        } else if (m_pluginId == mixxx::AnalyzerSoundTouchBeats::pluginInfo().id()) {
            m_pPlugin = std::make_unique<mixxx::AnalyzerSoundTouchBeats>();
        } else {
            // This must not happen, because we have already verified above
            // that the PlugInId is valid
            DEBUG_ASSERT(false);
        }

        if (m_pPlugin) {
            if (m_pPlugin->initialize(m_sampleRate)) {
                qDebug() << "Beat calculation started with plugin" << m_pluginId;
            } else {
                qDebug() << "Beat calculation will not start.";
                m_pPlugin.reset();
                bShouldAnalyze = false;
            }
        } else {
            bShouldAnalyze = false;
        }
    }

    // The detector needs the beat positions of the plugin. A plugin that
    // gives only a BPM gives it nothing to work with.
    if (bShouldAnalyze && m_pPlugin->supportsBeatTracking() &&
            m_bpmSettings.getDownbeatDetectionEnabled()) {
        m_pDownbeatDetector = std::make_unique<mixxx::DownbeatDetector>(
                m_sampleRate, mixxx::BarPhase::kDefaultBeatsPerBar);
    }
    return bShouldAnalyze;
}

bool AnalyzerBeats::shouldAnalyze(TrackPointer pTrack) const {
    bool bpmLock = pTrack->isBpmLocked();
    if (bpmLock) {
        qDebug() << "Track is BpmLocked: Beat calculation will not start";
        return false;
    }

    QString pluginID = m_bpmSettings.getBeatPluginId();
    if (pluginID.isEmpty()) {
        pluginID = defaultPlugin().id();
    }

    // If the track already has a Beats object then we need to decide whether to
    // analyze this track or not.
    const mixxx::BeatsPointer pBeats = pTrack->getBeats();
    if (!pBeats) {
        return true;
    }
    if (!pBeats->getBpmInRange(mixxx::audio::kStartFramePos,
                       mixxx::audio::FramePos{
                               pTrack->getDuration() * pBeats->getSampleRate()})
                    .isValid()) {
        // Tracks with an invalid bpm <= 0 should be re-analyzed,
        // independent of the preference settings. We expect that
        // all tracks have a bpm > 0 when analyzed. Users that want
        // to keep their zero bpm tracks could lock them to prevent
        // this re-analysis (see the check above).
        qDebug() << "Re-analyzing track with invalid BPM despite preference settings.";
        return true;
    }

    QString subVersion = pBeats->getSubVersion();
    if (subVersion == mixxx::rekordboxconstants::beatsSubversion) {
        return m_bPreferencesReanalyzeImported;
    }

    if (subVersion.isEmpty() && pBeats->firstBeat() <= mixxx::audio::kStartFramePos &&
            m_pluginId != mixxx::AnalyzerSoundTouchBeats::pluginInfo().id()) {
        // This happens if the beat grid was created from the metadata BPM value.
        qDebug() << "First beat is 0 for grid so analyzing track to find first beat.";
        return true;
    }

    QString version = pBeats->getVersion();
    QHash<QString, QString> extraVersionInfo = getExtraVersionInfo(
            pluginID,
            m_bPreferencesFastAnalysis);
    QString newVersion = BeatFactory::getPreferredVersion(
            m_bPreferencesFixedTempo);
    QString newSubVersion = BeatFactory::getPreferredSubVersion(
            extraVersionInfo);

    if (version == newVersion && subVersion == newSubVersion) {
        // If the version and settings have not changed then if the world is
        // sane, re-analyzing will do nothing.
        return false;
    }
    // Beat grid exists but version and settings differ
    if (!m_bPreferencesReanalyzeOldBpm) {
        qDebug() << "Beat calculation skips analyzing because the track has"
                << "a BPM computed by a previous Mixxx version and user"
                << "preferences indicate we should not change it.";
        return false;
    }

    return true;
}

bool AnalyzerBeats::processSamples(const CSAMPLE* pIn, SINT count) {
    VERIFY_OR_DEBUG_ASSERT(m_pPlugin || m_downbeatOnly) {
        return false;
    }
    if (!m_pPlugin && !m_pDownbeatDetector) {
        // The external command reads the track file. Nothing here wants the
        // samples, thus the mix to mono and its buffer do not happen.
        return true;
    }

    SINT numFrames = count / m_channelCount;
    const CSAMPLE* pBeatInput = pIn;
    CSAMPLE* pDrumChannel = nullptr;

    if (m_channelCount == mixxx::audio::ChannelCount::stem()) {
        // We have an 8 channel soundsource. The only implemented soundsource with
        // 8ch is the NI STEM file format.
        // TODO: If we add other soundsources with 8ch, we need to rework this condition.
        //
        // For NI STEM we mix all the stems together except the first one,
        // which contains drums or beats by convention.
        count = numFrames * mixxx::audio::ChannelCount::stereo();
        pDrumChannel = SampleUtil::alloc(count);

        VERIFY_OR_DEBUG_ASSERT(pDrumChannel) {
            return false;
        }

        if (m_bpmSettings.getStemStrategy() == BeatDetectionSettings::StemStrategy::Enforced) {
            SampleUtil::copyOneStereoFromMulti(pDrumChannel, pIn, numFrames, m_channelCount, 0);
        } else {
            SampleUtil::mixMultichannelToStereo(pDrumChannel, pIn, numFrames, m_channelCount);
        }

        pBeatInput = pDrumChannel;
    } else if (m_channelCount > mixxx::audio::ChannelCount::stereo()) {
        DEBUG_ASSERT(!"Unsupported channel count");
        return false;
    }

    m_currentFrame += numFrames;
    if (m_currentFrame > m_maxFramesToProcess) {
        return true; // silently ignore all remaining samples
    }

    if (m_pDownbeatDetector) {
        m_pDownbeatDetector->processSamples(pBeatInput,
                count,
                pDrumChannel ? mixxx::audio::ChannelCount::stereo()
                             : static_cast<int>(m_channelCount));
    }

    bool ret = m_pPlugin ? m_pPlugin->processSamples(pBeatInput, count) : true;
    if (pDrumChannel) {
        SampleUtil::free(pDrumChannel);
    }
    return ret;
}

void AnalyzerBeats::cleanup() {
    m_pPlugin.reset();
    m_pDownbeatDetector.reset();
}

void AnalyzerBeats::storeResults(TrackPointer pTrack) {
    if (m_downbeatOnly) {
        storeDownbeatOnly(pTrack);
        return;
    }

    VERIFY_OR_DEBUG_ASSERT(m_pPlugin) {
        return;
    }

    if (!m_pPlugin->finalize()) {
        qWarning() << "Beat/BPM analysis failed";
        return;
    }

    mixxx::BeatsPointer pBeats;
    QVector<mixxx::audio::FramePos> beats;
    if (m_pPlugin->supportsBeatTracking()) {
        beats = m_pPlugin->getBeats();
        QHash<QString, QString> extraVersionInfo = getExtraVersionInfo(
                m_pluginId, m_bPreferencesFastAnalysis);
        pBeats = BeatFactory::makePreferredBeats(
                beats,
                extraVersionInfo,
                m_bPreferencesFixedTempo,
                m_sampleRate);
        qDebug() << "AnalyzerBeats plugin detected" << beats.size()
                 << "beats. Predominant BPM:"
                 << (pBeats ? pBeats->getBpmInRange(
                                      mixxx::audio::kStartFramePos,
                                      mixxx::audio::FramePos{
                                              pTrack->getDuration() *
                                              pBeats->getSampleRate()})
                            : mixxx::Bpm());
    } else {
        mixxx::Bpm bpm = m_pPlugin->getBpm();
        qDebug() << "AnalyzerBeats plugin detected constant BPM: " << bpm;
        pBeats = mixxx::Beats::fromConstTempo(m_sampleRate, mixxx::audio::kStartFramePos, bpm);
    }

    if (pBeats && !beats.isEmpty() && m_bpmSettings.getDownbeatDetectionEnabled()) {
        pBeats = detectDownbeat(pTrack, pBeats, beats);
    }

    pTrack->trySetBeats(pBeats);
}

void AnalyzerBeats::storeDownbeatOnly(const TrackPointer& pTrack) {
    const mixxx::BeatsPointer pBeats = pTrack->getBeats();
    if (!pBeats) {
        return;
    }
    const mixxx::audio::FramePos endPosition{
            pTrack->getDuration() * pBeats->getSampleRate()};
    const QVector<mixxx::audio::FramePos> beatPositions =
            mixxx::gridBeatPositions(*pBeats, endPosition);
    if (beatPositions.isEmpty()) {
        return;
    }
    const mixxx::BeatsPointer pWithBarPhase =
            detectDownbeat(pTrack, pBeats, beatPositions);
    // A grid that keeps its phase needs no write, thus the track stays clean.
    if (pWithBarPhase && pWithBarPhase->barPhase() != pBeats->barPhase()) {
        pTrack->trySetBeats(pWithBarPhase);
    }
}

mixxx::BeatsPointer AnalyzerBeats::detectDownbeat(const TrackPointer& pTrack,
        const mixxx::BeatsPointer& pBeats,
        const QVector<mixxx::audio::FramePos>& beatPositions) {
    const QString location = pTrack->getLocation();
    std::function<mixxx::DownbeatPhase()> builtIn;
    if (m_pDownbeatDetector) {
        builtIn = [this, &beatPositions]() {
            return m_pDownbeatDetector->finalize(beatPositions);
        };
    }
    const mixxx::DownbeatResult result = mixxx::runDownbeatDetectors(
            m_downbeatSettings,
            location,
            beatPositions,
            m_sampleRate,
            m_cancelCheck,
            builtIn);
    if (result.source.isEmpty()) {
        return pBeats;
    }
    qDebug() << "muxic downbeat:" << location << "source" << result.source
             << "accepted" << result.phase.accepted << "phase" << result.phase.phase
             << "confidence" << result.phase.confidence
             << (builtIn ? QString() : QStringLiteral("no fallback"))
             << result.message;
    if (!result.phase.accepted || result.phase.phase >= beatPositions.size()) {
        return pBeats;
    }
    const auto pWithBarPhase =
            pBeats->trySetDownbeatNear(beatPositions.at(result.phase.phase));
    return pWithBarPhase ? *pWithBarPhase : pBeats;
}

// static
QHash<QString, QString> AnalyzerBeats::getExtraVersionInfo(
        const QString& pluginId, bool bPreferencesFastAnalysis) {
    QHash<QString, QString> extraVersionInfo;
    extraVersionInfo["vamp_plugin_id"] = pluginId;
    if (bPreferencesFastAnalysis) {
        extraVersionInfo["fast_analysis"] = "1";
    }
    return extraVersionInfo;
}
