#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <memory>

#include "preferences/configobject.h"
#include "preferences/usersettings.h"
#include "util/parented_ptr.h"

class ControlProxy;
class KeyboardEventFilter;
class QWidget;
class WLibraryWindow;
class WSingletonContainer;

/// Move the library area of the skin to a window of its own, and back.
/// The control `[Skin],show_library_window` selects the state.
class LibraryWindowManager : public QObject {
    Q_OBJECT
  public:
    static const ConfigKey kShowConfigKey;

    LibraryWindowManager(UserSettingsPointer pConfig,
            QWidget* pMainWindow,
            std::shared_ptr<KeyboardEventFilter> pKeyboard,
            QObject* pParent = nullptr);
    ~LibraryWindowManager() override;

    /// Take the library area of a skin that is now in the main window.
    /// Open the window again if the control asks for it.
    void setSkin(QWidget* pSkinRoot);

    /// Put the library back in the skin. Call this before the skin goes away
    /// and before Mixxx stops.
    void clearSkin();

    bool isDetached() const {
        return m_pWindow != nullptr;
    }

  private slots:
    void slotShowControlChanged(double value);
    void slotCloseRequested();

  private:
    void detach();
    void attach();
    QWidget* findLibraryContainer(QWidget* pSkinRoot);
    WSingletonContainer* visibleContainer() const;

    const UserSettingsPointer m_pConfig;
    const QPointer<QWidget> m_pMainWindow;
    const std::shared_ptr<KeyboardEventFilter> m_pKeyboard;

    QPointer<QWidget> m_pSkinRoot;
    QPointer<QWidget> m_pLibraryContainer;
    /// The places of the skin that can hold the library. The skins put the
    /// library in a singleton, thus one skin has one place for each page.
    QList<QPointer<WSingletonContainer>> m_containers;
    /// The stand-in that keeps the place of the library in a skin that has no
    /// library singleton.
    QPointer<QWidget> m_pPlaceholder;

    std::unique_ptr<WLibraryWindow> m_pWindow;
    parented_ptr<ControlProxy> m_pShowControl;
};
