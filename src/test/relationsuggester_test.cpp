#include "muxic/relatedtracks/relationsuggester.h"

#include <gtest/gtest.h>

#include <QSet>

#include "track/keyutils.h"

using mixxx::track::io::key::ChromaticKey;
using muxic::RelationSuggester;

namespace {

constexpr int kNumKeys = 24;

ChromaticKey keyAt(int index) {
    // The key values run from C_MAJOR (1) to B_MINOR (24).
    return static_cast<ChromaticKey>(index + 1);
}

} // anonymous namespace

TEST(RelationSuggesterTest, BpmRangesHoldHalfAndDoubleTime) {
    const QList<RelationSuggester::BpmRange> ranges = RelationSuggester::bpmRanges(128.0);
    ASSERT_EQ(3, static_cast<int>(ranges.size()));
    EXPECT_NEAR(64.0 * 0.97, ranges.at(0).lower, 1e-9);
    EXPECT_NEAR(64.0 * 1.03, ranges.at(0).upper, 1e-9);
    EXPECT_NEAR(128.0 * 0.97, ranges.at(1).lower, 1e-9);
    EXPECT_NEAR(128.0 * 1.03, ranges.at(1).upper, 1e-9);
    EXPECT_NEAR(256.0 * 0.97, ranges.at(2).lower, 1e-9);
    EXPECT_NEAR(256.0 * 1.03, ranges.at(2).upper, 1e-9);
}

TEST(RelationSuggesterTest, BpmRangesOfAnUnknownTempoAreEmpty) {
    EXPECT_TRUE(RelationSuggester::bpmRanges(0.0).isEmpty());
    EXPECT_TRUE(RelationSuggester::bpmRanges(-1.0).isEmpty());
}

TEST(RelationSuggesterTest, MatchesBpmWithinThreePercent) {
    EXPECT_TRUE(RelationSuggester::matchesBpm(128.0, 128.0));
    EXPECT_TRUE(RelationSuggester::matchesBpm(131.8, 128.0));
    EXPECT_TRUE(RelationSuggester::matchesBpm(124.2, 128.0));
    EXPECT_FALSE(RelationSuggester::matchesBpm(133.0, 128.0));
    EXPECT_FALSE(RelationSuggester::matchesBpm(123.0, 128.0));
}

TEST(RelationSuggesterTest, MatchesBpmAtHalfAndDoubleTime) {
    EXPECT_TRUE(RelationSuggester::matchesBpm(64.0, 128.0));
    EXPECT_TRUE(RelationSuggester::matchesBpm(256.0, 128.0));
    EXPECT_TRUE(RelationSuggester::matchesBpm(174.0, 87.0));
    EXPECT_FALSE(RelationSuggester::matchesBpm(96.0, 128.0));
}

TEST(RelationSuggesterTest, MatchesBpmRefusesAnUnknownTempo) {
    EXPECT_FALSE(RelationSuggester::matchesBpm(0.0, 128.0));
    EXPECT_FALSE(RelationSuggester::matchesBpm(128.0, 0.0));
}

TEST(RelationSuggesterTest, CompatibleKeysOfAnUnknownKeyAreEmpty) {
    EXPECT_TRUE(RelationSuggester::compatibleKeys(
            mixxx::track::io::key::INVALID)
                        .isEmpty());
}

TEST(RelationSuggesterTest, EveryKeyHasSixCamelotNeighbours) {
    for (int i = 0; i < kNumKeys; ++i) {
        const ChromaticKey key = keyAt(i);
        const QList<ChromaticKey> keys = RelationSuggester::compatibleKeys(key);
        QSet<int> uniqueKeys;
        for (const auto compatibleKey : keys) {
            uniqueKeys.insert(static_cast<int>(compatibleKey));
        }
        EXPECT_EQ(6, static_cast<int>(uniqueKeys.size()))
                << "key " << static_cast<int>(key);
    }
}

TEST(RelationSuggesterTest, CamelotNeighboursOfEveryKey) {
    for (int i = 0; i < kNumKeys; ++i) {
        const ChromaticKey key = keyAt(i);
        const int wheelNumber = KeyUtils::keyToOpenKeyNumber(key);
        const bool major = KeyUtils::keyIsMajor(key);
        const int stepUp = wheelNumber == 12 ? 1 : wheelNumber + 1;
        const int stepDown = wheelNumber == 1 ? 12 : wheelNumber - 1;

        // The key itself, the relative major or minor, and one step in each
        // direction on both rings of the wheel.
        EXPECT_TRUE(RelationSuggester::matchesKey(key, key));
        EXPECT_TRUE(RelationSuggester::matchesKey(
                KeyUtils::openKeyNumberToKey(wheelNumber, !major), key));
        EXPECT_TRUE(RelationSuggester::matchesKey(
                KeyUtils::openKeyNumberToKey(stepUp, major), key));
        EXPECT_TRUE(RelationSuggester::matchesKey(
                KeyUtils::openKeyNumberToKey(stepDown, major), key));

        // Two steps away on the wheel does not mix.
        const int twoStepsUp = stepUp == 12 ? 1 : stepUp + 1;
        EXPECT_FALSE(RelationSuggester::matchesKey(
                KeyUtils::openKeyNumberToKey(twoStepsUp, major), key));
    }
}

TEST(RelationSuggesterTest, MatchesKeyIsSymmetric) {
    for (int i = 0; i < kNumKeys; ++i) {
        const ChromaticKey key1 = keyAt(i);
        for (int j = 0; j < kNumKeys; ++j) {
            const ChromaticKey key2 = keyAt(j);
            EXPECT_EQ(RelationSuggester::matchesKey(key1, key2),
                    RelationSuggester::matchesKey(key2, key1))
                    << "keys " << static_cast<int>(key1) << " and "
                    << static_cast<int>(key2);
        }
    }
}
