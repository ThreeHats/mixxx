#pragma once

#include "library/tabledelegates/stardelegate.h"

namespace muxic {

/// Paints the stars of a relation rating, and nothing when the row carries
/// no relation. StarDelegate would paint five placeholders instead.
class RelationRatingDelegate : public StarDelegate {
    Q_OBJECT

  public:
    explicit RelationRatingDelegate(QTableView* pTableView);
    ~RelationRatingDelegate() override = default;

    void paintItem(QPainter* pPainter,
            const QStyleOptionViewItem& option,
            const QModelIndex& index) const override;

  protected slots:
    void cellEntered(const QModelIndex& index) override;
};

} // namespace muxic
