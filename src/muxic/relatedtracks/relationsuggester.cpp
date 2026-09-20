#include "muxic/relatedtracks/relationsuggester.h"

#include "track/keyutils.h"

namespace muxic {

// static
QList<RelationSuggester::BpmRange> RelationSuggester::bpmRanges(
        double referenceBpm,
        double tolerance) {
    QList<BpmRange> ranges;
    if (referenceBpm <= 0.0 || tolerance < 0.0) {
        return ranges;
    }
    // A track at the half or the double tempo mixes as well as one at the
    // same tempo.
    const double factors[] = {0.5, 1.0, 2.0};
    for (const double factor : factors) {
        const double bpm = referenceBpm * factor;
        ranges.append(BpmRange{bpm * (1.0 - tolerance), bpm * (1.0 + tolerance)});
    }
    return ranges;
}

// static
bool RelationSuggester::matchesBpm(
        double bpm,
        double referenceBpm,
        double tolerance) {
    if (bpm <= 0.0) {
        return false;
    }
    const QList<BpmRange> ranges = bpmRanges(referenceBpm, tolerance);
    for (const auto& range : ranges) {
        if (bpm >= range.lower && bpm <= range.upper) {
            return true;
        }
    }
    return false;
}

// static
QList<mixxx::track::io::key::ChromaticKey> RelationSuggester::compatibleKeys(
        mixxx::track::io::key::ChromaticKey referenceKey) {
    return KeyUtils::getCompatibleKeys(referenceKey);
}

// static
bool RelationSuggester::matchesKey(
        mixxx::track::io::key::ChromaticKey key,
        mixxx::track::io::key::ChromaticKey referenceKey) {
    return compatibleKeys(referenceKey).contains(key);
}

} // namespace muxic
