#include <gtest/gtest.h>

#include <QList>
#include <QRect>

#include "librarywindow/librarywindowplacement.h"

namespace {

using Screen = LibraryWindowPlacement::Screen;

const QString kPrimary = QStringLiteral("HDMI-1");
const QString kSecond = QStringLiteral("DP-2");

QList<Screen> twoScreens() {
    return {Screen{kPrimary, QRect(0, 0, 1920, 1080)},
            Screen{kSecond, QRect(1920, 0, 2560, 1440)}};
}

TEST(LibraryWindowPlacementTest, FormatAndParseGeometry) {
    const QRect geometry(1930, 40, 2000, 1200);
    const QString text = LibraryWindowPlacement::formatGeometry(geometry);
    EXPECT_EQ(QStringLiteral("1930,40,2000,1200"), text);
    EXPECT_EQ(geometry, LibraryWindowPlacement::parseGeometry(text));
}

TEST(LibraryWindowPlacementTest, ParseRejectsBadText) {
    EXPECT_FALSE(LibraryWindowPlacement::parseGeometry(QString()).isValid());
    EXPECT_FALSE(LibraryWindowPlacement::parseGeometry(QStringLiteral("1,2,3")).isValid());
    EXPECT_FALSE(LibraryWindowPlacement::parseGeometry(QStringLiteral("a,b,c,d")).isValid());
    // A window smaller than the minimum is not a geometry that Mixxx wrote.
    EXPECT_FALSE(LibraryWindowPlacement::parseGeometry(QStringLiteral("0,0,10,10")).isValid());
}

TEST(LibraryWindowPlacementTest, FormatOfAnInvalidRectangleIsEmpty) {
    EXPECT_TRUE(LibraryWindowPlacement::formatGeometry(QRect()).isEmpty());
}

TEST(LibraryWindowPlacementTest, ScreenNameFromTheCenterOfTheWindow) {
    EXPECT_EQ(kSecond,
            LibraryWindowPlacement::screenNameFor(
                    QRect(2000, 100, 800, 600), twoScreens()));
    EXPECT_EQ(kPrimary,
            LibraryWindowPlacement::screenNameFor(
                    QRect(10, 10, 800, 600), twoScreens()));
    EXPECT_TRUE(LibraryWindowPlacement::screenNameFor(
            QRect(9000, 9000, 800, 600), twoScreens())
                        .isEmpty());
}

TEST(LibraryWindowPlacementTest, KeepsTheGeometryOnTheSavedScreen) {
    const QRect saved(2000, 100, 1600, 900);
    EXPECT_EQ(saved,
            LibraryWindowPlacement::resolveGeometry(
                    saved, kSecond, twoScreens(), kPrimary));
}

TEST(LibraryWindowPlacementTest, MovesToThePrimaryScreenIfTheSavedScreenIsAbsent) {
    const QRect saved(2000, 100, 1600, 900);
    const QList<Screen> screens = {Screen{kPrimary, QRect(0, 0, 1920, 1080)}};
    const QRect result = LibraryWindowPlacement::resolveGeometry(
            saved, kSecond, screens, kPrimary);
    EXPECT_EQ(saved.size(), result.size());
    EXPECT_EQ(QRect(0, 0, 1920, 1080).center(), result.center());
}

TEST(LibraryWindowPlacementTest, GivesADefaultSizeWithNoSavedGeometry) {
    const QRect result = LibraryWindowPlacement::resolveGeometry(
            QRect(), QString(), twoScreens(), kPrimary);
    EXPECT_EQ(LibraryWindowPlacement::kDefaultWidth, result.width());
    EXPECT_EQ(LibraryWindowPlacement::kDefaultHeight, result.height());
    EXPECT_EQ(QRect(0, 0, 1920, 1080).center(), result.center());
}

TEST(LibraryWindowPlacementTest, MovesAWindowThatIsOutsideTheSavedScreen) {
    // The screen became smaller while Mixxx was off.
    const QList<Screen> screens = {Screen{kPrimary, QRect(0, 0, 1280, 720)}};
    const QRect result = LibraryWindowPlacement::resolveGeometry(
            QRect(1000, 600, 1000, 700), kPrimary, screens, kPrimary);
    EXPECT_TRUE(QRect(0, 0, 1280, 720).contains(result));
}

TEST(LibraryWindowPlacementTest, FallsBackToTheFirstScreenIfThePrimaryIsAbsent) {
    const QRect result = LibraryWindowPlacement::resolveGeometry(QRect(),
            QString(),
            twoScreens(),
            QStringLiteral("VGA-9"));
    EXPECT_EQ(QRect(0, 0, 1920, 1080).center(), result.center());
}

TEST(LibraryWindowPlacementTest, KeepsTheGeometryWithNoScreens) {
    const QRect saved(2000, 100, 1600, 900);
    EXPECT_EQ(saved,
            LibraryWindowPlacement::resolveGeometry(
                    saved, kSecond, QList<Screen>(), kPrimary));
}

} // namespace
