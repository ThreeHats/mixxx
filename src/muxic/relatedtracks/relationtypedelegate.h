#pragma once

#include <QStringList>

#include "library/tabledelegates/tableitemdelegate.h"

namespace muxic {

class RelatedTracksTableModel;

/// Edits the relation type with a combo box that the user can also type in.
class RelationTypeDelegate : public TableItemDelegate {
    Q_OBJECT

  public:
    RelationTypeDelegate(QTableView* pTableView, const RelatedTracksTableModel* pModel);
    ~RelationTypeDelegate() override = default;

    QWidget* createEditor(QWidget* pParent,
            const QStyleOptionViewItem& option,
            const QModelIndex& index) const override;
    void setEditorData(QWidget* pEditor, const QModelIndex& index) const override;
    void setModelData(QWidget* pEditor,
            QAbstractItemModel* pModel,
            const QModelIndex& index) const override;

  private:
    const RelatedTracksTableModel* const m_pModel;
};

} // namespace muxic
