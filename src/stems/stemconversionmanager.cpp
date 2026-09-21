#include "stems/stemconversionmanager.h"

#include <algorithm>

#include "library/trackcollectionmanager.h"
#include "moc_stemconversionmanager.cpp"
#include "stems/dlgstemconversion.h"
#include "stems/stemtrackcopy.h"
#include "track/track.h"
#include "track/trackref.h"
#include "util/assert.h"
#include "util/fileinfo.h"
#include "util/logger.h"

namespace {

const mixxx::Logger kLogger("StemConversionManager");

} // anonymous namespace

namespace mixxx {

StemConversionManager::StemConversionManager(UserSettingsPointer pConfig,
        TrackCollectionManager* pTrackCollectionManager,
        QObject* pParent)
        : QObject(pParent),
          m_pConfig(std::move(pConfig)),
          m_pTrackCollectionManager(pTrackCollectionManager),
          m_nextJobId(1),
          m_startScheduled(false) {
}

StemConversionManager::~StemConversionManager() {
    cancelAll();
}

int StemConversionManager::enqueue(const TrackPointerList& tracks) {
    const StemConversionSettings settings = StemConversionSettings::readFrom(m_pConfig);
    int added = 0;
    for (const TrackPointer& pTrack : tracks) {
        if (!pTrack) {
            continue;
        }
        auto* pJob = new StemConversionJob(settings, pTrack, this);
        JobEntry entry;
        entry.status.id = m_nextJobId++;
        entry.status.title = pJob->sourceTitle();
        entry.status.outputFilePath = pJob->outputFilePath();

        // Two jobs that write one file would race.
        if (isOutputPathTaken(entry.status.outputFilePath)) {
            entry.status.state = StemConversionJob::State::Failed;
            entry.status.message =
                    tr("Another conversion in the list writes \"%1\".")
                            .arg(entry.status.outputFilePath);
            delete pJob;
            m_jobs.append(entry);
            added++;
            continue;
        }

        entry.pJob = pJob;
        const int jobId = entry.status.id;
        connect(pJob, &StemConversionJob::changed, this, [this, jobId] {
            const int index = indexOfJob(jobId);
            if (index < 0 || !m_jobs.at(index).pJob) {
                return;
            }
            m_jobs[index].status.state = m_jobs.at(index).pJob->state();
            m_jobs[index].status.progress = m_jobs.at(index).pJob->progress();
            emit jobsChanged();
        });
        connect(pJob, &StemConversionJob::finished, this, [this, jobId] {
            onJobFinished(jobId);
        });
        m_jobs.append(entry);
        added++;
    }
    if (added > 0) {
        emit jobsChanged();
        scheduleStartNext();
    }
    return added;
}

bool StemConversionManager::isOutputPathTaken(const QString& outputFilePath) const {
    if (outputFilePath.isEmpty()) {
        return false;
    }
    for (const JobEntry& entry : m_jobs) {
        if (entry.pJob && entry.status.outputFilePath == outputFilePath) {
            return true;
        }
    }
    return false;
}

void StemConversionManager::cancel(int jobId) {
    const int index = indexOfJob(jobId);
    if (index < 0) {
        return;
    }
    if (m_jobs.at(index).pJob) {
        m_jobs.at(index).pJob->cancel();
    }
}

void StemConversionManager::cancelAll() {
    for (const JobEntry& entry : std::as_const(m_jobs)) {
        if (entry.pJob) {
            entry.pJob->cancel();
        }
    }
}

void StemConversionManager::clearFinished() {
    const int before = m_jobs.size();
    m_jobs.erase(std::remove_if(m_jobs.begin(),
                         m_jobs.end(),
                         [](const JobEntry& entry) {
                             return !entry.pJob;
                         }),
            m_jobs.end());
    if (m_jobs.size() != before) {
        emit jobsChanged();
    }
}

QList<StemConversionManager::JobStatus> StemConversionManager::jobStatuses() const {
    QList<JobStatus> statuses;
    statuses.reserve(m_jobs.size());
    for (const JobEntry& entry : m_jobs) {
        statuses.append(entry.status);
    }
    return statuses;
}

void StemConversionManager::scheduleStartNext() {
    if (m_startScheduled) {
        return;
    }
    // The event loop breaks the chain of a list of jobs that all fail at
    // once, thus no call frame nests per job.
    m_startScheduled = true;
    QMetaObject::invokeMethod(this, &StemConversionManager::startNext, Qt::QueuedConnection);
}

void StemConversionManager::startNext() {
    m_startScheduled = false;
    StemConversionJob* pNext = nullptr;
    for (const JobEntry& entry : std::as_const(m_jobs)) {
        if (!entry.pJob) {
            continue;
        }
        if (entry.pJob->state() != StemConversionJob::State::Queued) {
            // One job at a time.
            return;
        }
        if (!pNext) {
            pNext = entry.pJob;
        }
    }
    if (pNext) {
        pNext->start();
    }
}

void StemConversionManager::onJobFinished(int jobId) {
    const int index = indexOfJob(jobId);
    VERIFY_OR_DEBUG_ASSERT(index >= 0 && m_jobs.at(index).pJob) {
        return;
    }
    JobEntry& entry = m_jobs[index];
    entry.status.state = entry.pJob->state();
    entry.status.progress = entry.pJob->progress();
    entry.status.message = entry.pJob->errorMessage();
    entry.status.outputFilePath = entry.pJob->outputFilePath();

    if (entry.status.state == StemConversionJob::State::Succeeded) {
        addStemTrackToLibrary(&entry);
    }

    entry.pJob->deleteLater();
    entry.pJob.clear();
    emit jobsChanged();
    scheduleStartNext();
}

void StemConversionManager::addStemTrackToLibrary(JobEntry* pEntry) {
    VERIFY_OR_DEBUG_ASSERT(pEntry && pEntry->pJob && m_pTrackCollectionManager) {
        return;
    }
    const TrackPointer pSourceTrack = pEntry->pJob->sourceTrack();
    auto fileInfo = FileInfo(pEntry->status.outputFilePath);
    fileInfo.checkFileExists();
    const TrackPointer pStemTrack =
            m_pTrackCollectionManager->getOrAddTrack(TrackRef::fromFileInfo(fileInfo));
    if (!pStemTrack) {
        pEntry->status.state = StemConversionJob::State::Failed;
        pEntry->status.message = tr("The stem file cannot be added to the library.");
        return;
    }

    copyTrackToStem(*pSourceTrack, pStemTrack.get());
    m_pTrackCollectionManager->saveTrack(pStemTrack);

    const audio::SampleRate sourceSampleRate = pSourceTrack->getSampleRate();
    const audio::SampleRate stemSampleRate = pStemTrack->getSampleRate();
    if (sourceSampleRate.isValid() && stemSampleRate.isValid() &&
            sourceSampleRate != stemSampleRate) {
        pEntry->status.message =
                tr("The stem file has %1 Hz and the source has %2 Hz. The cue "
                   "points and the grid keep their time, not their frame.")
                        .arg(QString::number(stemSampleRate.value()),
                                QString::number(sourceSampleRate.value()));
        kLogger.info() << "Stem file sample rate" << stemSampleRate
                       << "differs from the source rate" << sourceSampleRate;
    }
}

void StemConversionManager::showConversions(QWidget* pParent) {
    QWidget* pTopLevel = pParent ? pParent->window() : nullptr;
    if (!m_pDialog) {
        m_pDialog = new DlgStemConversion(pTopLevel, this);
    }
    m_pDialog->show();
    m_pDialog->raise();
    m_pDialog->activateWindow();
}

int StemConversionManager::indexOfJob(int jobId) const {
    for (int index = 0; index < m_jobs.size(); ++index) {
        if (m_jobs.at(index).status.id == jobId) {
            return index;
        }
    }
    return -1;
}

} // namespace mixxx
