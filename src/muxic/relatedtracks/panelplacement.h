#pragma once

#include <QObject>
#include <QPointer>

#include "preferences/configobject.h"
#include "preferences/usersettings.h"
#include "util/parented_ptr.h"

class ControlProxy;
class QSplitter;
class QWidget;

namespace muxic {

/// Holds the place of a panel in a splitter of the skin.
///
/// A control shows and hides the panel. The height that the user gives it
/// stays in the settings, and the panel takes at most half of the splitter.
class PanelPlacement : public QObject {
    Q_OBJECT

  public:
    PanelPlacement(QWidget* pPanel,
            QSplitter* pSplitter,
            UserSettingsPointer pConfig,
            const ConfigKey& showConfigKey,
            const ConfigKey& heightConfigKey,
            int defaultHeight);
    ~PanelPlacement() override;

    /// The height that the panel asks for, before the cap.
    int wantedHeight() const;

  signals:
    /// The panel came on screen or left it.
    void shownChanged(bool shown);

  protected:
    bool eventFilter(QObject* pObject, QEvent* pEvent) override;

  private slots:
    void slotShowControlChanged(double value);
    void slotSplitterMoved();

  private:
    /// Gives the panel the height of the settings, at most half of the
    /// splitter. Waits for a splitter that has a size.
    void restoreHeight();
    void saveHeight();

    const QPointer<QWidget> m_pPanel;
    const QPointer<QSplitter> m_pSplitter;
    const UserSettingsPointer m_pConfig;
    const ConfigKey m_heightConfigKey;
    const int m_defaultHeight;
    parented_ptr<ControlProxy> m_pShowControl;
    /// True while the panel waits for a splitter that has a size.
    bool m_heightPending;
};

} // namespace muxic
