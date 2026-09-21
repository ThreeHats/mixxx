#include "osc/osccontroller.h"

#include <QThread>

#include "control/controlpushbutton.h"
#include "mixer/basetrackplayer.h"
#include "mixer/playermanager.h"
#include "moc_osccontroller.cpp"
#include "osc/oscservice.h"
#include "track/track.h"

namespace {

const QString kConfigGroup = QStringLiteral("[Osc]");

} // namespace

namespace mixxx {
namespace osc {

Controller::Controller(UserSettingsPointer pConfig,
        PlayerManagerInterface* pPlayerManager,
        QObject* pParent)
        : QObject(pParent),
          m_pConfig(pConfig),
          m_pPlayerManager(pPlayerManager),
          m_pThread(std::make_unique<QThread>()),
          m_pService(new Service()),
          m_connectedDecks(0) {
    m_pThread->setObjectName(QStringLiteral("OSC"));
    m_pService->moveToThread(m_pThread.get());
    connect(m_pThread.get(), &QThread::finished, m_pService, &QObject::deleteLater);
    m_pThread->start();

    m_pEnabledControl = std::make_unique<ControlPushButton>(
            ConfigKey(kConfigGroup, QStringLiteral("enabled")));
    m_pEnabledControl->setButtonMode(mixxx::control::ButtonMode::Toggle);
    connect(m_pEnabledControl.get(),
            &ControlObject::valueChanged,
            this,
            &Controller::slotEnabledChanged);

    m_pReloadControl = std::make_unique<ControlPushButton>(
            ConfigKey(kConfigGroup, QStringLiteral("reload")));
    m_pReloadControl->setButtonMode(mixxx::control::ButtonMode::Trigger);
    connect(m_pReloadControl.get(),
            &ControlObject::valueChanged,
            this,
            &Controller::slotReloadTriggered);

    if (m_pPlayerManager) {
        connect(m_pPlayerManager,
                &PlayerManagerInterface::numberOfDecksChanged,
                this,
                &Controller::slotNumberOfDecksChanged);
    }
    connectDecks();
    reload();
}

Controller::~Controller() {
    if (m_pService) {
        QMetaObject::invokeMethod(
                m_pService, [this] { m_pService->stop(); }, Qt::BlockingQueuedConnection);
    }
    m_pThread->quit();
    m_pThread->wait();
}

void Controller::reload() {
    const Config config = Config::load(m_pConfig);
    const int deckCount = m_pPlayerManager ? m_pPlayerManager->numberOfDecks() : 0;
    m_pEnabledControl->set(config.enabled ? 1.0 : 0.0);
    Service* pService = m_pService;
    QMetaObject::invokeMethod(pService, [pService, config, deckCount] {
        pService->applyConfig(config, deckCount);
    });
}

void Controller::slotEnabledChanged(double value) {
    const bool enabled = value > 0;
    m_pConfig->setValue(ConfigKey(kConfigGroup, QStringLiteral("Enabled")), enabled);
    reload();
}

void Controller::slotReloadTriggered(double value) {
    if (value > 0) {
        // The control is a mailbox. Clear it, else the next Apply finds it
        // already set and sends no change.
        m_pReloadControl->set(0.0);
        reload();
    }
}

void Controller::slotNumberOfDecksChanged(int decks) {
    Q_UNUSED(decks);
    connectDecks();
    reload();
}

void Controller::connectDecks() {
    if (!m_pPlayerManager) {
        return;
    }
    const int decks = m_pPlayerManager->numberOfDecks();
    for (int deck = m_connectedDecks; deck < decks; deck++) {
        BaseTrackPlayer* pPlayer = m_pPlayerManager->getDeckBase(deck);
        if (!pPlayer) {
            continue;
        }
        const QString group = PlayerManager::groupForDeck(deck);
        connect(pPlayer,
                &BaseTrackPlayer::newTrackLoaded,
                this,
                [this, group](TrackPointer pTrack) { sendTrackInfo(group, pTrack); });
        connect(pPlayer, &BaseTrackPlayer::playerEmpty, this, [this, group] {
            sendTrackInfo(group, TrackPointer());
        });
    }
    m_connectedDecks = std::max(m_connectedDecks, decks);
}

void Controller::sendTrackInfo(const QString& group, const TrackPointer& pTrack) {
    TrackInfo info;
    if (pTrack) {
        info.artist = pTrack->getArtist();
        info.title = pTrack->getTitle();
        info.album = pTrack->getAlbum();
        info.year = pTrack->getYear();
        info.genre = pTrack->getGenre();
        info.location = pTrack->getLocation();
    }
    Service* pService = m_pService;
    QMetaObject::invokeMethod(pService, [pService, group, info] {
        pService->setTrackInfo(group, info);
    });
}

} // namespace osc
} // namespace mixxx
