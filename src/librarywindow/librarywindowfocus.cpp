#include "librarywindow/librarywindowfocus.h"

namespace mixxx {
namespace librarywindow {

bool needsWindowActivation(const QWidget* pTargetWindow,
        const QWidget* pActiveWindow,
        const QWidget* pActiveModalWidget) {
    if (pTargetWindow == nullptr) {
        return false;
    }
    if (pActiveWindow == nullptr) {
        // Another application is in front. Mixxx must stay behind it.
        return false;
    }
    if (pActiveModalWidget != nullptr) {
        // A modal dialog owns the keyboard until the user answers it.
        return false;
    }
    return pTargetWindow != pActiveWindow;
}

} // namespace librarywindow
} // namespace mixxx
