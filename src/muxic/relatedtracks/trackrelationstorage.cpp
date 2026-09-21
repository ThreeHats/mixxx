#include "muxic/relatedtracks/trackrelationstorage.h"

#include <algorithm>

#include "library/dao/trackschema.h"
#include "moc_trackrelationstorage.cpp"
#include "muxic/relatedtracks/trackrelationschema.h"
#include "util/db/fwdsqlquery.h"
#include "util/db/sqltransaction.h"
#include "util/logger.h"

namespace muxic {

namespace {

const mixxx::Logger kLogger("TrackRelationStorage");

const QString kPairIndexName = QStringLiteral(TRACK_RELATIONS_TABLE "_pair");
const QString kTargetIndexName = QStringLiteral(TRACK_RELATIONS_TABLE "_target");

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

// This index holds the rule that a pair of tracks has one row, in either
// order. SQLite 3.9 and later index an expression.
const QString kCreatePairIndexQuery =
        QStringLiteral(
                "CREATE UNIQUE INDEX IF NOT EXISTS %1 ON %2 "
                "(min(%3,%4), max(%3,%4))")
                .arg(kPairIndexName,
                        QStringLiteral(TRACK_RELATIONS_TABLE),
                        TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                        TRACKRELATIONSTABLE_TARGET_TRACK_ID);

const QString kCreateTargetIndexQuery =
        QStringLiteral("CREATE INDEX IF NOT EXISTS %1 ON %2 (%3)")
                .arg(kTargetIndexName,
                        QStringLiteral(TRACK_RELATIONS_TABLE),
                        TRACKRELATIONSTABLE_TARGET_TRACK_ID);

// Each row of a pair that a row with a lower id already holds.
const QString kSelectDuplicatePairs =
        QStringLiteral(
                "SELECT a.%1, b.%1 FROM %2 a, %2 b "
                "WHERE min(a.%3,a.%4)=min(b.%3,b.%4) "
                "AND max(a.%3,a.%4)=max(b.%3,b.%4) AND a.%1<b.%1")
                .arg(TRACKRELATIONSTABLE_ID,
                        QStringLiteral(TRACK_RELATIONS_TABLE),
                        TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                        TRACKRELATIONSTABLE_TARGET_TRACK_ID);

// Folds one row into another. The row that stays keeps each field that it
// has and takes the fields that it lacks.
const QString kFoldRowQuery =
        QStringLiteral(
                "UPDATE %1 SET "
                "%2 = max(%2, (SELECT %2 FROM %1 WHERE %6=:fromId)),"
                "%3 = CASE WHEN %3<>'' THEN %3 ELSE "
                "(SELECT %3 FROM %1 WHERE %6=:fromId) END,"
                "%4 = CASE WHEN %4<>0 THEN %4 ELSE "
                "(SELECT %4 FROM %1 WHERE %6=:fromId) END,"
                "%5 = CASE WHEN %5<>'' THEN %5 ELSE "
                "(SELECT %5 FROM %1 WHERE %6=:fromId) END "
                "WHERE %6=:intoId")
                .arg(QStringLiteral(TRACK_RELATIONS_TABLE),
                        TRACKRELATIONSTABLE_BIDIRECTIONAL,
                        TRACKRELATIONSTABLE_RELATION_TYPE,
                        TRACKRELATIONSTABLE_RATING,
                        TRACKRELATIONSTABLE_NOTES,
                        TRACKRELATIONSTABLE_ID);

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
    pRelation->setType(
            query.fieldValue(query.fieldIndex(TRACKRELATIONSTABLE_RELATION_TYPE))
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

QString joinTrackIds(const QList<TrackId>& trackIds) {
    QStringList numbers;
    numbers.reserve(trackIds.size());
    for (const auto& trackId : trackIds) {
        if (trackId.isValid()) {
            numbers.append(trackId.toString());
        }
    }
    return numbers.join(QChar(','));
}

} // anonymous namespace

TrackRelationStorage::TrackRelationStorage(QObject* pParent)
        : QObject(pParent) {
}

// static
void TrackRelationStorage::createTableAndIndices(const QSqlDatabase& database) {
    if (!FwdSqlQuery(database, kCreateTableQuery).execPrepared()) {
        kLogger.warning() << "Failed to create table" << TRACK_RELATIONS_TABLE;
        return;
    }

    // A table from an older build can hold a pair in both orders. The unique
    // index of the pair refuses such rows, thus fold them first.
    QList<QPair<qint64, qint64>> foldPairs;
    FwdSqlQuery duplicates(database, kSelectDuplicatePairs);
    if (duplicates.execPrepared()) {
        while (duplicates.next()) {
            foldPairs.append(qMakePair(
                    duplicates.fieldValue(DbFieldIndex(0)).toLongLong(),
                    duplicates.fieldValue(DbFieldIndex(1)).toLongLong()));
        }
    }
    for (const auto& foldPair : std::as_const(foldPairs)) {
        FwdSqlQuery fold(database, kFoldRowQuery);
        fold.bindValue(QStringLiteral(":intoId"), QVariant(foldPair.first));
        fold.bindValue(QStringLiteral(":fromId"), QVariant(foldPair.second));
        fold.execPrepared();
        FwdSqlQuery drop(database,
                QStringLiteral("DELETE FROM %1 WHERE %2=:id")
                        .arg(QStringLiteral(TRACK_RELATIONS_TABLE),
                                TRACKRELATIONSTABLE_ID));
        drop.bindValue(QStringLiteral(":id"), QVariant(foldPair.second));
        drop.execPrepared();
    }

    FwdSqlQuery(database, kCreatePairIndexQuery).execPrepared();
    FwdSqlQuery(database, kCreateTargetIndexQuery).execPrepared();
}

void TrackRelationStorage::repairDatabase(const QSqlDatabase& database) {
    DEBUG_ASSERT_QOBJECT_THREAD_AFFINITY(this);

    createTableAndIndices(database);

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
    createTableAndIndices(m_database);
}

void TrackRelationStorage::disconnectDatabase() {
    DEBUG_ASSERT_QOBJECT_THREAD_AFFINITY(this);

    m_database = QSqlDatabase();
}

bool TrackRelationStorage::saveRelationWorker(
        const TrackRelation& relation, bool* pChanged) {
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
    if (pChanged != nullptr) {
        *pChanged = !exists ||
                storedRelation.isBidirectional() != bidirectional ||
                storedRelation.getType() != relation.getType() ||
                storedRelation.getRating() != clampRating(relation.getRating()) ||
                storedRelation.getNotes() != relation.getNotes();
    }

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
        query.bindValue(QStringLiteral(":rating"),
                QVariant(clampRating(relation.getRating())));
        query.bindValue(QStringLiteral(":notes"), textValue(relation.getNotes()));
        query.bindValue(QStringLiteral(":id"), storedRelation.getId());
        return query.execPrepared();
    }

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
    return query.execPrepared();
}

bool TrackRelationStorage::saveRelation(const TrackRelation& relation) {
    return saveRelations(QList<TrackRelation>{relation}) > 0;
}

int TrackRelationStorage::saveRelations(const QList<TrackRelation>& relations) {
    DEBUG_ASSERT_QOBJECT_THREAD_AFFINITY(this);

    if (relations.isEmpty()) {
        return 0;
    }
    int written = 0;
    bool anyChanged = false;
    SqlTransaction transaction(m_database);
    for (const auto& relation : relations) {
        bool changed = false;
        if (saveRelationWorker(relation, &changed)) {
            ++written;
            anyChanged = anyChanged || changed;
        }
    }
    VERIFY_OR_DEBUG_ASSERT(transaction.commit()) {
        return 0;
    }
    if (anyChanged) {
        emit relationsChanged();
    }
    return written;
}

bool TrackRelationStorage::updateRelationFields(const TrackRelation& relation) {
    DEBUG_ASSERT_QOBJECT_THREAD_AFFINITY(this);

    if (!relation.isValid()) {
        return false;
    }
    SqlTransaction transaction(m_database);
    if (!saveRelationWorker(relation, nullptr)) {
        return false;
    }
    VERIFY_OR_DEBUG_ASSERT(transaction.commit()) {
        return false;
    }
    return true;
}

int TrackRelationStorage::removeRelationsWorker(const QList<TrackIdPair>& pairs) {
    FwdSqlQuery query(m_database,
            QStringLiteral(
                    "DELETE FROM %1 WHERE (%2=:trackId1 AND %3=:trackId2) "
                    "OR (%2=:trackId2 AND %3=:trackId1)")
                    .arg(QStringLiteral(TRACK_RELATIONS_TABLE),
                            TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                            TRACKRELATIONSTABLE_TARGET_TRACK_ID));
    VERIFY_OR_DEBUG_ASSERT(query.isPrepared()) {
        return 0;
    }
    int removed = 0;
    for (const auto& pair : pairs) {
        query.bindValue(QStringLiteral(":trackId1"), pair.first);
        query.bindValue(QStringLiteral(":trackId2"), pair.second);
        if (query.execPrepared()) {
            removed += query.numRowsAffected();
        }
    }
    return removed;
}

bool TrackRelationStorage::removeRelation(TrackId trackId1, TrackId trackId2) {
    return removeRelations(QList<TrackIdPair>{qMakePair(trackId1, trackId2)}) > 0;
}

int TrackRelationStorage::removeRelations(const QList<TrackIdPair>& pairs) {
    DEBUG_ASSERT_QOBJECT_THREAD_AFFINITY(this);

    if (pairs.isEmpty()) {
        return 0;
    }
    SqlTransaction transaction(m_database);
    const int removed = removeRelationsWorker(pairs);
    VERIFY_OR_DEBUG_ASSERT(transaction.commit()) {
        return 0;
    }
    if (removed > 0) {
        emit relationsChanged();
    }
    return removed;
}

int TrackRelationStorage::removeRelationsOfTracksWorker(const QList<TrackId>& trackIds) {
    FwdSqlQuery query(m_database,
            QStringLiteral("DELETE FROM %1 WHERE %2=:trackId OR %3=:trackId")
                    .arg(QStringLiteral(TRACK_RELATIONS_TABLE),
                            TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                            TRACKRELATIONSTABLE_TARGET_TRACK_ID));
    if (!query.isPrepared()) {
        return -1;
    }
    int removed = 0;
    for (const auto& trackId : trackIds) {
        query.bindValue(QStringLiteral(":trackId"), trackId);
        if (!query.execPrepared()) {
            return -1;
        }
        removed += query.numRowsAffected();
    }
    return removed;
}

int TrackRelationStorage::removeAllRelationsOfTracks(const QList<TrackId>& trackIds) {
    DEBUG_ASSERT_QOBJECT_THREAD_AFFINITY(this);

    if (trackIds.isEmpty()) {
        return 0;
    }
    SqlTransaction transaction(m_database);
    const int removed = removeRelationsOfTracksWorker(trackIds);
    if (removed < 0) {
        return 0;
    }
    VERIFY_OR_DEBUG_ASSERT(transaction.commit()) {
        return 0;
    }
    if (removed > 0) {
        emit relationsChanged();
    }
    return removed;
}

bool TrackRelationStorage::onPurgingTracks(const QList<TrackId>& trackIds) {
    return removeRelationsOfTracksWorker(trackIds) >= 0;
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
    return countRelationsOfTracks(QList<TrackId>{trackId});
}

uint TrackRelationStorage::countRelationsOfTracks(const QList<TrackId>& trackIds) const {
    const QString idList = joinTrackIds(trackIds);
    if (idList.isEmpty()) {
        return 0;
    }
    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT COUNT(*) FROM %1 WHERE %2 IN (%4) OR %3 IN (%4)")
                    .arg(QStringLiteral(TRACK_RELATIONS_TABLE),
                            TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                            TRACKRELATIONSTABLE_TARGET_TRACK_ID,
                            idList));
    VERIFY_OR_DEBUG_ASSERT(query.execPrepared() && query.next()) {
        return 0;
    }
    return query.fieldValue(DbFieldIndex(0)).toUInt();
}

bool TrackRelationStorage::anyTrackHasRelation(const QList<TrackId>& trackIds) const {
    const QString idList = joinTrackIds(trackIds);
    if (idList.isEmpty()) {
        return false;
    }
    FwdSqlQuery query(m_database,
            QStringLiteral("SELECT 1 FROM %1 WHERE %2 IN (%4) OR %3 IN (%4) LIMIT 1")
                    .arg(QStringLiteral(TRACK_RELATIONS_TABLE),
                            TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                            TRACKRELATIONSTABLE_TARGET_TRACK_ID,
                            idList));
    VERIFY_OR_DEBUG_ASSERT(query.execPrepared()) {
        return false;
    }
    return query.next();
}

// static
QString TrackRelationStorage::formatSubselectQueryForAllRelatedTrackIds() {
    return QStringLiteral("SELECT %2 FROM %1 UNION SELECT %3 FROM %1")
            .arg(QStringLiteral(TRACK_RELATIONS_TABLE),
                    TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                    TRACKRELATIONSTABLE_TARGET_TRACK_ID);
}

} // namespace muxic
