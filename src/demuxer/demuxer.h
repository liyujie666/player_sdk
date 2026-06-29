#ifndef DEMUXER_H
#define DEMUXER_H

#include <shared_mutex>
#include <atomic>
#include <string>

extern "C"
{
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
}

class Demuxer
{
public:
    enum class MediaType {
        FILE_TYPE,
        RTSP_TYPE,
        RTMP_TYPE,
        HTTP_TYPE,
        HTTPS_TYPE,
        HLS_TYPE
    };

    Demuxer();
    ~Demuxer();

    Demuxer(const Demuxer&) = delete;
    Demuxer& operator=(const Demuxer&) = delete;

    int open(const std::string& filename);
    void close();
    int readPacket(AVPacket *pkt);
    int seek(int64_t ts_us, bool videoSeek = true);

    AVStream* getStream(AVMediaType type) const;
    int getStreamIndex(AVMediaType type) const;
    bool hasStream(AVMediaType type) const;

    int64_t getDuration() const;
    MediaType mediaType() const;
    AVFormatContext* formatContext() const;
    bool isOpen() const;


private:
    static MediaType parseMediaType(const std::string& filename);
    void findStreams();
    void closeInternal();
    int getStreamIndexInternal(AVMediaType type) const;
    bool isAttachedPic(AVStream* stream);
    static int interruptCallback(void* opaque);


    AVFormatContext *fmtCtx_ = nullptr;
    MediaType mediaType_ = MediaType::FILE_TYPE;

    int audioStreamIndex_ = -1;
    int videoStreamIndex_ = -1;
    int coverStreamIndex_ = -1;

    mutable std::shared_mutex lock_;
    std::atomic<bool> abort_{false};
    std::atomic<bool> isOpened_{false};


};

#endif // DEMUXER_H
