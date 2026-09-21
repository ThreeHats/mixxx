#include "muxic/relatedtracks/trackrelationstorage.h"

#include <algorithm>

#include "library/dao/trackschema.h"
#include "moc_trackrelationstorage.cpp"
#include "muxic/relatedtracks/trackrelationschema.h"
#include "util/db/fwdsqlquery.h"
#include "util/logger.h"

using namespace muxic;

namespace {

const mixxx::Logger kLogger("TrackRelationStorage");

const QString kCreateTableQuery =
        QStringLiteral(
                "CREATE TABLE IF NOT EXISTS %1 ("
                "%2 INTEGER PRIMARY KEY AUTOINCREMENT,"
                "%3 INTEGER NOT NULL REFERENCES %10(%11),"
                "%4 INTEGER NOT NULL REFERENCES %10(%11),"
                "%5 INTEGER NOT NULL DEFAULT 0,"
                "%6 TEXT NOT NULL DEFAULT '',"
                "%7 INTEGER NOT NULL DEFAULT 0,"
                "%8 TEXT NOT NULL DEFAULT '',"
                "%9 TEXT DEFAULT CURRENT_TIMESTAMP,"
                "UNIQUE (%3,%4))")
                .arg(QStringLiteral(TRACK_RELATIONS_TABLE),
                        TRACKRELATIONSTABLE_ID,
                        TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                        TRACKRELATIONSTABLE_TARGET_TRACK_ID,
                        TRACKRELATIONSTABLE_BIDIRECTIONAL,
                        TRACKRELATIONSTABLE_RELATION_TYPE,
                        TRACKRELATIONSTABLE_RATING,
                        TRACKRELATIONSTABLE_NOTES,
                        TRACKRELATIONSTABLE_DATETIME_ADDED,
                        QStringLiteral(LIBRARY_TABLE),
                        LIBRARYTABLE_ID);

const QString kCreateTargetIndexQuery =
        QStringLiteral("CREATE INDEX IF NOT EXISTS %1_target ON %1 (%2)")
                .arg(QStringLiteral(TRACK_RELATIONS_TABLE),
                        TRACKRELATIONSTABLE_TARGET_TRACK_ID);

const QString kSelectColumns =
        QStringLiteral("%1,%2,%3,%4,%5,%6,%7")
                .arg(TRACKRELATIONSTABLE_ID,
                        TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                        TRACKRELATIONSTABLE_TARGET_TRACK_ID,
                        TRACKRELATIONSTABLE_BIDIRECTIONAL,
                        TRACKRELATIONSTABLE_RELATION_TYPE,
                        TRACKRELATIONSTABLE_RATING,
                        TRACKRELATIONSTABLE_NOTES);

void readRelationFromQuery(const FwdSqlQuery& query, TrackRelation* pRelation) {
    pRelation->setId(TrackRelationId(
            query.fieldValue(query.fieldIndex(TRACKRELATIONSTABLE_ID))));
    pRelation->setSourceTrackId(TrackId(query.fieldValue(
            query.fieldIndex(TRACKRELATIONSTABLE_SOURCE_TRACK_ID))));
    pRelation->setTargetTrackId(TrackId(query.fieldValue(
            query.fieldIndex(TRACKRELATIONSTABLE_TARGET_TRACK_ID))));
    pRelation->setBidirectional(query.fieldValueBoolean(
            query.fieldIndex(TRACKRELATIONSTABLE_BIDIRECTIONAL)));
    pRelation->setType(query.fieldValue(
                                    query.fieldIndex(TRACKRELATIONSTABLE_RELATION_TYPE))
                               .toString());
    pRelation->setRating(
            query.fieldValue(query.fieldIndex(TRACKRELATIONSTABLE_RATING)).toInt());
    pRelation->setNotes(
            query.fieldValue(query.fieldIndex(TRACKRELATIONSTABLE_NOTES)).toString());
}

int clampRating(int rating) {
    return std::min(std::max(rating, TrackRelation::kMinRating), TrackRelation::kMaxRating);
}

// A null QString binds as SQL NULL, which the NOT NULL columns refuse.
QVariant textValue(const QString& text) {
    return QVariant(text.isNull() ? QStringLiteral("") : text);
}

} // anonymous namespace

