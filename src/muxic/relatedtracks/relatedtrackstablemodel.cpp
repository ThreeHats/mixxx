#include "muxic/relatedtracks/relatedtrackstablemodel.h"

#include <QTableView>
#include <algorithm>

#include "library/dao/trackschema.h"
#include "library/starrating.h"
#include "library/tabledelegates/stardelegate.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_relatedtrackstablemodel.cpp"
#include "muxic/relatedtracks/relationsuggester.h"
#include "muxic/relatedtracks/relationtypedelegate.h"
#include "muxic/relatedtracks/trackrelationschema.h"
#include "muxic/relatedtracks/trackrelationstorage.h"
#include "track/track.h"
#include "util/db/fwdsqlquery.h"

using namespace muxic;

namespace {

const QString kModelName = QStringLiteral("relatedtracks");

const QString kRelationTypeColumn = QStringLiteral("relation_type");
const QString kRelationRatingColumn = QStringLiteral("relation_rating");
const QString kRelationNotesColumn = QStringLiteral("relation_notes");
const QString kRelationDirectionColumn = QStringLiteral("relation_direction");
const QString kRelationCountColumn = QStringLiteral("relation_count");

// Every view of this model has the same columns, thus the table view keeps
// its header when the mode changes.
const QStringList kRelationColumns{kRelationTypeColumn,
        kRelationRatingColumn,
        kRelationNotesColumn,
        kRelationDirectionColumn,
        kRelationCountColumn};

// The arrow of a relation that leads one way, and of one that leads both
// ways. A symbol needs no translation.
const QString kOneWayArrow = QStringLiteral("→");
const QString kBothWaysArrow = QStringLiteral("↔");

QString qualified(const QString& column) {
    return QStringLiteral(LIBRARY_TABLE ".") + column;
}

QString bpmRangeClause(double referenceBpm) {
    const QList<RelationSuggester::BpmRange> ranges =
            RelationSuggester::bpmRanges(referenceBpm);
    if (ranges.isEmpty()) {
        return QString();
    }
    QStringList terms;
    terms.reserve(ranges.size());
    for (const auto& range : ranges) {
        terms.append(QStringLiteral("(%1 BETWEEN %2 AND %3)")
                             .arg(qualified(LIBRARYTABLE_BPM),
                                     QString::number(range.lower, 'f', 4),
                                     QString::number(range.upper, 'f', 4)));
    }
    return QChar('(') + terms.join(QStringLiteral(" OR ")) + QChar(')');
}

QString compatibleKeyClause(mixxx::track::io::key::ChromaticKey referenceKey) {
    const QList<mixxx::track::io::key::ChromaticKey> keys =
            RelationSuggester::compatibleKeys(referenceKey);
    if (keys.isEmpty()) {
        return QString();
    }
    QStringList numbers;
    numbers.reserve(keys.size());
    for (const auto key : keys) {
        numbers.append(QString::number(static_cast<int>(key)));
    }
    return QStringLiteral("(%1 IN (%2))")
            .arg(qualified(LIBRARYTABLE_KEY_ID), numbers.join(QChar(',')));
}

/// Counts the relations of the library row.
QString formatRelationCountColumn() {
    return QStringLiteral("(SELECT COUNT(*) FROM %1 WHERE %1.%2=%4 OR %1.%3=%4) AS %5")
            .arg(QStringLiteral(TRACK_RELATIONS_TABLE),
                    TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                    TRACKRELATIONSTABLE_TARGET_TRACK_ID,
                    qualified(LIBRARYTABLE_ID),
                    kRelationCountColumn);
}

/// The first three columns of every view of this model.
QString formatTrackColumns() {
    return QStringLiteral("%1,'' AS %2,%3 AS %4")
            .arg(qualified(LIBRARYTABLE_ID),
                    LIBRARYTABLE_PREVIEW,
                    qualified(LIBRARYTABLE_COVERART_DIGEST),
                    LIBRARYTABLE_COVERART);
}

} // anonymous namespace

