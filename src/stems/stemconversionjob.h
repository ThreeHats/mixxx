#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <memory>

#include "stems/stemconversionsettings.h"
#include "track/track_decl.h"
#include "util/parented_ptr.h"

class QProcess;

namespace mixxx {

/// One conversion of one track to a stem file. The job drives external
/// programs with QProcess, thus it blocks neither the GUI nor the engine.
class StemConversionJob : public QObject {
    Q_OBJECT

  public:
    enum class State {
        Queued,
        Separating,
        Encoding,
        Muxing,
        Tagging,
        Succeeded,
        Failed,
        Cancelled,
    };

    StemConversionJob(const StemConversionSettings& settings,
            TrackPointer pSourceTrack,
            QObject* pParent = nullptr);
    ~StemConversionJob() override;

    const TrackPointer& sourceTrack() const {
        return m_pSourceTrack;
    }
    QString sourceTitle() const;

    State state() const {
        return m_state;
    }
    /// The progress of the job, from 0.0 to 1.0.
    double progress() const {
        return m_progress;
    }
    const QString& errorMessage() const {
        return m_errorMessage;
    }
    const QString& outputFilePath() const {
        return m_outputFilePath;
    }
    bool isDone() const;

    /// The text of the state, for the conversion window.
    static QString stateText(State state);

    /// Find the four stem files that the separator wrote, in manifest order.
    /// Returns an empty list on an error.
    static QStringList findStemFiles(const QString& directoryPath,
            QString* pErrorMessage);

  public slots:
    void start();
    void cancel();

  signals:
    void changed();
    void finished();

  private:
    void runSeparator();
    void runNextEncode();
    void runMux();
    void runTag();
    void complete();

    /// Write the cover image of the source file in the work directory.
    /// Returns an empty path when the source carries no image.
    QString writeSourceCoverImage();
    void exportSourceTags();

    void startProcess(const QStringList& command, State state);
    void onProcessFinished(int exitCode);
    void onStandardErrorReady();
    void fail(const QString& message);
    void setState(State state);
    void setProgress(double progress);

    const StemConversionSettings m_settings;
    const TrackPointer m_pSourceTrack;
    const QString m_sourceFilePath;
    QString m_outputFilePath;

    std::unique_ptr<QTemporaryDir> m_pWorkDir;
    parented_ptr<QProcess> m_pProcess;

    QStringList m_muxInputs;
    QStringList m_encodeQueue;
    int m_encodeCount;

    State m_state;
    double m_progress;
    QString m_errorMessage;
    QString m_standardErrorTail;
    bool m_cancelRequested;
};

using StemConversionJobPointer = std::shared_ptr<StemConversionJob>;

} // namespace mixxx
