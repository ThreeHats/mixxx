#pragma once

#include <QList>
#include <QRect>
#include <QSize>
#include <QString>

/// Where the floating library window goes on the desktop.
/// The functions are pure, thus a test can run them with no display.
class LibraryWindowPlacement {
  public:
    /// A screen of the desktop: the name and the area in virtual coordinates.
    struct Screen {
        QString name;
        QRect geometry;
    };

    static constexpr int kDefaultWidth = 1000;
    static constexpr int kDefaultHeight = 700;
    static constexpr int kMinimumWidth = 320;
    static constexpr int kMinimumHeight = 240;

    static QString formatGeometry(const QRect& geometry);
    /// Return an invalid rectangle if the text is not a geometry.
    static QRect parseGeometry(const QString& text);

    /// Return the name of the screen that holds the center of the area.
    /// Return an empty string if no screen holds it.
    static QString screenNameFor(const QRect& geometry, const QList<Screen>& screens);

    /// Return the area for the window. The window goes back to the saved
    /// screen. If the screens do not have the saved screen, the window goes to
    /// the center of the primary screen.
    static QRect resolveGeometry(const QRect& savedGeometry,
            const QString& savedScreenName,
            const QList<Screen>& screens,
            const QString& primaryScreenName);
};
