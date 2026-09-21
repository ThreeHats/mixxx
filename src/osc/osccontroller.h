#pragma once

#include <QObject>
#include <memory>

#include "preferences/usersettings.h"
#include "track/track_decl.h"

class ControlPushButton;
class PlayerManagerInterface;
class QThread;

namespace mixxx {
namespace osc {

class Service;

/// Holds the OSC service and the thread that runs it. It lives on the main
/// thread, reads the settings, watches the decks and hands each change over.
class Controller : public QObject {
    Q_OBJECT

  public:
    Controller(UserSettingsPointer pConfig,
            PlayerManagerInterface* pPlayerManager,
            QObject* pParent = nullptr);
    ~Controller() override;

    /// Read the settings again, then start, restart or stop the service. The
    /// preferences page calls this through the control `[Osc] reload`.
    void reload();

  private slots:
    void slotNumberOfDecksChanged(int decks);
    void slotEnabledChanged(double value);
    void slotReloadTriggered(double value);

  private:
    void connectDecks();
    void sendTrackInfo(const QString& group, const TrackPointer& pTrack);

    UserSettingsPointer m_pConfig;
    PlayerManagerInterface* m_pPlayerManager;
    std::unique_ptr<QThread> m_pThread;
    Service* m_pService;
    std::unique_ptr<ControlPushButton> m_pEnabledControl;
    std::unique_ptr<ControlPushButton> m_pReloadControl;
    int m_connectedDecks;
};

} // namespace osc
} // namespace mixxx