namespace muxic {

TrackRelationStorage::TrackRelationStorage(QObject* pParent)
        : QObject(pParent) {
}

void TrackRelationStorage::repairDatabase(const QSqlDatabase& database) {
    DEBUG_ASSERT_QOBJECT_THREAD_AFFINITY(this);

    // The table must exist before the repair, because a repair can run
    // before the first connectDatabase().
    FwdSqlQuery(database, kCreateTableQuery).execPrepared();

    // Delete the relations of tracks that left the library.
    FwdSqlQuery(database,
            QStringLiteral(
                    "DELETE FROM %1 WHERE %2 NOT IN (SELECT %4 FROM %3) "
                    "OR %5 NOT IN (SELECT %4 FROM %3)")
                    .arg(QStringLiteral(TRACK_RELATIONS_TABLE),
                            TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                            QStringLiteral(LIBRARY_TABLE),
                            LIBRARYTABLE_ID,
                            TRACKRELATIONSTABLE_TARGET_TRACK_ID))
            .execPrepared();

    // Delete the relations of a track with itself.
    FwdSqlQuery(database,
            QStringLiteral("DELETE FROM %1 WHERE %2=%3")
                    .arg(QStringLiteral(TRACK_RELATIONS_TABLE),
                            TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                            TRACKRELATIONSTABLE_TARGET_TRACK_ID))
            .execPrepared();
}

void TrackRelationStorage::connectDatabase(const QSqlDatabase& database) {
    DEBUG_ASSERT_QOBJECT_THREAD_AFFINITY(this);

    m_database = database;
    if (!FwdSqlQuery(m_database, kCreateTableQuery).execPrepared()) {
        kLogger.warning() << "Failed to create table" << TRACK_RELATIONS_TABLE;
        return;
    }
    FwdSqlQuery(m_database, kCreateTargetIndexQuery).execPrepared();
}

void TrackRelationStorage::disconnectDatabase() {
    DEBUG_ASSERT_QOBJECT_THREAD_AFFINITY(this);

    m_database = QSqlDatabase();
}

bool TrackRelationStorage::saveRelation(const TrackRelation& relation) {
    DEBUG_ASSERT_QOBJECT_THREAD_AFFINITY(this);

    if (!relation.isValid()) {
        kLogger.warning() << "Refusing a relation of a track with itself or "
                             "with an unknown track";
        return false;
    }

    TrackRelation storedRelation;
    const bool exists = readRelation(relation.getSourceTrackId(),
            relation.getTargetTrackId(),
            &storedRelation);
    // A save on the reverse pair makes the stored relation go both ways.
    const bool reversed = exists &&
            storedRelation.getSourceTrackId() != relation.getSourceTrackId();
    const bool bidirectional = relation.isBidirectional() || reversed;
    // A new row and a new direction change which rows a view holds. A change
    // of the other fields does not.
    const bool rowsChanged = !exists || storedRelation.isBidirectional() != bidirectional;

    if (exists) {
        FwdSqlQuery query(m_database,
                QStringLiteral(
                        "UPDATE %1 SET %2=:bidirectional,%3=:type,"
                        "%4=:rating,%5=:notes WHERE %6=:id")
                        .arg(QStringLiteral(TRACK_RELATIONS_TABLE),
                                TRACKRELATIONSTABLE_BIDIRECTIONAL,
                                TRACKRELATIONSTABLE_RELATION_TYPE,
                                TRACKRELATIONSTABLE_RATING,
                                TRACKRELATIONSTABLE_NOTES,
                                TRACKRELATIONSTABLE_ID));
        VERIFY_OR_DEBUG_ASSERT(query.isPrepared()) {
            return false;
        }
        query.bindValue(QStringLiteral(":bidirectional"), QVariant(bidirectional ? 1 : 0));
        query.bindValue(QStringLiteral(":type"), textValue(relation.getType()));
        query.bindValue(QStringLiteral(":rating"), QVariant(clampRating(relation.getRating())));
        query.bindValue(QStringLiteral(":notes"), textValue(relation.getNotes()));
        query.bindValue(QStringLiteral(":id"), storedRelation.getId());
        VERIFY_OR_DEBUG_ASSERT(query.execPrepared()) {
            return false;
        }
    } else {
        FwdSqlQuery query(m_database,
                QStringLiteral(
                        "INSERT INTO %1 (%2,%3,%4,%5,%6,%7) VALUES "
                        "(:sourceTrackId,:targetTrackId,:bidirectional,"
                        ":type,:rating,:notes)")
                        .arg(QStringLiteral(TRACK_RELATIONS_TABLE),
                                TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                                TRACKRELATIONSTABLE_TARGET_TRACK_ID,
                                TRACKRELATIONSTABLE_BIDIRECTIONAL,
                                TRACKRELATIONSTABLE_RELATION_TYPE,
                                TRACKRELATIONSTABLE_RATING,
                                TRACKRELATIONSTABLE_NOTES));
        VERIFY_OR_DEBUG_ASSERT(query.isPrepared()) {
            return false;
        }
        query.bindValue(QStringLiteral(":sourceTrackId"), relation.getSourceTrackId());
        query.bindValue(QStringLiteral(":targetTrackId"), relation.getTargetTrackId());
        query.bindValue(QStringLiteral(":bidirectional"), QVariant(bidirectional ? 1 : 0));
        query.bindValue(QStringLiteral(":type"), textValue(relation.getType()));
        query.bindValue(QStringLiteral(":rating"), QVariant(clampRating(relation.getRating())));
        query.bindValue(QStringLiteral(":notes"), textValue(relation.getNotes()));
        VERIFY_OR_DEBUG_ASSERT(query.execPrepared()) {
            return false;
        }
    }

    if (rowsChanged) {
        emit relationsChanged();
    } else {
        emit relationUpdated(relation.getSourceTrackId(), relation.getTargetTrackId());
    }
    return true;
}

bool TrackRelationStorage::removeRelationWithoutSignal(TrackId trackId1, TrackId trackId2) {
    FwdSqlQuery query(m_database,
            QStringLiteral(
                    "DELETE FROM %1 WHERE (%2=:trackId1 AND %3=:trackId2) "
                    "OR (%2=:trackId2 AND %3=:trackId1)")
                    .arg(QStringLiteral(TRACK_RELATIONS_TABLE),
                            TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                            TRACKRELATIONSTABLE_TARGET_TRACK_ID));
    VERIFY_OR_DEBUG_ASSERT(query.isPrepared()) {
        return false;
    }
    query.bindValue(QStringLiteral(":trackId1"), trackId1);
    query.bindValue(QStringLiteral(":trackId2"), trackId2);
    return query.execPrepared();
}

bool TrackRelationStorage::removeRelation(TrackId trackId1, TrackId trackId2) {
    DEBUG_ASSERT_QOBJECT_THREAD_AFFINITY(this);

    if (!removeRelationWithoutSignal(trackId1, trackId2)) {
        return false;
    }
    emit relationsChanged();
    return true;
}

bool TrackRelationStorage::removeAllRelationsOfTracks(const QList<TrackId>& trackIds) {
    DEBUG_ASSERT_QOBJECT_THREAD_AFFINITY(this);

    if (trackIds.isEmpty()) {
        return true;
    }
    if (!onPurgingTracks(trackIds)) {
        return false;
    }
    emit relationsChanged();
    return true;
}

bool TrackRelationStorage::onPurgingTracks(const QList<TrackId>& trackIds) {
    FwdSqlQuery query(m_database,
            QStringLiteral("DELETE FROM %1 WHERE %2=:trackId OR %3=:trackId")
                    .arg(QStringLiteral(TRACK_RELATIONS_TABLE),
                            TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                            TRACKRELATIONSTABLE_TARGET_TRACK_ID));
    if (!query.isPrepared()) {
        return false;
    }
    for (const auto& trackId : trackIds) {
        query.bindValue(QStringLiteral(":trackId"), trackId);
        if (!query.execPrepared()) {
            return false;
        }
    }
    return true;
}

void TrackRelationStorage::afterPurgingTracks() {
    DEBUG_ASSERT_QOBJECT_THREAD_AFFINITY(this);

    emit relationsChanged();
}

bool TrackRelationStorage::readRelation(
        TrackId trackId1,
        TrackId trackId2,
        TrackRelation* pRelation) const {
    FwdSqlQuery query(m_database,
            QStringLiteral(
                    "SELECT %1 FROM %2 WHERE (%3=:trackId1 AND %4=:trackId2) "
                    "OR (%3=:trackId2 AND %4=:trackId1)")
                    .arg(kSelectColumns,
                            QStringLiteral(TRACK_RELATIONS_TABLE),
                            TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                            TRACKRELATIONSTABLE_TARGET_TRACK_ID));
    VERIFY_OR_DEBUG_ASSERT(query.isPrepared()) {
        return false;
    }
    query.bindValue(QStringLiteral(":trackId1"), trackId1);
    query.bindValue(QStringLiteral(":trackId2"), trackId2);
    VERIFY_OR_DEBUG_ASSERT(query.execPrepared()) {
        return false;
    }
    if (!query.next()) {
        return false;
    }
    if (pRelation != nullptr) {
        readRelationFromQuery(query, pRelation);
    }
    return true;
}

QList<TrackRelation> TrackRelationStorage::readRelationsFrom(TrackId trackId) const {
    QList<TrackRelation> relations;
    FwdSqlQuery query(m_database,
            QStringLiteral(
                    "SELECT %1 FROM %2 WHERE %3=:trackId "
                    "OR (%4=:trackId AND %5<>0)")
                    .arg(kSelectColumns,
                            QStringLiteral(TRACK_RELATIONS_TABLE),
                            TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                            TRACKRELATIONSTABLE_TARGET_TRACK_ID,
                            TRACKRELATIONSTABLE_BIDIRECTIONAL));
    VERIFY_OR_DEBUG_ASSERT(query.isPrepared()) {
        return relations;
    }
    query.bindValue(QStringLiteral(":trackId"), trackId);
    VERIFY_OR_DEBUG_ASSERT(query.execPrepared()) {
        return relations;
    }
    while (query.next()) {
        TrackRelation relation;
        readRelationFromQuery(query, &relation);
        relations.append(relation);
    }
    return relations;
}

QStringList TrackRelationStorage::readRelationTypes() const {
    QStringList types;
    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT DISTINCT %1 FROM %2 WHERE %1<>'' ORDER BY %1")
                    .arg(TRACKRELATIONSTABLE_RELATION_TYPE,
                            QStringLiteral(TRACK_RELATIONS_TABLE)));
    VERIFY_OR_DEBUG_ASSERT(query.execPrepared()) {
        return types;
    }
    while (query.next()) {
        types.append(query.fieldValue(DbFieldIndex(0)).toString());
    }
    return types;
}

