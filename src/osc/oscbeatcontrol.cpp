#include "osc/oscbeatcontrol.h"

#include <chrono>
#include <cmath>

#include "control/controlproxy.h"
#include "engine/engine.h"
#include "moc_oscbeatcontrol.cpp"
#include "track/track.h"
#include "util/performancetimer.h"
#include "waveform/visualplayposition.h"

namespace {

const QString kAppGroup = QStringLiteral("[App]");

/// A rate below this is a stop, a scratch backwards or a rewind. The beat of
/// such a deck is no clock, thus the engine reports none.
constexpr double kMinRate = 0.01;

qint64 monotonicNowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
}

} // namespace

namespace mixxx {
namespace osc {

BeatControl::BeatControl(const QString& group, UserSettingsPointer pConfig)
        : EngineControl(group, pConfig),
          m_pSampleRate(std::make_unique<ControlProxy>(
                  kAppGroup, QStringLiteral("samplerate"), this)),
          m_prevBeatPosition(mixxx::audio::kInvalidFramePos),
          m_nextBeatPosition(mixxx::audio::kInvalidFramePos),
          m_lastReportedBeatPosition(mixxx::audio::kInvalidFramePos),
          m_seq(0) {
    BeatFeed::writeGroup(&m_groupName, group);
}

BeatControl::~BeatControl() = default;

void BeatControl::trackLoaded(TrackPointer pNewTrack) {
    mixxx::BeatsPointer pBeats;
    if (pNewTrack) {
        pBeats = pNewTrack->getBeats();
    }
    trackBeatsUpdated(pBeats);
}

void BeatControl::trackBeatsUpdated(mixxx::BeatsPointer pBeats) {
    m_pBeats = pBeats;
    m_prevBeatPosition = mixxx::audio::kInvalidFramePos;
    m_nextBeatPosition = mixxx::audio::kInvalidFramePos;
    m_lastReportedBeatPosition = mixxx::audio::kInvalidFramePos;
    m_seq = 0;
}

// static
qint64 BeatControl::bufferDacStampNs() {
    PerformanceTimer callbackEntry;
    double entryToDacSecs = 0.0;
    VisualPlayPosition::getCallbackEntryToDacSecs(&callbackEntry, &entryToDacSecs);
    if (!callbackEntry.running()) {
        return 0;
    }
    const qint64 entryNs = monotonicNowNs() - callbackEntry.elapsed().toIntegerNanos();
    return entryNs + static_cast<qint64>(std::llround(entryToDacSecs * 1e9));
}

void BeatControl::process(const double rate,
        mixxx::audio::FramePos currentPosition,
        const std::size_t bufferSize) {
    if (!BeatFeed::enabled()) {
        return;
    }
    const mixxx::BeatsPointer pBeats = m_pBeats;
    if (!pBeats || !currentPosition.isValid() || rate < kMinRate) {
        return;
    }
    const double sampleRate = m_pSampleRate->get();
    if (sampleRate <= 0) {
        return;
    }

    // `currentPosition` is the end of the buffer that the engine has just
    // made, and `rate` is the track frames that one output frame carries.
    const double bufferFrames = static_cast<double>(bufferSize) /
            mixxx::kEngineChannelOutputCount;
    const double framesAdvanced = rate * bufferFrames;

    if (!m_prevBeatPosition.isValid() || !m_nextBeatPosition.isValid() ||
            currentPosition >= m_nextBeatPosition ||
            currentPosition <= m_prevBeatPosition) {
        pBeats->findPrevNextBeats(currentPosition,
                &m_prevBeatPosition,
                &m_nextBeatPosition,
                false);
    }
    if (!m_prevBeatPosition.isValid() || !m_nextBeatPosition.isValid() ||
            m_prevBeatPosition == m_lastReportedBeatPosition) {
        return;
    }

    const double framesSinceBeat = currentPosition - m_prevBeatPosition;
    m_lastReportedBeatPosition = m_prevBeatPosition;
    if (framesSinceBeat < 0 || framesSinceBeat > framesAdvanced) {
        // A seek, a loop or a scratch put the beat outside of the buffer that
        // the engine has just made, thus its instant is unknown. Wait for the
        // next beat.
        return;
    }

    const qint64 dacStampNs = bufferDacStampNs();
    if (dacStampNs == 0) {
        return;
    }
    const double bufferSecs = bufferFrames / sampleRate;
    const double intoBufferSecs = (1.0 - framesSinceBeat / framesAdvanced) * bufferSecs;
    const double beatSecs = (m_nextBeatPosition - m_prevBeatPosition) / rate / sampleRate;

    BeatEvent event;
    event.group = m_groupName;
    event.stampNs = dacStampNs + static_cast<qint64>(std::llround(intoBufferSecs * 1e9));
    event.trackBeat = static_cast<qint32>(
            pBeats->iteratorFrom(m_prevBeatPosition) - pBeats->cfirstmarker());
    event.seq = ++m_seq;
    event.bpm = beatSecs > 0 ? static_cast<float>(60.0 / beatSecs) : 0.0f;
    BeatFeed::push(event);
}

} // namespace osc
} // namespace mixxx
