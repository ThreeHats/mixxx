#pragma once

#include <QList>

#include "track/trackid.h"

namespace muxic {

/// A deck and the track that it holds.
struct DeckTrack {
    /// The deck number, one based.
    int deckNumber = 0;
    TrackId trackId;

    bool operator==(const DeckTrack& other) const {
        return deckNumber == other.deckNumber && trackId == other.trackId;
    }
    bool operator!=(const DeckTrack& other) const {
        return !(*this == other);
    }
};

using DeckTrackList = QList<DeckTrack>;

} // namespace muxic
