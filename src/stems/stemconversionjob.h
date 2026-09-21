#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTemporaryDir>
#include <atomic>
#include <memory>

#include "stems/stemconversionsettings.h"
#include "track/track_decl.h"
#include "util/parented_ptr.h"

namespace mixxx {

/// One conversion of one track to a stem file. The programs run in QProcess
/// and the file steps in a worker thread, thus no step blocks the GUI.
class StemConversionJob : public QObject {
    Q_OBJECT

  public:
    enum class State {
        Queued,
        Preparing,
        Separating,
        Collecting,
        Encoding,
        Muxing,
        Tagging,
        Finishing,
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
    /// The path that the job writes. It is known before the job starts.
    const QString& outputFilePath() const {
        return m_outputFilePath;
    }
    bool isDone() const;

    /// The text of the state, for the conversion window.
    static QString stateText(State state);

    /// The four stem files that the separator wrote, in manifest order. The
    /// base name of the source drops out of each name. Empty on an error.
    static QStringList findStemFiles(const QString& directoryPath,
            const QString& sourceBaseName,
            QString* pErrorMessage);

  public slots:
    void start();
    void cancel();

  signals:
    void changed();
    void finished();

  private:
    // These steps touch files. They run in a worker thread and return an
    // error message, or an empty string after success.
    QString prepare();
    QString collectStemFiles();
    QString finish();

    void runAsyncStep(QString (StemConversionJob::*step)(), State state);
    void onAsyncStepFinished();

    void runSeparator();
    void runNextEncode();
    void runMux();
    void runTag();
    void succeed();

    QString writeSourceCoverImage();
    QString exportSourceTags();
    QString moveIntoPlace();
    QString separatedDirPath() const;
    void cleanUp();

    void startProcess(const QStringList& command, State state);
    void onProcessFinished(int exitCode);
    void onProcessError(QProcess::ProcessError error);
    void onOutputReady();
    void fail(const QString& message);
    void setState(State state);
    void setProgress(double progress);

    const StemConversionSettings m_settings;
    const TrackPointer m_pSourceTrack;
    const QString m_sourceFilePath;
    const QString m_outputFilePath;

    std::unique_ptr<QTemporaryDir> m_pWorkDir;
    parented_ptr<QProcess> m_pProcess;
    QFutureWatcher<QString> m_watcher;

    QString m_workOutputFilePath;
    QString m_manifestFilePath;
    QString m_coverImageFilePath;
    /// The file in the output directory that the job owns. Only this file
    /// and the work directory go away on a failure.
    QString m_partFilePath;

    QStringList m_muxInputs;
    QStringList m_encodeQueue;
    int m_encodeCount;

    State m_state;
    double m_progress;
    QString m_errorMessage;
    QString m_outputTail;
    std::atomic<bool> m_cancelRequested;
};

} // namespace mixxx
