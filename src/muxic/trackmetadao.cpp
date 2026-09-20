#include "muxic/trackmetadao.h"

#include <QDateTime>
#include <QSqlQuery>
#include <QTimer>
#include <algorithm>

#include "library/queryutil.h"
#include "moc_trackmetadao.cpp"
#include "util/assert.h"
#include "util/math.h"

namespace {

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

} // namespace

namespace muxic {

TrackMetaDao::TrackMetaDao(QObject* parent)
        : QObject(parent),
          m_pPollTimer(nullptr),
          m_lastUpdatedAt(0) {
}

TrackMetaDao::~TrackMetaDao() {
    if (s_pInstance == this) {
        s_pInstance = nullptr;
    }
}

// static
TrackMetaDao* TrackMetaDao::instance() {
    return s_pInstance;
}

void TrackMetaDao::initialize(const QSqlDatabase& database, int pollMillis) {
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

    // The library cache reads the table with the rest of the row, thus only
    // rows that change after this moment need a report.
    if (query.exec(QStringLiteral("SELECT MAX(updated_at) FROM muxic_track_meta")) &&
            query.next()) {
        m_lastUpdatedAt = query.value(0).toLongLong();
    }

    if (pollMillis > 0 && !m_pPollTimer) {
        m_pPollTimer = new QTimer(this);
        connect(m_pPollTimer, &QTimer::timeout, this, &TrackMetaDao::reloadChanged);
        m_pPollTimer->start(pollMillis);
    }
}

void TrackMetaDao::finish() {
    if (m_pPollTimer) {
        m_pPollTimer->stop();
    }
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

    m_lastUpdatedAt = math_max(m_lastUpdatedAt, updatedAt);
    emit tracksChanged(QSet<TrackId>{trackId});
    return true;
}

void TrackMetaDao::purgeTracks(const QSet<TrackId>& trackIds) {
    if (trackIds.isEmpty() || !m_database.isOpen()) {
        return;
    }
    QStringList idStrings;
    idStrings.reserve(trackIds.size());
    for (const auto& trackId : trackIds) {
        idStrings << trackId.toString();
    }
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("DELETE FROM muxic_track_meta WHERE track_id IN (%1)")
                          .arg(idStrings.join(QChar(','))));
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
    }
}

void TrackMetaDao::reloadChanged() {
    if (!m_database.isOpen()) {
        return;
    }
    QSqlQuery query(m_database);
    query.setForwardOnly(true);
    query.prepare(QStringLiteral(
            "SELECT track_id, updated_at FROM muxic_track_meta "
            "WHERE updated_at > :since"));
    query.bindValue(QStringLiteral(":since"), m_lastUpdatedAt);
    if (!query.exec()) {
        LOG_FAILED_QUERY(query);
        return;
    }
    QSet<TrackId> trackIds;
    while (query.next()) {
        trackIds.insert(TrackId(query.value(0)));
        m_lastUpdatedAt = math_max(m_lastUpdatedAt, query.value(1).toLongLong());
    }
    if (!trackIds.isEmpty()) {
        emit tracksChanged(trackIds);
    }
}

} // namespace muxic
