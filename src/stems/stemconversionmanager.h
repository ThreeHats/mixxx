#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

#include "preferences/usersettings.h"
#include "stems/stemconversionjob.h"
#include "track/track_decl.h"

class TrackCollectionManager;

namespace mixxx {

/// The queue of the stem conversions. It runs one job at a time, because a
/// separation model takes the memory of the graphics card.
class StemConversionManager : public QObject {
    Q_OBJECT

  public:
    struct JobStatus {
        int id = 0;
        QString title;
        StemConversionJob::State state = StemConversionJob::State::Queued;
        double progress = 0.0;
        QString message;
        QString outputFilePath;
    };

    StemConversionManager(UserSettingsPointer pConfig,
            TrackCollectionManager* pTrackCollectionManager,
            QObject* pParent = nullptr);
    ~StemConversionManager() override;

    /// Put the tracks in the queue. Returns the number of new jobs.
    int enqueue(const TrackPointerList& tracks);

    void cancel(int jobId);
    void cancelAll();
    /// Drop the jobs that are done, failed or cancelled.
    void clearFinished();

    QList<JobStatus> jobStatuses() const;

  signals:
    void jobsChanged();

  private:
    struct JobEntry {
        JobStatus status;
        QPointer<StemConversionJob> pJob;
    };

    void startNext();
    void onJobFinished(int jobId);
    void addStemTrackToLibrary(JobEntry* pEntry);
    int indexOfJob(int jobId) const;

    const UserSettingsPointer m_pConfig;
    TrackCollectionManager* const m_pTrackCollectionManager;

    QList<JobEntry> m_jobs;
    int m_nextJobId;
};

} // namespace mixxx
