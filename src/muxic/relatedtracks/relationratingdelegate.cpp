#include "muxic/relatedtracks/relationratingdelegate.h"

#include <QModelIndex>

#include "moc_relationratingdelegate.cpp"

namespace muxic {

RelationRatingDelegate::RelationRatingDelegate(QTableView* pTableView)
        : StarDelegate(pTableView) {
}

void RelationRatingDelegate::paintItem(QPainter* pPainter,
        const QStyleOptionViewItem& option,
        const QModelIndex& index) const {
    if (!index.data().isValid()) {
        paintItemBackground(pPainter, option, index);
        return;
    }
    StarDelegate::paintItem(pPainter, option, index);
}

void RelationRatingDelegate::cellEntered(const QModelIndex& index) {
    if (index.isValid() && !index.flags().testFlag(Qt::ItemIsEditable)) {
        // The stars of this cell are a report, not an editor. An invalid
        // index closes an editor that another cell left open.
        StarDelegate::cellEntered(QModelIndex());
        return;
    }
    StarDelegate::cellEntered(index);
}

} // namespace muxic
