#pragma once

#include <QList>

#include "proto/keys.pb.h"

namespace muxic {

/// Finds the tracks that can follow a reference track, from the tempo and
/// the key only. The harmonic rule is KeyUtils::getCompatibleKeys(), which
/// walks the Circle of Fifths. Upstream pull request 16554 by
/// FaithfulSparrow tints the library rows with the same rule.
class RelationSuggester {
  public:
    /// A closed tempo window in beats per minute.
    struct BpmRange {
        double lower;
        double upper;
    };

    /// The width of a tempo window, as a part of the tempo.
    static constexpr double kBpmTolerance = 0.03;

    /// The tempo windows of a reference tempo: the same tempo, the half
    /// tempo and the double tempo. The list is empty for a tempo of zero.
    static QList<BpmRange> bpmRanges(
            double referenceBpm,
            double tolerance = kBpmTolerance);

    /// The keys that mix with the reference key: the key itself, the
    /// relative major or minor, and one step each way on the wheel.
    static QList<mixxx::track::io::key::ChromaticKey> compatibleKeys(
            mixxx::track::io::key::ChromaticKey referenceKey);
};

} // namespace muxic
