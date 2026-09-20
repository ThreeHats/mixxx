#pragma once

#include <QList>
#include <QRegularExpression>
#include <QStringList>

#include "preferences/configobject.h"

namespace mixxx {
namespace osc {

/// Holds a control to one message per interval. A change that comes too early
/// waits, thus the last value always goes out.
class RateLimiter {
  public:
    explicit RateLimiter(int minIntervalMs = 0);

    /// Offer a new value. Returns true when the caller must send it now.
    bool offer(double value, qint64 nowMs);

    /// Takes the value that waits when its time has come. Returns false when
    /// no value waits or the interval is not over.
    bool takeDue(qint64 nowMs, double* pValue);

    bool hasPending() const {
        return m_hasPending;
    }
    int minIntervalMs() const {
        return m_minIntervalMs;
    }

  private:
    int m_minIntervalMs;
    qint64 m_lastSentMs;
    double m_pendingValue;
    bool m_hasPending;
    bool m_everSent;
};

/// Says which controls a message from the network may write.
class ControlFilter {
  public:
    ControlFilter() = default;

    /// True lets a message write any control of Mixxx.
    void setAllowAll(bool allowAll);

    /// The keys that a message may write. Each entry is a glob, for example
    /// `hotcue_*_activate`. The group is not part of the test.
    void setAllowedKeys(const QStringList& globs);

    bool isAllowed(const ConfigKey& key) const;

  private:
    bool m_allowAll = false;
    QList<QRegularExpression> m_allowed;
};

} // namespace osc
} // namespace mixxx
