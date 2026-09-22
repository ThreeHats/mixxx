#pragma once

#include <QByteArray>
#include <QList>
#include <QString>
#include <QVector>
#include <memory>
#include <optional>

#include "audio/frame.h"
#include "audio/types.h"
#include "track/bpm.h"
#include "util/types.h"

#define BEAT_GRID_1_VERSION "BeatGrid-1.0"
#define BEAT_GRID_2_VERSION "BeatGrid-2.0"
#define BEAT_MAP_VERSION "BeatMap-1.0"

namespace mixxx {

class Beats;
typedef std::shared_ptr<const Beats> BeatsPointer;

/// A beat marker is denotes the border of a tempo section inside a track.
class BeatMarker {
  public:
    BeatMarker(mixxx::audio::FramePos position, int beatsTillNextMarker)
            : m_position(position), m_beatsTillNextMarker(beatsTillNextMarker) {
        DEBUG_ASSERT(m_position.isValid());
        DEBUG_ASSERT(!m_position.isFractional());
        DEBUG_ASSERT(m_beatsTillNextMarker > 0);
    }

    mixxx::audio::FramePos position() const {
        return m_position;
    }

    int beatsTillNextMarker() const {
        return m_beatsTillNextMarker;
    }

  private:
    mixxx::audio::FramePos m_position;
    int m_beatsTillNextMarker;
};

inline bool operator==(const BeatMarker& lhs, const BeatMarker& rhs) {
    return (lhs.position() == rhs.position() &&
            lhs.beatsTillNextMarker() == rhs.beatsTillNextMarker());
}

inline bool operator!=(const BeatMarker& lhs, const BeatMarker& rhs) {
    return !(lhs == rhs);
}

/// The bar phase of a beat grid: which beat of the grid is beat one, and how
/// many beats a bar has. A grid index counts from the anchor beat, which is
/// the beat at the first marker.
class BarPhase {
  public:
    static constexpr int kDefaultBeatsPerBar = 4;

    explicit BarPhase(int downbeatOffset, int beatsPerBar = kDefaultBeatsPerBar);

    /// The grid index of the first downbeat, from 0 to `beatsPerBar() - 1`.
    int downbeatOffset() const {
        return m_downbeatOffset;
    }

    int beatsPerBar() const {
        return m_beatsPerBar;
    }

    /// The place of the beat with the grid index `beatIndex` in its bar,
    /// from 1 to `beatsPerBar()`.
    int beatInBar(int beatIndex) const {
        const long long offset = static_cast<long long>(beatIndex) - m_downbeatOffset;
        return static_cast<int>(((offset % m_beatsPerBar) + m_beatsPerBar) %
                       m_beatsPerBar) +
                1;
    }

    bool isDownbeat(int beatIndex) const {
        return beatInBar(beatIndex) == 1;
    }

    /// The phase after a change of the beat length by `bpmScaleFactor`. A
    /// factor that is no whole number of beats rounds to the nearest beat.
    BarPhase scaled(double bpmScaleFactor) const;

  private:
    int m_downbeatOffset;
    int m_beatsPerBar;
};

inline bool operator==(const BarPhase& lhs, const BarPhase& rhs) {
    return lhs.downbeatOffset() == rhs.downbeatOffset() &&
            lhs.beatsPerBar() == rhs.beatsPerBar();
}

inline bool operator!=(const BarPhase& lhs, const BarPhase& rhs) {
    return !(lhs == rhs);
}

/// This class represents the beats of a track.
///
/// Internally, it uses the following data structure:
/// - 0 - N beat markers, followed by
/// - exactly one tempo marker ("last marker").
///
/// If the track has a constant tempo, there are 0 beat markers, and the last
/// marker is positioned at the first downbeat and is set to the tracks BPM.
///
/// All instances of this class are supposed to be managed by std::shared_ptr!
class Beats : private std::enable_shared_from_this<Beats> {
  public:
    class ConstIterator {
      public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type = mixxx::audio::FramePos;
        using difference_type = int;
        using pointer = value_type*;
        using reference = value_type&;

