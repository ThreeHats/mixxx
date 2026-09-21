#pragma once

#include <algorithm>
#include <array>

#include "analyzer/constants.h"
#include "util/math.h"
#include "waveform/waveform.h"

namespace mixxx {

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

/// The factor that lifts the overlaid stem waveforms to the height of the
/// waveform of the same music in a file with no stems. The stems sum to the
/// mix, thus the loudest stem alone is much smaller than the mix. One factor
/// for all stems of a strip keeps their relative size.
inline float stemOverlayScale(unsigned char allPeak, unsigned char loudestStemPeak) {
    if (loudestStemPeak == 0) {
        return 1.0f;
    }
    return static_cast<float>(allPeak) / static_cast<float>(loudestStemPeak);
}

} // namespace mixxx
