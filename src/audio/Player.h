#pragma once

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "AudioOutput.h"
#include "Decoder.h"

namespace auralbit::audio {

enum class PlayerState { Stopped, Playing, Paused };

class Player {
public:
    Player();
    ~Player();

    Player(const Player&) = delete;
    Player& operator=(const Player&) = delete;

    bool load(const std::string& path);
    void play();
    void pause();
    void stop();
    bool seek_seconds(double seconds);

    PlayerState state() const { return state_.load(std::memory_order_acquire); }
    double position_seconds() const;
    double duration_seconds() const;

private:
    void decode_loop();

    AudioOutput output_;

    std::mutex mu_;  // Guards decoder_ and seek_request_.
    std::unique_ptr<Decoder> decoder_;
    int64_t seek_request_ = -1;  // Frame index, or -1 for none.

    std::thread thread_;
    std::condition_variable cv_;
    std::atomic<PlayerState> state_{PlayerState::Stopped};
    std::atomic<bool> quit_{false};
    std::atomic<uint64_t> cursor_frames_{0};
    std::atomic<uint64_t> total_frames_{0};
    std::atomic<uint32_t> sample_rate_{0};
};

}  // namespace auralbit::audio
