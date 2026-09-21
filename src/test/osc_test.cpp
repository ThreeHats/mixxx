#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QUdpSocket>
#include <limits>
#include <memory>
#include <vector>

#include "control/controlobject.h"
#include "control/controlpotmeter.h"
#include "osc/oscaddress.h"
#include "osc/oscbeatfeed.h"
#include "osc/oscconfig.h"
#include "osc/oscmessage.h"
#include "osc/oscpolicy.h"
#include "osc/oscservice.h"
#include "test/mixxxtest.h"

namespace {

using namespace mixxx::osc;

/// Runs the event loop until the test holds or the time is over.
bool waitFor(const std::function<bool()>& predicate, int timeoutMs = 3000) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        if (predicate()) {
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return predicate();
}

Config testConfig() {
    Config config;
    config.enabled = true;
    config.listenHost = QStringLiteral("127.0.0.1");
    config.listenPort = 0;
    config.snapshotIntervalSeconds = 0;
    config.allowedKeys = defaultAllowedKeys();
    return config;
}

TEST(OscAddressTest, ControlToPathAndBack) {
    const ConfigKey key(QStringLiteral("[Channel1]"), QStringLiteral("play"));
    const QString path = pathForControl(key);
    EXPECT_QSTRING_EQ(QStringLiteral("/mixxx/Channel1/play"), path);
    EXPECT_TRUE(controlForPath(path) == key);
}

TEST(OscAddressTest, MainGroupAndLongKeys) {
    EXPECT_QSTRING_EQ(QStringLiteral("/mixxx/Master/crossfader"),
            pathForControl(ConfigKey(QStringLiteral("[Master]"),
                    QStringLiteral("crossfader"))));
    EXPECT_QSTRING_EQ(QStringLiteral("/mixxx/Channel2/hotcue_3_activate"),
            pathForControl(ConfigKey(QStringLiteral("[Channel2]"),
                    QStringLiteral("hotcue_3_activate"))));
}

TEST(OscAddressTest, RejectsWhatOscKeepsForPatterns) {
    EXPECT_TRUE(pathForControl(ConfigKey(QStringLiteral("[Chan*1]"),
                                       QStringLiteral("play")))
                        .isEmpty());
    EXPECT_TRUE(pathForControl(ConfigKey(QStringLiteral("[Channel1]"),
                                       QStringLiteral("a b")))
                        .isEmpty());
    EXPECT_TRUE(pathForControl(ConfigKey(QStringLiteral("Channel1"),
                                       QStringLiteral("play")))
                        .isEmpty());
}

TEST(OscAddressTest, RejectsPathsThatAreNotControls) {
    EXPECT_FALSE(controlForPath(QStringLiteral("/mixxx/snapshot")).isValid());
    EXPECT_FALSE(controlForPath(QStringLiteral("/other/Channel1/play")).isValid());
    EXPECT_FALSE(controlForPath(QStringLiteral("/mixxx/Channel1/a/b")).isValid());
}

TEST(OscFilterTest, DefaultListTakesTransportOnly) {
    ControlFilter filter;
    filter.setAllowedKeys(defaultAllowedKeys());
    const QString deck = QStringLiteral("[Channel1]");
    EXPECT_TRUE(filter.isAllowed(ConfigKey(deck, QStringLiteral("play"))));
    EXPECT_TRUE(filter.isAllowed(ConfigKey(deck, QStringLiteral("beatjump"))));
    EXPECT_TRUE(filter.isAllowed(ConfigKey(deck, QStringLiteral("hotcue_4_activate"))));
    EXPECT_TRUE(filter.isAllowed(ConfigKey(deck, QStringLiteral("sync_enabled"))));
    EXPECT_TRUE(filter.isAllowed(ConfigKey(deck, QStringLiteral("rate"))));
    EXPECT_FALSE(filter.isAllowed(ConfigKey(deck, QStringLiteral("volume"))));
    EXPECT_FALSE(filter.isAllowed(ConfigKey(deck, QStringLiteral("eject"))));
    EXPECT_FALSE(filter.isAllowed(
            ConfigKey(QStringLiteral("[Library]"), QStringLiteral("AutoDjAddBottom"))));
}

TEST(OscFilterTest, AllowAllTakesEverything) {
    ControlFilter filter;
    filter.setAllowedKeys(QStringList());
    EXPECT_FALSE(filter.isAllowed(
            ConfigKey(QStringLiteral("[Channel1]"), QStringLiteral("play"))));
    filter.setAllowAll(true);
    EXPECT_TRUE(filter.isAllowed(
            ConfigKey(QStringLiteral("[Channel1]"), QStringLiteral("eject"))));
    EXPECT_FALSE(filter.isAllowed(ConfigKey()));
}

TEST(OscRateLimiterTest, ZeroIntervalSendsEachChange) {
    RateLimiter limiter(0);
    EXPECT_TRUE(limiter.offer(1.0, 1000));
    EXPECT_TRUE(limiter.offer(2.0, 1000));
    EXPECT_FALSE(limiter.hasPending());
}

TEST(OscRateLimiterTest, HoldsBackAndKeepsTheLastValue) {
    RateLimiter limiter(50);
    EXPECT_TRUE(limiter.offer(1.0, 1000));
    EXPECT_FALSE(limiter.offer(2.0, 1010));
    EXPECT_FALSE(limiter.offer(3.0, 1020));
    EXPECT_TRUE(limiter.hasPending());

    double value = 0;
    EXPECT_FALSE(limiter.takeDue(1040, &value));
    ASSERT_TRUE(limiter.takeDue(1050, &value));
    EXPECT_DOUBLE_EQ(3.0, value);
    EXPECT_FALSE(limiter.hasPending());
    EXPECT_FALSE(limiter.takeDue(2000, &value));
}

TEST(OscRateLimiterTest, SendsAtOnceAfterAQuietTime) {
    RateLimiter limiter(50);
    EXPECT_TRUE(limiter.offer(1.0, 1000));
    EXPECT_TRUE(limiter.offer(2.0, 1100));
}

TEST(OscConfigTest, ParsesPublishRules) {
    QStringList errors;
    const QList<PublishRule> rules = parsePublishRules(
            QStringLiteral("# a comment\n"
                           "[ChannelN] play\n"
                           "[ChannelN] playposition 100\n"
                           "\n"
                           "[Master] crossfader 30   # after the rule\n"
                           "Channel1 play\n"
                           "[Channel1] play notanumber\n"),
            &errors);
    ASSERT_EQ(3, rules.size());
    EXPECT_QSTRING_EQ(QStringLiteral("[ChannelN]"), rules.at(0).group);
    EXPECT_QSTRING_EQ(QStringLiteral("play"), rules.at(0).key);
    EXPECT_EQ(0, rules.at(0).minIntervalMs);
    EXPECT_EQ(100, rules.at(1).minIntervalMs);
    EXPECT_QSTRING_EQ(QStringLiteral("[Master]"), rules.at(2).group);
    EXPECT_EQ(30, rules.at(2).minIntervalMs);
    EXPECT_EQ(2, errors.size());
}

TEST(OscConfigTest, ExpandsTheDeckToken) {
    const QList<PublishRule> rules = parsePublishRules(
            QStringLiteral("[ChannelN] play\n[Master] crossfader\n"), nullptr);
    const QList<PublishRule> expanded = expandPublishRules(rules, 3);
    ASSERT_EQ(4, expanded.size());
    EXPECT_QSTRING_EQ(QStringLiteral("[Channel1]"), expanded.at(0).group);
    EXPECT_QSTRING_EQ(QStringLiteral("[Channel3]"), expanded.at(2).group);
    EXPECT_QSTRING_EQ(QStringLiteral("[Master]"), expanded.at(3).group);
}

TEST(OscConfigTest, TheDefaultListParsesAndCoversTheRig) {
    QStringList errors;
    const QList<PublishRule> rules =
            parsePublishRules(defaultPublishRulesText(), &errors);
    EXPECT_TRUE(errors.isEmpty());
    EXPECT_TRUE(rules.contains(PublishRule{deckGroupToken(), QStringLiteral("play"), 0}));
    EXPECT_TRUE(rules.contains(
            PublishRule{QStringLiteral("[Master]"), QStringLiteral("crossfader"), 30}));
}

TEST(OscConfigTest, ParsesTargets) {
    const QList<Target> targets =
            parseTargets(QStringLiteral("127.0.0.1:9001, [::1]:9002, bad, 1.2.3.4:0"));
    ASSERT_EQ(2, targets.size());
    EXPECT_QSTRING_EQ(QStringLiteral("127.0.0.1:9001"), targets.at(0).toString());
    EXPECT_QSTRING_EQ(QStringLiteral("[::1]:9002"), targets.at(1).toString());
    EXPECT_QSTRING_EQ(QStringLiteral("127.0.0.1:9001, [::1]:9002"),
            targetsToString(targets));
}

TEST(OscBeatFeedTest, CarriesABeatFromTheEngineThread) {
    BeatFeed::clear();
    BeatEvent sent;
    BeatFeed::writeGroup(&sent.group, QStringLiteral("[Channel2]"));
    sent.stampNs = Q_INT64_C(1234567890123456);
    sent.trackBeat = -3;
    sent.seq = 7;
    sent.bpm = 128.5f;
    sent.beatInBar = 2;
    BeatFeed::push(sent);

    BeatEvent received[4];
    ASSERT_EQ(1, BeatFeed::pop(received, 4));
    EXPECT_QSTRING_EQ(QStringLiteral("[Channel2]"), BeatFeed::readGroup(received[0]));
    EXPECT_EQ(sent.stampNs, received[0].stampNs);
    EXPECT_EQ(-3, received[0].trackBeat);
    EXPECT_EQ(7, received[0].seq);
    EXPECT_FLOAT_EQ(128.5f, received[0].bpm);
    EXPECT_EQ(2, received[0].beatInBar);
    EXPECT_EQ(0, BeatFeed::pop(received, 4));
}

TEST(OscBeatFeedTest, ALongGroupNameIsCut) {
    BeatEvent event;
    BeatFeed::writeGroup(&event.group, QStringLiteral("[AGroupNameThatIsFarTooLong]"));
    EXPECT_EQ(kBeatGroupSize - 1, BeatFeed::readGroup(event).size());
}

TEST(OscMessageTest, RoundTripsThroughTheWireFormat) {
    Message message;
    message.addInt64(Q_INT64_C(-9007199254740993));
    message.addInt32(42);
    message.addFloat(1.5f);
    message.addString(QStringLiteral("hello"));
    const QByteArray datagram = message.serialise(QStringLiteral("/mixxx/Channel1/beat"));
    ASSERT_FALSE(datagram.isEmpty());

    IncomingMessage parsed;
    ASSERT_TRUE(parseMessage(datagram, &parsed));
    EXPECT_QSTRING_EQ(QStringLiteral("/mixxx/Channel1/beat"), parsed.path);
    EXPECT_QSTRING_EQ(QStringLiteral("hifs"), parsed.types);
    ASSERT_EQ(4, parsed.args.size());
    EXPECT_EQ(Q_INT64_C(-9007199254740993), parsed.args.at(0).toLongLong());
    EXPECT_EQ(42, parsed.args.at(1).toInt());
    EXPECT_DOUBLE_EQ(1.5, parsed.args.at(2).toDouble());
    EXPECT_QSTRING_EQ(QStringLiteral("hello"), parsed.args.at(3).toString());
}

TEST(OscMessageTest, RefusesWhatIsNotAMessage) {
    IncomingMessage parsed;
    EXPECT_FALSE(parseMessage(QByteArray(), &parsed));
    EXPECT_FALSE(parseMessage(QByteArrayLiteral("#bundle\0\0\0\0\0\0\0\0\0"), &parsed));
}

TEST(OscConfigTest, OnlyADeckSendsBeats) {
    EXPECT_TRUE(groupSendsBeats(QStringLiteral("[Channel1]"), false));
    EXPECT_TRUE(groupSendsBeats(QStringLiteral("[Channel4]"), false));
    EXPECT_FALSE(groupSendsBeats(QStringLiteral("[Sampler1]"), false));
    EXPECT_TRUE(groupSendsBeats(QStringLiteral("[Sampler1]"), true));
    EXPECT_FALSE(groupSendsBeats(QStringLiteral("[PreviewDeck1]"), true));
    EXPECT_FALSE(groupSendsBeats(QStringLiteral("[Master]"), true));
}

TEST(OscConfigTest, AStateRequestNeedsALoopbackOrATarget) {
    const QList<Target> targets = parseTargets(QStringLiteral("10.0.0.5:9001"));
    Target loopback;
    loopback.host = QHostAddress(QHostAddress::LocalHost);
    loopback.port = 1234;
    Target known;
    known.host = QHostAddress(QStringLiteral("10.0.0.5"));
    known.port = 5555;
    Target stranger;
    stranger.host = QHostAddress(QStringLiteral("10.0.0.9"));
    stranger.port = 5555;

    EXPECT_TRUE(isTrustedSource(loopback, targets, false));
    EXPECT_TRUE(isTrustedSource(known, targets, false));
    EXPECT_FALSE(isTrustedSource(stranger, targets, false));
    EXPECT_TRUE(isTrustedSource(stranger, targets, true));
    EXPECT_FALSE(isTrustedSource(Target(), targets, true));
}

TEST(OscMessageTest, ANumberComesOnlyFromANumber) {
    Message number;
    number.addFloat(1.5f);
    IncomingMessage parsed;
    ASSERT_TRUE(parseMessage(number.serialise(QStringLiteral("/mixxx/Channel1/play")), &parsed));
    double value = 0;
    ASSERT_TRUE(parsed.firstNumber(&value));
    EXPECT_DOUBLE_EQ(1.5, value);

    Message text;
    text.addString(QStringLiteral("1.5"));
    ASSERT_TRUE(parseMessage(text.serialise(QStringLiteral("/mixxx/Channel1/play")), &parsed));
    EXPECT_FALSE(parsed.firstNumber(&value));

    Message empty;
    ASSERT_TRUE(parseMessage(empty.serialise(QStringLiteral("/mixxx/Channel1/play")), &parsed));
    EXPECT_FALSE(parsed.firstNumber(&value));
}

TEST(OscMessageTest, ANumberThatIsNotFiniteIsNoNumber) {
    for (const float bad : {std::numeric_limits<float>::quiet_NaN(),
                 std::numeric_limits<float>::infinity(),
                 -std::numeric_limits<float>::infinity()}) {
        Message message;
        message.addFloat(bad);
        IncomingMessage parsed;
        ASSERT_TRUE(parseMessage(
                message.serialise(QStringLiteral("/mixxx/Channel1/play")), &parsed));
        double value = 0;
        EXPECT_FALSE(parsed.firstNumber(&value));
    }
}

TEST(OscMessageTest, ABlobAndANilCarryNoNumber) {
    for (const char type : {'b', 'N'}) {
        lo_message message = lo_message_new();
        if (type == 'b') {
            const char payload[4] = {1, 2, 3, 4};
            lo_blob blob = lo_blob_new(sizeof(payload), payload);
            lo_message_add_blob(message, blob);
            lo_blob_free(blob);
        } else {
            lo_message_add_nil(message);
        }
        const char* path = "/mixxx/Channel1/play";
        const size_t length = lo_message_length(message, path);
        QByteArray datagram(static_cast<int>(length), '\0');
        size_t written = length;
        ASSERT_NE(nullptr, lo_message_serialise(message, path, datagram.data(), &written));
        datagram.resize(static_cast<int>(written));
        lo_message_free(message);

        IncomingMessage parsed;
        ASSERT_TRUE(parseMessage(datagram, &parsed));
        ASSERT_EQ(1, parsed.args.size());
        double value = 0;
        EXPECT_FALSE(parsed.firstNumber(&value));
    }
}

TEST(OscMessageTest, ARuinedDatagramIsRefused) {
    Message message;
    message.addFloat(1.0f);
    const QByteArray good = message.serialise(QStringLiteral("/mixxx/Channel1/play"));
    IncomingMessage parsed;

    // Cut off the argument.
    EXPECT_FALSE(parseMessage(good.left(good.size() - 4), &parsed));
    // A type tag that promises more than the datagram holds.
    QByteArray tooManyTypes = good;
    const int tagIndex = tooManyTypes.indexOf(',');
    ASSERT_GT(tagIndex, 0);
    tooManyTypes[tagIndex + 1] = 'f';
    tooManyTypes[tagIndex + 2] = 'f';
    EXPECT_FALSE(parseMessage(tooManyTypes, &parsed));
    // More than the module reads.
    EXPECT_FALSE(parseMessage(QByteArray(kMaxDatagramSize + 1, '/'), &parsed));
}

class OscServiceTest : public MixxxTest {
  protected:
    void SetUp() override {
        ASSERT_TRUE(m_client.bind(QHostAddress::LocalHost, 0));
    }

