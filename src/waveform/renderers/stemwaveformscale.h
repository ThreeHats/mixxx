#pragma once

#include <algorithm>
#include <array>
#include <limits>

#include "util/math.h"
#include "waveform/waveform.h"

namespace mixxx {

constexpr float kWaveformPeakMax =
        static_cast<float>(std::numeric_limits<unsigned char>::max());

/// The peak of the mix and the peak of each stem over one strip of a waveform.
struct StemStripPeaks {
    unsigned char all{};
    std::array<unsigned char, kMaxSupportedStems> stems{};

    unsigned char loudestStem(int stemCount) const {
        return *std::max_element(stems.cbegin(), stems.cbegin() + stemCount);
    }
};

/// Read the peak of the mix and of each stem out of one strip of the waveform
/// data. The data holds the left and the right channel one after the other.
inline StemStripPeaks stemStripPeaks(const WaveformData* pData,
        int indexStart,
        int indexStop,
        int stemCount) {
    StemStripPeaks peaks;
    for (int chn = 0; chn < 2; chn++) {
        for (int i = indexStart + chn; i < indexStop + chn; i += 2) {
            const WaveformData& datum = pData[i];
            peaks.all = math_max(peaks.all, datum.filtered.all);
            for (int stemIdx = 0; stemIdx < stemCount; stemIdx++) {
                peaks.stems[stemIdx] = math_max(
                        peaks.stems[stemIdx], datum.stems[stemIdx]);
            }
        }
    }
    return peaks;
}

/// The factor that lifts the overlaid stems to the height of the mix. The
/// four stems sum to the mix, thus one stem alone is much smaller.
inline float stemOverlayScale(unsigned char allPeak, unsigned char loudestStemPeak) {
    if (allPeak == 0 || loudestStemPeak == 0) {
        return 1.0f;
    }
    return static_cast<float>(allPeak) / static_cast<float>(loudestStemPeak);
}

/// The half height in pixels that a signal renderer draws for the mix.
inline float mixStripHalfHeight(unsigned char allPeak,
        float allGain,
        float halfBreadth) {
    return allGain * halfBreadth * static_cast<float>(allPeak) / kWaveformPeakMax;
}

/// The half height in pixels that the stem renderer draws for one stem. The
/// scale lifts the stems to the mix, the volume is the fader of the stem.
inline float stemStripHalfHeight(unsigned char stemPeak,
        float stemScale,
        float volume,
        float allGain,
        float halfBreadth) {
    return allGain * halfBreadth * static_cast<float>(stemPeak) * stemScale *
            volume / kWaveformPeakMax;
}

} // namespace mixxx
