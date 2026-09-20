#include "librarywindow/wlibrarywindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QIcon>
#include <QLayoutItem>
#include <QScreen>
#include <QVBoxLayout>

#include "defs_urls.h"
#include "librarywindow/librarywindowplacement.h"
#include "moc_wlibrarywindow.cpp"
#include "util/assert.h"

namespace {

const QString kGroup = QStringLiteral("[LibraryWindow]");
const ConfigKey kGeometryConfigKey(kGroup, QStringLiteral("geometry"));
const ConfigKey kScreenConfigKey(kGroup, QStringLiteral("screen"));
const ConfigKey kStateConfigKey(kGroup, QStringLiteral("state"));

const QString kStateNormal = QStringLiteral("normal");
const QString kStateMaximized = QStringLiteral("maximized");
const QString kStateFullScreen = QStringLiteral("fullscreen");

QList<LibraryWindowPlacement::Screen> desktopScreens() {
    QList<LibraryWindowPlacement::Screen> screens;
    const auto qScreens = QGuiApplication::screens();
    screens.reserve(qScreens.size());
    for (const QScreen* pScreen : qScreens) {
        screens.append({pScreen->name(), pScreen->geometry()});
    }
    return screens;
}

QString primaryScreenName() {
    const QScreen* pScreen = QGuiApplication::primaryScreen();
    return pScreen ? pScreen->name() : QString();
}

} // namespace

WLibraryWindow::WLibraryWindow(UserSettingsPointer pConfig)
        : QWidget(nullptr, Qt::Window),
          m_pConfig(pConfig),
          m_pLayout(new QVBoxLayout(this)) {
    setObjectName(QStringLiteral("LibraryWindow"));
    setWindowTitle(tr("Mixxx Library"));
    setWindowIcon(QIcon(MIXXX_ICON_PATH));
    // The main window alone decides when Mixxx stops.
    setAttribute(Qt::WA_QuitOnClose, false);
    setMinimumSize(LibraryWindowPlacement::kMinimumWidth,
            LibraryWindowPlacement::kMinimumHeight);
    m_pLayout->setContentsMargins(0, 0, 0, 0);
    m_pLayout->setSpacing(0);

    // A window manager with no title bar gives no other way to close.
    auto* pCloseAction = new QAction(this);
    pCloseAction->setShortcut(QKeySequence::Close);
    connect(pCloseAction, &QAction::triggered, this, &WLibraryWindow::close);
    addAction(pCloseAction);
}

WLibraryWindow::~WLibraryWindow() {
    // The manager puts the library back in the skin before it deletes the
    // window. An empty layout here proves that this happened.
    VERIFY_OR_DEBUG_ASSERT(m_pLayout->count() == 0) {
        while (QLayoutItem* pItem = m_pLayout->takeAt(0)) {
            if (pItem->widget()) {
                pItem->widget()->setParent(nullptr);
            }
            delete pItem;
        }
    }
}

void WLibraryWindow::adoptLibraryWidget(QWidget* pLibraryWidget) {
    VERIFY_OR_DEBUG_ASSERT(pLibraryWidget) {
        return;
    }
    QWidget* pOldParent = pLibraryWidget->parentWidget();
    if (pOldParent && pOldParent->layout()) {
        pOldParent->layout()->removeWidget(pLibraryWidget);
    }
    m_pLayout->addWidget(pLibraryWidget);
    pLibraryWidget->show();
}

void WLibraryWindow::applyStyle(const QString& baseStyleSheet,
        const QString& skinStyleSheet,
        const QPalette& palette) {
    setPalette(palette);
    setAutoFillBackground(true);
    setStyleSheet(baseStyleSheet + QChar('\n') + skinStyleSheet);
}

void WLibraryWindow::restorePlacement() {
    const QRect savedGeometry = LibraryWindowPlacement::parseGeometry(
            m_pConfig->getValueString(kGeometryConfigKey));
    const QString savedScreen = m_pConfig->getValueString(kScreenConfigKey);
    setGeometry(LibraryWindowPlacement::resolveGeometry(
            savedGeometry, savedScreen, desktopScreens(), primaryScreenName()));

    const QString state = m_pConfig->getValueString(kStateConfigKey);
    if (state == kStateFullScreen) {
        showFullScreen();
    } else if (state == kStateMaximized) {
        showMaximized();
    } else {
        showNormal();
    }
}

void WLibraryWindow::savePlacement() {
    QString state = kStateNormal;
    if (isFullScreen()) {
        state = kStateFullScreen;
    } else if (isMaximized()) {
        state = kStateMaximized;
    }
    QRect rect = (state == kStateNormal) ? geometry() : normalGeometry();
    if (!rect.isValid()) {
        rect = geometry();
    }
    m_pConfig->set(kGeometryConfigKey,
            ConfigValue(LibraryWindowPlacement::formatGeometry(rect)));
    m_pConfig->set(kScreenConfigKey,
            ConfigValue(LibraryWindowPlacement::screenNameFor(geometry(), desktopScreens())));
    m_pConfig->set(kStateConfigKey, ConfigValue(state));
}

void WLibraryWindow::closeEvent(QCloseEvent* pEvent) {
    // The manager takes the library back and deletes the window. Refuse the
    // close, because the library must not go away with the window.
    pEvent->ignore();
    emit closeRequested();
}