    void sendToService(const QString& path, const Message& message) {
        const QByteArray datagram = message.serialise(path);
        ASSERT_FALSE(datagram.isEmpty());
        m_client.writeDatagram(datagram,
                QHostAddress(QHostAddress::LocalHost),
                m_service.boundPort());
    }

    /// Collects each message that reaches the client socket.
    void collect() {
        while (m_client.hasPendingDatagrams()) {
            QByteArray datagram(
                    static_cast<int>(m_client.pendingDatagramSize()), '\0');
            const qint64 read = m_client.readDatagram(datagram.data(), datagram.size());
            if (read < 0) {
                return;
            }
            datagram.resize(static_cast<int>(read));
            IncomingMessage message;
            if (parseMessage(datagram, &message)) {
                m_received.append(message);
            }
        }
    }

    bool waitForPath(const QString& path) {
        return waitFor([this, &path] {
            collect();
            for (const IncomingMessage& message : m_received) {
                if (message.path == path) {
                    return true;
                }
            }
            return false;
        });
    }

    const IncomingMessage* find(const QString& path) const {
        for (const IncomingMessage& message : m_received) {
            if (message.path == path) {
                return &message;
            }
        }
        return nullptr;
    }

    Service m_service;
    QUdpSocket m_client;
    QList<IncomingMessage> m_received;
};

TEST_F(OscServiceTest, ListensOnAPortOfItsOwn) {
    m_service.applyConfig(testConfig(), 0);
    EXPECT_NE(0, m_service.boundPort());
    m_service.stop();
    EXPECT_EQ(0, m_service.boundPort());
}

TEST_F(OscServiceTest, AMessageWritesAControlOnTheAllowList) {
    ControlObject play(ConfigKey(QStringLiteral("[Channel1]"), QStringLiteral("play")));
    play.set(0.0);
    m_service.applyConfig(testConfig(), 0);
    ASSERT_NE(0, m_service.boundPort());

    Message message;
    message.addFloat(1.0f);
    sendToService(QStringLiteral("/mixxx/Channel1/play"), message);

    EXPECT_TRUE(waitFor([&play] { return play.get() > 0; }));
    EXPECT_DOUBLE_EQ(1.0, play.get());
}

TEST_F(OscServiceTest, TheAllowListHoldsBackTheOtherControls) {
    ControlPotmeter volume(
            ConfigKey(QStringLiteral("[Channel1]"), QStringLiteral("volume")), 0.0, 1.0);
    volume.set(0.25);
    m_service.applyConfig(testConfig(), 0);

    Message message;
    message.addFloat(1.0f);
    sendToService(QStringLiteral("/mixxx/Channel1/volume"), message);

    EXPECT_FALSE(waitFor([&volume] { return volume.get() > 0.5; }, 300));
    EXPECT_DOUBLE_EQ(0.25, volume.get());
}

TEST_F(OscServiceTest, AReaderThatAsksGetsTheWholeState) {
    ControlPotmeter volume(
            ConfigKey(QStringLiteral("[Channel1]"), QStringLiteral("volume")), 0.0, 1.0);
    volume.set(0.5);
    Config config = testConfig();
    config.publishRules = QList<PublishRule>{
            PublishRule{QStringLiteral("[Channel1]"), QStringLiteral("volume"), 0}};
    m_service.applyConfig(config, 0);

    TrackInfo info;
    info.artist = QStringLiteral("An Artist");
    info.title = QStringLiteral("A Title");
    m_service.setTrackInfo(QStringLiteral("[Channel1]"), info);

    Message subscribe;
    sendToService(QStringLiteral("/mixxx/subscribe"), subscribe);

    ASSERT_TRUE(waitForPath(QStringLiteral("/mixxx/snapshot_end")));
    const IncomingMessage* pVolume = find(QStringLiteral("/mixxx/Channel1/volume"));
    ASSERT_NE(nullptr, pVolume);
    ASSERT_EQ(1, pVolume->args.size());
    EXPECT_NEAR(0.5, pVolume->args.at(0).toDouble(), 1e-6);

    const IncomingMessage* pArtist = find(QStringLiteral("/mixxx/Channel1/artist"));
    ASSERT_NE(nullptr, pArtist);
    EXPECT_QSTRING_EQ(QStringLiteral("An Artist"), pArtist->args.at(0).toString());
}

TEST_F(OscServiceTest, AChangeOfAControlGoesOut) {
    ControlPotmeter crossfader(
            ConfigKey(QStringLiteral("[Master]"), QStringLiteral("crossfader")),
            -1.0,
            1.0);
    crossfader.set(0.0);
    Config config = testConfig();
    config.publishRules = QList<PublishRule>{
            PublishRule{QStringLiteral("[Master]"), QStringLiteral("crossfader"), 0}};
    m_service.applyConfig(config, 0);

    Message subscribe;
    sendToService(QStringLiteral("/mixxx/subscribe"), subscribe);
    ASSERT_TRUE(waitForPath(QStringLiteral("/mixxx/snapshot_end")));
    m_received.clear();

    crossfader.set(-0.75);
    ASSERT_TRUE(waitForPath(QStringLiteral("/mixxx/Master/crossfader")));
    const IncomingMessage* pMessage = find(QStringLiteral("/mixxx/Master/crossfader"));
    ASSERT_NE(nullptr, pMessage);
    EXPECT_NEAR(-0.75, pMessage->args.at(0).toDouble(), 1e-6);
}

TEST_F(OscServiceTest, ABeatOfTheEngineReachesTheReader) {
    m_service.applyConfig(testConfig(), 0);
    Message subscribe;
    sendToService(QStringLiteral("/mixxx/subscribe"), subscribe);
    ASSERT_TRUE(waitForPath(QStringLiteral("/mixxx/snapshot_end")));

    BeatEvent event;
    BeatFeed::writeGroup(&event.group, QStringLiteral("[Channel1]"));
    event.stampNs = Q_INT64_C(9007199254740993);
    event.trackBeat = 12;
    event.seq = 5;
    event.bpm = 174.0f;
    event.beatInBar = 3;
    BeatFeed::push(event);

    ASSERT_TRUE(waitForPath(QStringLiteral("/mixxx/Channel1/beat")));
    const IncomingMessage* pBeat = find(QStringLiteral("/mixxx/Channel1/beat"));
    ASSERT_NE(nullptr, pBeat);
    EXPECT_QSTRING_EQ(QStringLiteral("hifii"), pBeat->types);
    EXPECT_EQ(Q_INT64_C(9007199254740993), pBeat->args.at(0).toLongLong());
    EXPECT_EQ(12, pBeat->args.at(1).toInt());
    EXPECT_FLOAT_EQ(174.0f, static_cast<float>(pBeat->args.at(2).toDouble()));
    EXPECT_EQ(5, pBeat->args.at(3).toInt());
    EXPECT_EQ(3, pBeat->args.at(4).toInt());
}

TEST_F(OscServiceTest, AnArgumentThatIsNoNumberLeavesTheControlAlone) {
    ControlObject play(ConfigKey(QStringLiteral("[Channel1]"), QStringLiteral("play")));
    play.set(1.0);
    m_service.applyConfig(testConfig(), 0);

    lo_message message = lo_message_new();
    const char payload[4] = {1, 2, 3, 4};
    lo_blob blob = lo_blob_new(sizeof(payload), payload);
    lo_message_add_blob(message, blob);
    lo_blob_free(blob);
    const char* path = "/mixxx/Channel1/play";
    const size_t length = lo_message_length(message, path);
    QByteArray datagram(static_cast<int>(length), '\0');
    size_t written = length;
    ASSERT_NE(nullptr, lo_message_serialise(message, path, datagram.data(), &written));
    datagram.resize(static_cast<int>(written));
    lo_message_free(message);
    m_client.writeDatagram(datagram,
            QHostAddress(QHostAddress::LocalHost),
            m_service.boundPort());

    EXPECT_FALSE(waitFor([&play] { return play.get() < 0.5; }, 300));
    EXPECT_DOUBLE_EQ(1.0, play.get());
}

TEST_F(OscServiceTest, AMessageWithoutAnArgumentLeavesTheControlAlone) {
    ControlObject play(ConfigKey(QStringLiteral("[Channel1]"), QStringLiteral("play")));
    play.set(1.0);
    m_service.applyConfig(testConfig(), 0);

    Message message;
    sendToService(QStringLiteral("/mixxx/Channel1/play"), message);
    EXPECT_FALSE(waitFor([&play] { return play.get() < 0.5; }, 300));
    EXPECT_DOUBLE_EQ(1.0, play.get());
}

TEST_F(OscServiceTest, TheReadersHaveAnUpperNumber) {
    m_service.applyConfig(testConfig(), 0);
    ASSERT_NE(0, m_service.boundPort());

    std::vector<std::unique_ptr<QUdpSocket>> readers;
    for (int i = 0; i < kMaxSubscribers + 4; i++) {
        auto pSocket = std::make_unique<QUdpSocket>();
        ASSERT_TRUE(pSocket->bind(QHostAddress::LocalHost, 0));
        Message subscribe;
        const QByteArray datagram = subscribe.serialise(QStringLiteral("/mixxx/subscribe"));
        pSocket->writeDatagram(datagram,
                QHostAddress(QHostAddress::LocalHost),
                m_service.boundPort());
        readers.push_back(std::move(pSocket));
    }

    waitFor([this] { return m_service.subscriberCount() > kMaxSubscribers; }, 500);
    EXPECT_EQ(kMaxSubscribers, m_service.subscriberCount());
}

TEST_F(OscServiceTest, AReaderThatAsksAgainAtOnceGetsOneSnapshot) {
    Config config = testConfig();
    config.publishRules = QList<PublishRule>{
            PublishRule{QStringLiteral("[Channel1]"), QStringLiteral("play"), 0}};
    ControlObject play(ConfigKey(QStringLiteral("[Channel1]"), QStringLiteral("play")));
    m_service.applyConfig(config, 0);

    Message subscribe;
    sendToService(QStringLiteral("/mixxx/subscribe"), subscribe);
    ASSERT_TRUE(waitForPath(QStringLiteral("/mixxx/snapshot_end")));
    m_received.clear();

    for (int i = 0; i < 5; i++) {
        Message again;
        sendToService(QStringLiteral("/mixxx/subscribe"), again);
    }
    waitFor([] { return false; }, 300);
    collect();
    int snapshots = 0;
    for (const IncomingMessage& message : m_received) {
        if (message.path == QStringLiteral("/mixxx/snapshot_end")) {
            snapshots++;
        }
    }
    EXPECT_EQ(0, snapshots);
    EXPECT_EQ(1, m_service.subscriberCount());
}

} // namespace
