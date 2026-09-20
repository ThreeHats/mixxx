#include "osc/oscpolicy.h"

namespace mixxx {
namespace osc {

RateLimiter::RateLimiter(int minIntervalMs)
        : m_minIntervalMs(minIntervalMs),
          m_lastSentMs(0),
          m_pendingValue(0.0),
          m_hasPending(false),
          m_everSent(false) {
}

bool RateLimiter::offer(double value, qint64 nowMs) {
    if (m_minIntervalMs <= 0 || !m_everSent || nowMs - m_lastSentMs >= m_minIntervalMs) {
        m_lastSentMs = nowMs;
        m_everSent = true;
        m_hasPending = false;
        return true;
    }
    m_pendingValue = value;
    m_hasPending = true;
    return false;
}

bool RateLimiter::takeDue(qint64 nowMs, double* pValue) {
    if (!m_hasPending || nowMs - m_lastSentMs < m_minIntervalMs) {
        return false;
    }
    *pValue = m_pendingValue;
    m_hasPending = false;
    m_lastSentMs = nowMs;
    return true;
}

void ControlFilter::setAllowAll(bool allowAll) {
    m_allowAll = allowAll;
}

void ControlFilter::setAllowedKeys(const QStringList& globs) {
    m_allowed.clear();
    m_allowed.reserve(globs.size());
    for (const QString& glob : globs) {
        const QString trimmed = glob.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        const QRegularExpression expression(
                QRegularExpression::wildcardToRegularExpression(trimmed));
        if (expression.isValid()) {
            m_allowed.append(expression);
        }
    }
}

bool ControlFilter::isAllowed(const ConfigKey& key) const {
    if (!key.isValid()) {
        return false;
    }
    if (m_allowAll) {
        return true;
    }
    for (const QRegularExpression& expression : m_allowed) {
        if (expression.match(key.item).hasMatch()) {
            return true;
        }
    }
    return false;
}

} // namespace osc
} // namespace mixxx
