#include "librarywindow/librarywindowmanager.h"

#include <QLayout>
#include <QLayoutItem>
#include <utility>

#include "control/controlproxy.h"
#include "controllers/keyboard/keyboardeventfilter.h"
#include "librarywindow/wlibrarywindow.h"
#include "moc_librarywindowmanager.cpp"
#include "util/assert.h"
#include "util/logger.h"
#include "widget/wlibrary.h"
#include "widget/wlibrarysidebar.h"
#include "widget/woverview.h"
#include "widget/wsearchlineedit.h"
#include "widget/wsingletoncontainer.h"
#include "widget/wwaveformviewer.h"

namespace {

const mixxx::Logger kLogger("LibraryWindowManager");

/// Return the closest widget that holds both widgets.
QWidget* commonAncestor(QWidget* pFirst, QWidget* pSecond) {
    if (pFirst == nullptr) {
        return pSecond;
    }
    if (pSecond == nullptr) {
        return pFirst;
    }
    QWidget* pCandidate = pFirst;
    while (pCandidate != nullptr && pCandidate != pSecond &&
            !pCandidate->isAncestorOf(pSecond)) {
        pCandidate = pCandidate->parentWidget();
    }
    return pCandidate;
}

bool holdsDeckWidgets(const QWidget* pWidget) {
    return pWidget->findChild<WWaveformViewer*>() != nullptr ||
            pWidget->findChild<WOverview*>() != nullptr;
}

} // namespace

const ConfigKey LibraryWindowManager::kShowConfigKey(
        QStringLiteral("[Skin]"), QStringLiteral("show_library_window"));

LibraryWindowManager::LibraryWindowManager(UserSettingsPointer pConfig,
        QWidget* pMainWindow,
        std::shared_ptr<KeyboardEventFilter> pKeyboard,
        QObject* pParent)
        : QObject(pParent),
          m_pConfig(pConfig),
          m_pMainWindow(pMainWindow),
          m_pKeyboard(pKeyboard),
          m_pShowControl(make_parented<ControlProxy>(kShowConfigKey, this)) {
    m_pShowControl->connectValueChanged(
            this, &LibraryWindowManager::slotShowControlChanged);
}

LibraryWindowManager::~LibraryWindowManager() {
    clearSkin();
}

void LibraryWindowManager::setSkin(QWidget* pSkinRoot) {
    VERIFY_OR_DEBUG_ASSERT(!m_pWindow) {
        clearSkin();
    }
    m_pSkinRoot = pSkinRoot;
    m_pLibraryContainer = findLibraryContainer(pSkinRoot);
    if (!m_pLibraryContainer) {
        kLogger.info() << "This skin has no library area for a window of its own.";
        return;
    }
    if (m_pShowControl->toBool()) {
        detach();
    }
}

