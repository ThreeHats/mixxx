#include "muxic/relatedtracks/relatedtracksfeature.h"

#include "control/controlproxy.h"
#include "control/controlpushbutton.h"
#include "library/library.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/treeitem.h"
#include "library/treeitemmodel.h"
#include "mixer/playerinfo.h"
#include "mixer/playermanager.h"
#include "moc_relatedtracksfeature.cpp"
#include "muxic/relatedtracks/trackrelation.h"
#include "muxic/relatedtracks/trackrelationstorage.h"
#include "track/track.h"

namespace muxic {

namespace {

const QString kViewName = QStringLiteral("RELATEDTRACKSHOME");

const ConfigKey kRelateActiveDecksConfigKey(
        QStringLiteral("[Library]"), QStringLiteral("relate_active_decks"));

// A selection moves while the user holds a key. The read of the table
// waits for the selection to settle.
constexpr int kRefreshDelayMillis = 150;

bool deckHasTrack(int deckNumber, TrackPointer* pTrack) {
    const TrackPointer pDeckTrack = PlayerInfo::instance().getTrackInfo(
            PlayerManager::groupForDeck(deckNumber - 1));
    if (!pDeckTrack || !pDeckTrack->getId().isValid()) {
        return false;
    }
    if (pTrack != nullptr) {
        *pTrack = pDeckTrack;
    }
    return true;
}

bool deckIsPlaying(int deckNumber) {
    ControlProxy play(PlayerManager::groupForDeck(deckNumber - 1),
            QStringLiteral("play"));
    return play.valid() && play.toBool();
}

QVariant nodeToVariant(const RelatedNode& node) {
    return QVariant::fromValue(node);
}

RelatedNode nodeFromVariant(const QVariant& data) {
    if (data.canConvert<RelatedNode>()) {
        return data.value<RelatedNode>();
    }
    return RelatedNode{};
}

} // anonymous namespace

RelatedTracksFeature::RelatedTracksFeature(Library* pLibrary, UserSettingsPointer pConfig)
        : BaseTrackSetFeature(pLibrary, pConfig, kViewName, QStringLiteral("related")),
          m_tableModel(this, pLibrary->trackCollectionManager()),
          m_modelIsVisible(false),
          m_numDecks(0) {
    m_pRelateActiveDecksControl =
            std::make_unique<ControlPushButton>(kRelateActiveDecksConfigKey);
    m_pRelateActiveDecksControl->setButtonMode(mixxx::control::ButtonMode::Trigger);
    connect(m_pRelateActiveDecksControl.get(),
            &ControlObject::valueChanged,
            this,
            &RelatedTracksFeature::slotRelateActiveDecks);

    m_pNumDecksControl = std::make_unique<ControlProxy>(
            QStringLiteral("[App]"), QStringLiteral("num_decks"), this);
    m_pNumDecksControl->connectValueChanged(
            this, &RelatedTracksFeature::slotNumDecksChanged);
    m_numDecks = static_cast<int>(m_pNumDecksControl->get());

    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(kRefreshDelayMillis);
    connect(&m_refreshTimer,
            &QTimer::timeout,
            this,
            &RelatedTracksFeature::slotRefreshTimeout);

    rebuildChildModel();

    connect(pLibrary,
            &Library::trackSelected,
            this,
            &RelatedTracksFeature::slotTrackSelected);
    connect(pLibrary,
            &Library::showRelatedTracks,
            this,
            &RelatedTracksFeature::slotShowRelatedTracks);
    // The library names the model that the view shows. This feature reads
    // its table again only for its own model.
    connect(pLibrary,
            &Library::showTrackModel,
            this,
            [this](QAbstractItemModel* pModel, bool) {
                m_modelIsVisible = (pModel == &m_tableModel);
            });
    connect(pLibrary, &Library::switchToView, this, [this](const QString&) {
        m_modelIsVisible = false;
    });
    connect(&PlayerInfo::instance(),
            &PlayerInfo::trackChanged,
            this,
            &RelatedTracksFeature::slotDeckTrackChanged);
    connect(&storage(),
            &TrackRelationStorage::relationsChanged,
            this,
            &RelatedTracksFeature::slotRelationsChanged);
}

RelatedTracksFeature::~RelatedTracksFeature() = default;

QVariant RelatedTracksFeature::title() {
    return tr("Related Tracks");
}

const TrackRelationStorage& RelatedTracksFeature::storage() const {
    return m_pLibrary->trackCollectionManager()->internalCollection()->trackRelations();
}

TrackRelationStorage& RelatedTracksFeature::storage() {
    return m_pLibrary->trackCollectionManager()->internalCollection()->trackRelations();
}

QString RelatedTracksFeature::deckLabel(int deckNumber) const {
    TrackPointer pTrack;
    if (deckHasTrack(deckNumber, &pTrack)) {
        return tr("Deck %1: %2").arg(QString::number(deckNumber), pTrack->getInfo());
    }
    return tr("Deck %1").arg(deckNumber);
}

void RelatedTracksFeature::updateDeckLabel(int deckNumber) {
    if (deckNumber < 1) {
        return;
    }
    const QString label = deckLabel(deckNumber);
    const int groupCount = m_pSidebarModel->rowCount();
    for (int group = 0; group < groupCount; ++group) {
        const QModelIndex groupIndex = m_pSidebarModel->index(group, 0);
        const int childCount = m_pSidebarModel->rowCount(groupIndex);
        for (int child = 0; child < childCount; ++child) {
            const QModelIndex childIndex = m_pSidebarModel->index(child, 0, groupIndex);
            const RelatedNode node = nodeFromVariant(
                    m_pSidebarModel->data(childIndex, TreeItemModel::kDataRole));
            const bool isThisDeck = node.deckNumber == deckNumber &&
                    (node.kind == RelatedNodeKind::RelatedToDeck ||
                            node.kind == RelatedNodeKind::SuggestedForDeck);
            if (isThisDeck) {
                m_pSidebarModel->setData(childIndex, label, Qt::DisplayRole);
            }
        }
    }
}

void RelatedTracksFeature::rebuildChildModel() {
    std::unique_ptr<TreeItem> pRootItem = TreeItem::newRoot(this);

    TreeItem* pRelatedItem = pRootItem->appendChild(
            tr("Related"), nodeToVariant(RelatedNode{RelatedNodeKind::Root, 0}));
    pRelatedItem->appendChild(tr("Selected track"),
            nodeToVariant(RelatedNode{RelatedNodeKind::RelatedToSelected, 0}));
    for (int deck = 1; deck <= m_numDecks; ++deck) {
        pRelatedItem->appendChild(deckLabel(deck),
                nodeToVariant(RelatedNode{RelatedNodeKind::RelatedToDeck, deck}));
    }

    TreeItem* pSuggestedItem = pRootItem->appendChild(
            tr("Suggestions"), nodeToVariant(RelatedNode{RelatedNodeKind::Root, 0}));
    pSuggestedItem->appendChild(tr("Selected track"),
            nodeToVariant(RelatedNode{RelatedNodeKind::SuggestedForSelected, 0}));
    for (int deck = 1; deck <= m_numDecks; ++deck) {
        pSuggestedItem->appendChild(deckLabel(deck),
                nodeToVariant(RelatedNode{RelatedNodeKind::SuggestedForDeck, deck}));
    }

    m_pSidebarModel->setRootItem(std::move(pRootItem));
}

TrackId RelatedTracksFeature::referenceTrackIdOf(const RelatedNode& node) const {
    switch (node.kind) {
    case RelatedNodeKind::RelatedToSelected:
    case RelatedNodeKind::SuggestedForSelected:
        return m_selectedTrackId;
    case RelatedNodeKind::RelatedToDeck:
    case RelatedNodeKind::SuggestedForDeck: {
        TrackPointer pTrack;
        if (deckHasTrack(node.deckNumber, &pTrack)) {
            return pTrack->getId();
        }
        return TrackId();
    }
    case RelatedNodeKind::Root:
        break;
    }
    return TrackId();
}

void RelatedTracksFeature::selectNode(const RelatedNode& node) {
    m_refreshTimer.stop();
    switch (node.kind) {
    case RelatedNodeKind::Root:
        m_tableModel.selectAllRelated();
        break;
    case RelatedNodeKind::RelatedToSelected:
    case RelatedNodeKind::RelatedToDeck:
        m_tableModel.selectRelatedTo(referenceTrackIdOf(node));
        break;
    case RelatedNodeKind::SuggestedForSelected:
    case RelatedNodeKind::SuggestedForDeck:
        m_tableModel.selectSuggestedFor(referenceTrackIdOf(node));
        break;
    }
    m_shownNode = node;
}

void RelatedTracksFeature::showNode(const RelatedNode& node) {
    emit saveModelState();
    selectNode(node);
    emit showTrackModel(&m_tableModel);
    emit enableCoverArtDisplay(true);
}

void RelatedTracksFeature::scheduleRefresh(const RelatedNode& node) {
    if (!m_modelIsVisible || m_shownNode != node) {
        return;
    }
    m_refreshTimer.start();
}

void RelatedTracksFeature::slotRefreshTimeout() {
    if (!m_modelIsVisible) {
        return;
    }
    selectNode(m_shownNode);
}

void RelatedTracksFeature::activate() {
    showNode(RelatedNode{RelatedNodeKind::Root, 0});
}

void RelatedTracksFeature::activateChild(const QModelIndex& index) {
    TreeItem* pItem = static_cast<TreeItem*>(index.internalPointer());
    VERIFY_OR_DEBUG_ASSERT(pItem) {
        return;
    }
    showNode(nodeFromVariant(pItem->getData()));
}

QModelIndex RelatedTracksFeature::indexOfSelectedTrackNode() const {
    const QModelIndex relatedIndex = m_pSidebarModel->index(0, 0);
    if (!relatedIndex.isValid()) {
        return QModelIndex();
    }
    return m_pSidebarModel->index(0, 0, relatedIndex);
}

void RelatedTracksFeature::slotShowRelatedTracks(TrackId trackId) {
    if (!trackId.isValid()) {
        return;
    }
    m_selectedTrackId = trackId;
    const QModelIndex index = indexOfSelectedTrackNode();
    if (index.isValid()) {
        selectAndActivate(index);
    } else {
        showNode(RelatedNode{RelatedNodeKind::RelatedToSelected, 0});
    }
}

void RelatedTracksFeature::slotTrackSelected(TrackPointer pTrack) {
    const TrackId trackId = pTrack ? pTrack->getId() : TrackId();
    // A move to this view clears the selection of the library view. The
    // reference track must stay.
    if (!trackId.isValid() || trackId == m_selectedTrackId) {
        return;
    }
    m_selectedTrackId = trackId;
    scheduleRefresh(RelatedNode{RelatedNodeKind::RelatedToSelected, 0});
    scheduleRefresh(RelatedNode{RelatedNodeKind::SuggestedForSelected, 0});
}

void RelatedTracksFeature::slotDeckTrackChanged(const QString& group,
        TrackPointer pNewTrack,
        TrackPointer pOldTrack) {
    Q_UNUSED(pNewTrack);
    Q_UNUSED(pOldTrack);
    int deckNumber = 0;
    if (!PlayerManager::isDeckGroup(group, &deckNumber)) {
        return;
    }
    updateDeckLabel(deckNumber);
    scheduleRefresh(RelatedNode{RelatedNodeKind::RelatedToDeck, deckNumber});
    scheduleRefresh(RelatedNode{RelatedNodeKind::SuggestedForDeck, deckNumber});
}

void RelatedTracksFeature::slotRelationsChanged() {
    const bool showsSuggestions =
            m_shownNode.kind == RelatedNodeKind::SuggestedForSelected ||
            m_shownNode.kind == RelatedNodeKind::SuggestedForDeck;
    if (!m_modelIsVisible || showsSuggestions) {
        return;
    }
    selectNode(m_shownNode);
}

void RelatedTracksFeature::slotNumDecksChanged(double numDecks) {
    const int newNumDecks = static_cast<int>(numDecks);
    if (newNumDecks == m_numDecks) {
        return;
    }
    m_numDecks = newNumDecks;
    rebuildChildModel();
}

void RelatedTracksFeature::slotRelateActiveDecks(double value) {
    if (value <= 0.0) {
        return;
    }

    QList<int> decks;
    for (int deck = 1; deck <= m_numDecks; ++deck) {
        if (deckHasTrack(deck, nullptr) && deckIsPlaying(deck)) {
            decks.append(deck);
        }
    }
    if (decks.size() != 2) {
        // The control takes the first two decks when the count is not two.
        decks.clear();
        if (deckHasTrack(1, nullptr) && deckHasTrack(2, nullptr)) {
            decks << 1 << 2;
        }
    }
    if (decks.size() != 2) {
        return;
    }

    TrackPointer pSourceTrack;
    TrackPointer pTargetTrack;
    if (!deckHasTrack(decks.at(0), &pSourceTrack) ||
            !deckHasTrack(decks.at(1), &pTargetTrack)) {
        return;
    }

    TrackRelation relation(pSourceTrack->getId(), pTargetTrack->getId());
    relation.setBidirectional(true);
    relation.setType(kDefaultTrackRelationType);
    storage().saveRelation(relation);
}

} // namespace muxic
