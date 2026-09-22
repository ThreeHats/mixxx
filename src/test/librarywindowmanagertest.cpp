#include <gtest/gtest.h>

#include <QApplication>
#include <QDomDocument>
#include <QDomElement>
#include <QVBoxLayout>
#include <QWidget>
#include <memory>

#include "control/controlpushbutton.h"
#include "librarywindow/librarywindowmanager.h"
#include "librarywindow/wlibrarywindow.h"
#include "skin/legacy/skincontext.h"
#include "test/mixxxtest.h"
#include "widget/wlibrary.h"
#include "widget/wsingletoncontainer.h"

namespace {

const ConfigKey kShowKey(QStringLiteral("[Skin]"), QStringLiteral("show_library_window"));
const ConfigKey kMaximizedKey(
        QStringLiteral("[Skin]"), QStringLiteral("show_maximized_library"));
const QString kLibrarySingleton = QStringLiteral("LibrarySingleton");
const QString kPreviewSingleton = QStringLiteral("PreviewSingleton");

QWidget* makeBox(QWidget* pParent) {
    auto* pWidget = new QWidget(pParent);
    auto* pLayout = new QVBoxLayout(pWidget);
    pLayout->setContentsMargins(0, 0, 0, 0);
    if (pParent && pParent->layout()) {
        pParent->layout()->addWidget(pWidget);
    }
    return pWidget;
}

/// Build the skin tree of a legacy skin: two pages, each with a place for the
/// library, plus a second singleton that stands for the preview deck.
class LibraryWindowManagerTest : public MixxxTest {
  protected:
    void SetUp() override {
        m_pShowControl = std::make_unique<ControlPushButton>(kShowKey);
        m_pMaximizedControl = std::make_unique<ControlPushButton>(kMaximizedKey);
        m_pMainWindow = std::make_unique<QWidget>();

        m_pSkinRoot = new QWidget(m_pMainWindow.get());
        auto* pRootLayout = new QVBoxLayout(m_pSkinRoot);
        pRootLayout->setContentsMargins(0, 0, 0, 0);

        m_pDecoy = makeBox(m_pSkinRoot);

        // The parser makes a singleton as a hidden child of the skin and does
        // not put it in a layout.
        m_pLibraryArea = new QWidget(m_pSkinRoot);
        auto* pAreaLayout = new QVBoxLayout(m_pLibraryArea);
        pAreaLayout->setContentsMargins(0, 0, 0, 0);
        pAreaLayout->addWidget(new WLibrary(m_pLibraryArea));
        m_pLibraryArea->hide();

        m_pPreviewArea = new QWidget(m_pSkinRoot);
        m_pPreviewArea->hide();

        m_context = std::make_unique<SkinContext>(config(), QStringLiteral("test"));
        m_context->defineSingleton(kLibrarySingleton, m_pLibraryArea);
        m_context->defineSingleton(kPreviewSingleton, m_pPreviewArea);

        m_pPageA = makeBox(m_pSkinRoot);
        m_pLibrarySlotA = makeContainer(m_pPageA, kLibrarySingleton);
        m_pPreviewSlot = makeContainer(m_pPageA, kPreviewSingleton);

        // The page that the widget stack does not show.
        m_pPageB = makeBox(m_pSkinRoot);
        m_pLibrarySlotB = makeContainer(m_pPageB, kLibrarySingleton);
        m_pPageB->hide();

        m_pMainWindow->show();
        QApplication::processEvents();

        m_pManager = std::make_unique<LibraryWindowManager>(
                config(), m_pMainWindow.get(), nullptr);
    }

    void TearDown() override {
        m_pManager.reset();
        m_pMainWindow.reset();
        m_pMaximizedControl.reset();
        m_pShowControl.reset();
    }

    WSingletonContainer* makeContainer(QWidget* pParent, const QString& name) {
        auto* pContainer = new WSingletonContainer(pParent);
        QDomElement node = m_dom.createElement(QStringLiteral("SingletonContainer"));
        QDomElement objectName = m_dom.createElement(QStringLiteral("ObjectName"));
        objectName.appendChild(m_dom.createTextNode(name));
        node.appendChild(objectName);
        pContainer->setup(node, *m_context);
        pParent->layout()->addWidget(pContainer);
        return pContainer;
    }

    WLibraryWindow* findWindow() const {
        for (QWidget* pWidget : QApplication::topLevelWidgets()) {
            if (pWidget->objectName() == QStringLiteral("LibraryWindow")) {
                return qobject_cast<WLibraryWindow*>(pWidget);
            }
        }
        return nullptr;
    }

