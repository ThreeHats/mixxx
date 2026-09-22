#include "preferences/dialog/dlgprefbeats.h"

#include "analyzer/analyzerbeats.h"
#include "defs_urls.h"
#include "moc_dlgprefbeats.cpp"

DlgPrefBeats::DlgPrefBeats(QWidget* parent, UserSettingsPointer pConfig)
        : DlgPreferencePage(parent),
          m_pConfig(pConfig),
          m_bpmSettings(pConfig),
          m_downbeatSettings(mixxx::ExternalDownbeatSettings::readFrom(pConfig)),
          m_bAnalyzerEnabled(m_bpmSettings.getBpmDetectionEnabledDefault()),
          m_bFixedTempoEnabled(m_bpmSettings.getFixedTempoAssumptionDefault()),
          m_bFastAnalysisEnabled(m_bpmSettings.getFastAnalysisDefault()),
          m_bDetectDownbeats(m_bpmSettings.getDownbeatDetectionEnabledDefault()),
          m_bReanalyze(m_bpmSettings.getReanalyzeWhenSettingsChangeDefault()),
          m_bReanalyzeImported(m_bpmSettings.getReanalyzeImportedDefault()),
          m_stemStrategy(BeatDetectionSettings::StemStrategy::Disabled) {
    setupUi(this);

    m_availablePlugins = AnalyzerBeats::availablePlugins();
    for (const auto& info : std::as_const(m_availablePlugins)) {
        comboBoxBeatPlugin->addItem(info.name(), info.id());
    }

    comboBoxDownbeatDetector->addItem(tr("Built in"));
    comboBoxDownbeatDetector->addItem(tr("External command"));

    slotUpdate();

    // TODO (#13466) Keeping the setting hidden for now
    comboBoxStemStrategy->hide();
    labelStemStrategy->hide();

    // Connections
    connect(comboBoxBeatPlugin,
            QOverload<int>::of(&QComboBox::currentIndexChanged),
            this,
            &DlgPrefBeats::pluginSelected);
    connect(checkBoxAnalyzerEnabled,
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
            &QCheckBox::checkStateChanged,
#else
            &QCheckBox::stateChanged,
#endif
            this,
            &DlgPrefBeats::analyzerEnabled);
    connect(checkBoxFixedTempo,
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
            &QCheckBox::checkStateChanged,
#else
            &QCheckBox::stateChanged,
#endif
            this,
            &DlgPrefBeats::fixedtempoEnabled);
    connect(checkBoxFastAnalysis,
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
            &QCheckBox::checkStateChanged,
#else
            &QCheckBox::stateChanged,
#endif
            this,
            &DlgPrefBeats::fastAnalysisEnabled);
    connect(checkBoxDetectDownbeats,
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
            &QCheckBox::checkStateChanged,
#else
            &QCheckBox::stateChanged,
#endif
            this,
            &DlgPrefBeats::detectDownbeatsEnabled);
    connect(checkBoxReanalyze,
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
            &QCheckBox::checkStateChanged,
#else
            &QCheckBox::stateChanged,
#endif
            this,
            &DlgPrefBeats::slotReanalyzeChanged);
    connect(checkBoxReanalyzeImported,
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
            &QCheckBox::checkStateChanged,
#else
            &QCheckBox::stateChanged,
#endif
            this,
            &DlgPrefBeats::slotReanalyzeImportedChanged);
    connect(comboBoxStemStrategy,
            &QComboBox::currentIndexChanged,
            this,
            &DlgPrefBeats::slotStemStrategyChanged);
    connect(comboBoxDownbeatDetector,
            &QComboBox::currentIndexChanged,
            this,
            &DlgPrefBeats::slotDownbeatDetectorChanged);
    connect(lineEditDownbeatCommand,
            &QLineEdit::textChanged,
            this,
            [this](const QString& text) {
                m_downbeatSettings.setCommand(text);
            });
    connect(spinBoxDownbeatTimeout,
            QOverload<int>::of(&QSpinBox::valueChanged),
            this,
            [this](int seconds) {
                m_downbeatSettings.setTimeoutSeconds(seconds);
            });

    setScrollSafeGuard(comboBoxBeatPlugin);
    setScrollSafeGuard(comboBoxDownbeatDetector);
    setScrollSafeGuard(spinBoxDownbeatTimeout);
}

DlgPrefBeats::~DlgPrefBeats() {
}

