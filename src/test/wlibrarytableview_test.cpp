#include "widget/wlibrarytableview.h"

#include <gtest/gtest.h>

#include <QApplication>
#include <QHeaderView>
#include <QScrollBar>
#include <QStandardItemModel>
#include <QWheelEvent>
#include <memory>

#include "test/mixxxtest.h"

namespace {

constexpr int kRows = 200;
constexpr int kColumns = 12;
constexpr int kColumnWidth = 200;
constexpr int kViewWidth = 300;
constexpr int kViewHeight = 200;

/// The smallest table that the library base class accepts.
class TestTableView : public WLibraryTableView {
  public:
    explicit TestTableView(UserSettingsPointer pConfig)
            : WLibraryTableView(nullptr, pConfig) {
    }

    void onShow() override {
    }

    bool hasFocus() const override {
        return QWidget::hasFocus();
    }

  protected:
    QString getModelStateKey() const override {
        return QStringLiteral("test");
    }
};

class WLibraryTableViewTest : public MixxxTest {
  protected:
    void SetUp() override {
        m_pModel = std::make_unique<QStandardItemModel>(kRows, kColumns);
        for (int row = 0; row < kRows; ++row) {
            for (int column = 0; column < kColumns; ++column) {
                m_pModel->setItem(row,
                        column,
                        new QStandardItem(QStringLiteral("r%1c%2")
                                                  .arg(row)
                                                  .arg(column)));
            }
        }

        m_pView = std::make_unique<TestTableView>(config());
        m_pView->setModel(m_pModel.get());
        m_pView->horizontalHeader()->setDefaultSectionSize(kColumnWidth);
        for (int column = 0; column < kColumns; ++column) {
            m_pView->setColumnWidth(column, kColumnWidth);
        }
        m_pView->resize(kViewWidth, kViewHeight);
        m_pView->show();
        QApplication::processEvents();
    }

    void TearDown() override {
        m_pView.reset();
        m_pModel.reset();
    }

    /// Send a wheel of one notch down to the table, as the window system does.
    bool sendWheel(Qt::KeyboardModifiers modifiers, int angle = -120) {
        const QPointF pos(kViewWidth / 2.0, kViewHeight / 2.0);
        QWheelEvent event(pos,
                m_pView->mapToGlobal(pos.toPoint()),
                QPoint(0, 0),
                QPoint(0, angle),
                Qt::NoButton,
                modifiers,
                Qt::NoScrollPhase,
                false,
                Qt::MouseEventNotSynthesized);
        QApplication::sendEvent(m_pView->viewport(), &event);
        QApplication::processEvents();
        return event.isAccepted();
    }

    int horizontalValue() const {
        return m_pView->horizontalScrollBar()->value();
    }

    int verticalValue() const {
        return m_pView->verticalScrollBar()->value();
    }

    std::unique_ptr<QStandardItemModel> m_pModel;
    std::unique_ptr<TestTableView> m_pView;
};

TEST_F(WLibraryTableViewTest, TheTableCanScrollBothWays) {
    // Without this the other tests cannot fail.
    ASSERT_GT(m_pView->horizontalScrollBar()->maximum(), 0);
    ASSERT_GT(m_pView->verticalScrollBar()->maximum(), 0);
}

TEST_F(WLibraryTableViewTest, ShiftAndTheWheelMoveTheTableToTheSide) {
    EXPECT_TRUE(sendWheel(Qt::ShiftModifier));

    EXPECT_GT(horizontalValue(), 0);
    EXPECT_EQ(0, verticalValue());
}

TEST_F(WLibraryTableViewTest, ShiftAndTheWheelUpMoveTheTableBack) {
    sendWheel(Qt::ShiftModifier);
    const int afterFirstWheel = horizontalValue();
    ASSERT_GT(afterFirstWheel, 0);

    sendWheel(Qt::ShiftModifier, 120);

    EXPECT_LT(horizontalValue(), afterFirstWheel);
    EXPECT_EQ(0, verticalValue());
}

TEST_F(WLibraryTableViewTest, TheWheelAloneMovesTheTableDown) {
    sendWheel(Qt::NoModifier);

    EXPECT_GT(verticalValue(), 0);
    EXPECT_EQ(0, horizontalValue());
}

TEST_F(WLibraryTableViewTest, AWheelToTheSideKeepsItsDirection) {
    // A wheel that already goes to the side needs no help from Shift.
    const QPointF pos(kViewWidth / 2.0, kViewHeight / 2.0);
    QWheelEvent event(pos,
            m_pView->mapToGlobal(pos.toPoint()),
            QPoint(0, 0),
            QPoint(-120, 0),
            Qt::NoButton,
            Qt::ShiftModifier,
            Qt::NoScrollPhase,
            false,
            Qt::MouseEventNotSynthesized);
    QApplication::sendEvent(m_pView->viewport(), &event);
    QApplication::processEvents();

    EXPECT_GT(horizontalValue(), 0);
    EXPECT_EQ(0, verticalValue());
}

} // namespace
