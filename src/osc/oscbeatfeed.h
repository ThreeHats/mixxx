#pragma once

#include <QString>
#include <array>

namespace mixxx {
namespace osc {

/// Room for the longest group name that a deck, a sampler or a preview deck
/// has, with the terminator.
constexpr int kBeatGroupSize = 24;

/// One beat of one deck, as the engine thread measured it.
struct BeatEvent {
    /// The group of the deck, for example `[Channel1]`, zero terminated.
    std::array<char, kBeatGroupSize> group;
    /// CLOCK_MONOTONIC nanoseconds of the beat at the output of the sound
    /// card. See `tools/muxic/docs/osc.md` for the error of this number.
    qint64 stampNs;
    /// The place of the beat in the beat grid of the track. Beat 0 is the
    /// anchor beat of the grid.
    qint32 trackBeat;
    /// Counts the beats that this deck put in the queue since it took its
    /// beat grid. It tells a reader that a beat was dropped.
    qint32 seq;
    /// The beat rate that the listener hears, in beats per minute.
    float bpm;
};

/// The queue that carries beats from the engine thread to the OSC thread.
/// One writer (the engine thread) and one reader (the OSC thread) share it
/// without a lock. It is static because the engine builds its decks long
/// before the OSC service starts, in the same way that `VisualPlayPosition`
/// shares the time of the audio callback.
class BeatFeed {
  public:
    /// Put one beat in the queue. The engine thread calls this. It does not
    /// block and it does not allocate. A full queue drops the beat.
    static void push(const BeatEvent& event);

    /// Take up to `count` beats out of the queue. Returns how many it took.
    static int pop(BeatEvent* pEvents, int count);

    /// Drop the beats that are in the queue.
    static void clear();

    /// The engine thread measures a beat only while this is true.
    static void setEnabled(bool enabled);
    static bool enabled();

    /// Fill `group` from a Mixxx group name. Long names are cut.
    static void writeGroup(std::array<char, kBeatGroupSize>* pGroup,
            const QString& group);

    /// The group name of an event.
    static QString readGroup(const BeatEvent& event);
};

} // namespace osc
} // namespace mixxx
