#include "framepool.h"
#include "utils/log.h"

FramePool::FramePool(size_t maxSize) : maxSize_(maxSize) {
    for (auto& p : ring_) p = nullptr;
}

FramePool::~FramePool() {
    clear();
}

AVFrame* FramePool::get() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ringRead_ != ringWrite_) {
        AVFrame* frame = ring_[ringRead_ % kRingCapacity];
        ring_[ringRead_ % kRingCapacity] = nullptr;
        ringRead_++;
        av_frame_unref(frame);
        getCount_++;
        return frame;
    }
    createCount_++;
    return av_frame_alloc();
}

void FramePool::recycle(AVFrame* frame) {
    if (!frame) return;
    av_frame_unref(frame);
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = (ringWrite_ >= ringRead_) ? (ringWrite_ - ringRead_) : (kRingCapacity - ringRead_ + ringWrite_);
    if (count >= maxSize_) {
        freeCount_++;
        av_frame_free(&frame);
    } else {
        ring_[ringWrite_ % kRingCapacity] = frame;
        ringWrite_++;
        recycleCount_++;
    }
}

void FramePool::setMaxSize(size_t newMaxSize) {
    if (newMaxSize == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    maxSize_ = newMaxSize;
}

size_t FramePool::maxSize() const {
    return maxSize_;
}

void FramePool::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (ringRead_ != ringWrite_) {
        AVFrame* frame = ring_[ringRead_ % kRingCapacity];
        if (frame) {
            av_frame_free(&frame);
            freed_++;
        }
        ringRead_++;
    }
    ringRead_ = 0;
    ringWrite_ = 0;
}

void FramePool::printStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t inPool = (ringWrite_ >= ringRead_) ? (ringWrite_ - ringRead_) : (kRingCapacity - ringRead_ + ringWrite_);
    SP_LOG_INFO("[FramePool] Allocated:%d Get:%d Recycled:%d Freed:%d InPool:%zu",
                createCount_.load(), getCount_.load(),
                recycleCount_.load(), freeCount_.load(), inPool);
}
