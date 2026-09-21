#include "muxic/relatedtracks/relatedtracksmenu.h"

#include <QMessageBox>

#include "library/library.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/trackmodel.h"
#include "mixer/playerinfo.h"
#include "mixer/playermanager.h"
#include "moc_relatedtracksmenu.cpp"
#include "muxic/relatedtracks/relatedtrackstablemodel.h"
#include "muxic/relatedtracks/trackrelation.h"
#include "muxic/relatedtracks/trackrelationstorage.h"
#include "track/track.h"
#include "util/qt.h"

namespace muxic {

namespace {

// A remove above this count asks the user first.
constexpr uint kConfirmRemoveCount = 5;

} // anonymous namespace

RelatedTracksMenu::RelatedTracksMenu(QWidget* pParent, Library* pLibrary)
        : QMenu(tr("Related Tracks"), pParent),
          m_pLibrary(pLibrary),
          m_pTrackModel(nullptr),
          m_isLoaded(false) {
    m_pRelateBothWaysAct = make_parented<QAction>(tr("Relate Both Ways"), this);
    connect(m_pRelateBothWaysAct, &QAction::triggered, this, [this] {
        setRelationsBidirectional(true);
    });

    m_pRelateOneWayAct = make_parented<QAction>(tr("Relate One Way"), this);
    connect(m_pRelateOneWayAct, &QAction::triggered, this, [this] {
        setRelationsBidirectional(false);
    });

    m_pRemoveRelationsAct = make_parented<QAction>(tr("Remove Relations"), this);
    connect(m_pRemoveRelationsAct,
            &QAction::triggered,
            this,
            &RelatedTracksMenu::slotRemoveRelations);

    m_pShowRelatedTracksAct = make_parented<QAction>(tr("Show Related Tracks"), this);
    connect(m_pShowRelatedTracksAct,
            &QAction::triggered,
            this,
            &RelatedTracksMenu::slotShowRelatedTracks);

    connect(this, &QMenu::aboutToShow, this, &RelatedTracksMenu::slotPopulate);
}

RelatedTracksTableModel* RelatedTracksMenu::relatedTracksModel() const {
    return dynamic_cast<RelatedTracksTableModel*>(m_pTrackModel);
}

void RelatedTracksMenu::updateSelection(TrackModel* pTrackModel,
        const QModelIndexList& trackIndices,
        const TrackIdList& trackIds) {
    m_pTrackModel = pTrackModel;
    m_trackIndices = trackIndices;
    m_trackIds = trackIds;
    // The submenu reads the decks and the relations when it opens. The
    // track menu opens without a query.
    m_isLoaded = false;
    setEnabled(!m_trackIds.isEmpty());
}

void RelatedTracksMenu::slotPopulate() {
    if (m_isLoaded) {
        return;
    }
    clear();

    PollingControlProxy numDecks(QStringLiteral("[App]"), QStringLiteral("num_decks"));
    const int deckCount = static_cast<int>(numDecks.get());
    for (int deck = 1; deck <= deckCount; ++deck) {
        const TrackPointer pDeckTrack = PlayerInfo::instance().getTrackInfo(
                PlayerManager::groupForDeck(deck - 1));
        if (!pDeckTrack) {
            continue;
        }
        const TrackId deckTrackId = pDeckTrack->getId();
        if (!deckTrackId.isValid()) {
            continue;
        }
        const QString label =
                tr("Relate to Deck %1: %2")
                        .arg(QString::number(deck), pDeckTrack->getInfo());
        auto pAction = make_parented<QAction>(
                mixxx::escapeTextPropertyWithoutShortcuts(label), this);
        addAction(pAction);
        connect(pAction, &QAction::triggered, this, [this, deckTrackId] {
            relateSelectionToTrack(deckTrackId);
        });
    }
    if (actions().isEmpty()) {
        auto pAction = make_parented<QAction>(tr("No track on a deck"), this);
        pAction->setEnabled(false);
        addAction(pAction);
    }

    // A direction has a meaning only in a view with one relation per row.
    RelatedTracksTableModel* pRelatedModel = relatedTracksModel();
    bool anyOneWay = false;
    bool anyBothWays = false;
    if (pRelatedModel && pRelatedModel->showsOneRelationPerRow()) {
        for (const QModelIndex& trackIndex : std::as_const(m_trackIndices)) {
            TrackRelation relation;
            if (!pRelatedModel->relationForIndex(trackIndex, &relation)) {
                continue;
            }
            if (relation.isBidirectional()) {
                anyBothWays = true;
            } else {
                anyOneWay = true;
            }
        }
    }
    if (anyOneWay || anyBothWays) {
        addSeparator();
        if (anyOneWay) {
            addAction(m_pRelateBothWaysAct);
        }
        if (anyBothWays) {
            addAction(m_pRelateOneWayAct);
        }
    }

    const TrackRelationStorage& relations = m_pLibrary->trackCollectionManager()
                                                    ->internalCollection()
                                                    ->trackRelations();
    if (relations.anyTrackHasRelation(m_trackIds)) {
        addSeparator();
        addAction(m_pRemoveRelationsAct);
    }

    addSeparator();
    m_pShowRelatedTracksAct->setEnabled(m_trackIds.size() == 1);
    addAction(m_pShowRelatedTracksAct);

    m_isLoaded = true;
}

void RelatedTracksMenu::relateSelectionToTrack(TrackId targetTrackId) {
    QList<TrackRelation> relations;
    relations.reserve(m_trackIds.size());
    for (const auto& trackId : std::as_const(m_trackIds)) {
        if (trackId == targetTrackId) {
            continue;
        }
        TrackRelation relation(trackId, targetTrackId);
        relation.setType(kDefaultTrackRelationType);
        relations.append(relation);
    }
    m_pLibrary->trackCollectionManager()
            ->internalCollection()
            ->trackRelations()
            .saveRelations(relations);
}

void RelatedTracksMenu::setRelationsBidirectional(bool bidirectional) {
    RelatedTracksTableModel* pRelatedModel = relatedTracksModel();
    if (!pRelatedModel) {
        return;
    }
    pRelatedModel->setRelationsBidirectional(m_trackIndices, bidirectional);
}

void RelatedTracksMenu::slotRemoveRelations() {
    if (m_trackIds.isEmpty()) {
        return;
    }
    TrackRelationStorage& relations = m_pLibrary->trackCollectionManager()
                                              ->internalCollection()
                                              ->trackRelations();
    const uint count = relations.countRelationsOfTracks(m_trackIds);
    if (count == 0) {
        return;
    }
    if (count > kConfirmRemoveCount) {
        const auto answer = QMessageBox::question(nullptr,
                tr("Remove Relations"),
                tr("This removes %1 relations of the selected tracks. Continue?")
                        .arg(count),
                QMessageBox::Ok | QMessageBox::Cancel);
        if (answer != QMessageBox::Ok) {
            return;
        }
    }
    relations.removeAllRelationsOfTracks(m_trackIds);
}

void RelatedTracksMenu::slotShowRelatedTracks() {
    if (m_trackIds.size() != 1) {
        return;
    }
    emit m_pLibrary->showRelatedTracks(m_trackIds.first());
}

} // namespace muxic
