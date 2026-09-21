#pragma once

#include <array>
#include <optional>

#include "engine/controls/enginecontrol.h"
#include "osc/oscbeatfeed.h"
#include "track/beats.h"
#include "util/parented_ptr.h"

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
    /// When the first frame of the current buffer leaves the outputs, in
    /// CLOCK_MONOTONIC nanoseconds. 0 means that no time is known.
    qint64 outputStampNs() const;

    /// The place of a beat in the grid. It steps a cached iterator, thus a
    /// grid with many tempo markers needs no walk on each beat.
    qint32 beatIndex(const mixxx::BeatsPointer& pBeats, mixxx::audio::FramePos position);

    /// The frames that one buffer of `bufferSize` samples carries. A stem
    /// deck has more than two channels.
    double bufferFrames(std::size_t bufferSize);

    mixxx::BeatsPointer m_pBeats;
    parented_ptr<ControlProxy> m_pSampleRate;
    parented_ptr<ControlProxy> m_pMainDelay;
    std::array<char, kBeatGroupSize> m_groupName;
    bool m_sendsBeats;

    mixxx::audio::FramePos m_prevBeatPosition;
    mixxx::audio::FramePos m_nextBeatPosition;
    mixxx::audio::FramePos m_lastReportedBeatPosition;
    mixxx::audio::FramePos m_lastPosition;
    std::optional<mixxx::Beats::ConstIterator> m_beatIterator;
    qint32 m_beatIndex;
    qint32 m_seq;
};

} // namespace osc
} // namespace mixxx
