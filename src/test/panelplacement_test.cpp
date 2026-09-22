#include "muxic/relatedtracks/panelplacement.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QSignalSpy>
#include <QSplitter>
#include <QWidget>
#include <memory>

#include "control/controlpushbutton.h"
#include "test/mixxxtest.h"

using muxic::PanelPlacement;

namespace {

const ConfigKey kShowKey(
        QStringLiteral("[Skin]"), QStringLiteral("show_related_tracks_panel"));
const ConfigKey kHeightKey(
        QStringLiteral("[Library]"), QStringLiteral("related_panel_height"));

constexpr int kDefaultHeight = 160;
constexpr int kSplitterHeight = 600;
// The handle of the splitter takes room, and a layout rounds.
constexpr int kSlack = 2;

/// The place of the panel in the splitter of a skin.
class PanelPlacementTest : public MixxxTest {
  protected:
    void SetUp() override {
        m_pShowControl = std::make_unique<ControlPushButton>(kShowKey, false, 1.0);
        m_pShowControl->setButtonMode(mixxx::control::ButtonMode::Toggle);

        m_pSplitter = std::make_unique<QSplitter>(Qt::Vertical);
        m_pSplitter->setChildrenCollapsible(false);
        m_pTable = new QWidget(m_pSplitter.get());
        m_pPanel = new QWidget(m_pSplitter.get());
        m_pSplitter->addWidget(m_pTable);
        m_pSplitter->addWidget(m_pPanel);
        m_pSplitter->resize(400, kSplitterHeight);
    }

    void TearDown() override {
        m_pSplitter.reset();
        m_pShowControl.reset();
    }

    /// Makes the placement and shows the splitter, as a skin does.
    PanelPlacement* show() {
        auto* pPlacement = new PanelPlacement(m_pPanel,
                m_pSplitter.get(),
                config(),
                kShowKey,
                kHeightKey,
                kDefaultHeight);
        m_pSplitter->show();
        settle();
        return pPlacement;
    }

    void settle() {
        application()->processEvents();
        application()->processEvents();
    }

    int panelHeight() const {
        const QList<int> sizes = m_pSplitter->sizes();
        return sizes.size() == 2 ? sizes.at(1) : -1;
    }

    /// The room that the two widgets of the splitter share.
    int totalHeight() const {
        const QList<int> sizes = m_pSplitter->sizes();
        return sizes.size() == 2 ? sizes.at(0) + sizes.at(1) : -1;
    }

    std::unique_ptr<ControlPushButton> m_pShowControl;
    std::unique_ptr<QSplitter> m_pSplitter;
    QWidget* m_pTable;
    QWidget* m_pPanel;
};

TEST_F(PanelPlacementTest, thePanelTakesTheHeightOfTheSettings) {
    config()->setValue(kHeightKey, 220);
    show();
    EXPECT_NEAR(220, panelHeight(), kSlack);
    // The library table keeps the rest of the room.
    EXPECT_NEAR(totalHeight() - 220, m_pSplitter->sizes().at(0), kSlack);
}

TEST_F(PanelPlacementTest, theDefaultHeightHoldsWithoutASetting) {
    ASSERT_FALSE(config()->exists(kHeightKey));
    PanelPlacement* pPlacement = show();
    EXPECT_EQ(kDefaultHeight, pPlacement->wantedHeight());
    EXPECT_NEAR(kDefaultHeight, panelHeight(), kSlack);
}

TEST_F(PanelPlacementTest, thePanelTakesAtMostHalfOfTheSplitter) {
    config()->setValue(kHeightKey, 5000);
    PanelPlacement* pPlacement = show();
    EXPECT_EQ(5000, pPlacement->wantedHeight());
    EXPECT_NEAR(totalHeight() / 2, panelHeight(), kSlack);
    EXPECT_LT(panelHeight(), kSplitterHeight);
}

TEST_F(PanelPlacementTest, aMoveOfTheSplitterKeepsTheHeight) {
    show();
    m_pSplitter->setSizes({kSplitterHeight - 90, 90});
    settle();
    emit m_pSplitter->splitterMoved(kSplitterHeight - 90, 1);
    // The settings keep the height that the panel has, not the one that
    // the caller asked for.
    EXPECT_EQ(panelHeight(), config()->getValue(kHeightKey, 0));
    EXPECT_NEAR(90, config()->getValue(kHeightKey, 0), kSlack);
}

TEST_F(PanelPlacementTest, theControlShowsAndHidesThePanel) {
    // The control starts at 1, thus the panel is there.
    PanelPlacement* pPlacement = show();
    QSignalSpy spy(pPlacement, &PanelPlacement::shownChanged);
    ASSERT_TRUE(spy.isValid());
    ASSERT_TRUE(m_pPanel->isVisible());

    m_pShowControl->set(0.0);
    settle();
    EXPECT_FALSE(m_pPanel->isVisible());
    // The library table takes the room of the panel.
    EXPECT_EQ(0, panelHeight());
    ASSERT_EQ(1, spy.count());
    EXPECT_FALSE(spy.takeFirst().at(0).toBool());

    m_pShowControl->set(1.0);
    settle();
    EXPECT_TRUE(m_pPanel->isVisible());
    EXPECT_NEAR(kDefaultHeight, panelHeight(), kSlack);
    ASSERT_EQ(1, spy.count());
    EXPECT_TRUE(spy.takeFirst().at(0).toBool());
}

TEST_F(PanelPlacementTest, aPanelThatStartsOffStaysOff) {
    m_pShowControl->set(0.0);
    show();
    EXPECT_FALSE(m_pPanel->isVisible());
    EXPECT_TRUE(m_pPanel->isHidden());
}

TEST_F(PanelPlacementTest, aMoveWhileThePanelIsOffKeepsTheOldHeight) {
    config()->setValue(kHeightKey, 200);
    show();
    m_pShowControl->set(0.0);
    settle();
    emit m_pSplitter->splitterMoved(kSplitterHeight, 1);
    // A hidden panel has no height of its own to keep.
    EXPECT_EQ(200, config()->getValue(kHeightKey, 0));
}

} // namespace
