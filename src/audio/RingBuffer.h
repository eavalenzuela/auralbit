#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace auralbit::audio {

// Single-producer / single-consumer lock-free ring of float samples.
// The capacity is always a power of two so we can mask instead of mod.
class RingBuffer {
public:
    explicit RingBuffer(size_t requested_capacity) {
        size_t cap = 1;
        while (cap < requested_capacity) cap <<= 1;
        buf_.assign(cap, 0.0f);
        mask_ = cap - 1;
    }

    size_t capacity() const { return buf_.size(); }

    size_t write_available() const {
        const size_t w = write_.load(std::memory_order_relaxed);
        const size_t r = read_.load(std::memory_order_acquire);
        return capacity() - (w - r);
    }

    size_t read_available() const {
        const size_t w = write_.load(std::memory_order_acquire);
        const size_t r = read_.load(std::memory_order_relaxed);
        return w - r;
    }

    size_t write(const float* src, size_t count) {
        const size_t avail = write_available();
        const size_t n = count < avail ? count : avail;
        const size_t w = write_.load(std::memory_order_relaxed);
        for (size_t i = 0; i < n; ++i) {
            buf_[(w + i) & mask_] = src[i];
        }
        write_.store(w + n, std::memory_order_release);
        return n;
    }

    size_t read(float* dst, size_t count) {
        const size_t avail = read_available();
        const size_t n = count < avail ? count : avail;
        const size_t r = read_.load(std::memory_order_relaxed);
        for (size_t i = 0; i < n; ++i) {
            dst[i] = buf_[(r + i) & mask_];
        }
        read_.store(r + n, std::memory_order_release);
        return n;
    }

    void clear() {
        // Caller must guarantee no concurrent reader/writer.
        read_.store(0, std::memory_order_relaxed);
        write_.store(0, std::memory_order_relaxed);
    }

private:
    std::vector<float> buf_;
    size_t mask_ = 0;
    std::atomic<size_t> write_{0};
    std::atomic<size_t> read_{0};
};

}  // namespace auralbit::audio