        ConstIterator(const Beats* beats,
                std::vector<BeatMarker>::const_iterator it,
                int beatOffset)
                : m_beats(beats),
                  m_it(it),
                  m_beatOffset(beatOffset) {
            updateValue();
        }

        mixxx::audio::FrameDiff_t beatLengthFrames() const;

        // Iterator methods

        const value_type& operator*() const {
            return m_value;
        }

        const value_type* operator->() const {
            return &m_value;
        }

        ConstIterator& operator++() {
            *this += 1;
            return *this;
        }

        ConstIterator& operator--() {
            *this -= 1;
            return *this;
        }

        ConstIterator operator++(difference_type) {
            ConstIterator tmp = *this;
            ++(*this);
            return tmp;
        }

        ConstIterator operator--(difference_type) {
            ConstIterator tmp = *this;
            --(*this);
            return tmp;
        }

        ConstIterator operator+(difference_type n) {
            ConstIterator tmp = *this;
            tmp += n;
            return tmp;
        }

        ConstIterator operator-(difference_type n) {
            ConstIterator tmp = *this;
            tmp -= n;
            return tmp;
        }

        ConstIterator operator+=(difference_type n);
        ConstIterator operator-=(difference_type n);

        difference_type operator-(const ConstIterator& other) const;

        friend bool operator==(const ConstIterator& lhs, const ConstIterator& rhs) {
            return lhs.m_beats == rhs.m_beats &&
                    lhs.m_it == rhs.m_it &&
                    lhs.m_beatOffset == rhs.m_beatOffset;
        }

        friend bool operator!=(const ConstIterator& lhs, const ConstIterator& rhs) {
            return !(lhs == rhs);
        }

      private:
        void updateValue();

        mixxx::audio::FramePos m_value;

        const Beats* m_beats;
        std::vector<BeatMarker>::const_iterator m_it;
        int m_beatOffset;
    };

    Beats(std::vector<BeatMarker> markers,
            mixxx::audio::FramePos lastMarkerPosition,
            mixxx::Bpm lastMarkerBpm,
            mixxx::audio::SampleRate sampleRate,
            const QString& subVersion,
            const std::optional<BarPhase>& barPhase = std::nullopt)
            : m_markers(std::move(markers)),
              m_lastMarkerPosition(lastMarkerPosition),
              m_lastMarkerBpm(lastMarkerBpm),
              m_sampleRate(sampleRate),
              m_barPhase(barPhase),
              m_subVersion(subVersion) {
        DEBUG_ASSERT(m_lastMarkerPosition.isValid());
        DEBUG_ASSERT(!m_lastMarkerPosition.isFractional());
        DEBUG_ASSERT(m_lastMarkerBpm.isValid());
        DEBUG_ASSERT(m_sampleRate.isValid());
    }

    Beats(mixxx::audio::FramePos lastMarkerPosition,
            mixxx::Bpm lastMarkerBpm,
            mixxx::audio::SampleRate sampleRate,
            const QString& subVersion,
            const std::optional<BarPhase>& barPhase = std::nullopt)
            : Beats(std::vector<BeatMarker>(),
                      lastMarkerPosition,
                      lastMarkerBpm,
                      sampleRate,
                      subVersion,
                      barPhase) {
    }

    ~Beats() = default;

    /// Returns an iterator pointing to the position of the first beat marker.
    ConstIterator cfirstmarker() const {
        return ConstIterator(this, m_markers.cbegin(), 0);
    }

    /// Returns an iterator pointing to the position of the first beat after
    /// the end beat marker.
    ConstIterator clastmarker() const {
        return ConstIterator(this, m_markers.cend(), 0);
    }

