#include "muxic/relatedtracks/relatedtrackstablemodel.h"

#include <QTableView>
#include <algorithm>
#include <utility>

#include "library/dao/trackschema.h"
#include "library/starrating.h"
#include "library/trackcollection.h"
#include "library/trackcollectionmanager.h"
#include "moc_relatedtrackstablemodel.cpp"
#include "muxic/relatedtracks/relationratingdelegate.h"
#include "muxic/relatedtracks/relationsuggester.h"
#include "muxic/relatedtracks/relationtypedelegate.h"
#include "muxic/relatedtracks/trackrelationschema.h"
#include "muxic/relatedtracks/trackrelationstorage.h"
#include "track/track.h"
#include "util/db/fwdsqlquery.h"

namespace muxic {

namespace {

const QString kModelName = QStringLiteral("relatedtracks");

const QString kRelationTypeColumn = QStringLiteral("relation_type");
const QString kRelationRatingColumn = QStringLiteral("relation_rating");
const QString kRelationNotesColumn = QStringLiteral("relation_notes");
const QString kRelationDirectionColumn = QStringLiteral("relation_direction");
const QString kRelationCountColumn = QStringLiteral("relation_count");

// The first column of a deck view, and a column that only its ORDER BY
// reads. The model shows no column that it does not name.
const QString kDeckNumberColumn = QStringLiteral("deck_number");
const QString kSortArtistColumn = QStringLiteral("deck_sort_artist");

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

/// The relation columns of a view that joins the relation table as `rel`.
QString formatJoinedRelationColumns() {
    return QStringLiteral(
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
}

/// The empty relation columns of a view that reads no relation.
QString formatEmptyRelationColumns() {
    return QStringLiteral("'' AS %1,NULL AS %2,'' AS %3,'' AS %4,NULL AS %5")
            .arg(kRelationTypeColumn,
                    kRelationRatingColumn,
                    kRelationNotesColumn,
                    kRelationDirectionColumn,
                    kRelationCountColumn);
}

/// The condition of the relations that lead from the reference track to the
/// library row.
QString formatRelationJoin(TrackId referenceTrackId) {
    if (!referenceTrackId.isValid()) {
        return QStringLiteral("0");
    }
    return QStringLiteral(
            "(rel.%1=%3 AND rel.%2=%4) OR "
            "(rel.%2=%3 AND rel.%1=%4 AND rel.%5<>0)")
            .arg(TRACKRELATIONSTABLE_SOURCE_TRACK_ID,
                    TRACKRELATIONSTABLE_TARGET_TRACK_ID,
                    referenceTrackId.toString(),
                    qualified(LIBRARYTABLE_ID),
                    TRACKRELATIONSTABLE_BIDIRECTIONAL);
}

/// The deck number and the name that the ORDER BY of a deck view reads.
QString formatDeckColumns(int deckNumber) {
    return QStringLiteral("%1 AS %2,%3 AS %4")
            .arg(QString::number(deckNumber),
                    kDeckNumberColumn,
                    qualified(LIBRARYTABLE_ARTIST),
                    kSortArtistColumn);
}

} // anonymous namespace

RelatedTracksTableModel::RelatedTracksTableModel(
        QObject* pParent,
        TrackCollectionManager* pTrackCollectionManager)
        : TrackSetTableModel(
                  pParent,
                  pTrackCollectionManager,
                  "mixxx.db.model.relatedtracks") {
}

void RelatedTracksTableModel::storeSearchText() {
    if (!initialized()) {
        return;
    }
    const QString searchText = currentSearch();
    const int modeKey = static_cast<int>(m_mode);
    if (searchText.trimmed().isEmpty()) {
        m_searchTexts.remove(modeKey);
    } else {
        m_searchTexts.insert(modeKey, searchText);
    }
}

void RelatedTracksTableModel::setRelationTable(const QString& tableName,
        const QString& viewQuery) {
    // A DROP and a CREATE throw away every prepared statement of the
    // connection. Write the view only when its text changes.
    if (m_viewQueries.value(tableName) != viewQuery) {
        FwdSqlQuery(m_database,
                QStringLiteral("DROP VIEW IF EXISTS %1").arg(tableName))
                .execPrepared();
        if (FwdSqlQuery(m_database, viewQuery).execPrepared()) {
            m_viewQueries.insert(tableName, viewQuery);
        } else {
            m_viewQueries.remove(tableName);
        }
    }

    QStringList columns;
    columns << LIBRARYTABLE_ID
            << LIBRARYTABLE_PREVIEW
            << LIBRARYTABLE_COVERART;
    QStringList extraColumns;
    QStringList titles;
    if (showsDecks()) {
        extraColumns << kDeckNumberColumn;
        titles << tr("Deck");
    }
    extraColumns << kRelationColumns;
    titles << tr("Relation") << tr("Relation Rating") << tr("Relation Note")
           << tr("Direction") << tr("Relations");
    const int firstExtraColumn = columns.size();
    columns << extraColumns;

    setTable(tableName,
            LIBRARYTABLE_ID,
            columns,
            m_pTrackCollectionManager->internalCollection()->getTrackSource());

    // The deck column and the relation columns belong to no ColumnCache
    // entry, thus they get their header here.
    DEBUG_ASSERT(titles.size() == extraColumns.size());
    for (int i = 0; i < titles.size(); ++i) {
        const int section = firstExtraColumn + i;
        setHeaderData(section, Qt::Horizontal, titles.at(i), Qt::DisplayRole);
        setHeaderData(section,
                Qt::Horizontal,
                extraColumns.at(i),
                TrackModel::kHeaderNameRole);
        const bool isDeckColumn = extraColumns.at(i) == kDeckNumberColumn;
        setHeaderData(section,
                Qt::Horizontal,
                isDeckColumn ? ColumnCache::defaultColumnWidth()
                             : ColumnCache::defaultColumnWidth() * 2,
                TrackModel::kHeaderWidthRole);
    }

    setSearch(m_searchTexts.value(static_cast<int>(m_mode)));
    if (showsDecks()) {
        // A deck view has one order: the deck, then the best relation. The
        // table view of the panel shows no sort indicator.
        const int deckColumn = fieldIndex(kDeckNumberColumn);
        setDefaultSort(deckColumn, Qt::AscendingOrder);
        setSort(deckColumn, Qt::AscendingOrder);
    } else {
        setDefaultSort(fieldIndex(ColumnCache::COLUMN_LIBRARYTABLE_ARTIST),
                Qt::AscendingOrder);
    }
    select();
}

void RelatedTracksTableModel::selectAllRelated() {
    storeSearchText();
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
    storeSearchText();
    m_mode = Mode::RelatedTo;
    m_referenceTrackId = trackId;

    const QString tableName = QStringLiteral("muxic_related_to");
    const QString viewQuery =
            QStringLiteral(
                    "CREATE TEMPORARY VIEW IF NOT EXISTS %1 AS SELECT %2,%3 "
                    "FROM " LIBRARY_TABLE ",%4 rel WHERE %5=0 AND (%6)")
                    .arg(tableName,
                            formatTrackColumns(),
                            formatJoinedRelationColumns(),
                            QStringLiteral(TRACK_RELATIONS_TABLE),
                            qualified(LIBRARYTABLE_MIXXXDELETED),
                            formatRelationJoin(trackId));

    setRelationTable(tableName, viewQuery);
}

QString RelatedTracksTableModel::formatSuggestionConditions(TrackId trackId) const {
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
    return conditions.join(QStringLiteral(" AND "));
}

void RelatedTracksTableModel::selectSuggestedFor(TrackId trackId) {
    storeSearchText();
    m_mode = Mode::SuggestedFor;
    m_referenceTrackId = trackId;

    const QString tableName = QStringLiteral("muxic_suggested");
    // This view reads the whole library. A count of the relations of each
    // row costs more than the rest of the query.
    const QString viewQuery =
            QStringLiteral(
                    "CREATE TEMPORARY VIEW IF NOT EXISTS %1 AS SELECT %2,%3 "
                    "FROM " LIBRARY_TABLE " WHERE %4")
                    .arg(tableName,
                            formatTrackColumns(),
                            formatEmptyRelationColumns(),
                            formatSuggestionConditions(trackId));

    setRelationTable(tableName, viewQuery);
}

QString RelatedTracksTableModel::formatRelatedToDeckBranch(
        const DeckTrack& deckTrack) const {
    return QStringLiteral(
            "SELECT %1,%2,%3 FROM " LIBRARY_TABLE ",%4 rel WHERE %5=0 AND (%6)")
            .arg(formatTrackColumns(),
                    formatDeckColumns(deckTrack.deckNumber),
                    formatJoinedRelationColumns(),
                    QStringLiteral(TRACK_RELATIONS_TABLE),
                    qualified(LIBRARYTABLE_MIXXXDELETED),
                    formatRelationJoin(deckTrack.trackId));
}

QString RelatedTracksTableModel::formatSuggestedForDeckBranch(
        const DeckTrack& deckTrack) const {
    return QStringLiteral("SELECT %1,%2,%3 FROM " LIBRARY_TABLE " WHERE %4")
            .arg(formatTrackColumns(),
                    formatDeckColumns(deckTrack.deckNumber),
                    formatEmptyRelationColumns(),
                    formatSuggestionConditions(deckTrack.trackId));
}

void RelatedTracksTableModel::selectRelatedToDecks(const DeckTrackList& deckTracks) {
    storeSearchText();
    m_mode = Mode::RelatedToDecks;
    m_referenceTrackId = TrackId();
    m_deckTracks = deckTracks;

    QStringList branches;
    for (const DeckTrack& deckTrack : std::as_const(m_deckTracks)) {
        branches.append(formatRelatedToDeckBranch(deckTrack));
    }
    if (branches.isEmpty()) {
        // Without a deck the view keeps its columns and holds no row.
        branches.append(formatRelatedToDeckBranch(DeckTrack{}));
    }

    const QString tableName = QStringLiteral("muxic_related_decks");
    const QString viewQuery =
            QStringLiteral("CREATE TEMPORARY VIEW IF NOT EXISTS %1 AS %2")
                    .arg(tableName, branches.join(QStringLiteral(" UNION ALL ")));

    setRelationTable(tableName, viewQuery);
}

void RelatedTracksTableModel::selectSuggestedForDecks(const DeckTrackList& deckTracks) {
    storeSearchText();
    m_mode = Mode::SuggestedForDecks;
    m_referenceTrackId = TrackId();
    m_deckTracks = deckTracks;

    QStringList branches;
    for (const DeckTrack& deckTrack : std::as_const(m_deckTracks)) {
        branches.append(formatSuggestedForDeckBranch(deckTrack));
    }
    if (branches.isEmpty()) {
        branches.append(formatSuggestedForDeckBranch(DeckTrack{}));
    }

    const QString tableName = QStringLiteral("muxic_suggested_decks");
    const QString viewQuery =
            QStringLiteral("CREATE TEMPORARY VIEW IF NOT EXISTS %1 AS %2")
                    .arg(tableName, branches.join(QStringLiteral(" UNION ALL ")));

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
    case Mode::RelatedToDecks:
        selectRelatedToDecks(m_deckTracks);
        return;
    case Mode::SuggestedForDecks:
        selectSuggestedForDecks(m_deckTracks);
        return;
    }
    DEBUG_ASSERT(!"unreachable");
}

void RelatedTracksTableModel::removeTracks(const QModelIndexList& indices) {
    if (indices.isEmpty()) {
        return;
    }
    // A write reports the change and the view can move its rows. Read
    // each row before the first write.
    QList<TrackId> trackIds;
    QSet<TrackId> uniqueTrackIds;
    for (const QModelIndex& index : indices) {
        const TrackId trackId = getTrackId(index);
        if (trackId.isValid() && !uniqueTrackIds.contains(trackId)) {
            uniqueTrackIds.insert(trackId);
            trackIds.append(trackId);
        }
    }
    if (trackIds.isEmpty()) {
        return;
    }

    if (showsOneRelationPerRow()) {
        QList<TrackIdPair> pairs;
        pairs.reserve(trackIds.size());
        for (const auto& trackId : std::as_const(trackIds)) {
            pairs.append(qMakePair(m_referenceTrackId, trackId));
        }
        if (storage().removeRelations(pairs) > 0) {
            removeTrackRows(uniqueTrackIds);
        }
        return;
    }
    if (m_mode == Mode::AllRelated && storage().removeAllRelationsOfTracks(trackIds) > 0) {
        removeTrackRows(uniqueTrackIds);
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
            Capability::Properties;

    // A deck view groups its rows by deck. A sort of the user would break
    // the groups, and `[Library],sort_column` belongs to the library table.
    if (!showsDecks()) {
        caps |= Capability::Sorting;
    }
    // The root node holds every relation. A Remove there would wipe the
    // table, thus only a view with one relation per row offers it.
    if (showsOneRelationPerRow()) {
        caps |= Capability::Remove;
    }
    return caps;
}

TrackModel::SortColumnId RelatedTracksTableModel::sortColumnIdFromColumnIndex(
        int column) const {
    if (showsDecks()) {
        return TrackModel::SortColumnId::Invalid;
    }
    return TrackSetTableModel::sortColumnIdFromColumnIndex(column);
}

int RelatedTracksTableModel::columnIndexFromSortColumnId(
        TrackModel::SortColumnId sortColumn) const {
    if (showsDecks()) {
        return -1;
    }
    return TrackSetTableModel::columnIndexFromSortColumnId(sortColumn);
}

bool RelatedTracksTableModel::isColumnHiddenByDefault(int column) {
    if (!showsDecks()) {
        return TrackSetTableModel::isColumnHiddenByDefault(column);
    }
    // The panel is short. It shows the deck, the relation and the columns
    // that say if two tracks mix.
    static const QList<ColumnCache::Column> kPanelColumns{
            ColumnCache::COLUMN_LIBRARYTABLE_ARTIST,
            ColumnCache::COLUMN_LIBRARYTABLE_TITLE,
            ColumnCache::COLUMN_LIBRARYTABLE_BPM,
            ColumnCache::COLUMN_LIBRARYTABLE_KEY,
            ColumnCache::COLUMN_LIBRARYTABLE_DURATION};
    if (column == fieldIndex(kDeckNumberColumn) ||
            column == fieldIndex(kRelationTypeColumn) ||
            column == fieldIndex(kRelationRatingColumn) ||
            column == fieldIndex(kRelationNotesColumn)) {
        return false;
    }
    for (const auto panelColumn : kPanelColumns) {
        if (column == fieldIndex(panelColumn)) {
            return false;
        }
    }
    return true;
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
    // The view holds the row that the edit changes. The storage reports
    // nothing and the cache of the row takes the new value.
    if (!storage().updateRelationFields(relation)) {
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
            return new RelationRatingDelegate(pTableView);
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
    // A write reports the change and the view can move its rows. Read
    // each relation before the first write.
    QSet<int> rows;
    QList<TrackRelation> relations;
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
        relations.append(relation);
    }
    return storage().saveRelations(relations);
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
    if (showsDecks() && column == fieldIndex(kDeckNumberColumn)) {
        // The deck view has one order: the decks in their own order, the
        // best relation of a deck first, then the artist.
        return QStringLiteral("%1.%2, CAST(%1.%3 AS INTEGER) DESC, %1.%4 COLLATE NOCASE")
                .arg(m_tableName,
                        kDeckNumberColumn,
                        kRelationRatingColumn,
                        kSortArtistColumn);
    }
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
    case Mode::RelatedToDecks:
        key += QStringLiteral(":decks");
        break;
    case Mode::SuggestedForDecks:
        key += QStringLiteral(":decks-suggested");
        break;
    }
    if (noSearch) {
        return key;
    }
    return key + QChar('#') + currentSearch();
}

} // namespace muxic
