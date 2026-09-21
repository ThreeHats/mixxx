#pragma once

#include <QString>

// The fork owns this table. Its DAO makes it, thus res/schema.xml keeps
// the upstream revision. The muxic hub reads and writes the same columns.
#define TRACK_RELATIONS_TABLE "muxic_track_relations"

const QString TRACKRELATIONSTABLE_ID = QStringLiteral("id");
const QString TRACKRELATIONSTABLE_SOURCE_TRACK_ID = QStringLiteral("source_track_id");
const QString TRACKRELATIONSTABLE_TARGET_TRACK_ID = QStringLiteral("target_track_id");
const QString TRACKRELATIONSTABLE_BIDIRECTIONAL = QStringLiteral("bidirectional");
const QString TRACKRELATIONSTABLE_RELATION_TYPE = QStringLiteral("relation_type");
const QString TRACKRELATIONSTABLE_RATING = QStringLiteral("rating");
const QString TRACKRELATIONSTABLE_NOTES = QStringLiteral("notes");
const QString TRACKRELATIONSTABLE_DATETIME_ADDED = QStringLiteral("datetime_added");
