#include <gtest/gtest.h>

#include <QDateTime>
#include <QSqlQuery>
#include <QVariant>

#include "library/trackcollection.h"
#include "muxic/librarycolumns.h"
#include "muxic/trackmeta.h"
#include "muxic/trackmetadao.h"
#include "test/librarytest.h"
#include "track/track.h"
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

    dao().purgeTracks(QSet<TrackId>{trackId});
    EXPECT_EQ(0, countRows());
    EXPECT_FALSE(dao().read(trackId).energy.has_value());
}

TEST_F(MuxicTrackMetaTest, ReloadReportsTheChangedRows) {
    const TrackId trackId = addTrack(QStringLiteral("-png.mp3"));
    ASSERT_TRUE(trackId.isValid());

    QSet<TrackId> reported;
    QObject::connect(&dao(),
            &muxic::TrackMetaDao::tracksChanged,
            [&reported](const QSet<TrackId>& trackIds) {
                reported.unite(trackIds);
            });

    // A write from outside of Mixxx, as the hub does it.
    QSqlQuery query(internalCollection()->database());
    query.prepare(QStringLiteral(
            "INSERT INTO muxic_track_meta (track_id, muxic_energy, updated_at) "
            "VALUES (:id, 8, :updated)"));
    query.bindValue(QStringLiteral(":id"), trackId.toVariant());
    query.bindValue(QStringLiteral(":updated"), QDateTime::currentMSecsSinceEpoch());
    ASSERT_TRUE(query.exec());

    dao().reloadChanged();
    EXPECT_EQ(QSet<TrackId>{trackId}, reported);

    // A second call reports nothing, because nothing changed again.
    reported.clear();
    dao().reloadChanged();
    EXPECT_TRUE(reported.isEmpty());
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
