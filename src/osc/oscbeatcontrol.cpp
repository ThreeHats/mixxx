#include "osc/oscbeatcontrol.h"

#include <chrono>
#include <cmath>

#include "control/controlproxy.h"
#include "engine/enginebuffer.h"
#include "moc_oscbeatcontrol.cpp"
#include "osc/oscconfig.h"
#include "track/track.h"
#include "util/performancetimer.h"
#include "waveform/visualplayposition.h"

namespace {

const QString kAppGroup = QStringLiteral("[App]");
const QString kMainGroup = QStringLiteral("[Master]");

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
          m_pSampleRate(make_parented<ControlProxy>(
                  kAppGroup, QStringLiteral("samplerate"), this)),
          m_pMainDelay(make_parented<ControlProxy>(
                  kMainGroup, QStringLiteral("delay"), this)),
          m_sendsBeats(groupSendsBeats(group, Config::samplerBeatsEnabled(pConfig))),
          m_prevBeatPosition(mixxx::audio::kInvalidFramePos),
          m_nextBeatPosition(mixxx::audio::kInvalidFramePos),
          m_lastReportedBeatPosition(mixxx::audio::kInvalidFramePos),
          m_lastPosition(mixxx::audio::kInvalidFramePos),
          m_beatIndex(0),
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
    m_lastPosition = mixxx::audio::kInvalidFramePos;
    m_beatIterator.reset();
    m_beatIndex = 0;
    m_seq = 0;
}

qint64 BeatControl::outputStampNs() const {
    PerformanceTimer callbackEntry;
    double entryToDacSecs = 0.0;
    VisualPlayPosition::getCallbackEntryToDacSecs(&callbackEntry, &entryToDacSecs);
    if (!callbackEntry.running()) {
        return 0;
    }
    // The main delay of the mixer runs after the engine, thus it moves the
    // sound later than the buffer.
    const double mainDelaySecs = std::max(0.0, m_pMainDelay->get()) / 1000.0;
    const qint64 entryNs = monotonicNowNs() - callbackEntry.elapsed().toIntegerNanos();
    return entryNs + static_cast<qint64>(std::llround((entryToDacSecs + mainDelaySecs) * 1e9));
}

double BeatControl::bufferFrames(std::size_t bufferSize) {
    const EngineBuffer* pBuffer = getEngineBuffer();
    const int channelCount = pBuffer
            ? static_cast<int>(pBuffer->getChannelCount())
            : static_cast<int>(mixxx::kEngineChannelOutputCount);
    if (channelCount <= 0) {
        return 0;
    }
    return static_cast<double>(bufferSize) / channelCount;
}

qint32 BeatControl::beatIndex(
        const mixxx::BeatsPointer& pBeats, mixxx::audio::FramePos position) {
    if (m_beatIterator) {
        if (**m_beatIterator == position) {
            return m_beatIndex;
        }
        mixxx::Beats::ConstIterator next = *m_beatIterator;
        ++next;
        if (*next == position) {
            m_beatIterator = next;
            return ++m_beatIndex;
        }
    }
    const mixxx::Beats::ConstIterator it = pBeats->iteratorFrom(position);
    m_beatIterator = it;
    m_beatIndex = static_cast<qint32>(it - pBeats->cfirstmarker());
    return m_beatIndex;
}

void BeatControl::process(const double rate,
        mixxx::audio::FramePos currentPosition,
        const std::size_t bufferSize) {
    if (!m_sendsBeats || !BeatFeed::enabled() || !currentPosition.isValid()) {
        return;
    }

    const mixxx::audio::FramePos previousPosition = m_lastPosition;
    m_lastPosition = currentPosition;
    if (!previousPosition.isValid() || currentPosition < previousPosition) {
        // A loop wrap, a seek or reverse play moved the deck backwards. The
        // beat that it reported last can come again.
        m_lastReportedBeatPosition = mixxx::audio::kInvalidFramePos;
    }

    const mixxx::BeatsPointer pBeats = m_pBeats;
    const double sampleRate = m_pSampleRate->get();
    const double frames = bufferFrames(bufferSize);
    if (!pBeats || rate < kMinRate || sampleRate <= 0 || frames <= 0) {
        return;
    }

    // `currentPosition` is the end of the buffer that the engine made, and
    // `rate` is the track frames that one output frame carries.
    const double framesAdvanced = rate * frames;

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
    // A seek or a scratch can put the beat outside of the buffer. Its instant
    // is then unknown, thus the deck waits for the next beat.
    if (framesSinceBeat < 0 || framesSinceBeat > framesAdvanced) {
        return;
    }

    const qint64 stampNs = outputStampNs();
    if (stampNs == 0) {
        return;
    }
    const double bufferSecs = frames / sampleRate;
    const double intoBufferSecs = (1.0 - framesSinceBeat / framesAdvanced) * bufferSecs;
    const double beatSecs = (m_nextBeatPosition - m_prevBeatPosition) / rate / sampleRate;

    BeatEvent event;
    event.group = m_groupName;
    event.stampNs = stampNs + static_cast<qint64>(std::llround(intoBufferSecs * 1e9));
    event.trackBeat = beatIndex(pBeats, m_prevBeatPosition);
    event.seq = ++m_seq;
    event.bpm = beatSecs > 0 ? static_cast<float>(60.0 / beatSecs) : 0.0f;
    BeatFeed::push(event);
}

} // namespace osc
} // namespace mixxx
