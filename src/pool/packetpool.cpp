#include "packetpool.h"
#include "utils/log.h"

PacketPool::PacketPool(size_t maxSize) : maxSize_(maxSize) {
    for (auto& p : ring_) p = nullptr;
}

PacketPool::~PacketPool() {
    clear();
}

AVPacket *PacketPool::get() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ringRead_ != ringWrite_) {
        AVPacket* pkt = ring_[ringRead_ % kRingCapacity];
        ring_[ringRead_ % kRingCapacity] = nullptr;
        ringRead_++;
        av_packet_unref(pkt);
        getCount_++;
        return pkt;
    }
    createCount_++;
    return av_packet_alloc();
}

void PacketPool::recycle(AVPacket *pkt) {
    if (!pkt) return;
    av_packet_unref(pkt);
    std::lock_guard<std::mutex> lock(mutex_);
    size_t count = (ringWrite_ >= ringRead_) ? (ringWrite_ - ringRead_) : (kRingCapacity - ringRead_ + ringWrite_);
    if (count >= maxSize_) {
        freeCount_++;
        av_packet_free(&pkt);
    } else {
        ring_[ringWrite_ % kRingCapacity] = pkt;
        ringWrite_++;
        recycleCount_++;
    }
}

void PacketPool::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    while (ringRead_ != ringWrite_) {
        AVPacket* pkt = ring_[ringRead_ % kRingCapacity];
        if (pkt) {
            av_packet_free(&pkt);
            freed_++;
        }
        ringRead_++;
    }
    ringRead_ = 0;
    ringWrite_ = 0;
}

void PacketPool::setMaxSize(size_t newMaxSize) {
    if (newMaxSize == 0) return;
    std::lock_guard<std::mutex> lock(mutex_);
    maxSize_ = newMaxSize;
}

size_t PacketPool::maxSize() const {
    return maxSize_;
}

void PacketPool::printStats() {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t inPool = (ringWrite_ >= ringRead_) ? (ringWrite_ - ringRead_) : (kRingCapacity - ringRead_ + ringWrite_);
    SP_LOG_INFO("[PacketPool] Allocated:%d Get:%d Recycled:%d Freed:%d InPool:%zu",
                createCount_.load(), getCount_.load(),
                recycleCount_.load(), freeCount_.load(), inPool);
}
