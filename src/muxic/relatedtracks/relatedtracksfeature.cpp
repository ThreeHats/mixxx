#include "muxic/relatedtracks/relatedtracksfeature.h"

#include "control/controlproxy.h"
#include "control/controlpushbutton.h"
#include "library/library.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "library/treeitem.h"
#include "mixer/playerinfo.h"
#include "mixer/playermanager.h"
#include "moc_relatedtracksfeature.cpp"
#include "muxic/relatedtracks/trackrelation.h"
#include "muxic/relatedtracks/trackrelationstorage.h"
#include "track/track.h"

namespace {

const QString kViewName = QStringLiteral("RELATEDTRACKSHOME");

const ConfigKey kRelateActiveDecksConfigKey(
        QStringLiteral("[Library]"), QStringLiteral("relate_active_decks"));

constexpr int kNodeKindFactor = 100;

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

} // anonymous namespace

namespace muxic {

RelatedTracksFeature::RelatedTracksFeature(Library* pLibrary, UserSettingsPointer pConfig)
        : BaseTrackSetFeature(pLibrary, pConfig, kViewName, QStringLiteral("related")),
          m_tableModel(this, pLibrary->trackCollectionManager()),
          m_numDecks(0) {
    m_pRelateActiveDecksControl =
            std::make_unique<ControlPushButton>(kRelateActiveDecksConfigKey);
    connect(m_pRelateActiveDecksControl.get(),
            &ControlObject::valueChanged,
            this,
            &RelatedTracksFeature::slotRelateActiveDecks);

    m_pNumDecksControl = std::make_unique<ControlProxy>(
            QStringLiteral("[App]"), QStringLiteral("num_decks"), this);
    m_pNumDecksControl->connectValueChanged(
            this, &RelatedTracksFeature::slotNumDecksChanged);
    m_numDecks = static_cast<int>(m_pNumDecksControl->get());

    rebuildChildModel();

    connect(pLibrary,
            &Library::trackSelected,
            this,
            &RelatedTracksFeature::slotTrackSelected);
    connect(pLibrary,
            &Library::showRelatedTracks,
            this,
            &RelatedTracksFeature::slotShowRelatedTracks);
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

TrackRelationStorage& RelatedTracksFeature::storage() const {
    return m_pLibrary->trackCollectionManager()->internalCollection()->trackRelations();
}

// static
QVariant RelatedTracksFeature::nodeToVariant(const Node& node) {
    return QVariant(static_cast<int>(node.kind) * kNodeKindFactor + node.deckNumber);
}

// static
RelatedTracksFeature::Node RelatedTracksFeature::nodeFromVariant(const QVariant& data) {
    Node node;
    bool ok = false;
    const int value = data.toInt(&ok);
    if (!ok) {
        return node;
    }
    node.kind = static_cast<NodeKind>(value / kNodeKindFactor);
    node.deckNumber = value % kNodeKindFactor;
    return node;
}

QString RelatedTracksFeature::deckLabel(int deckNumber) const {
    TrackPointer pTrack;
    if (deckHasTrack(deckNumber, &pTrack)) {
        return tr("Deck %1: %2").arg(QString::number(deckNumber), pTrack->getInfo());
    }
    return tr("Deck %1").arg(deckNumber);
}

void RelatedTracksFeature::updateDeckLabel(int deckNumber) {
    if (deckNumber < 1 || deckNumber > m_numDecks) {
        return;
    }
    const QString label = deckLabel(deckNumber);
    // Row 0 of a group is the node of the selected track, thus the row of a
    // deck is its number.
    for (int group = 0; group < 2; ++group) {
        const QModelIndex groupIndex = m_pSidebarModel->index(group, 0);
        if (!groupIndex.isValid()) {
            continue;
        }
        const QModelIndex deckIndex = m_pSidebarModel->index(deckNumber, 0, groupIndex);
        if (deckIndex.isValid()) {
            m_pSidebarModel->setData(deckIndex, label, Qt::DisplayRole);
        }
    }
}

void RelatedTracksFeature::rebuildChildModel() {
    std::unique_ptr<TreeItem> pRootItem = TreeItem::newRoot(this);

    TreeItem* pRelatedItem = pRootItem->appendChild(tr("Related"),
            nodeToVariant(Node{NodeKind::Root, 0}));
    pRelatedItem->appendChild(tr("Selected track"),
            nodeToVariant(Node{NodeKind::RelatedToSelected, 0}));
    for (int deck = 1; deck <= m_numDecks; ++deck) {
        pRelatedItem->appendChild(deckLabel(deck),
                nodeToVariant(Node{NodeKind::RelatedToDeck, deck}));
    }

    TreeItem* pSuggestedItem = pRootItem->appendChild(tr("Suggestions"),
            nodeToVariant(Node{NodeKind::Root, 0}));
    pSuggestedItem->appendChild(tr("Selected track"),
            nodeToVariant(Node{NodeKind::SuggestedForSelected, 0}));
    for (int deck = 1; deck <= m_numDecks; ++deck) {
        pSuggestedItem->appendChild(deckLabel(deck),
                nodeToVariant(Node{NodeKind::SuggestedForDeck, deck}));
    }

    m_pSidebarModel->setRootItem(std::move(pRootItem));
}

TrackId RelatedTracksFeature::referenceTrackIdOf(const Node& node) const {
    switch (node.kind) {
    case NodeKind::RelatedToSelected:
    case NodeKind::SuggestedForSelected:
        return m_selectedTrackId;
    case NodeKind::RelatedToDeck:
    case NodeKind::SuggestedForDeck: {
        TrackPointer pTrack;
        if (deckHasTrack(node.deckNumber, &pTrack)) {
            return pTrack->getId();
        }
        return TrackId();
    }
    case NodeKind::Root:
        break;
    }
    return TrackId();
}

void RelatedTracksFeature::showNode(const Node& node) {
    emit saveModelState();
    switch (node.kind) {
    case NodeKind::Root:
        m_tableModel.selectAllRelated();
        break;
    case NodeKind::RelatedToSelected:
    case NodeKind::RelatedToDeck:
        m_tableModel.selectRelatedTo(referenceTrackIdOf(node));
        break;
    case NodeKind::SuggestedForSelected:
    case NodeKind::SuggestedForDeck:
        m_tableModel.selectSuggestedFor(referenceTrackIdOf(node));
        break;
    }
    m_shownNode = node;
    emit showTrackModel(&m_tableModel);
    emit enableCoverArtDisplay(true);
}

void RelatedTracksFeature::refreshForTrackOfNode(const Node& node) {
    if (m_shownNode.kind != node.kind || m_shownNode.deckNumber != node.deckNumber) {
        return;
    }
    showNode(node);
}

void RelatedTracksFeature::activate() {
    showNode(Node{NodeKind::Root, 0});
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
        showNode(Node{NodeKind::RelatedToSelected, 0});
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
    refreshForTrackOfNode(Node{NodeKind::RelatedToSelected, 0});
    refreshForTrackOfNode(Node{NodeKind::SuggestedForSelected, 0});
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
    refreshForTrackOfNode(Node{NodeKind::RelatedToDeck, deckNumber});
    refreshForTrackOfNode(Node{NodeKind::SuggestedForDeck, deckNumber});
}

void RelatedTracksFeature::slotRelationsChanged() {
    if (m_shownNode.kind == NodeKind::SuggestedForSelected ||
            m_shownNode.kind == NodeKind::SuggestedForDeck) {
        return;
    }
    showNode(m_shownNode);
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

    QList<int> playingDecks;
    for (int deck = 1; deck <= m_numDecks; ++deck) {
        if (deckHasTrack(deck, nullptr) && deckIsPlaying(deck)) {
            playingDecks.append(deck);
        }
    }
    if (playingDecks.size() != 2) {
        // Without exactly two playing decks the control falls back to the
        // first two decks.
        playingDecks.clear();
        if (deckHasTrack(1, nullptr) && deckHasTrack(2, nullptr)) {
            playingDecks << 1 << 2;
        }
    }
    if (playingDecks.size() != 2) {
        return;
    }

    TrackPointer pSourceTrack;
    TrackPointer pTargetTrack;
    if (!deckHasTrack(playingDecks.at(0), &pSourceTrack) ||
            !deckHasTrack(playingDecks.at(1), &pTargetTrack)) {
        return;
    }

    TrackRelation relation(pSourceTrack->getId(), pTargetTrack->getId());
    relation.setBidirectional(true);
    relation.setType(kDefaultTrackRelationType);
    storage().saveRelation(relation);
}

} // namespace muxic