    /// Returns an iterator pointing to earliest representable beat position
    /// (which is INT_MIN beats before the first beat marker).
    ///
    /// Warning: Decrementing the iterator returned by this function will
    /// result in an integer underflow.
    ConstIterator cbegin() const {
        return ConstIterator(this, m_markers.cbegin(), std::numeric_limits<int>::lowest());
    }

    /// Returns an iterator pointing to latest representable beat position
    /// (which is INT_MAX beats behind the end beat marker).
    ///
    /// Warning: Incrementing the iterator returned by this function will
    /// result in an integer overflow.
    ConstIterator cend() const {
        return ConstIterator(this, m_markers.cend(), std::numeric_limits<int>::max());
    }

    ConstIterator iteratorFrom(audio::FramePos position) const;

    friend bool operator==(const Beats& lhs, const Beats& rhs) {
        return lhs.m_markers == rhs.m_markers &&
                lhs.m_lastMarkerPosition == rhs.m_lastMarkerPosition &&
                lhs.m_lastMarkerBpm == rhs.m_lastMarkerBpm &&
                lhs.m_barPhase == rhs.m_barPhase && lhs.m_sampleRate &&
                rhs.m_sampleRate;
    }

    friend bool operator!=(const Beats& lhs, const Beats& rhs) {
        return !(lhs == rhs);
    }

    BeatsPointer clonePointer() const {
        // All instances are immutable and can be shared safely
        return shared_from_this();
    }

    static mixxx::BeatsPointer fromByteArray(
            mixxx::audio::SampleRate sampleRate,
            const QString& beatsVersion,
            const QString& beatsSubVersion,
            const QByteArray& beatsSerialized);

    static BeatsPointer fromBeatGridByteArray(
            audio::SampleRate sampleRate,
            const QString& subVersion,
            const QByteArray& byteArray);

    static BeatsPointer fromBeatMapByteArray(
            audio::SampleRate sampleRate,
            const QString& subVersion,
            const QByteArray& byteArray);

    static mixxx::BeatsPointer fromConstTempo(
            audio::SampleRate sampleRate,
            audio::FramePos position,
            Bpm bpm,
            const QString& subVersion = QString(),
            const std::optional<BarPhase>& barPhase = std::nullopt);

    static mixxx::BeatsPointer fromBeatPositions(
            audio::SampleRate sampleRate,
            const QVector<audio::FramePos>& beatPositions,
            const QString& subVersion = QString(),
            const std::optional<BarPhase>& barPhase = std::nullopt);

    static mixxx::BeatsPointer fromBeatMarkers(
            audio::SampleRate sampleRate,
            const std::vector<BeatMarker>& beatMarker,
            const audio::FramePos lastMarkerPosition,
            const Bpm lastMarkerBpm,
            const QString& subVersion = QString());

    enum class BpmScale {
        Halve,
        TwoThirds,
        ThreeFourths,
        FourFifths,
        FiveFourths,
        FourThirds,
        ThreeHalves,
        Double,
    };

    /// Returns false if the beats implementation supports non-const beats.
    ///
    /// TODO: This is only needed for the "Asumme Constant Tempo" checkbox in
    /// `DlgTrackInfo`. This should probably be removed or reimplemented to
    /// check if all neighboring beats in this object have the same distance.
    bool hasConstantTempo() const {
        return m_markers.empty();
    }

    /// Serialize beats to QByteArray.
    QByteArray toByteArray() const;

    /// A string representing the version of the beat-processing code that
    /// produced this Beats instance. Used by BeatsFactory for associating a
    /// given serialization with the version that produced it.
    QString getVersion() const;
    /// A sub-version can be used to represent the preferences used to generate
    /// the beats object.
    QString getSubVersion() const {
        return m_subVersion;
    }

    ////////////////////////////////////////////////////////////////////////////
    // Beat calculations
    ////////////////////////////////////////////////////////////////////////////

    // TODO: We may want all of these find functions to return an integer
    //       instead of a double.
    // TODO: We may want to implement these with common code that returns
    //       the triple of closest, next, and prev.

