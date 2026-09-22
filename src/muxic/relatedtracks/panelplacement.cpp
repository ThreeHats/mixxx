#include "muxic/relatedtracks/panelplacement.h"

#include <QEvent>
#include <QSplitter>
#include <QWidget>
#include <algorithm>

#include "control/controlproxy.h"
#include "moc_panelplacement.cpp"
#include "util/assert.h"

namespace muxic {

PanelPlacement::PanelPlacement(QWidget* pPanel,
        QSplitter* pSplitter,
        UserSettingsPointer pConfig,
        const ConfigKey& showConfigKey,
        const ConfigKey& heightConfigKey,
        int defaultHeight)
        : QObject(pPanel),
          m_pPanel(pPanel),
          m_pSplitter(pSplitter),
          m_pConfig(pConfig),
          m_heightConfigKey(heightConfigKey),
          m_defaultHeight(defaultHeight),
          m_heightPending(false) {
    VERIFY_OR_DEBUG_ASSERT(pPanel != nullptr && pSplitter != nullptr) {
        return;
    }
    pPanel->installEventFilter(this);
    connect(pSplitter,
            &QSplitter::splitterMoved,
            this,
            &PanelPlacement::slotSplitterMoved);

    m_pShowControl = make_parented<ControlProxy>(showConfigKey, this);
    m_pShowControl->connectValueChanged(
            this, &PanelPlacement::slotShowControlChanged);
    pPanel->setVisible(m_pShowControl->toBool());
}

PanelPlacement::~PanelPlacement() = default;

int PanelPlacement::wantedHeight() const {
    return m_pConfig->getValue(m_heightConfigKey, m_defaultHeight);
}

void PanelPlacement::slotShowControlChanged(double value) {
    if (m_pPanel.isNull()) {
        return;
    }
    m_pPanel->setVisible(value > 0.0);
}

bool PanelPlacement::eventFilter(QObject* pObject, QEvent* pEvent) {
    if (pObject == m_pPanel) {
        switch (pEvent->type()) {
        case QEvent::Show:
            restoreHeight();
            emit shownChanged(true);
            break;
        case QEvent::Hide:
            emit shownChanged(false);
            break;
        case QEvent::Resize:
            if (m_heightPending) {
                // The skin gave the splitter no size at the first show.
                restoreHeight();
            }
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(pObject, pEvent);
}

void PanelPlacement::restoreHeight() {
    if (m_pSplitter.isNull()) {
        return;
    }
    const QList<int> sizes = m_pSplitter->sizes();
    if (sizes.size() != 2) {
        return;
    }
    const int total = sizes.at(0) + sizes.at(1);
    if (total <= 0) {
        m_heightPending = true;
        return;
    }
    m_heightPending = false;
    // The panel takes at most half of the library area.
    const int height = std::min(wantedHeight(), total / 2);
    if (height <= 0 || height == sizes.at(1)) {
        return;
    }
    m_pSplitter->setSizes({total - height, height});
}

void PanelPlacement::slotSplitterMoved() {
    saveHeight();
}

void PanelPlacement::saveHeight() {
    if (m_pSplitter.isNull() || m_pPanel.isNull() || !m_pPanel->isVisible()) {
        return;
    }
    const QList<int> sizes = m_pSplitter->sizes();
    if (sizes.size() != 2 || sizes.at(1) <= 0) {
        return;
    }
    m_pConfig->setValue(m_heightConfigKey, sizes.at(1));
}

} // namespace muxic
