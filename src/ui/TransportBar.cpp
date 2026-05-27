#include "TransportBar.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>

#include "visualizer/Visualizer.h"

namespace auralbit::ui {

namespace {

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

    visualizer_ = new visualizer::Visualizer(this);
    tLayout->addWidget(visualizer_, 1);

    btn_mute_ = makeTransportButton("🔊", this);
    tLayout->addWidget(btn_mute_);

    volume_ = new QSlider(Qt::Horizontal, this);
    volume_->setObjectName("volumeSlider");
    volume_->setRange(0, 100);
    volume_->setValue(100);
    volume_->setFixedWidth(70);
    tLayout->addWidget(volume_);

    chip_codec_ = makeChip("—", this);
    chip_rate_ = makeChip("—", this);
    tLayout->addWidget(chip_codec_);
    tLayout->addWidget(chip_rate_);

    connect(btn_prev_, &QToolButton::clicked, this, &TransportBar::prevClicked);
    connect(btn_play_, &QToolButton::clicked, this, &TransportBar::playPauseClicked);
    connect(btn_next_, &QToolButton::clicked, this, &TransportBar::nextClicked);

    connect(volume_, &QSlider::valueChanged, this, [this](int value) {
        updateMuteGlyph(value);
        emit volumeChanged(value / 100.0);
    });
    connect(btn_mute_, &QToolButton::clicked, this, [this] {
        if (volume_->value() > 0) {
            volume_before_mute_ = volume_->value();
            volume_->setValue(0);
        } else {
            volume_->setValue(volume_before_mute_ > 0 ? volume_before_mute_ : 100);
        }
    });
}

void TransportBar::setVolume(double volume) {
    const int v = static_cast<int>(volume * 100 + 0.5);
    QSignalBlocker block(volume_);  // Don't echo back into volumeChanged.
    volume_->setValue(v);
    updateMuteGlyph(v);
}

void TransportBar::updateMuteGlyph(int slider_value) {
    btn_mute_->setText(slider_value == 0 ? "🔇" : "🔊");
}

void TransportBar::setFormatChips(const QString& codec, const QString& sample_rate_khz) {
    chip_codec_->setText(codec.isEmpty() ? "—" : codec);
    chip_rate_->setText(sample_rate_khz.isEmpty() ? "—" : sample_rate_khz);
}

void TransportBar::setPlayer(audio::Player* player) { visualizer_->setPlayer(player); }

}  // namespace auralbit::ui
