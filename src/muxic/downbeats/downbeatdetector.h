#pragma once

#include <QVector>
#include <memory>
#include <vector>

#include "audio/frame.h"
#include "audio/types.h"
#include "util/types.h"

class DownBeat;

namespace mixxx {

class Beats;

/// The bar phase that the detector found in one track.
struct DownbeatPhase {
    /// The place of the first downbeat in the beat list, from 0 to the beats
    /// of a bar minus one. It means nothing while `accepted` is false.
    int phase = 0;
    /// The part of the bars that voted for `phase`, from 0 to 1, after the
    /// quarter that chance gives. It is a report for the log and for a test,
    /// not the rule that takes the phase.
    double confidence = 0.0;
    /// True when the vote passed the significance test. Only then does the
    /// track take the phase.
    bool accepted = false;
};

/// Finds which beat of a track is the first beat of a bar.
///
/// The detector takes the audio of the beat analyzer, hands it to the bar
/// tracker of qm-dsp, and scores the phase candidates again to get a
/// confidence. The method is the spectral difference of Davies and Plumbley,
/// EUSIPCO 2006. See `tools/muxic/docs/downbeats.md`.
class DownbeatDetector {
  public:
    /// A track with fewer beats than this gives no phase. 64 beats are 16
    /// bars, which the significance test needs to say anything.
    static constexpr int kMinBeats = 64;
    /// The votes must stand this many standard deviations over the quarter
    /// that chance gives. A flat signal then passes about once in a hundred
    /// tracks, at each track length.
    static constexpr double kSigmaFactor = 3.0;

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
    /// beat `i + 1`. Each bar votes for the transition that changed the audio
    /// most, and `scoreVotes` decides.
    static DownbeatPhase scorePhases(const std::vector<double>& beatSd, int beatsPerBar);

    /// Apply the significance test to a vote histogram. `votes[p]` counts the
    /// bars that called the phase `p` the first beat of the bar, and the size
    /// of the list is the beats of a bar. A phase below zero lets the phase
    /// with the most votes win. `maxBars` caps the trials at the bars that
    /// the track holds; a value below zero takes the votes as they are.
    /// Fewer than `kMinBeats / beats of a bar` bars give no phase, because
    /// the law below fits a handful of trials badly.
    ///
    /// Under no bar structure the votes for one phase follow a binomial law
    /// with the chance 1 / beats of a bar, thus the result counts as found
    /// only when the votes stand `kSigmaFactor` standard deviations over that
    /// chance AND no second phase reaches the same mark.
    static DownbeatPhase scoreVotes(
            const std::vector<int>& votes, int phase = -1, int maxBars = -1);

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

/// The positions of the beats of `beats` from the track start to
/// `endPosition`. A grid holds beats before the track and after it, thus a
/// caller that wants a beat list must give an end.
QVector<audio::FramePos> gridBeatPositions(
        const Beats& beats, audio::FramePos endPosition);

} // namespace mixxx
