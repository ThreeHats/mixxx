#include "librarywindow/wlibrarywindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QGuiApplication>
#include <QIcon>
#include <QLayoutItem>
#include <QScreen>
#include <QVBoxLayout>

#include "defs_urls.h"
#include "moc_wlibrarywindow.cpp"
#include "util/assert.h"

namespace {

const ConfigKey kGeometryConfigKey(
        QStringLiteral("[LibraryWindow]"), QStringLiteral("geometry"));

constexpr int kDefaultWidth = 1000;
constexpr int kDefaultHeight = 700;

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
    // restoreGeometry() keeps the frame, the screen, the scale and the
    // maximized or full screen state, and it refuses a value that does not fit.
    const QByteArray geometry = QByteArray::fromBase64(
            m_pConfig->getValueString(kGeometryConfigKey).toUtf8());
    if (geometry.isEmpty() || !restoreGeometry(geometry)) {
        const QScreen* pScreen = QGuiApplication::primaryScreen();
        const QRect area = pScreen ? pScreen->availableGeometry()
                                   : QRect(0, 0, kDefaultWidth, kDefaultHeight);
        resize(qMin(kDefaultWidth, area.width()), qMin(kDefaultHeight, area.height()));
        move(area.center() - rect().center());
    }
    show();
}

void WLibraryWindow::savePlacement() {
    m_pConfig->set(kGeometryConfigKey, ConfigValue(QString(saveGeometry().toBase64())));
}

void WLibraryWindow::closeEvent(QCloseEvent* pEvent) {
    // Accept the close, because a window that refuses one also stops the
    // logout of the desktop session. The manager takes the library back.
    savePlacement();
    QWidget::closeEvent(pEvent);
    emit closeRequested();
}
