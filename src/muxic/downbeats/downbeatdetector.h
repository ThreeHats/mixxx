#pragma once

#include <QVector>
#include <memory>
#include <vector>

#include "audio/frame.h"
#include "audio/types.h"
#include "util/types.h"

class DownBeat;

namespace mixxx {

/// The bar phase that the detector found in one track.
struct DownbeatPhase {
    /// The place of the first downbeat in the beat list, from 0 to the beats
    /// of a bar minus one.
    int phase = 0;
    /// How sure the detector is, from 0 to 1. 0 means that every phase is
    /// equal, which is what a signal with no bar structure gives.
    double confidence = 0.0;
};

/// Finds which beat of a track is the first beat of a bar.
///
/// The detector takes the audio of the beat analyzer, hands it to the bar
/// tracker of qm-dsp, and scores the phase candidates again to get a
/// confidence. The method is the spectral difference of Davies and Plumbley,
/// EUSIPCO 2006. See `tools/muxic/docs/downbeats.md`.
class DownbeatDetector {
  public:
    /// A track with fewer beats than this gives no phase.
    static constexpr int kMinBeats = 16;
    /// Below this confidence the detector reports no phase.
    static constexpr double kMinConfidence = 0.10;

    DownbeatDetector(audio::SampleRate sampleRate, int beatsPerBar);
    ~DownbeatDetector();

    /// Take one block of samples from the analyzer. `pIn` holds `count`
    /// samples of `channelCount` channels, one frame after the other.
    void processSamples(const CSAMPLE* pIn, SINT count, int channelCount);

    /// Find the bar phase of `beatPositions`. The confidence is 0 when the
    /// track gives the detector too little to work with.
    DownbeatPhase finalize(const QVector<audio::FramePos>& beatPositions);

    /// Score the phase candidates of a list of beat to beat differences.
    /// `beatSd[i]` is the change of the audio between the beat `i` and the
    /// beat `i + 1`.
    static DownbeatPhase scorePhases(const std::vector<double>& beatSd, int beatsPerBar);

  private:
    /// The frames that one block carries. The bar tracker needs blocks of one
    /// size, and that size must be a multiple of the decimation factor.
    static constexpr int kBlockFrames = 1024;
    /// The bar tracker works near 3 kHz.
    static constexpr int kDecimationFactor = 16;

    const int m_beatsPerBar;
    std::unique_ptr<DownBeat> m_pDownBeat;
    std::vector<float> m_block;
    int m_blockFill;
    int m_blocksPushed;
};

} // namespace mixxx
