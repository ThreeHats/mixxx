#pragma once

#include "audio/types.h"
#include "track/beats.h"
#include "track/track_decl.h"

class Track;

namespace mixxx {

/// Make a beat grid for another sample rate. The beats keep their time.
BeatsPointer rescaleBeats(const BeatsPointer& pBeats, audio::SampleRate sampleRate);

/// Copy the tags, the cues, the grid, the key and the track data of a source
/// track to a stem track. A difference of sample rate keeps the time.
void copyTrackToStem(const Track& sourceTrack, Track* pStemTrack);

} // namespace mixxx
