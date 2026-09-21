#include <gtest/gtest.h>

#include <QDateTime>
#include <QSqlQuery>
#include <QThread>
#include <QVariant>

#include "library/relocatedtrack.h"
#include "library/trackcollection.h"
#include "muxic/librarycolumns.h"
#include "muxic/trackmeta.h"
#include "muxic/trackmetadao.h"
#include "muxic/trackmetapoller.h"
#include "test/librarytest.h"
#include "track/track.h"
#include "track/trackref.h"
#include "util/math.h"

namespace {

class MuxicTrackMetaTest : public LibraryTest {
  protected:
    muxic::TrackMetaDao& dao() const {
        return internalCollection()->getMuxicTrackMetaDAO();
    }

    TrackId addTrack(QStringView type) {
        const TrackPointer pTrack = getOrAddTrackByLocation(getTestFile(type));
        return pTrack ? pTrack->getId() : TrackId();
    }

    /// A write as the muxic hub does it, with an explicit stamp.
    void hubWrite(TrackId trackId, int energy, qint64 updatedAt) const {
        QSqlQuery query(internalCollection()->database());
        query.prepare(QStringLiteral(
                "INSERT OR REPLACE INTO muxic_track_meta "
                "(track_id, muxic_energy, updated_at) VALUES (:id, :energy, :updated)"));
        query.bindValue(QStringLiteral(":id"), trackId.toVariant());
        query.bindValue(QStringLiteral(":energy"), energy);
        query.bindValue(QStringLiteral(":updated"), updatedAt);
        EXPECT_TRUE(query.exec());
    }

