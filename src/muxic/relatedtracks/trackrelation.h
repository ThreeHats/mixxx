#pragma once

#include <QString>

#include "track/trackid.h"
#include "util/db/dbid.h"

namespace muxic {

class TrackRelationId : public DbId {
  public:
    // Inherit constructors from base class
    using DbId::DbId;
};

// The relation type that the menu and the control write. The column holds
// free text, thus the muxic hub can write any other type.
const QString kDefaultTrackRelationType = QStringLiteral("mix");

/// A relation between two library tracks that mix well.
///
/// The relation leads from the source track to the target track. A relation
/// that goes both ways is one row, thus a pair of tracks has at most one
/// relation.
class TrackRelation final {
  public:
    static constexpr int kMinRating = 0;
    static constexpr int kMaxRating = 5;

    TrackRelation() = default;
    TrackRelation(TrackId sourceTrackId, TrackId targetTrackId)
            : m_sourceTrackId(sourceTrackId),
              m_targetTrackId(targetTrackId) {
    }

    TrackRelationId getId() const {
        return m_id;
    }
    void setId(TrackRelationId id) {
        m_id = id;
    }

    TrackId getSourceTrackId() const {
        return m_sourceTrackId;
    }
    void setSourceTrackId(TrackId trackId) {
        m_sourceTrackId = trackId;
    }

    TrackId getTargetTrackId() const {
        return m_targetTrackId;
    }
    void setTargetTrackId(TrackId trackId) {
        m_targetTrackId = trackId;
    }

    bool isBidirectional() const {
        return m_bidirectional;
    }
    void setBidirectional(bool bidirectional) {
        m_bidirectional = bidirectional;
    }

    const QString& getType() const {
        return m_type;
    }
    void setType(const QString& type) {
        m_type = type;
    }

    int getRating() const {
        return m_rating;
    }
    void setRating(int rating) {
        m_rating = rating;
    }

    const QString& getNotes() const {
        return m_notes;
    }
    void setNotes(const QString& notes) {
        m_notes = notes;
    }

    /// A relation needs two different tracks.
    bool isValid() const {
        return m_sourceTrackId.isValid() &&
                m_targetTrackId.isValid() &&
                m_sourceTrackId != m_targetTrackId;
    }

    /// The other end of the relation, or an invalid id.
    TrackId otherTrackId(TrackId trackId) const {
        if (trackId == m_sourceTrackId) {
            return m_targetTrackId;
        }
        if (trackId == m_targetTrackId) {
            return m_sourceTrackId;
        }
        return TrackId();
    }

  private:
    TrackRelationId m_id;
    TrackId m_sourceTrackId;
    TrackId m_targetTrackId;
    bool m_bidirectional = false;
    QString m_type;
    int m_rating = kMinRating;
    QString m_notes;
};

} // namespace muxic

Q_DECLARE_TYPEINFO(muxic::TrackRelationId, Q_MOVABLE_TYPE);
Q_DECLARE_METATYPE(muxic::TrackRelationId)