QUrl DlgPrefBeats::helpUrl() const {
    return QUrl(MIXXX_MANUAL_BEATS_URL);
}

void DlgPrefBeats::slotResetToDefaults() {
    if (m_availablePlugins.size() > 0) {
        m_selectedAnalyzerId = m_availablePlugins[0].id();
    }
    m_bAnalyzerEnabled = m_bpmSettings.getBpmDetectionEnabledDefault();
    m_bFixedTempoEnabled = m_bpmSettings.getFixedTempoAssumptionDefault();
    m_bFastAnalysisEnabled = m_bpmSettings.getFastAnalysisDefault();
    m_bDetectDownbeats = m_bpmSettings.getDownbeatDetectionEnabledDefault();
    m_bReanalyze = m_bpmSettings.getReanalyzeWhenSettingsChangeDefault();
    m_bReanalyzeImported = m_bpmSettings.getReanalyzeImportedDefault();
    m_stemStrategy = m_bpmSettings.getStemStrategyDefault();
    m_downbeatSettings = mixxx::ExternalDownbeatSettings();

    updateGui();
}

void DlgPrefBeats::pluginSelected(int i) {
    if (i == -1) {
        return;
    }
    m_selectedAnalyzerId = m_availablePlugins[i].id();
    updateGui();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
void DlgPrefBeats::analyzerEnabled(Qt::CheckState state) {
    m_bAnalyzerEnabled = (state == Qt::Checked);
#else
void DlgPrefBeats::analyzerEnabled(int i) {
    m_bAnalyzerEnabled = static_cast<bool>(i);
#endif
    updateGui();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
void DlgPrefBeats::fixedtempoEnabled(Qt::CheckState state) {
    m_bFixedTempoEnabled = (state == Qt::Checked);
#else
void DlgPrefBeats::fixedtempoEnabled(int i) {
    m_bFixedTempoEnabled = static_cast<bool>(i);
#endif
    updateGui();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
void DlgPrefBeats::detectDownbeatsEnabled(Qt::CheckState state) {
    m_bDetectDownbeats = (state == Qt::Checked);
#else
void DlgPrefBeats::detectDownbeatsEnabled(int i) {
    m_bDetectDownbeats = static_cast<bool>(i);
#endif
    updateGui();
}

void DlgPrefBeats::slotUpdate() {
    // Read true values from config
    m_selectedAnalyzerId = m_bpmSettings.getBeatPluginId();
    m_bAnalyzerEnabled = m_bpmSettings.getBpmDetectionEnabled();
    m_bFixedTempoEnabled = m_bpmSettings.getFixedTempoAssumption();
    m_bReanalyze = m_bpmSettings.getReanalyzeWhenSettingsChange();
    m_bReanalyzeImported = m_bpmSettings.getReanalyzeImported();
    m_bFastAnalysisEnabled = m_bpmSettings.getFastAnalysis();
    m_bDetectDownbeats = m_bpmSettings.getDownbeatDetectionEnabled();
    m_stemStrategy = m_bpmSettings.getStemStrategy();
    m_downbeatSettings = mixxx::ExternalDownbeatSettings::readFrom(m_pConfig);

    updateGui();
}

void DlgPrefBeats::updateGui() {
    checkBoxFixedTempo->setEnabled(m_bAnalyzerEnabled);
    comboBoxBeatPlugin->setEnabled(m_bAnalyzerEnabled);
    checkBoxAnalyzerEnabled->setChecked(m_bAnalyzerEnabled);
    // Fast analysis cannot be combined with non-constant tempo beatgrids.
    checkBoxFastAnalysis->setEnabled(m_bAnalyzerEnabled && m_bFixedTempoEnabled);
    checkBoxDetectDownbeats->setEnabled(m_bAnalyzerEnabled);
    const bool downbeatsOn = m_bAnalyzerEnabled && m_bDetectDownbeats;
    const bool commandOn = downbeatsOn &&
            m_downbeatSettings.detector() ==
                    mixxx::DownbeatDetectorChoice::ExternalCommand;
    comboBoxDownbeatDetector->setEnabled(downbeatsOn);
    labelDownbeatDetector->setEnabled(downbeatsOn);
    lineEditDownbeatCommand->setEnabled(commandOn);
    labelDownbeatCommand->setEnabled(commandOn);
    spinBoxDownbeatTimeout->setEnabled(commandOn);
    labelDownbeatTimeout->setEnabled(commandOn);
    checkBoxReanalyze->setEnabled(m_bAnalyzerEnabled);
    checkBoxReanalyzeImported->setEnabled(m_bAnalyzerEnabled);

    if (!m_bAnalyzerEnabled) {
        return;
    }

    if (m_availablePlugins.size() > 0) {
        bool found = false;
        for (int i = 0; i < m_availablePlugins.size(); ++i) {
            const auto& info = m_availablePlugins.at(i);
            if (info.id() == m_selectedAnalyzerId) {
                found = true;
                comboBoxBeatPlugin->setCurrentIndex(i);
                if (!m_availablePlugins[i].isConstantTempoSupported()) {
                    checkBoxFixedTempo->setEnabled(false);
                }
                break;
            }
        }
        if (!found) {
            comboBoxBeatPlugin->setCurrentIndex(0);
            m_selectedAnalyzerId = m_availablePlugins[0].id();
        }
    }

    checkBoxFixedTempo->setChecked(m_bFixedTempoEnabled);
    // Fast analysis cannot be combined with non-constant tempo beatgrids.
    checkBoxFastAnalysis->setChecked(m_bFastAnalysisEnabled && m_bFixedTempoEnabled);

    checkBoxDetectDownbeats->setChecked(m_bDetectDownbeats);
    comboBoxDownbeatDetector->setCurrentIndex(
            m_downbeatSettings.detector() ==
                            mixxx::DownbeatDetectorChoice::ExternalCommand
                    ? 1
                    : 0);
    lineEditDownbeatCommand->setText(m_downbeatSettings.command());
    spinBoxDownbeatTimeout->setValue(m_downbeatSettings.timeoutSeconds());
    checkBoxReanalyze->setChecked(m_bReanalyze);
    checkBoxReanalyzeImported->setChecked(m_bReanalyzeImported);

    comboBoxStemStrategy->setCurrentIndex(
            m_stemStrategy == BeatDetectionSettings::StemStrategy::Enforced
                    ? 1
                    : 0);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
void DlgPrefBeats::slotReanalyzeChanged(Qt::CheckState state) {
    m_bReanalyze = (state == Qt::Checked);
#else
void DlgPrefBeats::slotReanalyzeChanged(int value) {
    m_bReanalyze = static_cast<bool>(value);
#endif
    updateGui();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
void DlgPrefBeats::slotReanalyzeImportedChanged(Qt::CheckState state) {
    m_bReanalyzeImported = (state == Qt::Checked);
#else
void DlgPrefBeats::slotReanalyzeImportedChanged(int value) {
    m_bReanalyzeImported = static_cast<bool>(value);
#endif
    updateGui();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
void DlgPrefBeats::fastAnalysisEnabled(Qt::CheckState state) {
    m_bFastAnalysisEnabled = (state == Qt::Checked);
#else
void DlgPrefBeats::fastAnalysisEnabled(int i) {
    m_bFastAnalysisEnabled = static_cast<bool>(i);
#endif
    updateGui();
}

void DlgPrefBeats::slotStemStrategyChanged(int index) {
    switch (index) {
    case 1:
        m_stemStrategy = BeatDetectionSettings::StemStrategy::Enforced;
        break;
    default:
        m_stemStrategy = BeatDetectionSettings::StemStrategy::Disabled;
        break;
    }
    updateGui();
}

void DlgPrefBeats::slotDownbeatDetectorChanged(int index) {
    m_downbeatSettings.setDetector(index == 1
                    ? mixxx::DownbeatDetectorChoice::ExternalCommand
                    : mixxx::DownbeatDetectorChoice::BuiltIn);
    updateGui();
}

void DlgPrefBeats::slotApply() {
    m_bpmSettings.setBeatPluginId(m_selectedAnalyzerId);
    m_bpmSettings.setBpmDetectionEnabled(m_bAnalyzerEnabled);
    m_bpmSettings.setFixedTempoAssumption(m_bFixedTempoEnabled);
    m_bpmSettings.setReanalyzeWhenSettingsChange(m_bReanalyze);
    m_bpmSettings.setReanalyzeImported(m_bReanalyzeImported);
    m_bpmSettings.setFastAnalysis(m_bFastAnalysisEnabled);
    m_bpmSettings.setDownbeatDetectionEnabled(m_bDetectDownbeats);
    m_bpmSettings.setStemStrategy(m_stemStrategy);
    m_downbeatSettings.writeTo(m_pConfig);
}