    int countRows() const {
        QSqlQuery query(internalCollection()->database());
        EXPECT_TRUE(query.exec(QStringLiteral(
                "SELECT COUNT(*) FROM muxic_track_meta")));
        EXPECT_TRUE(query.next());
        return query.value(0).toInt();
    }
};

TEST_F(MuxicTrackMetaTest, CreateTableIsIdempotent) {
    // The track collection created the table already. A second call must not
    // fail and must not lose a row.
    const TrackId trackId = addTrack(QStringLiteral("-png.mp3"));
    ASSERT_TRUE(trackId.isValid());
    EXPECT_TRUE(dao().setEnergy(trackId, 7));

    dao().initialize(internalCollection()->database());
    EXPECT_EQ(1, countRows());
    EXPECT_EQ(7, dao().read(trackId).energy.value_or(0));
}

TEST_F(MuxicTrackMetaTest, MissingRowIsEmpty) {
    const TrackId trackId = addTrack(QStringLiteral("-png.mp3"));
    ASSERT_TRUE(trackId.isValid());

    const muxic::TrackMeta meta = dao().read(trackId);
    EXPECT_FALSE(meta.energy.has_value());
    EXPECT_FALSE(meta.danceability.has_value());
    EXPECT_TRUE(meta.tags.isEmpty());
}

TEST_F(MuxicTrackMetaTest, UpsertKeepsTheOtherColumns) {
    const TrackId trackId = addTrack(QStringLiteral("-png.mp3"));
    ASSERT_TRUE(trackId.isValid());

    EXPECT_TRUE(dao().setEnergy(trackId, 4));
    EXPECT_TRUE(dao().setTags(trackId, {QStringLiteral("vocal")}));
    EXPECT_TRUE(dao().setDanceability(trackId, 0.75));
    EXPECT_EQ(1, countRows());

    muxic::TrackMeta meta = dao().read(trackId);
    EXPECT_EQ(4, meta.energy.value_or(0));
    EXPECT_DOUBLE_EQ(0.75, meta.danceability.value_or(0.0));
    EXPECT_EQ(QStringList{QStringLiteral("vocal")}, meta.tags);

    // A second write of one column replaces only that column.
    EXPECT_TRUE(dao().setEnergy(trackId, 9));
    meta = dao().read(trackId);
    EXPECT_EQ(9, meta.energy.value_or(0));
    EXPECT_EQ(QStringList{QStringLiteral("vocal")}, meta.tags);

    // An empty value clears the column but keeps the row.
    EXPECT_TRUE(dao().setEnergy(trackId, std::nullopt));
    meta = dao().read(trackId);
    EXPECT_FALSE(meta.energy.has_value());
    EXPECT_EQ(1, countRows());
}

TEST_F(MuxicTrackMetaTest, ValuesOutOfRangeAreClamped) {
    const TrackId trackId = addTrack(QStringLiteral("-png.mp3"));
    ASSERT_TRUE(trackId.isValid());

    EXPECT_TRUE(dao().setEnergy(trackId, 42));
    EXPECT_EQ(muxic::kEnergyMax, dao().read(trackId).energy.value_or(0));
    EXPECT_TRUE(dao().setDanceability(trackId, -1.0));
    EXPECT_DOUBLE_EQ(muxic::kDanceabilityMin,
            dao().read(trackId).danceability.value_or(-99.0));
}

TEST_F(MuxicTrackMetaTest, PurgeRemovesTheRow) {
    const TrackId trackId = addTrack(QStringLiteral("-png.mp3"));
    ASSERT_TRUE(trackId.isValid());
    EXPECT_TRUE(dao().setEnergy(trackId, 5));
    EXPECT_EQ(1, countRows());

    trackCollectionManager()->purgeTracks(
            QList<TrackRef>{TrackRef::fromFilePath(getTestFile(QStringLiteral("-png.mp3")), trackId)});
    EXPECT_EQ(0, countRows());
}

TEST_F(MuxicTrackMetaTest, HideKeepsTheRow) {
    // Hiding a track keeps its library row, thus the values must stay. The
    // Hidden view shows them.
    const TrackId trackId = addTrack(QStringLiteral("-png.mp3"));
    ASSERT_TRUE(trackId.isValid());
    EXPECT_TRUE(dao().setEnergy(trackId, 5));
    EXPECT_TRUE(dao().setTags(trackId, {QStringLiteral("vocal")}));

    ASSERT_TRUE(trackCollectionManager()->hideTracks(QList<TrackId>{trackId}));

    EXPECT_EQ(1, countRows());
    const muxic::TrackMeta meta = dao().read(trackId);
    EXPECT_EQ(5, meta.energy.value_or(0));
    EXPECT_EQ(QStringList{QStringLiteral("vocal")}, meta.tags);
}

TEST_F(MuxicTrackMetaTest, RelocateCarriesTheRowToTheSurvivor) {
    const TrackId removedTrackId = addTrack(QStringLiteral("-png.mp3"));
    const TrackId keptTrackId = addTrack(QStringLiteral("-jpg.mp3"));
    ASSERT_TRUE(removedTrackId.isValid());
    ASSERT_TRUE(keptTrackId.isValid());
    EXPECT_TRUE(dao().setEnergy(removedTrackId, 6));
    EXPECT_TRUE(dao().setTags(removedTrackId, {QStringLiteral("bass")}));

    // A merge keeps the id of the track that was missing and removes the id of
    // the track that the scan added.
    const QList<RelocatedTrack> relocated{
            RelocatedTrack(TrackRef::fromFilePath(QStringLiteral("/old/a.mp3"), keptTrackId),
                    TrackRef::fromFilePath(QStringLiteral("/new/a.mp3"), removedTrackId))};
    dao().relocateTracks(relocated);

    EXPECT_EQ(1, countRows());
    const muxic::TrackMeta meta = dao().read(keptTrackId);
    EXPECT_EQ(6, meta.energy.value_or(0));
    EXPECT_EQ(QStringList{QStringLiteral("bass")}, meta.tags);
}

TEST_F(MuxicTrackMetaTest, RelocateKeepsTheValuesOfTheSurvivor) {
    const TrackId removedTrackId = addTrack(QStringLiteral("-png.mp3"));
    const TrackId keptTrackId = addTrack(QStringLiteral("-jpg.mp3"));
    ASSERT_TRUE(removedTrackId.isValid());
    ASSERT_TRUE(keptTrackId.isValid());
    EXPECT_TRUE(dao().setEnergy(removedTrackId, 6));
    EXPECT_TRUE(dao().setEnergy(keptTrackId, 2));

    const QList<RelocatedTrack> relocated{
            RelocatedTrack(TrackRef::fromFilePath(QStringLiteral("/old/a.mp3"), keptTrackId),
                    TrackRef::fromFilePath(QStringLiteral("/new/a.mp3"), removedTrackId))};
    dao().relocateTracks(relocated);

    EXPECT_EQ(1, countRows());
    EXPECT_EQ(2, dao().read(keptTrackId).energy.value_or(0));
}

TEST_F(MuxicTrackMetaTest, OrphanedRowsGoAway) {
    const TrackId trackId = addTrack(QStringLiteral("-png.mp3"));
    ASSERT_TRUE(trackId.isValid());
    EXPECT_TRUE(dao().setEnergy(trackId, 5));

    // The hub purged a track while Mixxx was closed. SQLite does not enforce
    // the REFERENCES clause, thus the row stays behind.
    QSqlQuery query(internalCollection()->database());
    query.prepare(QStringLiteral(
            "INSERT INTO muxic_track_meta (track_id, muxic_energy, updated_at) "
            "VALUES (987654, 3, 1)"));
    ASSERT_TRUE(query.exec());
    EXPECT_EQ(2, countRows());

    // Opening the database removes them.
    dao().initialize(internalCollection()->database());
    EXPECT_EQ(1, countRows());
    EXPECT_EQ(5, dao().read(trackId).energy.value_or(0));
}

TEST_F(MuxicTrackMetaTest, PollReportsRowsThatShareOneStamp) {
    const TrackId trackA = addTrack(QStringLiteral("-png.mp3"));
    const TrackId trackB = addTrack(QStringLiteral("-jpg.mp3"));
    ASSERT_TRUE(trackA.isValid());
    ASSERT_TRUE(trackB.isValid());

    // The poll has a thread and a connection of its own, as it does in Mixxx.
    QThread pollThread;
    muxic::TrackMetaPoller poller(dbConnectionPooler(), 0);
    poller.moveToThread(&pollThread);
    pollThread.start();
    QSet<TrackId> reported;
    QObject::connect(&poller,
            &muxic::TrackMetaPoller::tracksChanged,
            [&reported](const QSet<TrackId>& trackIds) {
                reported.unite(trackIds);
            });
    const auto callPoller = [&poller](const char* method) {
        QMetaObject::invokeMethod(&poller, method, Qt::BlockingQueuedConnection);
    };
    callPoller("start");

    // The hub commits row by row. Both rows carry the same millisecond, and a
    // poll runs between the two commits.
    const qint64 stamp = QDateTime::currentMSecsSinceEpoch();
    hubWrite(trackA, 4, stamp);
    callPoller("poll");
    EXPECT_EQ(QSet<TrackId>{trackA}, reported);

    reported.clear();
    hubWrite(trackB, 5, stamp);
    callPoller("poll");
    EXPECT_EQ(QSet<TrackId>{trackB}, reported);

    // A poll with no new row reports nothing.
    reported.clear();
    callPoller("poll");
    EXPECT_TRUE(reported.isEmpty());

    callPoller("stop");
    pollThread.quit();
    pollThread.wait();
}

TEST(MuxicTagsTest, NormalizeAndEncode) {
    EXPECT_EQ(QStringList({QStringLiteral("bass"), QStringLiteral("two words")}),
            muxic::parseTags(QStringLiteral(" Bass , Two   Words ,, ")));
    // A repeat is dropped, the order of the first use stays.
    EXPECT_EQ(QStringList({QStringLiteral("a"), QStringLiteral("b")}),
            muxic::parseTags(QStringLiteral("a,b,A")));
    EXPECT_EQ(QString(), muxic::encodeTags({}));
    EXPECT_EQ(QStringLiteral(",bass,vocal,"),
            muxic::encodeTags({QStringLiteral("bass"), QStringLiteral("vocal")}));
    EXPECT_EQ(QStringLiteral("bass, vocal"),
            muxic::formatTags(QStringLiteral(",bass,vocal,")));
    EXPECT_EQ(QStringList({QStringLiteral("bass"), QStringLiteral("vocal")}),
            muxic::decodeTags(QStringLiteral(",bass,vocal,")));
}

TEST(MuxicTagsTest, LikePatternMatchesWholeTagsOnly) {
    // The commas of the stored form make a tag that starts with another tag
    // no match.
    EXPECT_EQ(QStringLiteral("%,voc,%"), muxic::tagLikePattern(QStringLiteral("voc")));
    // The wildcards of LIKE lose their meaning inside a tag.
    EXPECT_EQ(QStringLiteral("%,a\\_b,%"), muxic::tagLikePattern(QStringLiteral("a_b")));
    EXPECT_EQ(QStringLiteral("%,a\\%b,%"), muxic::tagLikePattern(QStringLiteral("a%b")));
}

TEST(MuxicLufsTest, MatchesTheReplayGainReference) {
    // AnalyzerEbur128 stores db2ratio(kReplayGain2ReferenceLUFS - lufs).
    // The column reverses that.
    for (const double lufs : {-24.0, -18.0, -14.0, -8.5, -3.0}) {
        const double ratio = db2ratio(muxic::kReplayGainReferenceLufs - lufs);
        EXPECT_NEAR(lufs, muxic::lufsFromReplayGainRatio(ratio), 1e-9);
    }
    // A gain of 0 dB is the reference level.
    EXPECT_NEAR(muxic::kReplayGainReferenceLufs,
            muxic::lufsFromReplayGainRatio(1.0),
            1e-9);
}

TEST(MuxicLufsTest, InvalidRatioGivesAnEmptyCell) {
    EXPECT_FALSE(muxic::lufsFromReplayGainRatio(QVariant()).isValid());
    EXPECT_FALSE(muxic::lufsFromReplayGainRatio(QVariant(0.0)).isValid());
    EXPECT_FALSE(muxic::lufsFromReplayGainRatio(QVariant(-1.0)).isValid());
    EXPECT_TRUE(muxic::lufsFromReplayGainRatio(QVariant(1.0)).isValid());
}

TEST(MuxicColumnsTest, DisplayValues) {
    EXPECT_EQ(QStringLiteral("7"),
            muxic::displayValue(ColumnCache::COLUMN_MUXIC_ENERGY, QVariant(7))
                    .toString());
    EXPECT_FALSE(muxic::displayValue(
            ColumnCache::COLUMN_MUXIC_ENERGY, QVariant())
                         .isValid());
    EXPECT_EQ(QStringLiteral("0.75"),
            muxic::displayValue(
                    ColumnCache::COLUMN_MUXIC_DANCEABILITY, QVariant(0.75))
                    .toString());
    EXPECT_EQ(QStringLiteral("-8.3"),
            muxic::displayValue(ColumnCache::COLUMN_MUXIC_LUFS, QVariant(-8.26))
                    .toString());
    EXPECT_EQ(QStringLiteral("bass, vocal"),
            muxic::displayValue(ColumnCache::COLUMN_MUXIC_TAGS,
                    QVariant(QStringLiteral(",bass,vocal,")))
                    .toString());
}

} // namespace