void LibraryWindowManager::clearSkin() {
    attach();
    m_containers.clear();
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

void LibraryWindowManager::slotCloseRequested() {
    attach();
    m_pShowControl->set(0.0);
}

QWidget* LibraryWindowManager::findLibraryContainer(QWidget* pSkinRoot) {
    m_containers.clear();
    if (pSkinRoot == nullptr) {
        return nullptr;
    }
    WLibrary* pLibrary = pSkinRoot->findChild<WLibrary*>();
    if (pLibrary == nullptr) {
        return nullptr;
    }

    // Each skin of Mixxx builds the library one time and shows it through a
    // singleton container. Such a container is also the way back.
    QWidget* pContainer = nullptr;
    const auto skinContainers = pSkinRoot->findChildren<WSingletonContainer*>();
    for (WSingletonContainer* pSlot : skinContainers) {
        QWidget* pSingleton = pSlot->singletonWidget();
        if (pSingleton == nullptr) {
            continue;
        }
        if (pSingleton == pLibrary || pSingleton->isAncestorOf(pLibrary)) {
            pContainer = pSingleton;
            m_containers.append(pSlot);
        }
    }
    if (pContainer != nullptr) {
        return pContainer;
    }

    // A skin that holds the library directly: take the closest widget that
    // holds the tracks table, the sidebar and the search box.
    QWidget* pCandidate = pLibrary;
    pCandidate = commonAncestor(pCandidate, pSkinRoot->findChild<WLibrarySidebar*>());
    pCandidate = commonAncestor(pCandidate, pSkinRoot->findChild<WSearchLineEdit*>());
    if (pCandidate == nullptr || pCandidate == pSkinRoot || holdsDeckWidgets(pCandidate)) {
        return nullptr;
    }
    QWidget* pParent = pCandidate->parentWidget();
    if (pParent == nullptr || pParent->layout() == nullptr) {
        return nullptr;
    }
    return pCandidate;
}

WSingletonContainer* LibraryWindowManager::visibleContainer() const {
    WSingletonContainer* pFirst = nullptr;
    for (const auto& pSlot : m_containers) {
        if (pSlot.isNull()) {
            continue;
        }
        if (pSlot->isVisible()) {
            return pSlot;
        }
        if (pFirst == nullptr) {
            pFirst = pSlot;
        }
    }
    return pFirst;
}

void LibraryWindowManager::detach() {
    if (m_pWindow || m_pLibraryContainer.isNull() || m_pMainWindow.isNull()) {
        return;
    }

    auto pWindow = std::make_unique<WLibraryWindow>(m_pConfig);
    pWindow->applyStyle(m_pMainWindow->styleSheet(),
            m_pSkinRoot.isNull() ? QString() : m_pSkinRoot->styleSheet(),
            m_pMainWindow->palette());
    if (m_pKeyboard) {
        pWindow->installEventFilter(m_pKeyboard.get());
    }
    // A queued connection keeps the window alive until the close event is
    // complete, because the slot deletes the window.
    connect(pWindow.get(),
            &WLibraryWindow::closeRequested,
            this,
            &LibraryWindowManager::slotCloseRequested,
            Qt::QueuedConnection);

    if (m_containers.isEmpty()) {
        QWidget* pParent = m_pLibraryContainer->parentWidget();
        QLayout* pLayout = pParent == nullptr ? nullptr : pParent->layout();
        VERIFY_OR_DEBUG_ASSERT(pLayout) {
            return;
        }
        m_pPlaceholder = new QWidget(pParent);
        m_pPlaceholder->setFixedSize(0, 0);
        delete pLayout->replaceWidget(m_pLibraryContainer, m_pPlaceholder);
    }

    m_pWindow = std::move(pWindow);
    m_pWindow->adoptLibraryWidget(m_pLibraryContainer);
    // The main window now gives the free space to the decks.
    for (const auto& pSlot : std::as_const(m_containers)) {
        if (!pSlot.isNull()) {
            pSlot->hide();
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
        if (!m_containers.isEmpty()) {
            for (const auto& pSlot : std::as_const(m_containers)) {
                if (!pSlot.isNull()) {
                    pSlot->show();
                }
            }
            WSingletonContainer* pSlot = visibleContainer();
            VERIFY_OR_DEBUG_ASSERT(pSlot) {
                m_pLibraryContainer->setParent(m_pSkinRoot);
                m_pWindow.reset();
                return;
            }
            pSlot->adoptSingletonWidget();
        } else if (!m_pPlaceholder.isNull()) {
            QWidget* pParent = m_pPlaceholder->parentWidget();
            QLayout* pLayout = pParent == nullptr ? nullptr : pParent->layout();
            VERIFY_OR_DEBUG_ASSERT(pLayout) {
                m_pLibraryContainer->setParent(m_pSkinRoot);
                m_pWindow.reset();
                return;
            }
            m_pWindow->layout()->removeWidget(m_pLibraryContainer);
            m_pLibraryContainer->setParent(pParent);
            delete pLayout->replaceWidget(m_pPlaceholder, m_pLibraryContainer);
            m_pLibraryContainer->show();
        }
    }
    delete m_pPlaceholder.data();
    m_pPlaceholder = nullptr;
    m_pWindow.reset();
}
