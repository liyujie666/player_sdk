#ifndef DECODER_H
#define DECODER_H

#include <shared_mutex>
#include <atomic>
#include <string>

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/hwcontext.h>
#include <libavutil/error.h>
#include <libavutil/pixdesc.h>
}

class Decoder
{
public:
    explicit Decoder();
    ~Decoder();

    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;

    int init(AVCodecParameters *codecpar, AVMediaType type, const std::string &decodeName = "");
    int initHardware(AVHWDeviceType hwType = AV_HWDEVICE_TYPE_CUDA);
    int decode(AVPacket *pkt, AVFrame *&outFrame);
    void useHardware(bool isUse);
    AVHWDeviceType getBestHardwareType();
    std::string getHardwareDecoderName(AVCodecID codecId, AVHWDeviceType hwType);

    int flush();
    void close();

    AVCodecContext* codecCtx() const;
    AVMediaType mediaType() const;
    bool isHardware() const;
    bool isOpen() const;

private:
    AVCodec* findDecoder(AVCodecParameters *codecpar, AVHWDeviceType hwType, const std::string &decodeName = "");
    int hwFrameTransfer(AVFrame *srcFrame, AVFrame *&dstFrame);
    void closeInternal();

    static enum AVPixelFormat hwPixFmtCallback(AVCodecContext *ctx, const enum AVPixelFormat *pixFmts);

private:
    AVCodecContext *codecCtx_ = nullptr;
    AVMediaType mediaType_ = AVMEDIA_TYPE_UNKNOWN;
    AVBufferRef *hwDeviceCtx_ = nullptr;
    AVFrame *hwTmpFrame_ = nullptr;

    std::atomic<bool> isOpened_{false};
    std::atomic<bool> useHardware_{true};
    mutable std::shared_mutex lock_;
};

#endif // DECODER_H
