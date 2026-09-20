#include "preferences/dialog/dlgprefstems.h"

#include <QFileDialog>

#include "moc_dlgprefstems.cpp"
#include "stems/stemconversionsettings.h"

DlgPrefStems::DlgPrefStems(QWidget* pParent, UserSettingsPointer pConfig)
        : DlgPreferencePage(pParent),
          m_pConfig(std::move(pConfig)) {
    setupUi(this);

    connect(muxerPathBrowseButton,
            &QPushButton::clicked,
            this,
            &DlgPrefStems::slotBrowseMuxerPath);
    connect(outputDirectoryBrowseButton,
            &QPushButton::clicked,
            this,
            &DlgPrefStems::slotBrowseOutputDirectory);
    connect(customDirectoryRadio,
            &QRadioButton::toggled,
            this,
            &DlgPrefStems::slotOutputModeChanged);

    setScrollSafeGuardForAllInputWidgets(this);
}

void DlgPrefStems::slotUpdate() {
    const auto settings = mixxx::StemConversionSettings::readFrom(m_pConfig);
    separatorCommandEdit->setText(settings.separatorCommand());
    modelEdit->setText(settings.model());
    encoderCommandEdit->setText(settings.encoderCommand());
    muxerPathEdit->setText(settings.muxerPath());
    outputDirectoryEdit->setText(settings.outputDirectory());
    const bool custom =
            settings.outputMode() == mixxx::StemOutputMode::CustomDirectory;
    customDirectoryRadio->setChecked(custom);
    sourceDirectoryRadio->setChecked(!custom);
    slotOutputModeChanged();
}

void DlgPrefStems::slotApply() {
    mixxx::StemConversionSettings settings;
    settings.setSeparatorCommand(separatorCommandEdit->text());
    settings.setModel(modelEdit->text());
    settings.setEncoderCommand(encoderCommandEdit->text());
    settings.setMuxerPath(muxerPathEdit->text());
    settings.setOutputMode(customDirectoryRadio->isChecked()
                    ? mixxx::StemOutputMode::CustomDirectory
                    : mixxx::StemOutputMode::SourceDirectory);
    settings.setOutputDirectory(outputDirectoryEdit->text());
    settings.writeTo(m_pConfig);
}

void DlgPrefStems::slotResetToDefaults() {
    separatorCommandEdit->setText(
            mixxx::StemConversionSettings::defaultSeparatorCommand());
    modelEdit->setText(mixxx::StemConversionSettings::defaultModel());
    encoderCommandEdit->setText(
            mixxx::StemConversionSettings::defaultEncoderCommand());
    muxerPathEdit->setText(mixxx::StemConversionSettings::defaultMuxerPath());
    outputDirectoryEdit->clear();
    sourceDirectoryRadio->setChecked(true);
    slotOutputModeChanged();
}

void DlgPrefStems::slotBrowseMuxerPath() {
    const QString path = QFileDialog::getOpenFileName(
            this, tr("Select the MP4Box program"), muxerPathEdit->text());
    if (!path.isEmpty()) {
        muxerPathEdit->setText(path);
    }
}

void DlgPrefStems::slotBrowseOutputDirectory() {
    const QString path = QFileDialog::getExistingDirectory(
            this, tr("Select the stem directory"), outputDirectoryEdit->text());
    if (!path.isEmpty()) {
        outputDirectoryEdit->setText(path);
        customDirectoryRadio->setChecked(true);
    }
}

void DlgPrefStems::slotOutputModeChanged() {
    const bool custom = customDirectoryRadio->isChecked();
    outputDirectoryEdit->setEnabled(custom);
    outputDirectoryBrowseButton->setEnabled(custom);
}