namespace muxic {

RelatedTracksTableModel::RelatedTracksTableModel(
        QObject* pParent,
        TrackCollectionManager* pTrackCollectionManager)
        : TrackSetTableModel(
                  pParent,
                  pTrackCollectionManager,
                  "mixxx.db.model.relatedtracks") {
}

void RelatedTracksTableModel::setRelationTable(const QString& tableName,
        const QString& viewQuery) {
    // The reference track of a view can change between two selects, thus
    // the old view of that name has to go first.
    FwdSqlQuery(m_database,
            QStringLiteral("DROP VIEW IF EXISTS %1").arg(tableName))
            .execPrepared();
    FwdSqlQuery(m_database, viewQuery).execPrepared();

    QStringList columns;
    columns << LIBRARYTABLE_ID
            << LIBRARYTABLE_PREVIEW
            << LIBRARYTABLE_COVERART;
    const int firstRelationColumn = columns.size();
    columns << kRelationColumns;

    setTable(tableName,
            LIBRARYTABLE_ID,
            columns,
            m_pTrackCollectionManager->internalCollection()->getTrackSource());

    // The relation columns belong to no ColumnCache entry, thus they get
    // their header here.
    const QStringList titles{tr("Relation"),
            tr("Relation Rating"),
            tr("Relation Note"),
            tr("Direction"),
            tr("Relations")};
    DEBUG_ASSERT(titles.size() == kRelationColumns.size());
    for (int i = 0; i < titles.size(); ++i) {
        const int section = firstRelationColumn + i;
        setHeaderData(section, Qt::Horizontal, titles.at(i), Qt::DisplayRole);
        setHeaderData(section,
                Qt::Horizontal,
                kRelationColumns.at(i),
                TrackModel::kHeaderNameRole);
        setHeaderData(section,
                Qt::Horizontal,
                ColumnCache::defaultColumnWidth() * 2,
                TrackModel::kHeaderWidthRole);
    }

    setSearch(QString());
    setDefaultSort(fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_ARTIST), Qt::AscendingOrder);
    select();
}

void RelatedTracksTableModel::selectAllRelated() {
    m_mode = Mode::AllRelated;
    m_referenceTrackId = TrackId();

    const QString tableName = QStringLiteral("muxic_related_all");
    const QString relationColumns =
            QStringLiteral("'' AS %1,NULL AS %2,'' AS %3,'' AS %4,")
                    .arg(kRelationTypeColumn,
                            kRelationRatingColumn,
                            kRelationNotesColumn,
                            kRelationDirectionColumn) +
            formatRelationCountColumn();
    const QString viewQuery =
            QStringLiteral(
                    "CREATE TEMPORARY VIEW IF NOT EXISTS %1 AS SELECT %2,%3 "
                    "FROM " LIBRARY_TABLE " WHERE %4=0 AND %5 IN (%6)")
                    .arg(tableName,
                            formatTrackColumns(),
                            relationColumns,
                            qualified(LIBRARYTABLE_MIXXXDELETED),
                            qualified(LIBRARYTABLE_ID),
                            TrackRelationStorage::
                                    formatSubselectQueryForAllRelatedTrackIds());

    setRelationTable(tableName, viewQuery);
}

