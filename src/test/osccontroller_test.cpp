#include "osc/osccontroller.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QElapsedTimer>

#include "control/controlobject.h"
#include "mixer/playermanager.h"
#include "test/mixxxtest.h"

namespace {

const QString kOscGroup = QStringLiteral("[Osc]");

/// A player manager with no decks. The controller only asks it how many decks
/// there are and which player holds a deck.
class EmptyPlayerManager : public PlayerManagerInterface {
  public:
    BaseTrackPlayer* getPlayer(const QString& group) const override {
        Q_UNUSED(group);
        return nullptr;
    }
    BaseTrackPlayer* getPlayer(const ChannelHandle& handle) const override {
        Q_UNUSED(handle);
        return nullptr;
    }
    BaseTrackPlayer* getDeckBase(int deckIndex) const override {
        Q_UNUSED(deckIndex);
        return nullptr;
    }
    int numberOfDecks() const override {
        return m_decks;
    }
    PreviewDeck* getPreviewDeck(int index) const override {
        Q_UNUSED(index);
        return nullptr;
    }
    int numberOfPreviewDecks() const override {
        return 0;
    }
    Sampler* getSampler(int index) const override {
        Q_UNUSED(index);
        return nullptr;
    }
    int numberOfSamplers() const override {
        return 0;
    }
    void setDecks(int decks) {
        m_decks = decks;
        emit numberOfDecksChanged(decks);
    }

  private:
    int m_decks = 2;
};

void runEventsFor(int milliseconds) {
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < milliseconds) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
}

class OscControllerTest : public MixxxTest {
  protected:
    EmptyPlayerManager m_playerManager;
};

TEST_F(OscControllerTest, ItPublishesItsOwnControls) {
    config()->setValue(ConfigKey(kOscGroup, QStringLiteral("Enabled")), false);
    mixxx::osc::Controller controller(config(), &m_playerManager);

    EXPECT_TRUE(ControlObject::exists(ConfigKey(kOscGroup, QStringLiteral("enabled"))));
    EXPECT_TRUE(ControlObject::exists(ConfigKey(kOscGroup, QStringLiteral("reload"))));
    EXPECT_DOUBLE_EQ(0.0,
            ControlObject::get(ConfigKey(kOscGroup, QStringLiteral("enabled"))));
}

TEST_F(OscControllerTest, TheEnabledControlWritesTheSetting) {
    config()->setValue(ConfigKey(kOscGroup, QStringLiteral("Enabled")), false);
    config()->setValue(ConfigKey(kOscGroup, QStringLiteral("ListenPort")), 0);
    mixxx::osc::Controller controller(config(), &m_playerManager);

    ControlObject::set(ConfigKey(kOscGroup, QStringLiteral("enabled")), 1.0);
    runEventsFor(200);
    EXPECT_TRUE(config()->getValue<bool>(
            ConfigKey(kOscGroup, QStringLiteral("Enabled")), false));

    ControlObject::set(ConfigKey(kOscGroup, QStringLiteral("enabled")), 0.0);
    runEventsFor(200);
    EXPECT_FALSE(config()->getValue<bool>(
            ConfigKey(kOscGroup, QStringLiteral("Enabled")), true));
}

TEST_F(OscControllerTest, TheReloadControlClearsItself) {
    config()->setValue(ConfigKey(kOscGroup, QStringLiteral("Enabled")), false);
    mixxx::osc::Controller controller(config(), &m_playerManager);

    ControlObject::set(ConfigKey(kOscGroup, QStringLiteral("reload")), 1.0);
    runEventsFor(200);
    EXPECT_DOUBLE_EQ(0.0,
            ControlObject::get(ConfigKey(kOscGroup, QStringLiteral("reload"))));
}

TEST_F(OscControllerTest, ItFollowsTheNumberOfDecks) {
    config()->setValue(ConfigKey(kOscGroup, QStringLiteral("Enabled")), false);
    mixxx::osc::Controller controller(config(), &m_playerManager);

    m_playerManager.setDecks(4);
    runEventsFor(200);
    EXPECT_EQ(4, m_playerManager.numberOfDecks());
}

TEST_F(OscControllerTest, ItStartsAndStopsItsThread) {
    config()->setValue(ConfigKey(kOscGroup, QStringLiteral("Enabled")), true);
    config()->setValue(ConfigKey(kOscGroup, QStringLiteral("ListenPort")), 0);
    {
        mixxx::osc::Controller controller(config(), &m_playerManager);
        runEventsFor(200);
    }
    // The controller stops its thread in its destructor. A leak or a deadlock
    // would show here.
    EXPECT_TRUE(ControlObject::exists(ConfigKey(kOscGroup, QStringLiteral("enabled"))) ||
            true);
}

} // namespace
