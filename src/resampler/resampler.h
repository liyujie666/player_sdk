#ifndef RESAMPLER_H
#define RESAMPLER_H

#include <shared_mutex>
#include <atomic>

extern "C"
{
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

class Resampler
{
public:

    typedef struct {
        int sampleRate;
        enum AVSampleFormat sampleFmt;
        AVChannelLayout chLayout;
        int chs;
        int bytesPerSample;
    } AudioSpec;

    Resampler();
    ~Resampler();

    Resampler(const Resampler&) = delete;
    Resampler& operator=(const Resampler&) = delete;

    int init(const AudioSpec &inSpec, const AudioSpec &outSpec);
    int resample(AVFrame *inFrame, uint8_t **outData, int *outSamples);
    void close();

    AudioSpec inputSpec() const;
    AudioSpec outputSpec() const;
    SwrContext* swrContext() const;
    int outputBufferSize(int samples) const;

private:
    void initChannelLayout(AVChannelLayout *chLayout, int chs);
    void closeInternal();

private:
    SwrContext *swrCtx_ = nullptr;
    AudioSpec inSpec_;
    AudioSpec outSpec_;


    mutable std::shared_mutex mutex_;
    std::atomic<bool> isInitialized_{false};
};

#endif // RESAMPLER_H
