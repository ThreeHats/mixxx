#include "librarywindow/librarywindowmanager.h"

#include <utility>

#include "control/controlproxy.h"
#include "controllers/keyboard/keyboardeventfilter.h"
#include "librarywindow/wlibrarywindow.h"
#include "moc_librarywindowmanager.cpp"
#include "util/assert.h"
#include "util/logger.h"
#include "widget/wlibrary.h"
#include "widget/wsingletoncontainer.h"

namespace {

const mixxx::Logger kLogger("LibraryWindowManager");

const ConfigKey kShowConfigKey(
        QStringLiteral("[Skin]"), QStringLiteral("show_library_window"));

const ConfigKey kMaximizedConfigKey(
        QStringLiteral("[Skin]"), QStringLiteral("show_maximized_library"));

} // namespace

LibraryWindowManager::LibraryWindowManager(UserSettingsPointer pConfig,
        QWidget* pMainWindow,
        std::shared_ptr<KeyboardEventFilter> pKeyboard,
        QObject* pParent)
        : QObject(pParent),
          m_pConfig(pConfig),
          m_pMainWindow(pMainWindow),
          m_pKeyboard(pKeyboard),
          m_windowGeneration(0),
          m_pShowControl(make_parented<ControlProxy>(kShowConfigKey, this)),
          m_pMaximizedControl(
                  make_parented<ControlProxy>(kMaximizedConfigKey, this)) {
    m_pShowControl->connectValueChanged(
            this, &LibraryWindowManager::slotShowControlChanged);
    m_pMaximizedControl->connectValueChanged(
            this, &LibraryWindowManager::slotMaximizedControlChanged);
}

LibraryWindowManager::~LibraryWindowManager() {
    clearSkin();
}

void LibraryWindowManager::setSkin(QWidget* pSkinRoot) {
    VERIFY_OR_DEBUG_ASSERT(!m_pWindow) {
        clearSkin();
    }
    m_pSkinRoot = pSkinRoot;
    if (!collectLibraryArea(pSkinRoot)) {
        if (m_pShowControl->toBool()) {
            kLogger.warning() << "This skin has no library area for a window "
                                 "of its own. The library stays in the main window.";
            m_pShowControl->set(0.0);
        }
        return;
    }
    if (m_pShowControl->toBool()) {
        detach();
    }
}

void LibraryWindowManager::clearSkin() {
    attach();
    m_slots.clear();
    m_pLibraryContainer = nullptr;
    m_pSkinRoot = nullptr;
}

void LibraryWindowManager::slotShowControlChanged(double value) {
    if (value > 0.0) {
        detach();
    } else {
        attach();
    }
}

void LibraryWindowManager::slotMaximizedControlChanged(double value) {
    // The maximized page of a skin holds the small decks and the place of the
    // library. While the library is out, that page shows almost nothing.
    if (value > 0.0 && isDetached()) {
        m_pMaximizedControl->set(0.0);
    }
}

void LibraryWindowManager::onCloseRequested(quint64 generation) {
    // The call comes through the event loop, thus the window that asked can
    // be gone and another window can be open. Only this generation may close.
    if (generation != m_windowGeneration) {
        return;
    }
    attach();
    m_pShowControl->set(0.0);
}

bool LibraryWindowManager::collectLibraryArea(QWidget* pSkinRoot) {
    m_slots.clear();
    m_pLibraryContainer = nullptr;
    if (pSkinRoot == nullptr) {
        return false;
    }
    WLibrary* pLibrary = pSkinRoot->findChild<WLibrary*>();
    if (pLibrary == nullptr) {
        return false;
    }

    // Each skin of Mixxx builds the library one time as a singleton and shows
    // it through singleton containers. Such a container is also the way back.
    for (WSingletonContainer* pContainer :
            pSkinRoot->findChildren<WSingletonContainer*>()) {
        QWidget* pSingleton = pContainer->singletonWidget();
        if (pSingleton == nullptr) {
            continue;
        }
        if (pSingleton == pLibrary || pSingleton->isAncestorOf(pLibrary)) {
            m_pLibraryContainer = pSingleton;
            m_slots.append(LibrarySlot{pContainer, false});
        }
    }
    return !m_pLibraryContainer.isNull();
}

WSingletonContainer* LibraryWindowManager::visibleContainer() const {
    WSingletonContainer* pFirst = nullptr;
    for (const auto& slot : m_slots) {
        if (slot.pContainer.isNull()) {
            continue;
        }
        if (slot.pContainer->isVisible()) {
            return slot.pContainer;
        }
        if (pFirst == nullptr) {
            pFirst = slot.pContainer;
        }
    }
    return pFirst;
}

void LibraryWindowManager::detach() {
    if (m_pWindow || m_pLibraryContainer.isNull() || m_pMainWindow.isNull()) {
        return;
    }
    // Give the main window the usual page back before the library goes out.
    m_pMaximizedControl->set(0.0);

    auto pWindow = std::make_unique<WLibraryWindow>(m_pConfig);
    pWindow->applyStyle(m_pMainWindow->styleSheet(),
            m_pSkinRoot.isNull() ? QString() : m_pSkinRoot->styleSheet(),
            m_pMainWindow->palette());
    if (m_pKeyboard) {
        pWindow->installEventFilter(m_pKeyboard.get());
    }
    // A queued connection keeps the window alive until the close event is
    // complete, because the slot deletes the window.
    const quint64 generation = ++m_windowGeneration;
    connect(
            pWindow.get(),
            &WLibraryWindow::closeRequested,
            this,
            [this, generation] { onCloseRequested(generation); },
            Qt::QueuedConnection);

    m_pWindow = std::move(pWindow);
    m_pWindow->adoptLibraryWidget(m_pLibraryContainer);
    // The main window now gives the free space to the decks.
    for (auto& slot : m_slots) {
        if (!slot.pContainer.isNull()) {
            slot.wasHidden = slot.pContainer->isHidden();
            slot.pContainer->hide();
        }
    }
    m_pWindow->restorePlacement();
    m_pWindow->raise();
    m_pWindow->activateWindow();
}

void LibraryWindowManager::attach() {
    if (!m_pWindow) {
        return;
    }
    m_pWindow->savePlacement();

    if (!m_pLibraryContainer.isNull()) {
        // Give each place the state that the skin gave it.
        for (const auto& slot : std::as_const(m_slots)) {
            if (!slot.pContainer.isNull()) {
                slot.pContainer->setVisible(!slot.wasHidden);
            }
        }
        WSingletonContainer* pContainer = visibleContainer();
        VERIFY_OR_DEBUG_ASSERT(pContainer) {
            // The skin went away while the library was out. The library
            // widget belongs to that skin, thus it goes away too.
            m_pLibraryContainer->setParent(nullptr);
            m_pLibraryContainer->deleteLater();
            m_pLibraryContainer = nullptr;
            m_pWindow.reset();
            return;
        }
        pContainer->adoptSingletonWidget();
    }
    m_pWindow.reset();
}
