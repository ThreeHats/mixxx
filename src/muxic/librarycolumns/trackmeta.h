#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>
#include <optional>

/// Fork data of the muxic rig, kept out of the columns that upstream owns.
/// See tools/muxic/docs/library-columns.md.
namespace muxic {

const QString kTrackMetaTable = QStringLiteral("muxic_track_meta");
const QString kTrackMetaTrackId = QStringLiteral("track_id");

const QString kColumnEnergy = QStringLiteral("muxic_energy");
const QString kColumnDanceability = QStringLiteral("muxic_danceability");
const QString kColumnTags = QStringLiteral("muxic_tags");
/// A column of the view that holds the ReplayGain ratio of the track. The
/// table shows it as the integrated loudness.
const QString kColumnLufs = QStringLiteral("muxic_lufs");

constexpr int kEnergyMin = 1;
constexpr int kEnergyMax = 10;

constexpr double kDanceabilityMin = 0.0;
constexpr double kDanceabilityMax = 1.0;

/// The reference level of ReplayGain 2.0. AnalyzerEbur128 subtracts the
/// measured loudness from it to get the gain that the library stores.
constexpr double kReplayGainReferenceLufs = -18.0;

/// The values of one track. An empty member means that the table has no value.
struct TrackMeta {
    std::optional<int> energy;
    std::optional<double> danceability;
    QStringList tags;
};

/// Splits text into tags: lower case, trimmed, no empty item, no repeat.
/// The text separates the tags with commas.
QStringList parseTags(const QString& text);

/// The stored form of the tags, for example ",bass,vocal,".
/// An empty list gives an empty string.
QString encodeTags(const QStringList& tags);

/// The tags of a stored string.
QStringList decodeTags(const QString& stored);

/// The form that the user reads and edits, for example "bass, vocal".
QString formatTags(const QString& stored);

/// The LIKE pattern that matches one tag in the stored form, with the
/// wildcards of the tag itself escaped by kTagLikeEscape.
QString tagLikePattern(const QString& tag);

const QChar kTagLikeEscape = QLatin1Char('\\');

/// The integrated loudness in LUFS of a ReplayGain ratio.
double lufsFromReplayGainRatio(double ratio);

/// The same, from and to a QVariant. An invalid or absent ratio gives an
/// invalid value, which the table shows as an empty cell.
QVariant lufsFromReplayGainRatio(const QVariant& ratio);

} // namespace muxic
