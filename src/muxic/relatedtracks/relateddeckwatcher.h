#pragma once

#include <QObject>
#include <QTimer>
#include <memory>

#include "muxic/relatedtracks/decktrack.h"
#include "track/track_decl.h"

class ControlProxy;

namespace muxic {

class TrackRelationStorage;

/// Says when the related tracks panel must read its table again.
///
/// The watcher follows the decks and the relation table. A watcher that is
/// not active reports nothing and remembers the request.
class RelatedDeckWatcher : public QObject {
    Q_OBJECT

  public:
    /// A deck load moves the selection of the user too, thus a read waits.
    static constexpr int kSettleDelayMillis = 150;

    RelatedDeckWatcher(QObject* pParent,
            TrackRelationStorage* pStorage,
            int settleDelayMillis = kSettleDelayMillis);
    ~RelatedDeckWatcher() override;

    /// The decks that hold a track, in the order of the deck numbers.
    DeckTrackList deckTracks() const;

    bool isActive() const {
        return m_active;
    }
    /// The panel is on screen and its toggle is on.
    void setActive(bool active);

    /// A deck, a relation or the mode of the panel changed.
    void requestUpdate();

  signals:
    /// Read the table again.
    void updateNeeded();

  private slots:
    void slotDeckTrackChanged(const QString& group,
            TrackPointer pNewTrack,
            TrackPointer pOldTrack);
    void slotTimeout();

  private:
    std::unique_ptr<ControlProxy> m_pNumDecksControl;
    QTimer m_timer;
    bool m_active;
    /// A request that came in while the watcher was not active.
    bool m_pending;
};

} // namespace muxic
