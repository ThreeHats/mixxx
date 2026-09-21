#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <memory>

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
    LibraryWindowManager(UserSettingsPointer pConfig,
            QWidget* pMainWindow,
            std::shared_ptr<KeyboardEventFilter> pKeyboard,
            QObject* pParent = nullptr);
    ~LibraryWindowManager() override;

    /// Take the library area of a skin that is now in the main window, and
    /// open the window again if the control asks for it. A null skin, or a
    /// skin with no library singleton, turns the control off.
    void setSkin(QWidget* pSkinRoot);

    /// Put the library back in the skin. Call this before the skin goes away
    /// and before Mixxx stops.
    void clearSkin();

    bool isDetached() const {
        return m_pWindow != nullptr;
    }

  private slots:
    void slotShowControlChanged(double value);
    /// The connection of this slot is queued, thus the value can be old.
    void slotMaximizedControlChanged(double value);

  private:
    /// One place of the skin that can show the library. A skin has one place
    /// for each page that holds the library.
    struct LibrarySlot {
        QPointer<WSingletonContainer> pContainer;
        /// The skin kept this place hidden before the library went out.
        bool wasHidden;
    };

    void detach();
    void attach();
    void onCloseRequested(quint64 generation);
    /// Look for the library area of the skin, and keep it together with the
    /// places that show it. Return false if the skin has no such area.
    bool collectLibraryArea(QWidget* pSkinRoot);
    WSingletonContainer* visibleContainer() const;

    const UserSettingsPointer m_pConfig;
    const QPointer<QWidget> m_pMainWindow;
    const std::shared_ptr<KeyboardEventFilter> m_pKeyboard;

    QPointer<QWidget> m_pSkinRoot;
    QPointer<QWidget> m_pLibraryContainer;
    QList<LibrarySlot> m_slots;

    std::unique_ptr<WLibraryWindow> m_pWindow;
    /// The count of windows that the manager made. A close that arrives late
    /// names the window that asked for it.
    quint64 m_windowGeneration;
    parented_ptr<ControlProxy> m_pShowControl;
    parented_ptr<ControlProxy> m_pMaximizedControl;
};