void RelatedTracksTableModel::selectRelatedTo(TrackId trackId) {
    if (!trackId.isValid()) {
        // Without a reference track the node shows every related track.
        selectAllRelated();
        return;
    }
    m_mode = Mode::RelatedTo;
    m_referenceTrackId = trackId;

    const QString tableName = QStringLiteral("muxic_related_%1").arg(trackId.toString());
    const QString relationColumns =
            QStringLiteral(
                    "rel.%1 AS %2,rel.%3 AS %4,rel.%5 AS %6,"
                    "(CASE WHEN rel.%7<>0 THEN '%8' ELSE '%9' END)")
                    .arg(TRACKRELATIONSTABLE_RELATION_TYPE,
                            kRelationTypeColumn,
                            TRACKRELATIONSTABLE_RATING,
                            kRelationRatingColumn,
                            TRACKRELATIONSTABLE_NOTES,
                            kRelationNotesColumn,
                            TRACKRELATIONSTABLE_BIDIRECTIONAL,
                            kBothWaysArrow,
                            kOneWayArrow) +
            QStringLiteral(" AS %1,").arg(kRelationDirectionColumn) +
            formatRelationCountColumn();
    const QString relationJoin =
            QStringLiteral(
                    "(rel.%1=%3 AND rel.%2=%4) OR "
                    "(rel.%2=%3 AND rel.%1=%4 AND rel.%5<>0)")
                    .arg(TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                            TRACKRELATIONSTABLE_TARGET_TRACK_ID,
                            trackId.toString(),
                            qualified(LIBRARYTABLE_ID),
                            TRACKRELATIONSTABLE_BIDIRECTIONAL);
    const QString viewQuery =
            QStringLiteral(
                    "CREATE TEMPORARY VIEW IF NOT EXISTS %1 AS SELECT %2,%3 "
                    "FROM " LIBRARY_TABLE ",%4 rel WHERE %5=0 AND (%6)")
                    .arg(tableName,
                            formatTrackColumns(),
                            relationColumns,
                            QStringLiteral(TRACK_RELATIONS_TABLE),
                            qualified(LIBRARYTABLE_MIXXXDELETED),
                            relationJoin);

    setRelationTable(tableName, viewQuery);
}

void RelatedTracksTableModel::selectSuggestedFor(TrackId trackId) {
    m_mode = Mode::SuggestedFor;
    m_referenceTrackId = trackId;

    QString bpmClause;
    QString keyClause;
    if (trackId.isValid()) {
        const TrackPointer pTrack = m_pTrackCollectionManager->getTrackById(trackId);
        if (pTrack) {
            bpmClause = bpmRangeClause(pTrack->getBpm());
            keyClause = compatibleKeyClause(pTrack->getKey());
        }
    }

    QStringList conditions;
    conditions.append(qualified(LIBRARYTABLE_MIXXXDELETED) + QStringLiteral("=0"));
    if (trackId.isValid()) {
        conditions.append(qualified(LIBRARYTABLE_ID) +
                QStringLiteral("<>") + trackId.toString());
    }
    if (!bpmClause.isEmpty()) {
        conditions.append(bpmClause);
    }
    if (!keyClause.isEmpty()) {
        conditions.append(keyClause);
    }
    if (bpmClause.isEmpty() && keyClause.isEmpty()) {
        // The reference track has no tempo and no key, thus nothing fits it.
        conditions.append(QStringLiteral("0"));
    }

    const QString tableName = QStringLiteral("muxic_suggested");
    const QString relationColumns =
            QStringLiteral("'' AS %1,NULL AS %2,'' AS %3,'' AS %4,")
                    .arg(kRelationTypeColumn,
                            kRelationRatingColumn,
                            kRelationNotesColumn,
                            kRelationDirectionColumn) +
            formatRelationCountColumn();
    const QString viewQuery =
            QStringLiteral(
                    "CREATE TEMPORARY VIEW IF NOT EXISTS %1 AS SELECT %2,%3 "
                    "FROM " LIBRARY_TABLE " WHERE %4")
                    .arg(tableName,
                            formatTrackColumns(),
                            relationColumns,
                            conditions.join(QStringLiteral(" AND ")));

    setRelationTable(tableName, viewQuery);
}

void RelatedTracksTableModel::refresh() {
    switch (m_mode) {
    case Mode::AllRelated:
        selectAllRelated();
        return;
    case Mode::RelatedTo:
        selectRelatedTo(m_referenceTrackId);
        return;
    case Mode::SuggestedFor:
        selectSuggestedFor(m_referenceTrackId);
        return;
    }
    DEBUG_ASSERT(!"unreachable");
}

