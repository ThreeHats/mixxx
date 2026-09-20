#pragma once

#include <QString>
#include <QStringList>
#include <QVariant>

#include "library/columncache.h"
#include "track/trackid.h"

namespace muxic {

/// The columns that the fork adds to the library cache view, in the order in
/// which the view selects them.
const QStringList& viewColumns();

/// True for a column that this file owns.
bool isLibraryColumn(ColumnCache::Column column);

/// The SELECT expression of a muxic column, or an empty string for a column
/// of upstream.
QString viewSelectExpression(const QString& columnName);

/// The JOIN that gives the library cache view the muxic columns.
QString viewJoinClause();

/// The text of a cell, for Qt::DisplayRole.
QVariant displayValue(ColumnCache::Column column, const QVariant& rawValue);

/// The value that an editor starts with, for Qt::EditRole.
QVariant editValue(ColumnCache::Column column, const QVariant& rawValue);

/// Writes an edited cell through the data access object. Returns false when
/// the column is read only or no database is open.
bool setEditedValue(TrackId trackId, ColumnCache::Column column, const QVariant& value);

} // namespace muxic
