#include <gtest/gtest.h>

#include <QWidget>

#include "librarywindow/librarywindowfocus.h"
#include "test/mixxxtest.h"

namespace {

using mixxx::librarywindow::needsWindowActivation;

class LibraryWindowFocusTest : public MixxxTest {
  protected:
    QWidget m_mainWindow;
    QWidget m_libraryWindow;
    QWidget m_dialog;
};

TEST_F(LibraryWindowFocusTest, NoActivationWhileAnotherProgramIsInFront) {
    // QApplication::activeWindow() is null while Mixxx is behind. A controller
    // must not pull Mixxx over the program that the user works in.
    EXPECT_FALSE(needsWindowActivation(&m_libraryWindow, nullptr, nullptr));
    EXPECT_FALSE(needsWindowActivation(&m_mainWindow, nullptr, nullptr));
}

TEST_F(LibraryWindowFocusTest, NoActivationWhenTheWidgetIsInTheActiveWindow) {
    EXPECT_FALSE(needsWindowActivation(&m_mainWindow, &m_mainWindow, nullptr));
}

TEST_F(LibraryWindowFocusTest, ActivationWhenTheOtherMixxxWindowIsActive) {
    EXPECT_TRUE(needsWindowActivation(&m_libraryWindow, &m_mainWindow, nullptr));
    EXPECT_TRUE(needsWindowActivation(&m_mainWindow, &m_libraryWindow, nullptr));
}

TEST_F(LibraryWindowFocusTest, NoActivationWhileAModalDialogIsOpen) {
    EXPECT_FALSE(needsWindowActivation(&m_libraryWindow, &m_mainWindow, &m_dialog));
}

TEST_F(LibraryWindowFocusTest, NoActivationWithNoTargetWindow) {
    EXPECT_FALSE(needsWindowActivation(nullptr, &m_mainWindow, nullptr));
}

} // namespace
