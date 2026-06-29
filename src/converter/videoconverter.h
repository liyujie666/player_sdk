#ifndef VIDEOCONVERTER_H
#define VIDEOCONVERTER_H

#include <shared_mutex>
#include <atomic>

extern "C"
{
#include <libswscale/swscale.h>
#include <libavutil/avutil.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
}

class VideoConverter
{
public:
    struct VideoSpec {
        int width = 0;
        int height = 0;
        AVPixelFormat pixFmt = AV_PIX_FMT_NONE;
        int bufferSize = 0;
    };

    VideoConverter();
    ~VideoConverter();

    VideoConverter(const VideoConverter&) = delete;
    VideoConverter& operator=(const VideoConverter&) = delete;

    int init(const VideoSpec& inSpec, const VideoSpec& outSpec);
    int convert(const AVFrame *inFrame, AVFrame *&outFrame);
    void close();

    VideoSpec inSpec() const;
    VideoSpec outSpec() const;
    bool isReady() const;

private:
    void closeInternal();
    static int calcBufferSize(const VideoSpec& spec);

private:
    SwsContext* swsCtx_ = nullptr;
    VideoSpec inSpec_;
    VideoSpec outSpec_;

    mutable std::shared_mutex lock_;
    std::atomic<bool> isReady_{false};
};

#endif // VIDEOCONVERTER_H
