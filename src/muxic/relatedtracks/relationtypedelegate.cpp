#include "muxic/relatedtracks/relationtypedelegate.h"

#include <QComboBox>

#include "moc_relationtypedelegate.cpp"
#include "muxic/relatedtracks/relatedtrackstablemodel.h"

namespace muxic {

RelationTypeDelegate::RelationTypeDelegate(
        QTableView* pTableView,
        const RelatedTracksTableModel* pModel)
        : TableItemDelegate(pTableView),
          m_pModel(pModel) {
}

QWidget* RelationTypeDelegate::createEditor(QWidget* pParent,
        const QStyleOptionViewItem& option,
        const QModelIndex& index) const {
    Q_UNUSED(option);
    Q_UNUSED(index);
    auto* pComboBox = new QComboBox(pParent);
    pComboBox->setEditable(true);
    pComboBox->setInsertPolicy(QComboBox::NoInsert);
    if (m_pModel) {
        pComboBox->addItems(m_pModel->knownRelationTypes());
    }
    return pComboBox;
}

void RelationTypeDelegate::setEditorData(QWidget* pEditor, const QModelIndex& index) const {
    auto* pComboBox = qobject_cast<QComboBox*>(pEditor);
    VERIFY_OR_DEBUG_ASSERT(pComboBox) {
        return;
    }
    const QString type = index.data(Qt::EditRole).toString();
    const int row = pComboBox->findText(type);
    if (row >= 0) {
        pComboBox->setCurrentIndex(row);
    } else {
        pComboBox->setCurrentText(type);
    }
}

void RelationTypeDelegate::setModelData(QWidget* pEditor,
        QAbstractItemModel* pModel,
        const QModelIndex& index) const {
    auto* pComboBox = qobject_cast<QComboBox*>(pEditor);
    VERIFY_OR_DEBUG_ASSERT(pComboBox && pModel) {
        return;
    }
    pModel->setData(index, pComboBox->currentText().trimmed(), Qt::EditRole);
}

} // namespace muxic
