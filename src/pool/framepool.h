#ifndef FRAMEPOOL_H
#define FRAMEPOOL_H

#include <atomic>
#include <mutex>
#include <cstddef>
extern "C" {
#include "libavutil/frame.h"
}

class FramePool {
public:
    explicit FramePool(size_t maxSize = 128);
    ~FramePool();

    AVFrame* get();
    void recycle(AVFrame* frame);

    void setMaxSize(size_t newMaxSize);
    size_t maxSize() const;
    void clear();
    void printStats();

private:
    static constexpr size_t kRingCapacity = 256;

    AVFrame* ring_[kRingCapacity];
    size_t ringWrite_{0};
    size_t ringRead_{0};
    size_t maxSize_;
    std::mutex mutex_;  // Protects the ring buffer, supports MPMC scenarios

    std::atomic<int> createCount_{0};
    std::atomic<int> recycleCount_{0};
    std::atomic<int> getCount_{0};
    std::atomic<int> freeCount_{0};
    std::atomic<int> freed_{0};
};

#endif
