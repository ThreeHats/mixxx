#pragma once

#include <QMetaType>
#include <QModelIndex>
#include <QTimer>
#include <QVariant>
#include <memory>

#include "library/trackset/basetracksetfeature.h"
#include "muxic/relatedtracks/relatedtrackstablemodel.h"
#include "preferences/usersettings.h"
#include "track/track_decl.h"
#include "track/trackid.h"

class ControlProxy;
class ControlPushButton;
class Library;
class QAbstractItemModel;

namespace muxic {

class TrackRelationStorage;

/// What a sidebar node of the feature shows.
enum class RelatedNodeKind {
    Root,
    RelatedToSelected,
    RelatedToDeck,
    SuggestedForSelected,
    SuggestedForDeck,
};

/// The payload of a sidebar node.
struct RelatedNode {
    RelatedNodeKind kind = RelatedNodeKind::Root;
    /// The deck number, one based, of a deck node.
    int deckNumber = 0;

    bool operator==(const RelatedNode& other) const {
        return kind == other.kind && deckNumber == other.deckNumber;
    }
    bool operator!=(const RelatedNode& other) const {
        return !(*this == other);
    }
};

/// The sidebar node that shows the tracks that go with a reference track.
class RelatedTracksFeature : public BaseTrackSetFeature {
    Q_OBJECT

  public:
    RelatedTracksFeature(Library* pLibrary, UserSettingsPointer pConfig);
    ~RelatedTracksFeature() override;

    QVariant title() override;

    TreeItemModel* sidebarModel() const override {
        return m_pSidebarModel;
    }

    bool hasTrackTable() override {
        return true;
    }

  public slots:
    void activate() override;
    void activateChild(const QModelIndex& index) override;
    /// Shows the relations of the track and selects the matching node.
    void slotShowRelatedTracks(TrackId trackId);

  private slots:
    void slotTrackSelected(TrackPointer pTrack);
    void slotDeckTrackChanged(const QString& group,
            TrackPointer pNewTrack,
            TrackPointer pOldTrack);
    void slotRelationsChanged();
    void slotNumDecksChanged(double numDecks);
    void slotRelateActiveDecks(double value);
    void slotRefreshTimeout();

  private:
    void rebuildChildModel();
    /// The label of a deck node: the deck and the track on it.
    QString deckLabel(int deckNumber) const;
    void updateDeckLabel(int deckNumber);

    /// Reads the table of the node. The view keeps the model that it shows.
    void selectNode(const RelatedNode& node);
    /// Reads the table and gives the model to the view. Only a click of the
    /// user goes through here.
    void showNode(const RelatedNode& node);
    /// Starts the timer that reads the table again. A selection in the
    /// library moves while the user scrolls, thus the read waits.
    void scheduleRefresh(const RelatedNode& node);

    TrackId referenceTrackIdOf(const RelatedNode& node) const;
    QModelIndex indexOfSelectedTrackNode() const;

    const TrackRelationStorage& storage() const;
    TrackRelationStorage& storage();

    RelatedTracksTableModel m_tableModel;
    TrackId m_selectedTrackId;
    RelatedNode m_shownNode;
    /// True while the view shows the model of this feature.
    bool m_modelIsVisible;
    QTimer m_refreshTimer;

    std::unique_ptr<ControlPushButton> m_pRelateActiveDecksControl;
    std::unique_ptr<ControlProxy> m_pNumDecksControl;
    int m_numDecks;
};

} // namespace muxic

Q_DECLARE_METATYPE(muxic::RelatedNode)
