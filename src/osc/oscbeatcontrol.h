#pragma once

#include <array>
#include <memory>

#include "engine/controls/enginecontrol.h"
#include "osc/oscbeatfeed.h"
#include "track/beats.h"

class ControlProxy;

namespace mixxx {
namespace osc {

/// Measures the instant of each beat of one deck for `BeatFeed`. The engine
/// thread runs it, thus it allocates nothing and it locks nothing.
class BeatControl : public EngineControl {
    Q_OBJECT

  public:
    BeatControl(const QString& group, UserSettingsPointer pConfig);
    ~BeatControl() override;

    void process(const double rate,
            mixxx::audio::FramePos currentPosition,
            const std::size_t bufferSize) override;
    void trackLoaded(TrackPointer pNewTrack) override;
    void trackBeatsUpdated(mixxx::BeatsPointer pBeats) override;

  private:
    /// When the first frame of the current buffer reaches the sound card, in
    /// CLOCK_MONOTONIC nanoseconds. 0 means that no time is known.
    static qint64 bufferDacStampNs();

    mixxx::BeatsPointer m_pBeats;
    std::unique_ptr<ControlProxy> m_pSampleRate;
    std::array<char, kBeatGroupSize> m_groupName;

    mixxx::audio::FramePos m_prevBeatPosition;
    mixxx::audio::FramePos m_nextBeatPosition;
    mixxx::audio::FramePos m_lastReportedBeatPosition;
    qint32 m_seq;
};

} // namespace osc
} // namespace mixxx
