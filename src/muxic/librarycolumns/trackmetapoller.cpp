#include "muxic/librarycolumns/trackmetapoller.h"

#include <QSqlError>
#include <QSqlQuery>
#include <QTimer>

#include "moc_trackmetapoller.cpp"
#include "muxic/librarycolumns/trackmeta.h"
#include "util/assert.h"
#include "util/db/dbconnectionpooled.h"
#include "util/db/dbconnectionpooler.h"
#include "util/logger.h"

namespace {

const mixxx::Logger kLogger("muxic::TrackMetaPoller");

} // namespace

namespace muxic {

TrackMetaPoller::TrackMetaPoller(mixxx::DbConnectionPoolPtr pDbConnectionPool, int pollMillis)
        : m_pDbConnectionPool(std::move(pDbConnectionPool)),
          m_pollMillis(pollMillis),
          m_pTimer(nullptr),
          m_watermark(0),
          m_dataVersion(-1) {
}

TrackMetaPoller::~TrackMetaPoller() = default;

void TrackMetaPoller::start() {
    VERIFY_OR_DEBUG_ASSERT(!m_database.isOpen()) {
        return;
    }
    m_pooler.emplace(m_pDbConnectionPool);
    m_database = mixxx::DbConnectionPooled(m_pDbConnectionPool);
    if (!m_database.isOpen()) {
        kLogger.warning() << "Failed to open a database connection for the poll";
        m_pooler.reset();
        return;
    }

    QSqlQuery query(m_database);
    // Give up at once when the hub holds the write lock. The next tick tries
    // again, thus a long transaction of the hub costs no user interface time.
    query.exec(QStringLiteral("PRAGMA busy_timeout=%1").arg(kBusyTimeoutMillis));

    readWatermark();
    databaseChanged();

    if (m_pollMillis <= 0) {
        return;
    }
    m_pTimer = new QTimer(this);
    connect(m_pTimer, &QTimer::timeout, this, &TrackMetaPoller::pollIfChanged);
    m_pTimer->start(m_pollMillis);
}

void TrackMetaPoller::stop() {
    if (m_pTimer) {
        m_pTimer->stop();
        delete m_pTimer;
        m_pTimer = nullptr;
    }
    m_database = QSqlDatabase();
    m_pooler.reset();
}

void TrackMetaPoller::readWatermark() {
    QSqlQuery query(m_database);
    query.prepare(QStringLiteral("SELECT MAX(updated_at) FROM %1").arg(kTrackMetaTable));
    if (!query.exec() || !query.next()) {
        return;
    }
    m_watermark = query.value(0).toLongLong();

    // The library cache read these rows with the rest of the row already. Put
    // them into the set, so that the first poll reports nothing.
    query.prepare(QStringLiteral("SELECT %1 FROM %2 WHERE updated_at=:stamp")
                          .arg(kTrackMetaTrackId, kTrackMetaTable));
    query.bindValue(QStringLiteral(":stamp"), m_watermark);
    if (!query.exec()) {
        return;
    }
    while (query.next()) {
        m_reportedAtWatermark.insert(TrackId(query.value(0)));
    }
}

bool TrackMetaPoller::databaseChanged() {
    QSqlQuery query(m_database);
    if (!query.exec(QStringLiteral("PRAGMA data_version")) || !query.next()) {
        return false;
    }
    const int dataVersion = query.value(0).toInt();
    if (dataVersion == m_dataVersion) {
        return false;
    }
    m_dataVersion = dataVersion;
    return true;
}

void TrackMetaPoller::pollIfChanged() {
    // Near zero cost while the hub is idle: the counter only moves when
    // another connection commits.
    if (databaseChanged()) {
        poll();
    }
}

void TrackMetaPoller::poll() {
    if (!m_database.isOpen()) {
        return;
    }
    QSqlQuery query(m_database);
    query.setForwardOnly(true);
    query.prepare(QStringLiteral(
            "SELECT %1, updated_at FROM %2 "
            "WHERE updated_at >= :since ORDER BY updated_at")
                          .arg(kTrackMetaTrackId, kTrackMetaTable));
    query.bindValue(QStringLiteral(":since"), m_watermark);
    if (!query.exec()) {
        // A busy database is normal while the hub commits. Read the counter
        // again at the next tick.
        kLogger.debug() << "Poll skipped:" << query.lastError().text();
        m_dataVersion = -1;
        return;
    }

    qint64 newWatermark = m_watermark;
    QSet<TrackId> newAtWatermark;
    QSet<TrackId> chunk;
    while (query.next()) {
        const TrackId trackId(query.value(0));
        const qint64 updatedAt = query.value(1).toLongLong();
        if (updatedAt > newWatermark) {
            newWatermark = updatedAt;
            newAtWatermark.clear();
        }
        if (updatedAt == newWatermark) {
            newAtWatermark.insert(trackId);
        }
        if (updatedAt == m_watermark && m_reportedAtWatermark.contains(trackId)) {
            continue;
        }
        chunk.insert(trackId);
        if (chunk.size() >= kReportChunkSize) {
            emit tracksChanged(chunk);
            chunk.clear();
        }
    }
    if (!chunk.isEmpty()) {
        emit tracksChanged(chunk);
    }

    if (newWatermark > m_watermark) {
        m_watermark = newWatermark;
        m_reportedAtWatermark = newAtWatermark;
    } else {
        m_reportedAtWatermark.unite(newAtWatermark);
    }
}

} // namespace muxic
