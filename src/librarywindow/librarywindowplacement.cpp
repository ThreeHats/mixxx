#include "librarywindow/librarywindowplacement.h"

#include <QStringList>

namespace {

const LibraryWindowPlacement::Screen* findScreen(
        const QList<LibraryWindowPlacement::Screen>& screens,
        const QString& name) {
    if (name.isEmpty()) {
        return nullptr;
    }
    for (const auto& screen : screens) {
        if (screen.name == name) {
            return &screen;
        }
    }
    return nullptr;
}

QRect centerOn(const QSize& size, const QRect& area) {
    QSize fitted(qMin(size.width(), area.width()), qMin(size.height(), area.height()));
    return QRect(area.x() + (area.width() - fitted.width()) / 2,
            area.y() + (area.height() - fitted.height()) / 2,
            fitted.width(),
            fitted.height());
}

QRect moveInto(const QRect& geometry, const QRect& area) {
    QRect result(geometry);
    result.setWidth(qMin(result.width(), area.width()));
    result.setHeight(qMin(result.height(), area.height()));
    result.moveLeft(qBound(area.left(), result.left(), area.right() - result.width() + 1));
    result.moveTop(qBound(area.top(), result.top(), area.bottom() - result.height() + 1));
    return result;
}

} // namespace

QString LibraryWindowPlacement::formatGeometry(const QRect& geometry) {
    if (!geometry.isValid()) {
        return QString();
    }
    return QStringLiteral("%1,%2,%3,%4")
            .arg(geometry.x())
            .arg(geometry.y())
            .arg(geometry.width())
            .arg(geometry.height());
}

QRect LibraryWindowPlacement::parseGeometry(const QString& text) {
    const QStringList parts = text.split(QChar(','));
    if (parts.size() != 4) {
        return QRect();
    }
    int values[4];
    for (int i = 0; i < 4; ++i) {
        bool ok = false;
        values[i] = parts.at(i).trimmed().toInt(&ok);
        if (!ok) {
            return QRect();
        }
    }
    if (values[2] < kMinimumWidth || values[3] < kMinimumHeight) {
        return QRect();
    }
    return QRect(values[0], values[1], values[2], values[3]);
}

QString LibraryWindowPlacement::screenNameFor(
        const QRect& geometry, const QList<Screen>& screens) {
    if (!geometry.isValid()) {
        return QString();
    }
    const QPoint center = geometry.center();
    for (const auto& screen : screens) {
        if (screen.geometry.contains(center)) {
            return screen.name;
        }
    }
    return QString();
}

QRect LibraryWindowPlacement::resolveGeometry(const QRect& savedGeometry,
        const QString& savedScreenName,
        const QList<Screen>& screens,
        const QString& primaryScreenName) {
    const QSize size = savedGeometry.isValid()
            ? savedGeometry.size()
            : QSize(kDefaultWidth, kDefaultHeight);
    if (screens.isEmpty()) {
        return savedGeometry.isValid() ? savedGeometry : QRect(QPoint(0, 0), size);
    }

    const Screen* pScreen = findScreen(screens, savedScreenName);
    if (pScreen != nullptr && savedGeometry.isValid()) {
        return moveInto(savedGeometry, pScreen->geometry);
    }

    const Screen* pPrimary = findScreen(screens, primaryScreenName);
    if (pPrimary == nullptr) {
        pPrimary = &screens.first();
    }
    return centerOn(size, pPrimary->geometry);
}
