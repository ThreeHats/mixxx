#include "muxic/trackmetafields.h"

#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QWidget>

#include "muxic/trackmeta.h"
#include "muxic/trackmetadao.h"

namespace {

constexpr int kEnergyUnset = 0;
constexpr int kDanceabilityPrecision = 2;
constexpr int kLoudnessPrecision = 1;

} // namespace

namespace muxic {

TrackMetaFields::TrackMetaFields(QWidget* pGroupBox,
        QSpinBox* pEnergy,
        QLineEdit* pTags,
        QLabel* pDanceability,
        QLabel* pLoudness)
        : m_pGroupBox(pGroupBox),
          m_pEnergy(pEnergy),
          m_pTags(pTags),
          m_pDanceability(pDanceability),
          m_pLoudness(pLoudness),
          m_loadedEnergy(kEnergyUnset) {
}

void TrackMetaFields::load(TrackId trackId) {
    TrackMetaDao* pDao = TrackMetaDao::instance();
    m_pGroupBox->setEnabled(pDao != nullptr && trackId.isValid());
    if (!pDao || !trackId.isValid()) {
        clear();
        return;
    }
    m_trackId = trackId;
    const TrackMeta meta = pDao->read(trackId);
    m_loadedEnergy = meta.energy.value_or(kEnergyUnset);
    m_loadedTags = meta.tags.join(QStringLiteral(", "));
    m_pEnergy->setValue(m_loadedEnergy);
    m_pTags->setText(m_loadedTags);
    m_pDanceability->setText(meta.danceability
                    ? QString::number(*meta.danceability, 'f', kDanceabilityPrecision)
                    : QString());
}

void TrackMetaFields::showLoudness(double replayGainRatio) {
    const QVariant lufs = lufsFromReplayGainRatio(QVariant(replayGainRatio));
    m_pLoudness->setText(lufs.isValid()
                    ? QString::number(lufs.toDouble(), 'f', kLoudnessPrecision) +
                            QStringLiteral(" LUFS")
                    : QString());
}

void TrackMetaFields::clear() {
    m_trackId = TrackId();
    m_loadedEnergy = kEnergyUnset;
    m_loadedTags.clear();
    m_pEnergy->setValue(kEnergyUnset);
    m_pTags->clear();
    m_pDanceability->clear();
    m_pLoudness->clear();
}

void TrackMetaFields::save() {
    TrackMetaDao* pDao = TrackMetaDao::instance();
    if (!pDao || !m_trackId.isValid()) {
        return;
    }
    const int energy = m_pEnergy->value();
    if (energy != m_loadedEnergy) {
        pDao->setEnergy(m_trackId,
                energy >= kEnergyMin ? std::optional<int>(energy) : std::nullopt);
        m_loadedEnergy = energy;
    }
    const QString tags = m_pTags->text();
    if (parseTags(tags) != parseTags(m_loadedTags)) {
        pDao->setTags(m_trackId, parseTags(tags));
        m_loadedTags = tags;
    }
}

} // namespace muxic