void RelatedTracksTableModel::removeTracks(const QModelIndexList& indices) {
    if (indices.isEmpty()) {
        return;
    }
    TrackRelationStorage& storage =
            m_pTrackCollectionManager->internalCollection()->trackRelations();

    QSet<TrackId> removedTrackIds;
    for (const QModelIndex& index : indices) {
        const TrackId trackId = getTrackId(index);
        if (!trackId.isValid()) {
            continue;
        }
        if (m_mode == Mode::RelatedTo && m_referenceTrackId.isValid()) {
            if (storage.removeRelation(m_referenceTrackId, trackId)) {
                removedTrackIds.insert(trackId);
            }
        } else if (m_mode == Mode::AllRelated) {
            if (storage.removeAllRelationsOfTracks(QList<TrackId>{trackId})) {
                removedTrackIds.insert(trackId);
            }
        }
    }
    if (!removedTrackIds.isEmpty()) {
        removeTrackRows(removedTrackIds);
    }
}

TrackModel::Capabilities RelatedTracksTableModel::getCapabilities() const {
    Capabilities caps =
            Capability::AddToTrackSet |
            Capability::AddToAutoDJ |
            Capability::EditMetadata |
            Capability::LoadToDeck |
            Capability::LoadToSampler |
            Capability::LoadToPreviewDeck |
            Capability::ResetPlayed |
            Capability::Analyze |
            Capability::Properties |
            Capability::Sorting;

    if (m_mode != Mode::SuggestedFor) {
        caps |= Capability::Remove;
    }
    return caps;
}

TrackRelationStorage& RelatedTracksTableModel::storage() const {
    return m_pTrackCollectionManager->internalCollection()->trackRelations();
}

bool RelatedTracksTableModel::relationForIndex(
        const QModelIndex& index, TrackRelation* pRelation) const {
    if (!index.isValid() || !showsOneRelationPerRow()) {
        return false;
    }
    const TrackId trackId = getTrackId(index);
    if (!trackId.isValid()) {
        return false;
    }
    return storage().readRelation(m_referenceTrackId, trackId, pRelation);
}

QVariant RelatedTracksTableModel::data(const QModelIndex& index, int role) const {
    if (index.isValid() &&
            index.column() == fieldIndex(kRelationRatingColumn) &&
            (role == Qt::DisplayRole || role == Qt::EditRole)) {
        const QVariant value = rawValue(index);
        if (value.isNull()) {
            return QVariant();
        }
        return QVariant::fromValue(StarRating(value.toInt()));
    }
    return TrackSetTableModel::data(index, role);
}

bool RelatedTracksTableModel::writeRelationColumn(
        const QModelIndex& index, const QVariant& value) {
    TrackRelation relation;
    if (!relationForIndex(index, &relation)) {
        return false;
    }
    const int column = index.column();
    QVariant storedValue;
    if (column == fieldIndex(kRelationTypeColumn)) {
        relation.setType(value.toString().trimmed());
        storedValue = relation.getType();
    } else if (column == fieldIndex(kRelationRatingColumn)) {
        const int stars = value.canConvert<StarRating>()
                ? value.value<StarRating>().starCount()
                : value.toInt();
        relation.setRating(std::min(std::max(stars, TrackRelation::kMinRating),
                TrackRelation::kMaxRating));
        storedValue = relation.getRating();
    } else {
        relation.setNotes(value.toString());
        storedValue = relation.getNotes();
    }
    if (!storage().saveRelation(relation)) {
        return false;
    }
    setTableColumnValue(index.row(), column, storedValue);
    return true;
}

