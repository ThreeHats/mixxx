#pragma once

#include "library/trackset/tracksettablemodel.h"
#include "track/trackid.h"

namespace muxic {

/// Shows the tracks that go with a reference track.
class RelatedTracksTableModel final : public TrackSetTableModel {
    Q_OBJECT

  public:
    enum class Mode {
        /// Each track that has a relation.
        AllRelated,
        /// The tracks that a relation leads to from the reference track.
        RelatedTo,
        /// The tracks that fit the tempo and the key of the reference track.
        SuggestedFor,
    };

    RelatedTracksTableModel(QObject* pParent,
            TrackCollectionManager* pTrackCollectionManager);
    ~RelatedTracksTableModel() final = default;

    void selectAllRelated();
    void selectRelatedTo(TrackId trackId);
    void selectSuggestedFor(TrackId trackId);
    /// Reads the table again with the mode and the reference track of the
    /// last select.
    void refresh();

    Mode mode() const {
        return m_mode;
    }
    TrackId referenceTrackId() const {
        return m_referenceTrackId;
    }

    void removeTracks(const QModelIndexList& indices) final;

    Capabilities getCapabilities() const final;
    QString modelKey(bool noSearch) const override;

  private:
    void setRelationTable(const QString& tableName, const QString& viewQuery);

    Mode m_mode = Mode::AllRelated;
    TrackId m_referenceTrackId;
};

} // namespace muxic
