#include "muxic/relatedtracks/relateddeckwatcher.h"

#include <algorithm>

#include "control/controlproxy.h"
#include "mixer/playerinfo.h"
#include "mixer/playermanager.h"
#include "moc_relateddeckwatcher.cpp"
#include "muxic/relatedtracks/trackrelationstorage.h"
#include "track/track.h"
#include "util/defs.h"

namespace muxic {

RelatedDeckWatcher::RelatedDeckWatcher(QObject* pParent,
        TrackRelationStorage* pStorage,
        int settleDelayMillis)
        : QObject(pParent),
          m_active(false),
          m_pending(true) {
    m_pNumDecksControl = make_parented<ControlProxy>(
            QStringLiteral("[App]"), QStringLiteral("num_decks"), this);
    m_pNumDecksControl->connectValueChanged(
            this, [this](double) { requestUpdate(); });

    m_timer.setSingleShot(true);
    m_timer.setInterval(settleDelayMillis);
    connect(&m_timer, &QTimer::timeout, this, &RelatedDeckWatcher::slotTimeout);

    connect(&PlayerInfo::instance(),
            &PlayerInfo::trackChanged,
            this,
            &RelatedDeckWatcher::slotDeckTrackChanged);
    if (pStorage != nullptr) {
        connect(pStorage,
                &TrackRelationStorage::relationsChanged,
                this,
                &RelatedDeckWatcher::requestUpdate);
    }
}

RelatedDeckWatcher::~RelatedDeckWatcher() = default;

DeckTrackList RelatedDeckWatcher::deckTracks() const {
    DeckTrackList deckTracks;
    // A control holds a double that a script may set to anything.
    const int numDecks = std::clamp(
            static_cast<int>(m_pNumDecksControl->get()), 0, kMaxNumberOfDecks);
    for (int deck = 1; deck <= numDecks; ++deck) {
        const TrackPointer pTrack = PlayerInfo::instance().getTrackInfo(
                PlayerManager::groupForDeck(deck - 1));
        if (!pTrack) {
            continue;
        }
        const TrackId trackId = pTrack->getId();
        if (!trackId.isValid()) {
            continue;
        }
        deckTracks.append(DeckTrack{deck, trackId});
    }
    return deckTracks;
}

void RelatedDeckWatcher::setActive(bool active) {
    if (active == m_active) {
        return;
    }
    m_active = active;
    if (!active) {
        if (m_timer.isActive()) {
            m_timer.stop();
            m_pending = true;
        }
        return;
    }
    if (m_pending) {
        // The panel comes back with an old table. It waits for nothing.
        m_pending = false;
        emit updateNeeded();
    }
}

void RelatedDeckWatcher::requestUpdate() {
    if (!m_active) {
        m_pending = true;
        return;
    }
    m_timer.start();
}

void RelatedDeckWatcher::slotTimeout() {
    m_pending = false;
    emit updateNeeded();
}

void RelatedDeckWatcher::slotDeckTrackChanged(const QString& group,
        TrackPointer pNewTrack,
        TrackPointer pOldTrack) {
    Q_UNUSED(pNewTrack);
    Q_UNUSED(pOldTrack);
    if (!PlayerManager::isDeckGroup(group)) {
        return;
    }
    requestUpdate();
}

} // namespace muxic