    /// Starting from frame position `position`, return the frame position of
    /// the next beat in the track, or an invalid position if none exists. If
    /// `position` refers to the location of a beat, `position` is returned.
    audio::FramePos findNextBeat(audio::FramePos position) const;

    /// Starting from frame position `position`, return the frame position of
    /// the previous beat in the track, or an invalid position if none exists.
    /// If `position` refers to the location of beat, `position` is returned.
    audio::FramePos findPrevBeat(audio::FramePos position) const;

    /// Starting from frame position `position`, fill the frame position of the
    /// previous beat and next beat. Either can be invalid if none exists. If
    /// `position` refers to the location of the beat, the first value is
    /// `position`, and the second value is the next beat position. Returns
    /// `false` if *at least one* position is invalid.
    bool findPrevNextBeats(audio::FramePos position,
            audio::FramePos* prevBeatPosition,
            audio::FramePos* nextBeatPosition,
            bool snapToNearBeats) const;

    /// Return the frame position of the first beat in the track, or an invalid
    /// position if none exists.
    audio::FramePos firstBeat() const {
        return findNextBeat(mixxx::audio::kStartFramePos);
    }

    /// Starting from frame position `position`, return the frame position of
    /// the closest beat in the track, or an invalid position if none exists.
    audio::FramePos findClosestBeat(audio::FramePos position) const;

    /// Find the Nth beat from frame position `position`. Works with both
    /// positive and negative values of n. Calling findNthBeat with `n=0` is
    /// invalid and always returns an invalid frame position. Calling
    /// findNthBeat with `n=1` or `n=-1` is equivalent to calling
    /// `findNextBeat` and `findPrevBeat`, respectively. If `position` refers
    /// to the location of a beat, then `position` is returned. If no beat can
    /// be found, returns an invalid frame position.
    audio::FramePos findNthBeat(audio::FramePos position, int n) const;

    /// This function snaps the position to a beat if near.
    /// This is used for beat loops, where start and end positions might be slightly off
    /// due to rounding or quantizations by the engine buffer.
    /// It makes use of virtual findPrevNextBeats() as a single instance for snapping.
    audio::FramePos snapPosToNearBeat(audio::FramePos position) const;

    int numBeatsInRange(audio::FramePos startPosition, audio::FramePos endPosition) const;

    /// Find the frame position N beats away from `position`. The number of beats may be
    /// negative and does not need to be an integer. In this case the returned position will
    /// be between two beats as well at the same fraction.
    audio::FramePos findNBeatsFromPosition(
            audio::FramePos position, double beats) const;

    /// Return whether or not a beat exists between `startPosition` and `endPosition`.
    bool hasBeatInRange(audio::FramePos startPosition,
            audio::FramePos endPosition) const;

    /// Return the predominant BPM value between `startPosition` and `endPosition`
    /// if the BPM is valid, otherwise returns an invalid BPM value.
    mixxx::Bpm getBpmInRange(audio::FramePos startPosition,
            audio::FramePos endPosition) const;

    /// Return the arithmetic average BPM over the range of n*2 beats centered around
    /// frame position `position`. For example, n=4 results in an averaging of 8 beats.
    /// The returned Bpm value may be invalid.
    mixxx::Bpm getBpmAroundPosition(audio::FramePos position, int n) const;

    audio::SampleRate getSampleRate() const {
        return m_sampleRate;
    }

    const std::vector<BeatMarker>& getMarkers() const {
        return m_markers;
    }

    mixxx::audio::FramePos getLastMarkerPosition() const {
        return m_lastMarkerPosition;
    }
    mixxx::Bpm getLastMarkerBpm() const {
        return m_lastMarkerBpm;
    }

    ////////////////////////////////////////////////////////////////////////////
    // Bars
    ////////////////////////////////////////////////////////////////////////////

