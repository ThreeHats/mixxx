#include "osc/oscbeatfeed.h"

#include <atomic>
#include <cstring>

#include "util/fifo.h"

namespace {

/// Two seconds of beats at 200 beats per minute on four decks, thus a reader
/// that stops for a moment loses nothing.
constexpr int kQueueSize = 64;

FIFO<mixxx::osc::BeatEvent> s_queue(kQueueSize);
std::atomic<bool> s_enabled{false};

} // namespace

namespace mixxx {
namespace osc {

// static
void BeatFeed::push(const BeatEvent& event) {
    s_queue.write(&event, 1);
}

// static
int BeatFeed::pop(BeatEvent* pEvents, int count) {
    return s_queue.read(pEvents, count);
}

// static
void BeatFeed::clear() {
    s_queue.flushReadData(s_queue.readAvailable());
}

// static
void BeatFeed::setEnabled(bool enabled) {
    s_enabled.store(enabled, std::memory_order_release);
}

// static
bool BeatFeed::enabled() {
    return s_enabled.load(std::memory_order_acquire);
}

// static
void BeatFeed::writeGroup(std::array<char, kBeatGroupSize>* pGroup, const QString& group) {
    pGroup->fill('\0');
    const QByteArray latin = group.toLatin1();
    const int length = std::min<int>(latin.size(), kBeatGroupSize - 1);
    std::memcpy(pGroup->data(), latin.constData(), static_cast<std::size_t>(length));
}

// static
QString BeatFeed::readGroup(const BeatEvent& event) {
    const std::size_t length = ::strnlen(event.group.data(), kBeatGroupSize);
    return QString::fromLatin1(event.group.data(), static_cast<int>(length));
}

} // namespace osc
} // namespace mixxx
