#pragma once

#include <QDialog>
#include <QPointer>

#include "stems/stemconversionmanager.h"
#include "stems/ui_dlgstemconversion.h"

namespace mixxx {

/// The window that lists the stem conversions that wait, run or are done.
class DlgStemConversion : public QDialog, public Ui::DlgStemConversion {
    Q_OBJECT

  public:
    DlgStemConversion(QWidget* pParent, StemConversionManager* pManager);
    ~DlgStemConversion() override = default;

  private slots:
    void slotRefresh();
    void slotCancelSelected();

  private:
    const QPointer<StemConversionManager> m_pManager;
};

} // namespace mixxx
