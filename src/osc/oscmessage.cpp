#include "osc/oscmessage.h"

#include <cstring>

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

void Message::addDouble(double value) {
    if (m_message) {
        lo_message_add_double(m_message, value);
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

double IncomingMessage::firstNumber(double fallback) const {
    if (args.isEmpty()) {
        return fallback;
    }
    bool ok = false;
    const double value = args.first().toDouble(&ok);
    return ok ? value : fallback;
}

bool parseMessage(const QByteArray& datagram, IncomingMessage* pMessage) {
    if (datagram.isEmpty() || datagram.at(0) != '/') {
        // A bundle starts with `#bundle`. This module reads plain messages.
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
            pMessage->args.append(QVariant());
            break;
        }
    }
    lo_message_free(message);
    return true;
}

} // namespace osc
} // namespace mixxx
