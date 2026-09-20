#pragma once

#include <QList>

#include "proto/keys.pb.h"

namespace muxic {

/// Finds the tracks that can follow a reference track, from the tempo and
/// the key only. The harmonic rule comes from KeyUtils::getCompatibleKeys(),
/// which walks the Circle of Fifths. Upstream pull request 16554 by
/// FaithfulSparrow applies the same rule to tint the library rows.
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

    /// True if the tempo lies in one of the windows of the reference tempo.
    static bool matchesBpm(
            double bpm,
            double referenceBpm,
            double tolerance = kBpmTolerance);

    /// The keys that mix with the reference key: the key itself, the
    /// relative major or minor, and one step in each direction on the wheel.
    /// The list is empty for an unknown key.
    static QList<mixxx::track::io::key::ChromaticKey> compatibleKeys(
            mixxx::track::io::key::ChromaticKey referenceKey);

    /// True if the key mixes with the reference key.
    static bool matchesKey(
            mixxx::track::io::key::ChromaticKey key,
            mixxx::track::io::key::ChromaticKey referenceKey);
};

} // namespace muxic
