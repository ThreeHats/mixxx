#include "muxic/downbeats/downbeatdetector.h"

#include <dsp/tempotracking/DownBeat.h>

#include <algorithm>
#include <cmath>
#include <limits>

#include "util/assert.h"

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

    // The bar tracker wants the beats in blocks, and it reads the audio up to
    // the beat that comes after each one. A beat behind the audio that the
    // analyzer gave would give it an empty frame, thus the list stops there.
    std::vector<double> beats;
    beats.reserve(beatPositions.size());
    for (const audio::FramePos& position : beatPositions) {
        if (!position.isValid() || position < audio::kStartFramePos) {
            continue;
        }
        const double block = position.value() / kBlockFrames;
        if (block >= m_blocksPushed) {
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

    // `beatSd[i]` is the change into the beat `i + 1`, thus the candidate
    // that says "the beat `b` is a downbeat" collects the changes into the
    // beats `b`, `b + beatsPerBar` and so on.
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

    // Each bar votes for the transition that changed the audio most. The part
    // of the bars that voted for the winner tells how sure the detector is.
    int bars = 0;
    int votes = 0;
    for (int start = 0; start + beatsPerBar <= count; start += beatsPerBar) {
        int largest = start;
        for (int i = start + 1; i < start + beatsPerBar; i++) {
            if (beatSd[i] > beatSd[largest]) {
                largest = i;
            }
        }
        bars++;
        if ((largest + 1) % beatsPerBar == result.phase) {
            votes++;
        }
    }
    if (bars == 0) {
        return DownbeatPhase();
    }

    const double chance = 1.0 / beatsPerBar;
    const double part = static_cast<double>(votes) / bars;
    result.confidence = std::clamp((part - chance) / (1.0 - chance), 0.0, 1.0);
    return result;
}

} // namespace mixxx
