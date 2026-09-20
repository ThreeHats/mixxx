#pragma once

#include "preferences/dialog/dlgpreferencepage.h"
#include "preferences/dialog/ui_dlgprefstemsdlg.h"
#include "preferences/usersettings.h"

/// The preferences of the stem conversion: the separation command, the model,
/// the encode command, the muxer and the output place.
class DlgPrefStems : public DlgPreferencePage, public Ui::DlgPrefStemsDlg {
    Q_OBJECT

  public:
    DlgPrefStems(QWidget* pParent, UserSettingsPointer pConfig);
    ~DlgPrefStems() override = default;

  public slots:
    void slotUpdate() override;
    void slotApply() override;
    void slotResetToDefaults() override;

  private slots:
    void slotBrowseMuxerPath();
    void slotBrowseOutputDirectory();
    void slotOutputModeChanged();

  private:
    const UserSettingsPointer m_pConfig;
};
