#include "muxic/downbeats/downbeatdetector.h"

#include <dsp/tempotracking/DownBeat.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include "track/beats.h"
#include "util/assert.h"

namespace {

/// A grid holds an endless row of beats, thus a walk over it needs a bound
/// that no real track reaches.
constexpr int kMaxGridBeats = 1000000;

} // anonymous namespace

namespace mixxx {

DownbeatDetector::DownbeatDetector(audio::SampleRate sampleRate, int beatsPerBar)
        : m_beatsPerBar(std::max(2, beatsPerBar)),
          m_pDownBeat(std::make_unique<DownBeat>(
                  static_cast<float>(sampleRate.value()),
                  kDecimationFactor,
                  kBlockFrames)),
          m_block(kBlockFrames, 0.0f),
          m_blockFill(0),
          m_blocksPushed(0) {
    m_pDownBeat->setBeatsPerBar(m_beatsPerBar);
}

DownbeatDetector::~DownbeatDetector() = default;

void DownbeatDetector::processSamples(const CSAMPLE* pIn, SINT count, int channelCount) {
    VERIFY_OR_DEBUG_ASSERT(pIn && channelCount > 0) {
        return;
    }
    const SINT frames = count / channelCount;
    for (SINT frame = 0; frame < frames; frame++) {
        CSAMPLE sum = 0;
        for (int channel = 0; channel < channelCount; channel++) {
            sum += pIn[frame * channelCount + channel];
        }
        m_block[m_blockFill++] = static_cast<float>(sum / channelCount);
        if (m_blockFill == kBlockFrames) {
            m_pDownBeat->pushAudioBlock(m_block.data());
            m_blockFill = 0;
            m_blocksPushed++;
        }
    }
}

DownbeatPhase DownbeatDetector::finalize(const QVector<audio::FramePos>& beatPositions) {
    if (beatPositions.size() < kMinBeats || m_blocksPushed < 2) {
        return DownbeatPhase();
    }

    // The bar tracker wants the beats in blocks, and it reads the audio up
    // to the beat after each one. A beat behind the audio ends the list.
    std::vector<double> beats;
    beats.reserve(beatPositions.size());
    for (const audio::FramePos& position : beatPositions) {
        // A beat that the list cannot hold would move the later indices, and
        // the caller reads the phase as an index into its own list.
        if (!position.isValid() || position < audio::kStartFramePos) {
            return DownbeatPhase();
        }
        const double block = position.value() / kBlockFrames;
        if (block >= m_blocksPushed) {
            // The tail behind the audio drops off, thus the indices stand.
            break;
        }
        beats.push_back(block);
    }
    if (static_cast<int>(beats.size()) < kMinBeats) {
        return DownbeatPhase();
    }

    std::size_t audioLength = 0;
    const float* pAudio = m_pDownBeat->getBufferedAudio(audioLength);
    if (!pAudio || audioLength == 0) {
        return DownbeatPhase();
    }

    std::vector<int> downbeats;
    m_pDownBeat->findDownBeats(pAudio, audioLength, beats, downbeats);

    std::vector<double> beatSd;
    m_pDownBeat->getBeatSD(beatSd);
    return scorePhases(beatSd, m_beatsPerBar);
}

// static
DownbeatPhase DownbeatDetector::scorePhases(
        const std::vector<double>& beatSd, int beatsPerBar) {
    DownbeatPhase result;
    const int count = static_cast<int>(beatSd.size());
    if (beatsPerBar < 2 || count < beatsPerBar * 2) {
        return result;
    }
    for (const double value : beatSd) {
        if (!std::isfinite(value)) {
            return result;
        }
    }

    // `beatSd[i]` is the change into the beat `i + 1`. The candidate `b`
    // collects the changes into the beats `b`, `b + beatsPerBar` and so on.
    std::vector<double> sum(beatsPerBar, 0.0);
    std::vector<int> used(beatsPerBar, 0);
    for (int i = 0; i < count; i++) {
        const int candidate = (i + 1) % beatsPerBar;
        sum[candidate] += beatSd[i];
        used[candidate]++;
    }

    double bestMean = -std::numeric_limits<double>::max();
    for (int candidate = 0; candidate < beatsPerBar; candidate++) {
        if (used[candidate] == 0) {
            continue;
        }
        const double mean = sum[candidate] / used[candidate];
        if (mean > bestMean) {
            bestMean = mean;
            result.phase = candidate;
        }
    }

    // Each bar votes for the transition that changed the audio most.
    std::vector<int> votes(beatsPerBar, 0);
    for (int start = 0; start + beatsPerBar <= count; start += beatsPerBar) {
        int largest = start;
        for (int i = start + 1; i < start + beatsPerBar; i++) {
            if (beatSd[i] > beatSd[largest]) {
                largest = i;
            }
        }
        votes[(largest + 1) % beatsPerBar]++;
    }
    return scoreVotes(votes, result.phase);
}

// static
DownbeatPhase DownbeatDetector::scoreVotes(
        const std::vector<int>& votes, int phase, int maxBars) {
    DownbeatPhase result;
    const int beatsPerBar = static_cast<int>(votes.size());
    if (beatsPerBar < 2) {
        return result;
    }
    int bars = 0;
    int best = 0;
    for (int candidate = 0; candidate < beatsPerBar; candidate++) {
        if (votes[candidate] < 0) {
            return result;
        }
        bars += votes[candidate];
        if (votes[candidate] > votes[best]) {
            best = candidate;
        }
    }
    if (bars == 0) {
        return result;
    }

    // A program that counts a tempo of its own reports more downbeats than
    // the track has bars. Those votes are not one trial for each bar. The
    // cap holds the trials at the bars of the track.
    double trials = bars;
    double scale = 1.0;
    if (maxBars > 0 && bars > maxBars) {
        trials = maxBars;
        scale = trials / bars;
    }
    // A handful of bars says nothing, and the binomial law below fits such a
    // count badly. The built in path keeps this bound with kMinBeats already.
    if (trials < kMinBeats / beatsPerBar) {
        return result;
    }
    result.phase = (phase >= 0 && phase < beatsPerBar) ? phase : best;

    // Under no bar structure each bar votes for the winner with the chance
    // 1 / beatsPerBar, thus the votes follow a binomial law. A vote share
    // alone says nothing, because a short track reaches a large share by
    // chance. The phase counts as found only when the votes stand
    // `kSigmaFactor` standard deviations over the mean of that law.
    const double chance = 1.0 / beatsPerBar;
    const double mean = trials * chance;
    const double deviation = std::sqrt(trials * chance * (1.0 - chance));
    const double threshold = mean + kSigmaFactor * deviation;

    // Two phases over the mark mean that the two grids walk apart. One of
    // the phases is wrong, and a wrong bar is worse than no bar.
    double runnerUp = 0.0;
    for (int candidate = 0; candidate < beatsPerBar; candidate++) {
        if (candidate == result.phase) {
            continue;
        }
        runnerUp = std::max(runnerUp, votes[candidate] * scale);
    }

    const double part = static_cast<double>(votes[result.phase]) / bars;
    result.confidence = std::clamp((part - chance) / (1.0 - chance), 0.0, 1.0);
    result.accepted = votes[result.phase] * scale >= threshold && runnerUp < threshold;
    return result;
}

QVector<audio::FramePos> gridBeatPositions(
        const Beats& beats, audio::FramePos endPosition) {
    QVector<audio::FramePos> positions;
    if (!endPosition.isValid() || endPosition <= audio::kStartFramePos) {
        return positions;
    }
    audio::FramePos position = beats.firstBeat();
    while (position.isValid() && position <= endPosition &&
            positions.size() < kMaxGridBeats) {
        positions.append(position);
        const audio::FramePos next = beats.findNthBeat(position, 2);
        if (!next.isValid() || next <= position) {
            break;
        }
        position = next;
    }
    return positions;
}

} // namespace mixxx
