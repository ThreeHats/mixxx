#pragma once

#include <QMap>
#include <QString>
#include <QStringList>

#include "preferences/usersettings.h"

namespace mixxx {

/// The place where a stem conversion writes the stem file.
enum class StemOutputMode {
    SourceDirectory,
    CustomDirectory,
};

/// The four stems of a stem file, in the order of the stem manifest.
enum class StemRole {
    Drums = 0,
    Bass = 1,
    Other = 2,
    Vocals = 3,
};

/// The user settings of the stem conversion.
class StemConversionSettings {
  public:
    static constexpr int kStemCount = 4;

    static const QString kConfigGroup;
    static const QString kSeparatorCommandItem;
    static const QString kModelItem;
    static const QString kEncoderCommandItem;
    static const QString kMuxerPathItem;
    static const QString kOutputModeItem;
    static const QString kOutputDirectoryItem;

    static QString defaultSeparatorCommand();
    static QString defaultModel();
    static QString defaultEncoderCommand();
    static QString defaultMuxerPath();

    static StemConversionSettings readFrom(const UserSettingsPointer& pConfig);
    void writeTo(const UserSettingsPointer& pConfig) const;

    const QString& separatorCommand() const {
        return m_separatorCommand;
    }
    void setSeparatorCommand(const QString& command) {
        m_separatorCommand = command;
    }

    const QString& model() const {
        return m_model;
    }
    void setModel(const QString& model) {
        m_model = model;
    }

    /// The command that recodes one audio file for the muxer. An empty
    /// command puts the separated files in the stem file without a recode.
    const QString& encoderCommand() const {
        return m_encoderCommand;
    }
    void setEncoderCommand(const QString& command) {
        m_encoderCommand = command;
    }

    const QString& muxerPath() const {
        return m_muxerPath;
    }
    void setMuxerPath(const QString& path) {
        m_muxerPath = path;
    }

    StemOutputMode outputMode() const {
        return m_outputMode;
    }
    void setOutputMode(StemOutputMode mode) {
        m_outputMode = mode;
    }

    const QString& outputDirectory() const {
        return m_outputDirectory;
    }
    void setOutputDirectory(const QString& directory) {
        m_outputDirectory = directory;
    }

    /// The path of the stem file that a source file gets.
    QString outputFilePathFor(const QString& sourceFilePath) const;

  private:
    QString m_separatorCommand = defaultSeparatorCommand();
    QString m_model = defaultModel();
    QString m_encoderCommand = defaultEncoderCommand();
    QString m_muxerPath = defaultMuxerPath();
    StemOutputMode m_outputMode = StemOutputMode::SourceDirectory;
    QString m_outputDirectory;
};

/// Split a command template into a program and its arguments, and put the
/// value of each placeholder in place. Returns an empty list on an error.
QStringList expandCommandTemplate(const QString& commandTemplate,
        const QMap<QString, QString>& placeholders,
        QString* pErrorMessage);

/// The name of a stem in the stem manifest, for example "Drums".
QString stemRoleName(StemRole role);

/// The manifest that the stem atom of a stem file carries.
QString stemManifestJson();

} // namespace mixxx