    void setShow(bool show) {
        m_pShowControl->set(show ? 1.0 : 0.0);
    }

    /// The manager takes this control through the event loop.
    void setMaximized(bool maximized) {
        m_pMaximizedControl->set(maximized ? 1.0 : 0.0);
        QApplication::processEvents();
    }

    QDomDocument m_dom;
    std::unique_ptr<ControlPushButton> m_pShowControl;
    std::unique_ptr<ControlPushButton> m_pMaximizedControl;
    std::unique_ptr<QWidget> m_pMainWindow;
    std::unique_ptr<SkinContext> m_context;
    QWidget* m_pSkinRoot;
    QWidget* m_pDecoy;
    QWidget* m_pPageA;
    QWidget* m_pPageB;
    QWidget* m_pLibraryArea;
    QWidget* m_pPreviewArea;
    WSingletonContainer* m_pLibrarySlotA;
    WSingletonContainer* m_pLibrarySlotB;
    WSingletonContainer* m_pPreviewSlot;
    std::unique_ptr<LibraryWindowManager> m_pManager;
};

TEST_F(LibraryWindowManagerTest, TheVisiblePlaceTakesTheLibraryOnShow) {
    m_pManager->setSkin(m_pSkinRoot);
    EXPECT_FALSE(m_pManager->isDetached());
    EXPECT_EQ(m_pLibrarySlotA, m_pLibraryArea->parentWidget());
    // A second singleton keeps its own place.
    EXPECT_EQ(m_pPreviewSlot, m_pPreviewArea->parentWidget());
}

TEST_F(LibraryWindowManagerTest, TheControlMovesTheLibraryOutAndBack) {
    m_pManager->setSkin(m_pSkinRoot);

    setShow(true);
    EXPECT_TRUE(m_pManager->isDetached());
    WLibraryWindow* pWindow = findWindow();
    ASSERT_NE(nullptr, pWindow);
    EXPECT_EQ(pWindow, m_pLibraryArea->window());
    EXPECT_TRUE(m_pLibrarySlotA->isHidden());
    EXPECT_TRUE(m_pLibrarySlotB->isHidden());
    // Nothing else of the skin moves or hides.
    EXPECT_EQ(m_pSkinRoot, m_pDecoy->parentWidget());
    EXPECT_FALSE(m_pDecoy->isHidden());
    EXPECT_EQ(m_pPreviewSlot, m_pPreviewArea->parentWidget());

    setShow(false);
    EXPECT_FALSE(m_pManager->isDetached());
    EXPECT_EQ(m_pLibrarySlotA, m_pLibraryArea->parentWidget());
    EXPECT_FALSE(m_pLibrarySlotA->isHidden());
    EXPECT_EQ(nullptr, findWindow());
}

TEST_F(LibraryWindowManagerTest, APlaceThatTheSkinHidKeepsItsState) {
    // A skin can bind the visibility of a place to a control. The library must
    // not make such a place visible when it comes back.
    m_pLibrarySlotB->hide();
    m_pManager->setSkin(m_pSkinRoot);

    setShow(true);
    setShow(false);

    EXPECT_TRUE(m_pLibrarySlotB->isHidden());
    m_pPageB->show();
    QApplication::processEvents();
    EXPECT_TRUE(m_pLibrarySlotB->isHidden());
    EXPECT_EQ(m_pLibrarySlotA, m_pLibraryArea->parentWidget());
}

TEST_F(LibraryWindowManagerTest, ThePageThatWasHiddenStillTakesTheLibrary) {
    // The place of the other page of the widget stack is not hidden by the
    // skin, thus it must take the library when that page comes up.
    m_pManager->setSkin(m_pSkinRoot);

    setShow(true);
    setShow(false);

    m_pPageB->show();
    QApplication::processEvents();
    EXPECT_FALSE(m_pLibrarySlotB->isHidden());
    EXPECT_EQ(m_pLibrarySlotB, m_pLibraryArea->parentWidget());
}

TEST_F(LibraryWindowManagerTest, TheStateOfTheControlOpensTheWindowWithTheSkin) {
    setShow(true);
    m_pManager->setSkin(m_pSkinRoot);
    EXPECT_TRUE(m_pManager->isDetached());
}

TEST_F(LibraryWindowManagerTest, AClosedWindowGivesTheLibraryBack) {
    m_pManager->setSkin(m_pSkinRoot);
    setShow(true);
    WLibraryWindow* pWindow = findWindow();
    ASSERT_NE(nullptr, pWindow);

    // The window accepts the close, because a refused close also stops the
    // logout of the desktop session.
    EXPECT_TRUE(pWindow->close());
    QApplication::processEvents();

    EXPECT_FALSE(m_pManager->isDetached());
    EXPECT_EQ(m_pLibrarySlotA, m_pLibraryArea->parentWidget());
    EXPECT_FALSE(m_pShowControl->toBool());
}

TEST_F(LibraryWindowManagerTest, ALateCloseLeavesTheNewWindowOpen) {
    m_pManager->setSkin(m_pSkinRoot);
    setShow(true);
    WLibraryWindow* pWindow = findWindow();
    ASSERT_NE(nullptr, pWindow);

    // The close reaches the manager through the event loop. Before it
    // arrives, the library comes back and goes out again.
    pWindow->close();
    setShow(false);
    setShow(true);
    QApplication::processEvents();

    EXPECT_TRUE(m_pManager->isDetached());
    EXPECT_TRUE(m_pShowControl->toBool());
    ASSERT_NE(nullptr, findWindow());
    EXPECT_EQ(findWindow(), m_pLibraryArea->window());
}

TEST_F(LibraryWindowManagerTest, ClearSkinGivesTheLibraryBack) {
    m_pManager->setSkin(m_pSkinRoot);
    setShow(true);
    ASSERT_TRUE(m_pManager->isDetached());

    m_pManager->clearSkin();

    EXPECT_FALSE(m_pManager->isDetached());
    EXPECT_EQ(m_pLibrarySlotA, m_pLibraryArea->parentWidget());
    EXPECT_EQ(nullptr, findWindow());
}

TEST_F(LibraryWindowManagerTest, ASkinWithNoLibraryTurnsTheControlOff) {
    setShow(true);
    QWidget bareSkin;

    m_pManager->setSkin(&bareSkin);

    EXPECT_FALSE(m_pManager->isDetached());
    EXPECT_FALSE(m_pShowControl->toBool());
}

TEST_F(LibraryWindowManagerTest, ASkinReloadKeepsTheWindow) {
    setShow(true);
    m_pManager->setSkin(m_pSkinRoot);
    ASSERT_TRUE(m_pManager->isDetached());

    // A skin reload puts the library back, then takes the new skin.
    m_pManager->clearSkin();
    EXPECT_EQ(m_pLibrarySlotA, m_pLibraryArea->parentWidget());
    m_pManager->setSkin(m_pSkinRoot);

    EXPECT_TRUE(m_pManager->isDetached());
    ASSERT_NE(nullptr, findWindow());
    EXPECT_EQ(findWindow(), m_pLibraryArea->window());
}

TEST_F(LibraryWindowManagerTest, TheLibraryOutTurnsTheMaximizedPageOff) {
    // The maximized page of a skin holds only the small decks and the place of
    // the library, thus a maximized library hides the main window.
    m_pManager->setSkin(m_pSkinRoot);
    setMaximized(true);

    setShow(true);

    EXPECT_TRUE(m_pManager->isDetached());
    EXPECT_FALSE(m_pMaximizedControl->toBool());
}

TEST_F(LibraryWindowManagerTest, AMaximizeRequestDoesNothingWhileTheLibraryIsOut) {
    m_pManager->setSkin(m_pSkinRoot);
    setShow(true);
    ASSERT_TRUE(m_pManager->isDetached());

    setMaximized(true);

    EXPECT_FALSE(m_pMaximizedControl->toBool());
    EXPECT_TRUE(m_pManager->isDetached());
}

TEST_F(LibraryWindowManagerTest, ALateMaximizeRequestLeavesTheLibraryBackAlone) {
    // The manager takes the control through the event loop, thus the slot must
    // read the state of the window and not the state at the time of the set.
    m_pManager->setSkin(m_pSkinRoot);
    setShow(true);
    ASSERT_TRUE(m_pManager->isDetached());

    m_pMaximizedControl->set(1.0);
    setShow(false);
    QApplication::processEvents();

    EXPECT_FALSE(m_pManager->isDetached());
    EXPECT_TRUE(m_pMaximizedControl->toBool());
}

TEST_F(LibraryWindowManagerTest, TheLibraryBackGivesTheMaximizedPageBack) {
    m_pManager->setSkin(m_pSkinRoot);
    setShow(true);
    setShow(false);
    ASSERT_FALSE(m_pManager->isDetached());

    setMaximized(true);

    EXPECT_TRUE(m_pMaximizedControl->toBool());
}

} // namespace
