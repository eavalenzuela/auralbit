#include "Player.h"

#include <chrono>
#include <vector>

namespace auralbit::audio {

namespace {
constexpr size_t kDecodeChunkFrames = 2048;
}  // namespace

Player::Player() = default;

Player::~Player() {
    quit_.store(true, std::memory_order_release);
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    output_.stop();
}

bool Player::load(const std::string& path) {
    stop();

    auto decoder = open_decoder(path);
    if (!decoder) return false;

    const AudioFormat fmt = decoder->format();
    if (!output_.start(fmt.sample_rate, fmt.channels)) {
        return false;
    }

    {
        std::lock_guard lk(mu_);
        decoder_ = std::move(decoder);
        seek_request_ = -1;
    }
    sample_rate_.store(fmt.sample_rate, std::memory_order_release);
    total_frames_.store(decoder_->total_frames(), std::memory_order_release);
    cursor_frames_.store(0, std::memory_order_release);

    quit_.store(false, std::memory_order_release);
    state_.store(PlayerState::Paused, std::memory_order_release);
    thread_ = std::thread(&Player::decode_loop, this);
    return true;
}

void Player::play() {
    if (state_.load(std::memory_order_acquire) == PlayerState::Stopped) return;
    state_.store(PlayerState::Playing, std::memory_order_release);
    cv_.notify_all();
}

void Player::pause() {
    if (state_.load(std::memory_order_acquire) == PlayerState::Playing) {
        state_.store(PlayerState::Paused, std::memory_order_release);
    }
}

void Player::stop() {
    quit_.store(true, std::memory_order_release);
    state_.store(PlayerState::Stopped, std::memory_order_release);
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
    output_.stop();
    {
        std::lock_guard lk(mu_);
        decoder_.reset();
        seek_request_ = -1;
    }
    cursor_frames_.store(0, std::memory_order_release);
    total_frames_.store(0, std::memory_order_release);
    sample_rate_.store(0, std::memory_order_release);
}

bool Player::seek_seconds(double seconds) {
    const uint32_t sr = sample_rate_.load(std::memory_order_acquire);
    if (sr == 0) return false;
    const int64_t frame = static_cast<int64_t>(seconds * sr);
    std::lock_guard lk(mu_);
    seek_request_ = frame < 0 ? 0 : frame;
    cv_.notify_all();
    return true;
}

double Player::position_seconds() const {
    const uint32_t sr = sample_rate_.load(std::memory_order_acquire);
    if (sr == 0) return 0.0;
    return static_cast<double>(cursor_frames_.load(std::memory_order_acquire)) / sr;
}

double Player::duration_seconds() const {
    const uint32_t sr = sample_rate_.load(std::memory_order_acquire);
    if (sr == 0) return 0.0;
    return static_cast<double>(total_frames_.load(std::memory_order_acquire)) / sr;
}

void Player::decode_loop() {
    const uint16_t channels = output_.channels();
    std::vector<float> chunk(kDecodeChunkFrames * channels);

    while (!quit_.load(std::memory_order_acquire)) {
        // Handle pause: sleep until played or quit.
        {
            std::unique_lock lk(mu_);
            cv_.wait(lk, [&] {
                return quit_.load(std::memory_order_acquire) ||
                       state_.load(std::memory_order_acquire) == PlayerState::Playing ||
                       seek_request_ >= 0;
            });
            if (quit_.load(std::memory_order_acquire)) return;

            if (seek_request_ >= 0) {
                if (decoder_) {
                    decoder_->seek(static_cast<uint64_t>(seek_request_));
                    cursor_frames_.store(decoder_->cursor(), std::memory_order_release);
                    output_.ring().clear();
                }
                seek_request_ = -1;
            }
        }

        // Wait for buffer space; sleep briefly to avoid busy-loop.
        if (output_.ring().write_available() < kDecodeChunkFrames * channels) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        size_t frames_read = 0;
        {
            std::lock_guard lk(mu_);
            if (!decoder_) break;
            frames_read = decoder_->read(chunk.data(), kDecodeChunkFrames);
            cursor_frames_.store(decoder_->cursor(), std::memory_order_release);
        }

        if (frames_read == 0) {
            // EOF — wait for the ring to drain so MainWindow's auto-advance
            // doesn't tear down the device while the tail is still queued.
            while (!quit_.load(std::memory_order_acquire) &&
                   output_.ring().read_available() > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            // Let miniaudio's internal buffer flush before signalling Stopped.
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            state_.store(PlayerState::Stopped, std::memory_order_release);
            break;
        }

        const size_t samples = frames_read * channels;
        size_t written = 0;
        while (written < samples && !quit_.load(std::memory_order_acquire)) {
            written += output_.ring().write(chunk.data() + written, samples - written);
            if (written < samples) {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
    }
}

}  // namespace auralbit::audio
