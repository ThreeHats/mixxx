#pragma once

#include <QString>

#include "track/trackid.h"
#include "util/class.h"

class QLabel;
class QLineEdit;
class QSpinBox;
class QWidget;

namespace muxic {

/// The muxic group of the track properties dialog.
///
/// It keeps what it read, thus a save writes only a field that the user
/// changed. The muxic hub writes the same table while the dialog is open, and
/// an unchanged field must not put a stale value back.
class TrackMetaFields final {
  public:
    TrackMetaFields(QWidget* pGroupBox,
            QSpinBox* pEnergy,
            QLineEdit* pTags,
            QLabel* pDanceability,
            QLabel* pLoudness);

    /// Reads the values of the track and shows them.
    void load(TrackId trackId);
    /// Shows the loudness of a ReplayGain ratio.
    void showLoudness(double replayGainRatio);
    void clear();
    /// Writes the fields that the user changed.
    void save();

  private:
    QWidget* const m_pGroupBox;
    QSpinBox* const m_pEnergy;
    QLineEdit* const m_pTags;
    QLabel* const m_pDanceability;
    QLabel* const m_pLoudness;

    TrackId m_trackId;
    int m_loadedEnergy;
    QString m_loadedTags;

    DISALLOW_COPY_AND_ASSIGN(TrackMetaFields);
};

} // namespace muxic
