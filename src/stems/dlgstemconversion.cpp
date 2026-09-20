#include "stems/dlgstemconversion.h"

#include <QHeaderView>
#include <QTableWidgetItem>

#include "moc_dlgstemconversion.cpp"
#include "util/assert.h"

namespace {

constexpr int kColumnTrack = 0;
constexpr int kColumnState = 1;
constexpr int kColumnProgress = 2;
constexpr int kColumnMessage = 3;
constexpr int kColumnCount = 4;
constexpr int kJobIdRole = Qt::UserRole;

} // anonymous namespace

namespace mixxx {

DlgStemConversion::DlgStemConversion(QWidget* pParent, StemConversionManager* pManager)
        : QDialog(pParent),
          m_pManager(pManager) {
    setupUi(this);

    jobTable->setColumnCount(kColumnCount);
    jobTable->setHorizontalHeaderLabels(
            {tr("Track"), tr("State"), tr("Progress"), tr("Message")});
    jobTable->horizontalHeader()->setStretchLastSection(true);
    jobTable->verticalHeader()->setVisible(false);

    connect(closeButton, &QPushButton::clicked, this, &QDialog::close);
    connect(cancelSelectedButton,
            &QPushButton::clicked,
            this,
            &DlgStemConversion::slotCancelSelected);
    if (m_pManager) {
        connect(cancelAllButton,
                &QPushButton::clicked,
                m_pManager,
                &StemConversionManager::cancelAll);
        connect(clearFinishedButton,
                &QPushButton::clicked,
                m_pManager,
                &StemConversionManager::clearFinished);
        connect(m_pManager,
                &StemConversionManager::jobsChanged,
                this,
                &DlgStemConversion::slotRefresh);
    }
    slotRefresh();
}

void DlgStemConversion::slotRefresh() {
    if (!m_pManager) {
        return;
    }
    const QList<StemConversionManager::JobStatus> statuses = m_pManager->jobStatuses();
    jobTable->setRowCount(statuses.size());
    for (int row = 0; row < statuses.size(); ++row) {
        const StemConversionManager::JobStatus& status = statuses.at(row);
        auto* pTrackItem = new QTableWidgetItem(status.title);
        pTrackItem->setData(kJobIdRole, status.id);
        jobTable->setItem(row, kColumnTrack, pTrackItem);
        jobTable->setItem(row,
                kColumnState,
                new QTableWidgetItem(StemConversionJob::stateText(status.state)));
        const bool hasProgress = status.state != StemConversionJob::State::Failed &&
                status.state != StemConversionJob::State::Cancelled;
        jobTable->setItem(row,
                kColumnProgress,
                new QTableWidgetItem(hasProgress
                                ? QStringLiteral("%1 %").arg(QString::number(
                                          qRound(status.progress * 100)))
                                : QString()));
        jobTable->setItem(row, kColumnMessage, new QTableWidgetItem(status.message));
    }
    jobTable->resizeColumnToContents(kColumnState);
    jobTable->resizeColumnToContents(kColumnProgress);
}

void DlgStemConversion::slotCancelSelected() {
    if (!m_pManager) {
        return;
    }
    const QList<QTableWidgetItem*> selected = jobTable->selectedItems();
    QList<int> jobIds;
    for (const QTableWidgetItem* pItem : selected) {
        if (pItem->column() != kColumnTrack) {
            continue;
        }
        jobIds.append(pItem->data(kJobIdRole).toInt());
    }
    for (int jobId : jobIds) {
        m_pManager->cancel(jobId);
    }
}

} // namespace mixxx
