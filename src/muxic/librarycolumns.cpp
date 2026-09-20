#include "muxic/librarycolumns.h"

#include "library/dao/trackschema.h"
#include "muxic/trackmeta.h"
#include "muxic/trackmetadao.h"

namespace {

constexpr int kDanceabilityPrecision = 2;
constexpr int kLufsPrecision = 1;

} // namespace

namespace muxic {

const QStringList& viewColumns() {
    static const QStringList kColumns = {
            kColumnEnergy,
            kColumnDanceability,
            kColumnTags,
            kColumnLufs};
    return kColumns;
}

bool isLibraryColumn(ColumnCache::Column column) {
    switch (column) {
    case ColumnCache::COLUMN_MUXIC_ENERGY:
    case ColumnCache::COLUMN_MUXIC_DANCEABILITY:
    case ColumnCache::COLUMN_MUXIC_TAGS:
    case ColumnCache::COLUMN_MUXIC_LUFS:
        return true;
    default:
        return false;
    }
}

QString viewSelectExpression(const QString& columnName) {
    if (columnName == kColumnEnergy ||
            columnName == kColumnDanceability ||
            columnName == kColumnTags) {
        return kTrackMetaTable + QLatin1Char('.') + columnName;
    }
    if (columnName == kColumnLufs) {
        // The loudness is a pure function of the stored ReplayGain ratio, thus
        // it needs no value of its own.
        return QStringLiteral(LIBRARY_TABLE) + QLatin1Char('.') +
                LIBRARYTABLE_REPLAYGAIN + QStringLiteral(" AS ") + kColumnLufs;
    }
    return QString();
}

QString viewJoinClause() {
    return QStringLiteral(" LEFT JOIN ") + kTrackMetaTable +
            QStringLiteral(" ON ") + kTrackMetaTable + QLatin1Char('.') +
            kTrackMetaTrackId + QStringLiteral(" = ") +
            QStringLiteral(LIBRARY_TABLE) + QLatin1Char('.') + LIBRARYTABLE_ID;
}

QVariant displayValue(ColumnCache::Column column, const QVariant& rawValue) {
    if (rawValue.isNull()) {
        return QVariant();
    }
    switch (column) {
    case ColumnCache::COLUMN_MUXIC_ENERGY: {
        bool ok = false;
        const int energy = rawValue.toInt(&ok);
        if (!ok || energy < kEnergyMin) {
            return QVariant();
        }
        return QString::number(energy);
    }
    case ColumnCache::COLUMN_MUXIC_DANCEABILITY: {
        bool ok = false;
        const double danceability = rawValue.toDouble(&ok);
        if (!ok) {
            return QVariant();
        }
        return QString::number(danceability, 'f', kDanceabilityPrecision);
    }
    case ColumnCache::COLUMN_MUXIC_LUFS: {
        bool ok = false;
        const double lufs = rawValue.toDouble(&ok);
        if (!ok) {
            return QVariant();
        }
        return QString::number(lufs, 'f', kLufsPrecision);
    }
    case ColumnCache::COLUMN_MUXIC_TAGS:
        return formatTags(rawValue.toString());
    default:
        return rawValue;
    }
}

QVariant editValue(ColumnCache::Column column, const QVariant& rawValue) {
    switch (column) {
    case ColumnCache::COLUMN_MUXIC_ENERGY: {
        if (rawValue.isNull()) {
            return QString();
        }
        bool ok = false;
        const int energy = rawValue.toInt(&ok);
        if (!ok || energy < kEnergyMin) {
            return QString();
        }
        return QString::number(energy);
    }
    case ColumnCache::COLUMN_MUXIC_TAGS:
        return formatTags(rawValue.toString());
    default:
        return rawValue;
    }
}

bool setEditedValue(TrackId trackId, ColumnCache::Column column, const QVariant& value) {
    TrackMetaDao* pDao = TrackMetaDao::instance();
    if (!pDao || !trackId.isValid()) {
        return false;
    }
    switch (column) {
    case ColumnCache::COLUMN_MUXIC_ENERGY: {
        const QString text = value.toString().trimmed();
        if (text.isEmpty()) {
            return pDao->setEnergy(trackId, std::nullopt);
        }
        bool ok = false;
        const int energy = text.toInt(&ok);
        if (!ok || energy < kEnergyMin || energy > kEnergyMax) {
            return false;
        }
        return pDao->setEnergy(trackId, energy);
    }
    case ColumnCache::COLUMN_MUXIC_TAGS:
        return pDao->setTags(trackId, parseTags(value.toString()));
    default:
        return false;
    }
}

} // namespace muxic
