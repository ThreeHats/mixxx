#include "stems/stemconversionsettings.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QRegularExpression>

#include "util/assert.h"

namespace {

const QString kSourceDirectoryValue = QStringLiteral("source");
const QString kCustomDirectoryValue = QStringLiteral("custom");
const QString kStemFileSuffix = QStringLiteral(".stem.mp4");

// The colors that Mixxx gives to a stem file without colors.
const char* const kStemColors[] = {"#009E73", "#D55E00", "#CC79A7", "#56B4E9"};

} // anonymous namespace

namespace mixxx {

const QString StemConversionSettings::kConfigGroup = QStringLiteral("[Stems]");
const QString StemConversionSettings::kSeparatorCommandItem =
        QStringLiteral("SeparatorCommand");
const QString StemConversionSettings::kModelItem = QStringLiteral("Model");
const QString StemConversionSettings::kEncoderCommandItem =
        QStringLiteral("EncoderCommand");
const QString StemConversionSettings::kMuxerPathItem = QStringLiteral("MuxerPath");
const QString StemConversionSettings::kOutputModeItem = QStringLiteral("OutputMode");
const QString StemConversionSettings::kOutputDirectoryItem =
        QStringLiteral("OutputDirectory");

// static
QString StemConversionSettings::defaultSeparatorCommand() {
    return QStringLiteral("demucs -n $MODEL -o $OUTPUT_DIR \"$INPUT\"");
}

// static
QString StemConversionSettings::defaultModel() {
    return QStringLiteral("htdemucs");
}

// static
QString StemConversionSettings::defaultEncoderCommand() {
    return QStringLiteral(
            "ffmpeg -hide_banner -nostdin -y -i \"$INPUT\" -ar $SAMPLE_RATE "
            "-c:a aac -b:a 256k \"$OUTPUT\"");
}

// static
QString StemConversionSettings::defaultMuxerPath() {
    return QStringLiteral("MP4Box");
}

// static
StemConversionSettings StemConversionSettings::readFrom(
        const UserSettingsPointer& pConfig) {
    StemConversionSettings settings;
    if (!pConfig) {
        return settings;
    }
    settings.m_separatorCommand = pConfig->getValue(
            ConfigKey(kConfigGroup, kSeparatorCommandItem),
            defaultSeparatorCommand());
    settings.m_model = pConfig->getValue(
            ConfigKey(kConfigGroup, kModelItem), defaultModel());
    settings.m_encoderCommand = pConfig->getValue(
            ConfigKey(kConfigGroup, kEncoderCommandItem),
            defaultEncoderCommand());
    settings.m_muxerPath = pConfig->getValue(
            ConfigKey(kConfigGroup, kMuxerPathItem), defaultMuxerPath());
    settings.m_outputMode = pConfig->getValue(
                                    ConfigKey(kConfigGroup, kOutputModeItem),
                                    kSourceDirectoryValue) ==
                    kCustomDirectoryValue
            ? StemOutputMode::CustomDirectory
            : StemOutputMode::SourceDirectory;
    settings.m_outputDirectory = pConfig->getValue(
            ConfigKey(kConfigGroup, kOutputDirectoryItem), QString());
    return settings;
}

void StemConversionSettings::writeTo(const UserSettingsPointer& pConfig) const {
    VERIFY_OR_DEBUG_ASSERT(pConfig) {
        return;
    }
    pConfig->setValue(ConfigKey(kConfigGroup, kSeparatorCommandItem), m_separatorCommand);
    pConfig->setValue(ConfigKey(kConfigGroup, kModelItem), m_model);
    pConfig->setValue(ConfigKey(kConfigGroup, kEncoderCommandItem), m_encoderCommand);
    pConfig->setValue(ConfigKey(kConfigGroup, kMuxerPathItem), m_muxerPath);
    pConfig->setValue(ConfigKey(kConfigGroup, kOutputModeItem),
            m_outputMode == StemOutputMode::CustomDirectory
                    ? kCustomDirectoryValue
                    : kSourceDirectoryValue);
    pConfig->setValue(ConfigKey(kConfigGroup, kOutputDirectoryItem), m_outputDirectory);
}

QString StemConversionSettings::outputFilePathFor(const QString& sourceFilePath) const {
    const QFileInfo sourceFileInfo(sourceFilePath);
    QString baseName = sourceFileInfo.completeBaseName();
    // A source file that is already a stem file must not get a second suffix.
    if (baseName.endsWith(QStringLiteral(".stem"), Qt::CaseInsensitive)) {
        baseName.chop(5);
    }
    QString directory = sourceFileInfo.absolutePath();
    if (m_outputMode == StemOutputMode::CustomDirectory &&
            !m_outputDirectory.isEmpty()) {
        directory = m_outputDirectory;
    }
    return QDir(directory).absoluteFilePath(baseName + kStemFileSuffix);
}

QStringList expandCommandTemplate(const QString& commandTemplate,
        const QMap<QString, QString>& placeholders,
        QString* pErrorMessage) {
    const auto fail = [pErrorMessage](const QString& message) {
        if (pErrorMessage) {
            *pErrorMessage = message;
        }
        return QStringList();
    };

    QStringList tokens;
    QString token;
    bool hasToken = false;
    QChar quote;
    for (const QChar character : commandTemplate) {
        if (!quote.isNull()) {
            if (character == quote) {
                quote = QChar();
            } else {
                token.append(character);
            }
            continue;
        }
        if (character == '"' || character == '\'') {
            quote = character;
            hasToken = true;
            continue;
        }
        if (character.isSpace()) {
            if (hasToken) {
                tokens.append(token);
                token.clear();
                hasToken = false;
            }
            continue;
        }
        token.append(character);
        hasToken = true;
    }
    if (!quote.isNull()) {
        return fail(QObject::tr("The command has an unclosed quote."));
    }
    if (hasToken) {
        tokens.append(token);
    }
    if (tokens.isEmpty()) {
        return fail(QObject::tr("The command is empty."));
    }

    static const QRegularExpression placeholderRegex(
            QStringLiteral("\\$\\{([A-Z_][A-Z0-9_]*)\\}|\\$([A-Z_][A-Z0-9_]*)"));
    QStringList expandedTokens;
    expandedTokens.reserve(tokens.size());
    for (const QString& rawToken : std::as_const(tokens)) {
        QString expanded;
        int copiedUpTo = 0;
        auto matches = placeholderRegex.globalMatch(rawToken);
        while (matches.hasNext()) {
            const QRegularExpressionMatch match = matches.next();
            const QString name = match.captured(1).isEmpty()
                    ? match.captured(2)
                    : match.captured(1);
            if (!placeholders.contains(name)) {
                return fail(QObject::tr("The command uses the unknown placeholder $%1.")
                                    .arg(name));
            }
            expanded.append(rawToken.mid(copiedUpTo, match.capturedStart() - copiedUpTo));
            expanded.append(placeholders.value(name));
            copiedUpTo = match.capturedEnd();
        }
        expanded.append(rawToken.mid(copiedUpTo));
        expandedTokens.append(expanded);
    }
    if (expandedTokens.first().isEmpty()) {
        return fail(QObject::tr("The command has no program name."));
    }
    if (pErrorMessage) {
        pErrorMessage->clear();
    }
    return expandedTokens;
}

QString stemRoleName(StemRole role) {
    switch (role) {
    case StemRole::Drums:
        return QStringLiteral("Drums");
    case StemRole::Bass:
        return QStringLiteral("Bass");
    case StemRole::Other:
        return QStringLiteral("Other");
    case StemRole::Vocals:
        return QStringLiteral("Vocals");
    }
    DEBUG_ASSERT(!"unreachable");
    return QString();
}

QString stemManifestJson() {
    QJsonArray stems;
    for (int stemIndex = 0; stemIndex < StemConversionSettings::kStemCount; ++stemIndex) {
        QJsonObject stem;
        stem.insert(QStringLiteral("name"), stemRoleName(static_cast<StemRole>(stemIndex)));
        stem.insert(QStringLiteral("color"), QString::fromLatin1(kStemColors[stemIndex]));
        stems.append(stem);
    }

    QJsonObject compressor;
    compressor.insert(QStringLiteral("enabled"), false);
    QJsonObject limiter;
    limiter.insert(QStringLiteral("enabled"), false);
    QJsonObject masteringDsp;
    masteringDsp.insert(QStringLiteral("compressor"), compressor);
    masteringDsp.insert(QStringLiteral("limiter"), limiter);

    QJsonObject manifest;
    manifest.insert(QStringLiteral("version"), 1);
    manifest.insert(QStringLiteral("mastering_dsp"), masteringDsp);
    manifest.insert(QStringLiteral("stems"), stems);
    return QString::fromUtf8(QJsonDocument(manifest).toJson(QJsonDocument::Compact));
}

} // namespace mixxx