uint TrackRelationStorage::countRelations() const {
    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT COUNT(*) FROM %1")
                    .arg(QStringLiteral(TRACK_RELATIONS_TABLE)));
    VERIFY_OR_DEBUG_ASSERT(query.execPrepared() && query.next()) {
        return 0;
    }
    return query.fieldValue(DbFieldIndex(0)).toUInt();
}

uint TrackRelationStorage::countRelationsOfTrack(TrackId trackId) const {
    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT COUNT(*) FROM %1 WHERE %2=:trackId OR %3=:trackId")
                    .arg(QStringLiteral(TRACK_RELATIONS_TABLE),
                            TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                            TRACKRELATIONSTABLE_TARGET_TRACK_ID));
    VERIFY_OR_DEBUG_ASSERT(query.isPrepared()) {
        return 0;
    }
    query.bindValue(QStringLiteral(":trackId"), trackId);
    VERIFY_OR_DEBUG_ASSERT(query.execPrepared() && query.next()) {
        return 0;
    }
    return query.fieldValue(DbFieldIndex(0)).toUInt();
}

QSet<TrackId> TrackRelationStorage::collectRelatedTrackIds(
        const QList<TrackId>& trackIds) const {
    QSet<TrackId> relatedTrackIds;
    for (const auto& trackId : trackIds) {
        if (countRelationsOfTrack(trackId) > 0) {
            relatedTrackIds.insert(trackId);
        }
    }
    return relatedTrackIds;
}

// static
QString TrackRelationStorage::formatSubselectQueryForRelatedTrackIds(TrackId trackId) {
    return QStringLiteral(
            "SELECT %2 FROM %1 WHERE %3=%5 "
            "UNION SELECT %3 FROM %1 WHERE %2=%5 AND %4<>0")
            .arg(QStringLiteral(TRACK_RELATIONS_TABLE),
                    TRACKRELATIONSTABLE_TARGET_TRACK_ID,
                    TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                    TRACKRELATIONSTABLE_BIDIRECTIONAL,
                    trackId.toString());
}

// static
QString TrackRelationStorage::formatSubselectQueryForAllRelatedTrackIds() {
    return QStringLiteral("SELECT %2 FROM %1 UNION SELECT %3 FROM %1")
            .arg(QStringLiteral(TRACK_RELATIONS_TABLE),
                    TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                    TRACKRELATIONSTABLE_TARGET_TRACK_ID);
}

} // namespace muxic
