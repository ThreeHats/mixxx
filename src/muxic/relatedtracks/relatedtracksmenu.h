#pragma once

#include <QMenu>
#include <QModelIndexList>

#include "track/trackid.h"
#include "util/parented_ptr.h"

class Library;
class TrackModel;

namespace muxic {

class RelatedTracksTableModel;

/// The part of the track menu that reads and writes relations.
class RelatedTracksMenu : public QMenu {
    Q_OBJECT

  public:
    RelatedTracksMenu(QWidget* pParent, Library* pLibrary);
    ~RelatedTracksMenu() override = default;

    /// Reads the selection before the menu opens.
    void updateSelection(TrackModel* pTrackModel,
            const QModelIndexList& trackIndices,
            const TrackIdList& trackIds);

  private slots:
    void slotPopulate();
    void slotRemoveRelations();
    void slotShowRelatedTracks();

  private:
    void relateSelectionToTrack(TrackId targetTrackId);
    void setRelationsBidirectional(bool bidirectional);
    RelatedTracksTableModel* relatedTracksModel() const;

    Library* const m_pLibrary;
    TrackModel* m_pTrackModel;
    QModelIndexList m_trackIndices;
    TrackIdList m_trackIds;
    bool m_isLoaded;

    parented_ptr<QAction> m_pRelateBothWaysAct;
    parented_ptr<QAction> m_pRelateOneWayAct;
    parented_ptr<QAction> m_pRemoveRelationsAct;
    parented_ptr<QAction> m_pShowRelatedTracksAct;
};

} // namespace muxic
