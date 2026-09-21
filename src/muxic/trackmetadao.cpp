#include "muxic/trackmetadao.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QThread>
#include <algorithm>

#include "library/queryutil.h"
#include "library/relocatedtrack.h"
#include "moc_trackmetadao.cpp"
#include "muxic/trackmetapoller.h"
#include "util/assert.h"
#include "util/logger.h"

namespace {

const mixxx::Logger kLogger("muxic::TrackMetaDao");

muxic::TrackMetaDao* s_pInstance = nullptr;

const QString kCreateTable = QStringLiteral(
        "CREATE TABLE IF NOT EXISTS muxic_track_meta ("
        "track_id INTEGER PRIMARY KEY REFERENCES library(id),"
        "muxic_energy INTEGER,"
        "muxic_danceability REAL,"
        "muxic_tags TEXT,"
        "updated_at INTEGER NOT NULL DEFAULT 0)");

const QString kCreateIndex = QStringLiteral(
        "CREATE INDEX IF NOT EXISTS muxic_track_meta_updated_at "
        "ON muxic_track_meta (updated_at)");

const QStringList kExpectedColumns = {
        QStringLiteral("track_id"),
        QStringLiteral("muxic_energy"),
        QStringLiteral("muxic_danceability"),
        QStringLiteral("muxic_tags"),
        QStringLiteral("updated_at")};

} // namespace

namespace muxic {

TrackMetaDao::TrackMetaDao(QObject* parent)
        : QObject(parent),
          m_pollMillis(0),
          m_pPollThread(nullptr),
          m_pPoller(nullptr) {
}

TrackMetaDao::~TrackMetaDao() {
    stopPolling();
    if (s_pInstance == this) {
        s_pInstance = nullptr;
    }
}

// static
TrackMetaDao* TrackMetaDao::instance() {
    return s_pInstance;
}

void TrackMetaDao::initialize(const QSqlDatabase& database) {
    m_database = database;
    // The track properties dialog has no path to the track collection, thus
    // the object that holds the open database makes itself reachable.
    s_pInstance = this;

    QSqlQuery query(m_database);
    if (!query.exec(kCreateTable)) {
        LOG_FAILED_QUERY(query);
        return;
    }
    if (!query.exec(kCreateIndex)) {
        LOG_FAILED_QUERY(query);
    }
    warnAboutForeignTableShape();

    const int orphans = deleteOrphanedRows();
    if (orphans > 0) {
        kLogger.info() << "Removed" << orphans << "rows of tracks that left the library";
    }

    // A database that opens a second time gets the poll back.
    if (m_pDbConnectionPool && !m_pPollThread) {
        startPolling(m_pDbConnectionPool, m_pollMillis);
    }
}

void TrackMetaDao::warnAboutForeignTableShape() {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("PRAGMA table_info(%1)").arg(kTrackMetaTable));
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return;
    }
    QStringList columns;
    bool hasPrimaryKey = false;
    while (query.next()) {
        columns.append(query.value(QStringLiteral("name")).toString());
        if (query.value(QStringLiteral("name")).toString() == kTrackMetaTrackId &&
                query.value(QStringLiteral("pk")).toInt() > 0) {
            hasPrimaryKey = true;
        }
    }
    for (const QString& expected : kExpectedColumns) {
        if (!columns.contains(expected)) {
            kLogger.warning() << "Table" << kTrackMetaTable << "has no column"
                              << expected << "- the columns stay empty";
        }
    }
    if (!hasPrimaryKey) {
        kLogger.warning() << "Table" << kTrackMetaTable << "has no primary key on"
                          << kTrackMetaTrackId
                          << "- a second row for one track repeats the track "
                             "in every library view";
    }
}

int TrackMetaDao::deleteOrphanedRows() {
    if (!m_database.isOpen()) {
        return 0;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
            "DELETE FROM %1 WHERE %2 NOT IN (SELECT id FROM library)")
                          .arg(kTrackMetaTable, kTrackMetaTrackId));
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return 0;
    }
    return query.numRowsAffected();
}

void TrackMetaDao::startPolling(mixxx::DbConnectionPoolPtr pDbConnectionPool, int pollMillis) {
    VERIFY_OR_DEBUG_ASSERT(!m_pPollThread) {
        return;
    }
    if (!pDbConnectionPool || pollMillis <= 0) {
        return;
    }
    m_pDbConnectionPool = pDbConnectionPool;
    m_pollMillis = pollMillis;
    m_pPoller = new TrackMetaPoller(std::move(pDbConnectionPool), pollMillis);
    m_pPollThread = new QThread(this);
    m_pPollThread->setObjectName(QStringLiteral("muxic meta poll"));
    m_pPoller->moveToThread(m_pPollThread);
    connect(m_pPollThread, &QThread::started, m_pPoller, &TrackMetaPoller::start);
    connect(m_pPollThread, &QThread::finished, m_pPoller, &QObject::deleteLater);
    connect(m_pPoller,
            &TrackMetaPoller::tracksChanged,
            this,
            &TrackMetaDao::tracksChanged);
    m_pPollThread->start();
}

void TrackMetaDao::stopPolling() {
    if (!m_pPollThread) {
        return;
    }
    QMetaObject::invokeMethod(m_pPoller, "stop", Qt::BlockingQueuedConnection);
    m_pPollThread->quit();
    m_pPollThread->wait();
    delete m_pPollThread;
    m_pPollThread = nullptr;
    m_pPoller = nullptr;
}

