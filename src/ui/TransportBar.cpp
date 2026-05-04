#include "TransportBar.h"

#include <cmath>

#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>

namespace auralbit::ui {

namespace {

class VisualizerPlaceholder : public QWidget {
public:
    explicit VisualizerPlaceholder(QWidget* parent = nullptr) : QWidget(parent) {
        setMinimumHeight(28);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.fillRect(rect(), QColor("#1a1b1f"));
        const int rows = 7;
        const int cols = 60;
        const int cell_w = std::max(2, width() / cols);
        const int cell_h = std::max(2, height() / rows);
        for (int x = 0; x < cols; ++x) {
            const float t = static_cast<float>(x) / cols;
            const int filled =
                static_cast<int>((std::sin(t * 6.28f) * 0.5f + 0.5f) * rows);
            for (int y = 0; y < rows; ++y) {
                if (rows - 1 - y > filled) continue;
                QColor c("#d6a64a");
                c.setAlphaF(0.35f + 0.65f * (1.0f - static_cast<float>(y) / rows));
                p.fillRect(x * cell_w + 2, y * cell_h + 2, cell_w - 3, cell_h - 3, c);
            }
        }
    }
};

QLabel* makeChip(const QString& text, QWidget* parent) {
    auto* l = new QLabel(text, parent);
    l->setProperty("class", "chip");
    l->setProperty("accent", true);
    l->setAlignment(Qt::AlignCenter);
    return l;
}

QToolButton* makeTransportButton(const QString& glyph, QWidget* parent) {
    auto* b = new QToolButton(parent);
    b->setObjectName("transportBtn");
    b->setText(glyph);
    b->setAutoRaise(true);
    QFont f = b->font();
    f.setPointSize(14);
    b->setFont(f);
    return b;
}

}  // namespace

TransportBar::TransportBar(QWidget* parent) : QWidget(parent) {
    setObjectName("transportRow");
    auto* tLayout = new QHBoxLayout(this);
    tLayout->setContentsMargins(8, 6, 10, 8);
    tLayout->setSpacing(6);

    btn_prev_ = makeTransportButton("⏮", this);
    btn_play_ = makeTransportButton("⏸", this);
    btn_play_->setCheckable(true);
    btn_play_->setChecked(true);
    btn_next_ = makeTransportButton("⏭", this);

    tLayout->addWidget(btn_prev_);
    tLayout->addWidget(btn_play_);
    tLayout->addWidget(btn_next_);

    visualizer_ = new VisualizerPlaceholder(this);
    tLayout->addWidget(visualizer_, 1);

    chip_codec_ = makeChip("—", this);
    chip_rate_ = makeChip("—", this);
    tLayout->addWidget(chip_codec_);
    tLayout->addWidget(chip_rate_);

    connect(btn_prev_, &QToolButton::clicked, this, &TransportBar::prevClicked);
    connect(btn_play_, &QToolButton::clicked, this, &TransportBar::playPauseClicked);
    connect(btn_next_, &QToolButton::clicked, this, &TransportBar::nextClicked);
}

void TransportBar::setFormatChips(const QString& codec, const QString& sample_rate_khz) {
    chip_codec_->setText(codec.isEmpty() ? "—" : codec);
    chip_rate_->setText(sample_rate_khz.isEmpty() ? "—" : sample_rate_khz);
}

}  // namespace auralbit::ui
