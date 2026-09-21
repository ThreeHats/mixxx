#pragma once

class QWidget;

namespace mixxx {
namespace librarywindow {

/// Return true if the window that holds a library widget must become active
/// before that widget can take the keyboard focus. The answer is false while
/// another program is in front, thus a controller cannot pull Mixxx forward.
bool needsWindowActivation(const QWidget* pTargetWindow,
        const QWidget* pActiveWindow,
        const QWidget* pActiveModalWidget);

} // namespace librarywindow
} // namespace mixxx
