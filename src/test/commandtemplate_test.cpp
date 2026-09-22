#include "util/commandtemplate.h"

#include <gtest/gtest.h>

#include <QMap>
#include <QString>
#include <QStringList>

#include "test/mixxxtest.h"

namespace {

TEST(CommandTemplateTest, Substitutes) {
    QMap<QString, QString> placeholders;
    placeholders.insert(QStringLiteral("MODEL"), QStringLiteral("htdemucs"));
    placeholders.insert(QStringLiteral("OUTPUT_DIR"), QStringLiteral("/tmp/out"));
    placeholders.insert(QStringLiteral("INPUT"), QStringLiteral("/music/a b.mp3"));

    QString errorMessage;
    const QStringList command = mixxx::expandCommandTemplate(
            QStringLiteral("demucs -n $MODEL -o ${OUTPUT_DIR} \"$INPUT\""),
            placeholders,
            &errorMessage);

    EXPECT_TRUE(errorMessage.isEmpty());
    ASSERT_EQ(6, command.size());
    EXPECT_QSTRING_EQ(QStringLiteral("demucs"), command.at(0));
    EXPECT_QSTRING_EQ(QStringLiteral("-n"), command.at(1));
    EXPECT_QSTRING_EQ(QStringLiteral("htdemucs"), command.at(2));
    EXPECT_QSTRING_EQ(QStringLiteral("-o"), command.at(3));
    EXPECT_QSTRING_EQ(QStringLiteral("/tmp/out"), command.at(4));
    EXPECT_QSTRING_EQ(QStringLiteral("/music/a b.mp3"), command.at(5));
}

TEST(CommandTemplateTest, RejectsBadInput) {
    QMap<QString, QString> placeholders;
    placeholders.insert(QStringLiteral("INPUT"), QStringLiteral("/music/a.mp3"));
    placeholders.insert(QStringLiteral("MODEL"), QString());

    QString errorMessage;
    EXPECT_TRUE(mixxx::expandCommandTemplate(
            QStringLiteral("tool $NOPE"), placeholders, &errorMessage)
                        .isEmpty());
    EXPECT_TRUE(errorMessage.contains(QStringLiteral("NOPE")));

    // A name in the wrong case must not reach the program as plain text.
    EXPECT_TRUE(mixxx::expandCommandTemplate(
            QStringLiteral("tool $input"), placeholders, &errorMessage)
                        .isEmpty());
    EXPECT_TRUE(errorMessage.contains(QStringLiteral("input")));

    // An empty model would give the program a bare option value.
    EXPECT_TRUE(mixxx::expandCommandTemplate(
            QStringLiteral("tool -n $MODEL \"$INPUT\""), placeholders, &errorMessage)
                        .isEmpty());
    EXPECT_TRUE(errorMessage.contains(QStringLiteral("MODEL")));

    EXPECT_TRUE(mixxx::expandCommandTemplate(
            QStringLiteral("tool \"$INPUT"), placeholders, &errorMessage)
                        .isEmpty());
    EXPECT_FALSE(errorMessage.isEmpty());

    EXPECT_TRUE(mixxx::expandCommandTemplate(
            QStringLiteral("   "), placeholders, &errorMessage)
                        .isEmpty());
    EXPECT_FALSE(errorMessage.isEmpty());
}

} // namespace
