#include "NowPlayingStrip.h"

#include <algorithm>

#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPixmap>
#include <QSlider>
#include <QVBoxLayout>

namespace auralbit::ui {

namespace {

constexpr int kCoverSize = 44;

QString formatTime(double seconds) {
    if (seconds < 0) seconds = 0;
    const int total = static_cast<int>(seconds);
    return QString("%1:%2").arg(total / 60).arg(total % 60, 2, 10, QChar('0'));
}

// QSlider that jumps to the clicked position instead of paging toward it, so a
// single click on the bar seeks there directly (like every media player).
class SeekSlider : public QSlider {
public:
    using QSlider::QSlider;

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton && maximum() > minimum() && width() > 0) {
            const double frac =
                std::clamp(static_cast<double>(e->pos().x()) / width(), 0.0, 1.0);
            const int v = minimum() + static_cast<int>(frac * (maximum() - minimum()));
            setValue(v);
            emit sliderMoved(v);
            e->accept();
            return;
        }
        QSlider::mousePressEvent(e);
    }
};

}  // namespace

NowPlayingStrip::NowPlayingStrip(QWidget* parent) : QWidget(parent) {
    setObjectName("nowPlayingStrip");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(10, 6, 12, 6);
    root->setSpacing(4);

    auto* infoRow = new QHBoxLayout();
    infoRow->setContentsMargins(0, 0, 0, 0);
    infoRow->setSpacing(10);

    cover_ = new QLabel(this);
    cover_->setObjectName("nowCover");
    cover_->setFixedSize(kCoverSize, kCoverSize);
    cover_->setAlignment(Qt::AlignCenter);
    infoRow->addWidget(cover_);

    auto* textCol = new QVBoxLayout();
    textCol->setContentsMargins(0, 0, 0, 0);
    textCol->setSpacing(1);
    title_ = new QLabel(this);
    title_->setObjectName("nowTitle");
    subtitle_ = new QLabel(this);
    subtitle_->setObjectName("nowSubtitle");
    textCol->addStretch();
    textCol->addWidget(title_);
    textCol->addWidget(subtitle_);
    textCol->addStretch();
    infoRow->addLayout(textCol, 1);

    time_ = new QLabel(this);
    time_->setObjectName("nowTime");
    time_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    infoRow->addWidget(time_);

    root->addLayout(infoRow);

    seek_ = new SeekSlider(Qt::Horizontal, this);
    seek_->setObjectName("seekSlider");
    seek_->setRange(0, 0);
    seek_->setEnabled(false);
    root->addWidget(seek_);

    connect(seek_, &QSlider::sliderMoved, this, [this](int value_ms) {
        const double seconds = value_ms / 1000.0;
        updateTimeLabel(seconds);
        emit seekRequested(seconds);
    });

    clear();
}

void NowPlayingStrip::setTrack(const QString& title, const QString& subtitle,
                               const QString& cover_path, double duration_seconds) {
    title_->setText(title.isEmpty() ? "—" : title);
    subtitle_->setText(subtitle);
    duration_seconds_ = duration_seconds;

    QPixmap pm;
    if (!cover_path.isEmpty()) pm.load(cover_path);
    if (pm.isNull()) {
        cover_->setText("♪");
    } else {
        cover_->setText(QString());
        cover_->setPixmap(pm.scaled(kCoverSize, kCoverSize, Qt::KeepAspectRatioByExpanding,
                                    Qt::SmoothTransformation));
    }

    seek_->setRange(0, static_cast<int>(duration_seconds * 1000));
    seek_->setValue(0);
    seek_->setEnabled(duration_seconds > 0);
    updateTimeLabel(0);
}

void NowPlayingStrip::setPosition(double seconds) {
    if (seek_->isSliderDown()) return;  // Don't fight an active drag.
    seek_->setValue(static_cast<int>(seconds * 1000));
    updateTimeLabel(seconds);
}

void NowPlayingStrip::clear() {
    title_->setText("Nothing playing");
    subtitle_->clear();
    time_->clear();
    cover_->setPixmap(QPixmap());
    cover_->setText("♪");
    duration_seconds_ = 0.0;
    seek_->setRange(0, 0);
    seek_->setValue(0);
    seek_->setEnabled(false);
}

void NowPlayingStrip::updateTimeLabel(double position_seconds) {
    time_->setText(formatTime(position_seconds) + " / " + formatTime(duration_seconds_));
}

}  // namespace auralbit::ui