void TrackMetaDao::finish() {
    stopPolling();
    if (s_pInstance == this) {
        s_pInstance = nullptr;
    }
    m_database = QSqlDatabase();
}

TrackMeta TrackMetaDao::read(TrackId trackId) const {
    TrackMeta meta;
    VERIFY_OR_DEBUG_ASSERT(trackId.isValid()) {
        return meta;
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
            "SELECT muxic_energy, muxic_danceability, muxic_tags "
            "FROM muxic_track_meta WHERE track_id=:id"));
    query.bindValue(QStringLiteral(":id"), trackId.toVariant());
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return meta;
    }
    if (!query.next()) {
        return meta;
    }
    const QVariant energy = query.value(0);
    if (!energy.isNull()) {
        meta.energy = energy.toInt();
    }
    const QVariant danceability = query.value(1);
    if (!danceability.isNull()) {
        meta.danceability = danceability.toDouble();
    }
    meta.tags = decodeTags(query.value(2).toString());
    return meta;
}

bool TrackMetaDao::setEnergy(TrackId trackId, std::optional<int> energy) {
    QVariant value;
    if (energy) {
        value = QVariant(std::clamp(*energy, kEnergyMin, kEnergyMax));
    }
    return write(trackId, kColumnEnergy, value);
}

bool TrackMetaDao::setDanceability(TrackId trackId, std::optional<double> danceability) {
    QVariant value;
    if (danceability) {
        value = QVariant(std::clamp(*danceability, kDanceabilityMin, kDanceabilityMax));
    }
    return write(trackId, kColumnDanceability, value);
}

bool TrackMetaDao::setTags(TrackId trackId, const QStringList& tags) {
    const QString stored = encodeTags(tags);
    return write(trackId, kColumnTags, stored.isEmpty() ? QVariant() : QVariant(stored));
}

bool TrackMetaDao::write(TrackId trackId, const QString& column, const QVariant& value) {
    VERIFY_OR_DEBUG_ASSERT(trackId.isValid()) {
        return false;
    }
    const qint64 updatedAt = QDateTime::currentMSecsSinceEpoch();

    ScopedTransaction transaction(m_database);
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral(
            "INSERT OR IGNORE INTO muxic_track_meta (track_id) VALUES (:id)"));
    query.bindValue(QStringLiteral(":id"), trackId.toVariant());
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return false;
    }
    query.prepare(QStringLiteral(
            "UPDATE muxic_track_meta SET %1=:value, "
            "updated_at=:updated WHERE track_id=:id")
                          .arg(column));
    query.bindValue(QStringLiteral(":value"), value);
    query.bindValue(QStringLiteral(":updated"), updatedAt);
    query.bindValue(QStringLiteral(":id"), trackId.toVariant());
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return false;
    }
    transaction.commit();

    emit tracksChanged(QSet<TrackId>{trackId});
    return true;
}

bool TrackMetaDao::onPurgingTracks(const QList<TrackId>& trackIds) {
    if (trackIds.isEmpty() || !m_database.isOpen()) {
        return true;
    }
    QSqlQuery query(m_database);
    if (!query.prepare(QStringLiteral("DELETE FROM %1 WHERE %2=:trackId")
                               .arg(kTrackMetaTable, kTrackMetaTrackId))) {
        LOG_FAILED_QUERY(query);
        return false;
    }
    for (const auto& trackId : trackIds) {
        query.bindValue(QStringLiteral(":trackId"), trackId.toVariant());
        if (!query.exec()) {
            LOG_FAILED_QUERY(query);
            return false;
        }
    }
    return true;
}

void TrackMetaDao::relocateTracks(const QList<RelocatedTrack>& relocatedTracks) {
    if (relocatedTracks.isEmpty() || !m_database.isOpen()) {
        return;
    }
    QSet<TrackId> changedTrackIds;
    for (const auto& relocatedTrack : relocatedTracks) {
        const TrackId removedTrackId = relocatedTrack.deletedTrackId();
        const TrackId keptTrackId = relocatedTrack.updatedTrackRef().getId();
        if (!removedTrackId.isValid() || !keptTrackId.isValid() ||
                removedTrackId == keptTrackId) {
            continue;
        }
        // Give the row of the track that goes away to the track that stays,
        // but never overwrite values that the track that stays already has.
        QSqlQuery query(m_database);
        query.prepare(QStringLiteral(
                "UPDATE OR IGNORE %1 SET %2=:keptId WHERE %2=:removedId "
                "AND :keptId NOT IN (SELECT %2 FROM %1)")
                              .arg(kTrackMetaTable, kTrackMetaTrackId));
        query.bindValue(QStringLiteral(":keptId"), keptTrackId.toVariant());
        query.bindValue(QStringLiteral(":removedId"), removedTrackId.toVariant());
        if (!query.exec()) {
            LOG_FAILED_QUERY(query);
            continue;
        }
        if (query.numRowsAffected() > 0) {
            changedTrackIds.insert(keptTrackId);
        }
        // Whatever is left belongs to an id that the merge removed.
        query.prepare(QStringLiteral("DELETE FROM %1 WHERE %2=:removedId")
                              .arg(kTrackMetaTable, kTrackMetaTrackId));
        query.bindValue(QStringLiteral(":removedId"), removedTrackId.toVariant());
        if (!query.exec()) {
            LOG_FAILED_QUERY(query);
        }
    }
    if (!changedTrackIds.isEmpty()) {
        emit tracksChanged(changedTrackIds);
    }
}

} // namespace muxic