    /// The bar phase of this grid, or `nullopt` when no downbeat is known.
    const std::optional<BarPhase>& barPhase() const {
        return m_barPhase;
    }

    /// The grid index of `it`, counted from the anchor beat. The anchor beat
    /// has the index 0.
    int beatIndex(ConstIterator it) const {
        return it - cfirstmarker();
    }

    /// The place of the beat with the grid index `beatIndex` in its bar, from
    /// 1 to the beats of a bar. 0 means that this grid has no bar phase.
    int beatInBar(int beatIndex) const {
        return m_barPhase ? m_barPhase->beatInBar(beatIndex) : 0;
    }

    /// The place of the beat at or before `position` in its bar, from 1 to
    /// the beats of a bar. 0 means that this grid has no bar phase.
    int beatInBarAt(audio::FramePos position) const;

    ////////////////////////////////////////////////////////////////////////////
    // Beat mutations
    ////////////////////////////////////////////////////////////////////////////

    /// Translate all beats in the song by `offset` frames. Beats that lie
    /// before the start of the track or after the end of the track are *not*
    /// removed.
    //
    /// Returns a pointer to the modified beats object, or `nullopt` on
    /// failure.
    std::optional<BeatsPointer> tryTranslate(audio::FrameDiff_t offset) const;

    /// Translate all beats based on scalar 'xBeats', e.g. half a beat if xBeats
    /// is set to 0.5. Works only for tracks with constant BPM.
    //
    /// Returns a pointer to the modified beats object, or `nullopt` on
    /// failure.
    std::optional<BeatsPointer> tryTranslateBeats(double xBeats) const;

    /// Scale the position of every beat in the song by `scale`.
    //
    /// Returns a pointer to the modified beats object, or `nullopt` on
    /// failure.
    std::optional<BeatsPointer> tryScale(BpmScale scale) const;

    /// Adjust the beats so the global average BPM matches `bpm`.
    //
    /// Returns a pointer to the modified beats object, or `nullopt` on
    /// failure.
    std::optional<BeatsPointer> trySetBpm(mixxx::Bpm bpm) const;

    /// Give this grid another bar phase, or `nullopt` to drop the phase. No
    /// beat moves.
    //
    /// Returns a pointer to the modified beats object.
    BeatsPointer withBarPhase(const std::optional<BarPhase>& barPhase) const;

    /// Make the beat nearest to `position` the first beat of a bar. The beats
    /// of a bar stay, or become `BarPhase::kDefaultBeatsPerBar` when this grid
    /// has no phase yet.
    //
    /// Returns a pointer to the modified beats object, or `nullopt` when
    /// `position` has no beat near it.
    std::optional<BeatsPointer> trySetDownbeatNear(audio::FramePos position) const;

    /// Move the bar phase by `beats` beats.
    //
    /// Returns a pointer to the modified beats object, or `nullopt` when this
    /// grid has no phase.
    std::optional<BeatsPointer> tryShiftBarPhase(int beats) const;

  protected:
    /// Type tag for making public constructors of derived classes inaccessible.
    ///
    /// The constructors must be public for using std::make_shared().
    struct MakeSharedTag {};

    Beats() = default;

    bool isValid() const;

  private:
    Beats(const Beats&) = delete;
    Beats(Beats&&) = delete;

    QByteArray toBeatGridByteArray() const;
    QByteArray toBeatMapByteArray() const;

    mixxx::audio::FrameDiff_t firstBeatLengthFrames() const;
    mixxx::audio::FrameDiff_t lastBeatLengthFrames() const;

    std::vector<BeatMarker> m_markers;
    mixxx::audio::FramePos m_lastMarkerPosition;
    mixxx::Bpm m_lastMarkerBpm;
    mixxx::audio::SampleRate m_sampleRate;
    std::optional<BarPhase> m_barPhase;

    // The sub-version of this beatgrid.
    const QString m_subVersion;
};

} // namespace mixxx
