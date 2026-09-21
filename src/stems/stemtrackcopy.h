#pragma once

#include "audio/types.h"
#include "track/beats.h"
#include "track/track_decl.h"

class Track;

namespace mixxx {

/// Make a beat grid for another sample rate. The beats keep their time.
BeatsPointer rescaleBeats(const BeatsPointer& pBeats, audio::SampleRate sampleRate);

/// Copy the tags, the cues, the grid and the key to a stem track.
/// A different sample rate keeps the time of each position.
void copyTrackToStem(const Track& sourceTrack, Track* pStemTrack);

} // namespace mixxx
