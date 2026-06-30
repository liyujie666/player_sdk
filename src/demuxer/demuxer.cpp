#include "demuxer.h"
#include "utils/log.h"
#include <mutex>

#define FF_ERROR_BUF(ret) \
char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0}; \
    av_strerror(ret, errBuf, sizeof(errBuf));

#define CHECK_FF_ERROR(ret, func) \
if (ret < 0) { \
        FF_ERROR_BUF(ret); \
        SP_LOG_ERROR("[%s] Failed: %s Code: %d", #func, errBuf, ret); \
}

static void ffmpegGlobalInit() {
    static std::once_flag flag;
    std::call_once(flag, [](){
        avformat_network_init();
        av_log_set_level(AV_LOG_ERROR);
    });
}

Demuxer::Demuxer()
{
    ffmpegGlobalInit();
}

Demuxer::~Demuxer()
{
    close();
}

int Demuxer::open(const std::string& filename)
{
    if (filename.empty()) return AVERROR(EINVAL);

    std::unique_lock<std::shared_mutex> locker(lock_);
    closeInternal();
    abort_ = false;

    fmtCtx_ = avformat_alloc_context();
    if (!fmtCtx_) {
        SP_LOG_ERROR("avformat_alloc_context failed");
        return AVERROR(ENOMEM);
    }

    fmtCtx_->interrupt_callback.callback = interruptCallback;
    fmtCtx_->interrupt_callback.opaque = this;

    mediaType_ = parseMediaType(filename);

    AVDictionary* options = nullptr;
    if (mediaType_ == MediaType::RTSP_TYPE) {
        av_dict_set(&options, "rtsp_transport", "tcp", 0);
        av_dict_set(&options, "stimeout", "2000000", 0);
        av_dict_set(&options, "open_timeout", "2", 0);
    }

    int ret = avformat_open_input(&fmtCtx_, filename.c_str(), nullptr, &options);
    av_dict_free(&options);
    CHECK_FF_ERROR(ret, avformat_open_input);
    if (ret < 0) {
        avformat_free_context(fmtCtx_);
        fmtCtx_ = nullptr;
        return ret;
    }

    ret = avformat_find_stream_info(fmtCtx_, nullptr);
    CHECK_FF_ERROR(ret, avformat_find_stream_info);
    if (ret < 0) {
        closeInternal();
        return ret;
    }

    findStreams();
    isOpened_ = true;

    SP_LOG_DEBUG("Demuxer open success! Video:%d Audio:%d", videoStreamIndex_, audioStreamIndex_);
    return 0;
}

void Demuxer::close()
{
    std::unique_lock<std::shared_mutex> locker(lock_);
    closeInternal();
}

void Demuxer::closeInternal()
{
    abort_ = true;
    isOpened_ = false;

    if (fmtCtx_) {
        avformat_close_input(&fmtCtx_);
        fmtCtx_ = nullptr;
    }

    audioStreamIndex_ = -1;
    videoStreamIndex_ = -1;
}

int Demuxer::readPacket(AVPacket *pkt)
{
    if (!pkt || !isOpened_) return AVERROR(EINVAL);

    std::shared_lock<std::shared_mutex> locker(lock_);
    int ret = av_read_frame(fmtCtx_, pkt);
    return ret;
}

int Demuxer::seek(int64_t ts_us, bool useVideoStream)
{
    if (!isOpened_) return AVERROR(EINVAL);

    int streamIdx = -1;

    if (useVideoStream && videoStreamIndex_ >= 0) {
        streamIdx = videoStreamIndex_;
    } else if (audioStreamIndex_ >= 0) {
        streamIdx = audioStreamIndex_;
    } else {
        return AVERROR(EINVAL);
    }

    int64_t target_ts = av_rescale_q(
        ts_us,
        AV_TIME_BASE_Q,
        fmtCtx_->streams[streamIdx]->time_base
        );

    int ret = av_seek_frame(fmtCtx_, streamIdx, target_ts, AVSEEK_FLAG_BACKWARD);
    CHECK_FF_ERROR(ret, av_seek_frame);
    return ret;
}

AVStream *Demuxer::getStream(AVMediaType type) const
{
    std::shared_lock<std::shared_mutex> locker(lock_);
    int idx = getStreamIndexInternal(type);
    return (idx >= 0 && fmtCtx_) ? fmtCtx_->streams[idx] : nullptr;
}

int Demuxer::getStreamIndex(AVMediaType type) const
{
    std::shared_lock<std::shared_mutex> locker(lock_);
    return getStreamIndexInternal(type);
}

bool Demuxer::hasStream(AVMediaType type) const
{
    std::shared_lock<std::shared_mutex> locker(lock_);
    return getStreamIndexInternal(type) >= 0;
}

int64_t Demuxer::getDuration() const
{
    std::shared_lock<std::shared_mutex> locker(lock_);
    if (!fmtCtx_ || fmtCtx_->duration <= 0) return 0;
    // fmtCtx_->duration is in microseconds (AV_TIME_BASE), convert to milliseconds
    return fmtCtx_->duration / (AV_TIME_BASE / 1000);
}

Demuxer::MediaType Demuxer::mediaType() const
{
    std::shared_lock<std::shared_mutex> locker(lock_);
    return mediaType_;
}

AVFormatContext* Demuxer::formatContext() const
{
    std::shared_lock<std::shared_mutex> locker(lock_);
    return fmtCtx_;
}

bool Demuxer::isOpen() const
{
    return isOpened_;
}


int Demuxer::getStreamIndexInternal(AVMediaType type) const
{
    switch (type) {
    case AVMEDIA_TYPE_AUDIO: return audioStreamIndex_;
    case AVMEDIA_TYPE_VIDEO: return videoStreamIndex_;
    default: return -1;
    }
}

bool Demuxer::isAttachedPic(AVStream *stream)
{
    return stream->disposition & AV_DISPOSITION_ATTACHED_PIC;
}

void Demuxer::findStreams()
{
    audioStreamIndex_ = -1;
    videoStreamIndex_ = -1;
    coverStreamIndex_ = -1;

    for (int i = 0; i < fmtCtx_->nb_streams; i++) {
        AVStream* st = fmtCtx_->streams[i];
        AVCodecParameters* par = st->codecpar;
        if (!par) continue;

        if (par->codec_type == AVMEDIA_TYPE_VIDEO && isAttachedPic(st)) {
            coverStreamIndex_ = i;
            continue;
        }

        if (par->codec_type == AVMEDIA_TYPE_VIDEO && videoStreamIndex_ < 0) {
            videoStreamIndex_ = i;
        }

        if (par->codec_type == AVMEDIA_TYPE_AUDIO && audioStreamIndex_ < 0) {
            audioStreamIndex_ = i;
        }
    }
}

Demuxer::MediaType Demuxer::parseMediaType(const std::string& filename)
{
    std::string url = filename;
    // to lower case
    for (auto& c : url) c = (char)tolower((unsigned char)c);

    if (url.compare(0, 7, "rtsp://") == 0)  return MediaType::RTSP_TYPE;
    if (url.compare(0, 7, "rtmp://") == 0)  return MediaType::RTMP_TYPE;
    if (url.compare(0, 7, "http://") == 0)  return MediaType::HTTP_TYPE;
    if (url.compare(0, 8, "https://") == 0) return MediaType::HTTPS_TYPE;
    if (url.compare(0, 7, "hls://") == 0 ||
        url.find(".m3u8") != std::string::npos) return MediaType::HLS_TYPE;
    return MediaType::FILE_TYPE;
}

int Demuxer::interruptCallback(void* opaque)
{
    Demuxer* self = (Demuxer*)opaque;
    return self->abort_ ? 1 : 0;
}