bool RelatedTracksTableModel::setData(
        const QModelIndex& index, const QVariant& value, int role) {
    if (!index.isValid()) {
        return false;
    }
    const int column = index.column();
    if (column == fieldIndex(kRelationDirectionColumn) ||
            column == fieldIndex(kRelationCountColumn)) {
        return false;
    }
    if (column == fieldIndex(kRelationTypeColumn) ||
            column == fieldIndex(kRelationRatingColumn) ||
            column == fieldIndex(kRelationNotesColumn)) {
        if (role != Qt::EditRole) {
            return false;
        }
        return writeRelationColumn(index, value);
    }
    return TrackSetTableModel::setData(index, value, role);
}

Qt::ItemFlags RelatedTracksTableModel::flags(const QModelIndex& index) const {
    Qt::ItemFlags itemFlags = TrackSetTableModel::flags(index);
    if (!index.isValid()) {
        return itemFlags;
    }
    const int column = index.column();
    if (column == fieldIndex(kRelationTypeColumn) ||
            column == fieldIndex(kRelationRatingColumn) ||
            column == fieldIndex(kRelationNotesColumn)) {
        if (showsOneRelationPerRow()) {
            itemFlags |= Qt::ItemIsEditable;
        } else {
            itemFlags &= ~Qt::ItemIsEditable;
        }
    } else if (column == fieldIndex(kRelationDirectionColumn) ||
            column == fieldIndex(kRelationCountColumn)) {
        itemFlags &= ~Qt::ItemIsEditable;
    }
    return itemFlags;
}

QAbstractItemDelegate* RelatedTracksTableModel::delegateForColumn(
        const int column, QObject* pParent) {
    auto* pTableView = qobject_cast<QTableView*>(pParent);
    if (pTableView) {
        if (column == fieldIndex(kRelationRatingColumn)) {
            return new StarDelegate(pTableView);
        }
        if (column == fieldIndex(kRelationTypeColumn)) {
            return new RelationTypeDelegate(pTableView, this);
        }
    }
    return TrackSetTableModel::delegateForColumn(column, pParent);
}

int RelatedTracksTableModel::setRelationsBidirectional(
        const QModelIndexList& indices, bool bidirectional) {
    if (!showsOneRelationPerRow()) {
        return 0;
    }
    QSet<int> rows;
    int changed = 0;
    for (const QModelIndex& index : indices) {
        if (!index.isValid() || rows.contains(index.row())) {
            continue;
        }
        rows.insert(index.row());
        TrackRelation relation;
        if (!relationForIndex(index, &relation)) {
            continue;
        }
        if (relation.isBidirectional() == bidirectional) {
            continue;
        }
        relation.setBidirectional(bidirectional);
        if (storage().saveRelation(relation)) {
            ++changed;
        }
    }
    return changed;
}

QStringList RelatedTracksTableModel::knownRelationTypes() const {
    QStringList types = defaultTrackRelationTypes();
    const QStringList storedTypes = storage().readRelationTypes();
    for (const QString& type : storedTypes) {
        if (!types.contains(type)) {
            types.append(type);
        }
    }
    return types;
}

QString RelatedTracksTableModel::tableColumnSortExpression(int column) const {
    if (column == fieldIndex(kRelationRatingColumn)) {
        return QStringLiteral("CAST(%1.%2 AS INTEGER)")
                .arg(m_tableName, kRelationRatingColumn);
    }
    if (column == fieldIndex(kRelationCountColumn)) {
        return QStringLiteral("CAST(%1.%2 AS INTEGER)")
                .arg(m_tableName, kRelationCountColumn);
    }
    return TrackSetTableModel::tableColumnSortExpression(column);
}

QString RelatedTracksTableModel::modelKey(bool noSearch) const {
    QString key = kModelName;
    switch (m_mode) {
    case Mode::AllRelated:
        key += QStringLiteral(":all");
        break;
    case Mode::RelatedTo:
        key += QStringLiteral(":related");
        break;
    case Mode::SuggestedFor:
        key += QStringLiteral(":suggested");
        break;
    }
    if (noSearch) {
        return key;
    }
    return key + QChar('#') + currentSearch();
}

} // namespace muxic
