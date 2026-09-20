#pragma once

#include <lo/lo.h>

#include <QByteArray>
#include <QString>
#include <QVariant>
#include <QVector>

namespace mixxx {
namespace osc {

/// Builds one OSC message. liblo does the wire format; the transport is a
/// `QUdpSocket`, thus the bind address and the event loop stay with Qt.
class Message {
  public:
    Message();
    ~Message();
    Message(Message&& other) noexcept;
    Message& operator=(Message&& other) noexcept;
    Message(const Message&) = delete;
    Message& operator=(const Message&) = delete;

    void addInt32(qint32 value);
    void addInt64(qint64 value);
    void addFloat(float value);
    void addDouble(double value);
    void addString(const QString& value);

    /// The message with its address, ready for one datagram. The result is
    /// empty when the address is not valid.
    QByteArray serialise(const QString& path) const;

  private:
    lo_message m_message;
};

/// One message that came in from the network.
struct IncomingMessage {
    QString path;
    QString types;
    QVector<QVariant> args;

    /// The first argument as a number, or `fallback` when there is none.
    double firstNumber(double fallback) const;
};

/// Read a datagram. Returns false when it holds no OSC message.
bool parseMessage(const QByteArray& datagram, IncomingMessage* pMessage);

} // namespace osc
} // namespace mixxx
