#include "osc/oscmessage.h"

#include <cstring>

namespace {

/// Mixxx compiles with -ffast-math, thus `std::isfinite` can be optimized
/// away. The bit pattern answers without the floating point unit.
bool isFiniteNumber(double value) {
    quint64 bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    return ((bits >> 52) & 0x7FF) != 0x7FF;
}

} // namespace

namespace mixxx {
namespace osc {

Message::Message()
        : m_message(lo_message_new()) {
}

Message::~Message() {
    if (m_message) {
        lo_message_free(m_message);
    }
}

Message::Message(Message&& other) noexcept
        : m_message(other.m_message) {
    other.m_message = nullptr;
}

Message& Message::operator=(Message&& other) noexcept {
    if (this != &other) {
        if (m_message) {
            lo_message_free(m_message);
        }
        m_message = other.m_message;
        other.m_message = nullptr;
    }
    return *this;
}

void Message::addInt32(qint32 value) {
    if (m_message) {
        lo_message_add_int32(m_message, value);
    }
}

void Message::addInt64(qint64 value) {
    if (m_message) {
        lo_message_add_int64(m_message, value);
    }
}

void Message::addFloat(float value) {
    if (m_message) {
        lo_message_add_float(m_message, value);
    }
}

void Message::addString(const QString& value) {
    if (m_message) {
        lo_message_add_string(m_message, value.toUtf8().constData());
    }
}

QByteArray Message::serialise(const QString& path) const {
    if (!m_message || path.isEmpty()) {
        return QByteArray();
    }
    const QByteArray pathBytes = path.toLatin1();
    const size_t length = lo_message_length(m_message, pathBytes.constData());
    if (length == 0) {
        return QByteArray();
    }
    QByteArray datagram(static_cast<int>(length), '\0');
    size_t written = length;
    if (lo_message_serialise(m_message,
                pathBytes.constData(),
                datagram.data(),
                &written) == nullptr) {
        return QByteArray();
    }
    datagram.resize(static_cast<int>(written));
    return datagram;
}

bool IncomingMessage::firstNumber(double* pValue) const {
    if (args.isEmpty()) {
        return false;
    }
    const QVariant& arg = args.first();
    // Only a numeric type gives a number. Text that looks like a number does
    // not, because a control is not a text field.
    switch (arg.userType()) {
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::Double:
        break;
    default:
        return false;
    }
    const double value = arg.toDouble();
    if (!isFiniteNumber(value)) {
        return false;
    }
    *pValue = value;
    return true;
}

bool parseMessage(const QByteArray& datagram, IncomingMessage* pMessage) {
    // A bundle starts with `#bundle`. This module reads plain messages.
    if (datagram.isEmpty() || datagram.size() > kMaxDatagramSize ||
            datagram.at(0) != '/') {
        return false;
    }
    // liblo reads the datagram in place, thus it needs a copy it may change.
    QByteArray buffer = datagram;
    const char* pPath = lo_get_path(buffer.data(), buffer.size());
    if (pPath == nullptr) {
        return false;
    }
    pMessage->path = QString::fromLatin1(pPath);

    int result = 0;
    lo_message message = lo_message_deserialise(
            buffer.data(), static_cast<size_t>(buffer.size()), &result);
    if (message == nullptr) {
        return false;
    }
    const char* pTypes = lo_message_get_types(message);
    lo_arg** argv = lo_message_get_argv(message);
    pMessage->types = pTypes ? QString::fromLatin1(pTypes) : QString();
    pMessage->args.clear();
    if (!pMessage->types.isEmpty() && argv == nullptr) {
        lo_message_free(message);
        return false;
    }
    for (int i = 0; i < pMessage->types.size(); i++) {
        switch (pMessage->types.at(i).toLatin1()) {
        case 'i':
            pMessage->args.append(QVariant(static_cast<int>(argv[i]->i)));
            break;
        case 'h':
            pMessage->args.append(QVariant(static_cast<qlonglong>(argv[i]->h)));
            break;
        case 'f':
            pMessage->args.append(QVariant(static_cast<double>(argv[i]->f)));
            break;
        case 'd':
            pMessage->args.append(QVariant(argv[i]->d));
            break;
        case 's':
        case 'S':
            pMessage->args.append(QVariant(QString::fromUtf8(&argv[i]->s)));
            break;
        case 'T':
            pMessage->args.append(QVariant(1.0));
            break;
        case 'F':
            pMessage->args.append(QVariant(0.0));
            break;
        default:
            // A blob, a time tag, a MIDI message or nil carries no number.
            pMessage->args.append(QVariant());
            break;
        }
    }
    lo_message_free(message);
    return true;
}

} // namespace osc
} // namespace mixxx
