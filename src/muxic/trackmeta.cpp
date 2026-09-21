#include "muxic/trackmeta.h"

#include "track/replaygain.h"
#include "util/math.h"

namespace {

const QChar kTagSeparator = QLatin1Char(',');

} // namespace

namespace muxic {

QStringList parseTags(const QString& text) {
    QStringList tags;
    const QStringList parts = text.split(kTagSeparator, Qt::SkipEmptyParts);
    tags.reserve(parts.size());
    for (const QString& part : parts) {
        const QString tag = part.simplified().toLower();
        if (tag.isEmpty() || tags.contains(tag)) {
            continue;
        }
        tags.append(tag);
    }
    return tags;
}

QString encodeTags(const QStringList& tags) {
    if (tags.isEmpty()) {
        return QString();
    }
    return kTagSeparator + tags.join(kTagSeparator) + kTagSeparator;
}

QStringList decodeTags(const QString& stored) {
    return parseTags(stored);
}

QString formatTags(const QString& stored) {
    return decodeTags(stored).join(QStringLiteral(", "));
}

QString tagLikePattern(const QString& tag) {
    QString escaped;
    escaped.reserve(tag.size() + 4);
    for (const QChar c : tag) {
        if (c == QLatin1Char('%') || c == QLatin1Char('_') || c == kTagLikeEscape) {
            escaped.append(kTagLikeEscape);
        }
        escaped.append(c);
    }
    return QStringLiteral("%") + kTagSeparator + escaped + kTagSeparator +
            QStringLiteral("%");
}

double lufsFromReplayGainRatio(double ratio) {
    return kReplayGainReferenceLufs - ratio2db(ratio);
}

QVariant lufsFromReplayGainRatio(const QVariant& ratio) {
    if (ratio.isNull() || !ratio.canConvert<double>()) {
        return QVariant();
    }
    bool ok = false;
    const double ratioValue = ratio.toDouble(&ok);
    if (!ok || !mixxx::ReplayGain::isValidRatio(ratioValue)) {
        return QVariant();
    }
    return QVariant(lufsFromReplayGainRatio(ratioValue));
}

} // namespace muxic
