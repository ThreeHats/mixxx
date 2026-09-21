#pragma once

#include <QModelIndex>
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

namespace muxic {

class TrackRelationStorage;

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

  private:
    /// What a sidebar node shows.
    enum class NodeKind {
        Root,
        RelatedToSelected,
        RelatedToDeck,
        SuggestedForSelected,
        SuggestedForDeck,
    };

    struct Node {
        NodeKind kind = NodeKind::Root;
        /// The deck number, one based, for a deck node.
        int deckNumber = 0;
    };

    static QVariant nodeToVariant(const Node& node);
    static Node nodeFromVariant(const QVariant& data);

    void rebuildChildModel();
    /// The label of a deck node: the deck and the track on it.
    QString deckLabel(int deckNumber) const;
    void updateDeckLabel(int deckNumber);
    void showNode(const Node& node);
    /// Reads the table again when the shown node depends on the track.
    void refreshForTrackOfNode(const Node& node);
    TrackId referenceTrackIdOf(const Node& node) const;
    QModelIndex indexOfSelectedTrackNode() const;

    TrackRelationStorage& storage() const;

    RelatedTracksTableModel m_tableModel;
    TrackId m_selectedTrackId;
    Node m_shownNode;

    std::unique_ptr<ControlPushButton> m_pRelateActiveDecksControl;
    std::unique_ptr<ControlProxy> m_pNumDecksControl;
    int m_numDecks;
};

} // namespace muxic
