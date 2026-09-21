#include "muxic/relatedtracks/relationratingdelegate.h"

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

} // namespace muxic
