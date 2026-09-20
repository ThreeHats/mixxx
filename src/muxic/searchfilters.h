#pragma once

#include <QSqlDatabase>
#include <QString>
#include <QStringList>
#include <optional>

#include "library/searchquery.h"

namespace muxic {

/// A filter on a numeric column of muxic_track_meta. It takes the arguments of
/// a numeric filter of upstream, for example ">=7" or "5-8".
class MetaNumericFilterNode : public NumericFilterNode {
  public:
    enum class Field {
        Energy,
        Danceability,
    };

    MetaNumericFilterNode(const QStringList& sqlColumns,
            const QString& argument,
            Field field);

    bool match(const TrackPointer& pTrack) const override;

  private:
    bool matchValue(std::optional<double> value) const;

    const Field m_field;
};

/// Matches one whole tag of the muxic_tags column. The column keeps a comma
/// before the first tag and after the last one, thus a LIKE on ",tag," never
/// matches a part of a longer tag.
class TagFilterNode : public QueryNode {
  public:
    TagFilterNode(const QSqlDatabase& database, const QString& tag);

    bool match(const TrackPointer& pTrack) const override;
    QString toSql() const override;

  private:
    const QSqlDatabase m_database;
    const QString m_tag;
};

/// Matches a track that has no tag.
class NoTagFilterNode : public QueryNode {
  public:
    bool match(const TrackPointer& pTrack) const override;
    QString toSql() const override;
};

} // namespace muxic
