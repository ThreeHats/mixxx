#pragma once

#include <optional>

#include "track/track_decl.h"

/// A scheduled not-null track with additional options for analysis.
class AnalyzerTrack {
  public:
    struct Options {
        /// If set, overrides whether the analysis should assume constant BPM.
        std::optional<bool> useFixedTempo;
        /// Look for the first beat of the bar against the beat grid that the
        /// track has, and leave that grid alone.
        bool downbeatOnly = false;
    };

    // Two constructors, because a default argument of `Options()` would ask
    // for the member default of a nested class before the class ends.
    explicit AnalyzerTrack(TrackPointer track);
    AnalyzerTrack(TrackPointer track, Options options);

    /// Fetches the (not-null) track to be analyzed.
    const TrackPointer& getTrack() const;

    /// Fetches the additional options.
    const Options& getOptions() const;

  private:
    /// The (not-null) track to be analyzed.
    TrackPointer m_track;
    /// The additional options.
    Options m_options;
};
