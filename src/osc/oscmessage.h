#pragma once

#include <lo/lo.h>

#include <QByteArray>
#include <QString>
#include <QVariant>
#include <QVector>

namespace mixxx {
namespace osc {

/// The longest datagram that the module reads. Its own messages are far
/// shorter, thus a bigger one is a mistake or an attack.
constexpr int kMaxDatagramSize = 8192;

/// Builds one OSC message. liblo makes the wire format. The transport is a
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

    /// The first argument as a number. Returns false when there is none, when
    /// its type carries no number, or when the number is not finite.
    bool firstNumber(double* pValue) const;
};

/// Read a datagram. Returns false when it holds no OSC message.
bool parseMessage(const QByteArray& datagram, IncomingMessage* pMessage);

} // namespace osc
} // namespace mixxx
