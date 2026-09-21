#include "preferences/dialog/dlgprefosc.h"

#include "control/controlobject.h"
#include "moc_dlgprefosc.cpp"
#include "osc/oscconfig.h"

namespace {

const ConfigKey kReloadKey(QStringLiteral("[Osc]"), QStringLiteral("reload"));

} // namespace

DlgPrefOsc::DlgPrefOsc(QWidget* pParent, UserSettingsPointer pConfig)
        : DlgPreferencePage(pParent),
          m_pConfig(pConfig) {
    setupUi(this);
    setScrollSafeGuardForAllInputWidgets(this);
    LabelPublishFile->setText(
            tr("The file %1 lists the controls that go out.")
                    .arg(mixxx::osc::Config::publishFilePath(m_pConfig)));
    slotUpdate();
}

void DlgPrefOsc::slotUpdate() {
    const mixxx::osc::Config config = mixxx::osc::Config::load(m_pConfig);
    CheckBoxEnabled->setChecked(config.enabled);
    LineEditBindAddress->setText(config.listenHost);
    SpinBoxPort->setValue(config.listenPort);
    LineEditTargets->setText(mixxx::osc::targetsToString(config.targets));
    SpinBoxSnapshot->setValue(config.snapshotIntervalSeconds);
    CheckBoxAllowAll->setChecked(config.allowAllControls);
    CheckBoxAllowAnyHost->setChecked(config.allowRequestFromAnyHost);
}

void DlgPrefOsc::slotApply() {
    mixxx::osc::Config config = mixxx::osc::Config::load(m_pConfig);
    config.enabled = CheckBoxEnabled->isChecked();
    config.listenHost = LineEditBindAddress->text().trimmed();
    config.listenPort = static_cast<quint16>(SpinBoxPort->value());
    config.targets = mixxx::osc::parseTargets(LineEditTargets->text());
    config.snapshotIntervalSeconds = SpinBoxSnapshot->value();
    config.allowAllControls = CheckBoxAllowAll->isChecked();
    config.allowRequestFromAnyHost = CheckBoxAllowAnyHost->isChecked();
    config.save(m_pConfig);
    ControlObject::set(kReloadKey, 1.0);
}

void DlgPrefOsc::slotResetToDefaults() {
    CheckBoxEnabled->setChecked(false);
    LineEditBindAddress->setText(QStringLiteral("127.0.0.1"));
    SpinBoxPort->setValue(9000);
    LineEditTargets->clear();
    SpinBoxSnapshot->setValue(10);
    CheckBoxAllowAll->setChecked(false);
    CheckBoxAllowAnyHost->setChecked(false);
}
