#pragma once

#include "preferences/dialog/dlgpreferencepage.h"
#include "preferences/dialog/ui_dlgprefoscdlg.h"
#include "preferences/usersettings.h"

class QWidget;

/// The preferences page of the OSC state server. Apply writes the settings
/// and then triggers `[Osc] reload`, thus the server takes a change without a
/// restart of Mixxx.
class DlgPrefOsc : public DlgPreferencePage, public Ui::DlgPrefOscDlg {
    Q_OBJECT

  public:
    DlgPrefOsc(QWidget* pParent, UserSettingsPointer pConfig);

  public slots:
    void slotUpdate() override;
    void slotApply() override;
    void slotResetToDefaults() override;

  private:
    UserSettingsPointer m_pConfig;
};
