#include "LibraryTree.h"

#include <algorithm>

#include <QPainter>

namespace auralbit::ui {

LibraryTree::LibraryTree(QWidget* parent) : QTreeView(parent) {}

void LibraryTree::setCurrentTrack(const QModelIndex& idx) {
    current_ = idx;
    viewport()->update();
}

void LibraryTree::setProgress(double fraction) {
    progress_ = std::clamp(fraction, 0.0, 1.0);
    viewport()->update();
}

void LibraryTree::drawRow(QPainter* painter, const QStyleOptionViewItem& option,
                          const QModelIndex& index) const {
    if (current_.isValid() && progress_ > 0.0) {
        // Compare on row identity (column 0 of the same parent) so it doesn't
        // matter which column variant of the index is supplied.
        const QModelIndex row_index = index.sibling(index.row(), 0);
        const QModelIndex current_row = current_.sibling(current_.row(), 0);
        if (row_index == current_row) {
            const QRect r = option.rect;
            const int filled = static_cast<int>(r.width() * progress_);
            // Soft warm red so the row text stays legible. Alpha-blended.
            const QColor wash(220, 86, 86, 70);
            painter->fillRect(QRect(r.left(), r.top(), filled, r.height()), wash);
        }
    }
    QTreeView::drawRow(painter, option, index);
}

}  // namespace auralbit::ui
