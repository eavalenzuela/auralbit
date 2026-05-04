#pragma once

#include <QPersistentModelIndex>
#include <QTreeView>

namespace auralbit::ui {

// QTreeView that paints a left-to-right "currently playing" progress wash
// across the row of the currently playing track. We override drawRow so we
// can paint across the whole row regardless of column boundaries.
class LibraryTree : public QTreeView {
    Q_OBJECT
public:
    explicit LibraryTree(QWidget* parent = nullptr);

    void setCurrentTrack(const QModelIndex& idx);
    void setProgress(double fraction);  // [0, 1]

protected:
    void drawRow(QPainter* painter, const QStyleOptionViewItem& option,
                 const QModelIndex& index) const override;

private:
    QPersistentModelIndex current_;
    double progress_ = 0.0;
};

}  // namespace auralbit::ui
