#include "muxic/searchfilters.h"

#include "library/queryutil.h"
#include "muxic/trackmeta.h"
#include "muxic/trackmetadao.h"
#include "track/track.h"

namespace {

/// The stored values of a track, or an empty set when no database is open.
std::optional<muxic::TrackMeta> readMeta(const TrackPointer& pTrack) {
    muxic::TrackMetaDao* pDao = muxic::TrackMetaDao::instance();
    if (!pDao || !pTrack || !pTrack->getId().isValid()) {
        return std::nullopt;
    }
    return pDao->read(pTrack->getId());
}

} // namespace

namespace muxic {

MetaNumericFilterNode::MetaNumericFilterNode(const QStringList& sqlColumns,
        const QString& argument,
        Field field)
        : NumericFilterNode(sqlColumns),
          m_field(field) {
    init(argument);
}

bool MetaNumericFilterNode::matchValue(std::optional<double> value) const {
    if (!value) {
        return m_bNullQuery;
    }
    if (m_bNullQuery) {
        return false;
    }
    const double dValue = *value;
    if (m_bOperatorQuery) {
        return (m_operator == QLatin1String("=") && dValue == m_dOperatorArgument) ||
                (m_operator == QLatin1String("<") && dValue < m_dOperatorArgument) ||
                (m_operator == QLatin1String(">") && dValue > m_dOperatorArgument) ||
                (m_operator == QLatin1String("<=") && dValue <= m_dOperatorArgument) ||
                (m_operator == QLatin1String(">=") && dValue >= m_dOperatorArgument);
    }
    if (m_bRangeQuery) {
        return dValue >= m_dRangeLow && dValue <= m_dRangeHigh;
    }
    return false;
}

bool MetaNumericFilterNode::match(const TrackPointer& pTrack) const {
    const auto meta = readMeta(pTrack);
    if (!meta) {
        return true;
    }
    if (m_field == Field::Energy) {
        return matchValue(meta->energy
                        ? std::optional<double>(static_cast<double>(*meta->energy))
                        : std::nullopt);
    }
    return matchValue(meta->danceability);
}

TagFilterNode::TagFilterNode(const QSqlDatabase& database, const QString& tag)
        : m_database(database),
          m_tag(tag) {
}

bool TagFilterNode::match(const TrackPointer& pTrack) const {
    const auto meta = readMeta(pTrack);
    if (!meta) {
        return true;
    }
    return meta->tags.contains(m_tag);
}

QString TagFilterNode::toSql() const {
    if (m_tag.isEmpty()) {
        return QString();
    }
    const FieldEscaper escaper(m_database);
    // The IS NOT NULL keeps a negated filter (-tag:x) able to find the tracks
    // that have no tag at all.
    return QStringLiteral("%1 IS NOT NULL AND %1 LIKE %2 ESCAPE '%3'")
            .arg(kColumnTags,
                    escaper.escapeString(tagLikePattern(m_tag)),
                    QString(kTagLikeEscape));
}

bool NoTagFilterNode::match(const TrackPointer& pTrack) const {
    const auto meta = readMeta(pTrack);
    if (!meta) {
        return true;
    }
    return meta->tags.isEmpty();
}

QString NoTagFilterNode::toSql() const {
    return QStringLiteral("%1 IS NULL OR %1 IS ''").arg(kColumnTags);
}

} // namespace muxic
