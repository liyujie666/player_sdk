#include "decoder.h"
#include "utils/log.h"

#define FF_CHECK(ret, func) \
if (ret < 0) { \
        char errBuf[AV_ERROR_MAX_STRING_SIZE] = {0}; \
        av_strerror(ret, errBuf, sizeof(errBuf)); \
        SP_LOG_ERROR("[%s] Error: %s Code: %d", #func, errBuf, ret); \
}

enum AVPixelFormat Decoder::hwPixFmtCallback(AVCodecContext *ctx, const enum AVPixelFormat *pixFmts)
{
    AVHWDeviceContext *hwDevCtx = (AVHWDeviceContext*)ctx->hw_device_ctx->data;
    AVHWDeviceType hwType = hwDevCtx->type;

    for (int i = 0; pixFmts[i] != AV_PIX_FMT_NONE; i++) {
        switch(hwType) {
        case AV_HWDEVICE_TYPE_CUDA:    if (pixFmts[i] == AV_PIX_FMT_CUDA) return pixFmts[i]; break;
        case AV_HWDEVICE_TYPE_D3D11VA: if (pixFmts[i] == AV_PIX_FMT_D3D11) return pixFmts[i]; break;
        case AV_HWDEVICE_TYPE_QSV:     if (pixFmts[i] == AV_PIX_FMT_QSV) return pixFmts[i]; break;
        case AV_HWDEVICE_TYPE_VAAPI:   if (pixFmts[i] == AV_PIX_FMT_VAAPI) return pixFmts[i]; break;
        default: break;
        }
    }
    return AV_PIX_FMT_YUV420P;
}

Decoder::Decoder() {
}

Decoder::~Decoder() { close(); }

int Decoder::init(AVCodecParameters *codecpar, AVMediaType type, const std::string &decodeName)
{
    std::unique_lock<std::shared_mutex> locker(lock_);
    closeInternal();

    if (!codecpar) return AVERROR(EINVAL);
    mediaType_ = type;

    // ===== Try hardware decoding =====
    AVHWDeviceType hwType = AV_HWDEVICE_TYPE_NONE;
    if (useHardware_.load() && mediaType_ == AVMEDIA_TYPE_VIDEO) {
        hwType = getBestHardwareType();
    }

    if (hwType != AV_HWDEVICE_TYPE_NONE) {
        const AVCodec* hwCodec = findDecoder(codecpar, hwType, decodeName);
        if (hwCodec) {
            codecCtx_ = avcodec_alloc_context3(hwCodec);
            if (codecCtx_) {
                int ret = avcodec_parameters_to_context(codecCtx_, codecpar);
                if (ret >= 0) {
                    ret = initHardware(hwType);
                    if (ret >= 0) {
                        codecCtx_->get_format = Decoder::hwPixFmtCallback;
                        codecCtx_->hw_device_ctx = av_buffer_ref(hwDeviceCtx_);
                        ret = avcodec_open2(codecCtx_, hwCodec, nullptr);
                        if (ret >= 0) {
                            isOpened_ = true;
                            SP_LOG_DEBUG("Decoder init success | Mode: HW decode | Decoder name: %s", hwCodec->name);
                            return 0;
                        }
                    }
                }
                // Hardware decoding failed, clean up and fall back to software decoding
                SP_LOG_WARN("Hardware decoder init failed, falling back to software");
                useHardware_.store(false);
                avcodec_free_context(&codecCtx_);
                if (hwDeviceCtx_) { av_buffer_unref(&hwDeviceCtx_); hwDeviceCtx_ = nullptr; }
                if (hwTmpFrame_) { av_frame_free(&hwTmpFrame_); hwTmpFrame_ = nullptr; }
            }
        }
    }

    // ===== Software decoding =====
    const AVCodec* codec = findDecoder(codecpar, AV_HWDEVICE_TYPE_NONE, decodeName);
    if (!codec) {
        SP_LOG_ERROR("No suitable decoder found");
        return AVERROR_DECODER_NOT_FOUND;
    }

    codecCtx_ = avcodec_alloc_context3(codec);
    if (!codecCtx_) return AVERROR(ENOMEM);

    int ret = avcodec_parameters_to_context(codecCtx_, codecpar);
    if (ret < 0) goto failed;

    ret = avcodec_open2(codecCtx_, codec, nullptr);
    if (ret < 0) goto failed;

    isOpened_ = true;

    SP_LOG_DEBUG("Decoder init success | Mode: SW decode | Decoder name: %s", codec->name);
    return 0;

failed:
    closeInternal();
    return ret;
}

int Decoder::initHardware(AVHWDeviceType hwType)
{
    if (hwDeviceCtx_) return 0;

    int ret = av_hwdevice_ctx_create(&hwDeviceCtx_, hwType, nullptr, nullptr, 0);
    FF_CHECK(ret, av_hwdevice_ctx_create);
    if (ret < 0) return ret;

    if (!hwTmpFrame_) {
        hwTmpFrame_ = av_frame_alloc();
        if (!hwTmpFrame_) return AVERROR(ENOMEM);
    }
    return 0;
}

int Decoder::decode(AVPacket *pkt, AVFrame *&outFrame)
{
    if (!isOpened_) return AVERROR(EINVAL);
    av_frame_unref(outFrame);
    std::unique_lock<std::shared_mutex> locker(lock_);
    int ret = avcodec_send_packet(codecCtx_, pkt);
    if (ret == AVERROR(EAGAIN)) {
        // Send requires waiting, indicating the decoder still has un-fetched frames, fetch first
    } else if (ret == AVERROR_EOF) {
        return ret;
    } else if (ret < 0) {
        FF_CHECK(ret, avcodec_send_packet);
        return ret;
    }
    while (true) {
        ret = avcodec_receive_frame(codecCtx_, outFrame);
        if (ret == AVERROR(EAGAIN)) {
            return AVERROR(EAGAIN);
        }
        if (ret == AVERROR_EOF) return ret;
        if (ret < 0) { FF_CHECK(ret, avcodec_receive_frame); return ret; }
        if (useHardware_ && mediaType_ == AVMEDIA_TYPE_VIDEO) {
            ret = hwFrameTransfer(outFrame, hwTmpFrame_);
            FF_CHECK(ret, hwFrameTransfer);
            if (ret >= 0) {
                av_frame_unref(outFrame);
                av_frame_move_ref(outFrame, hwTmpFrame_);
            } else {
                av_frame_unref(outFrame);
                return ret;
            }
        }
        return 0;
    }
}
void Decoder::useHardware(bool isUse)
{
    useHardware_.store(isUse);
}

int Decoder::flush()
{
    if (!isOpened_) return AVERROR(EINVAL);
    std::unique_lock<std::shared_mutex> locker(lock_);
    avcodec_flush_buffers(codecCtx_);
    if (hwTmpFrame_) {
        av_frame_unref(hwTmpFrame_);
    }
    return 0;
}

void Decoder::close() {
    std::unique_lock<std::shared_mutex> locker(lock_);
    closeInternal();
}

void Decoder::closeInternal()
{
    isOpened_ = false;

    if (codecCtx_) {
        avcodec_free_context(&codecCtx_);
        codecCtx_ = nullptr;
    }
    if (hwDeviceCtx_) {
        av_buffer_unref(&hwDeviceCtx_);
        hwDeviceCtx_ = nullptr;
    }
    if (hwTmpFrame_) {
        av_frame_unref(hwTmpFrame_);
        av_frame_free(&hwTmpFrame_);
        hwTmpFrame_ = nullptr;
    }

    mediaType_ = AVMEDIA_TYPE_UNKNOWN;
}

int Decoder::hwFrameTransfer(AVFrame *srcFrame, AVFrame *&dstFrame)
{
    if (!srcFrame || !dstFrame) return AVERROR(EINVAL);
    av_frame_unref(dstFrame);

    int ret = av_hwframe_transfer_data(dstFrame, srcFrame, 0);
    FF_CHECK(ret, av_hwframe_transfer_data);
    if (ret < 0) return ret;

    av_frame_copy_props(dstFrame, srcFrame);

    return 0;
}

AVCodec* Decoder::findDecoder(AVCodecParameters *codecpar, AVHWDeviceType hwType, const std::string &decodeName)
{
    // trim
    std::string name = decodeName;
    auto start = name.find_first_not_of(" \t");
    auto end = name.find_last_not_of(" \t");
    if (start == std::string::npos) name.clear();
    else name = name.substr(start, end - start + 1);

    // 1. User-specified decoder
    if (!name.empty()) {
        const AVCodec* codec = avcodec_find_decoder_by_name(name.c_str());
        if (codec) {
            bool isHw = name.find("_cuvid") != std::string::npos ||
                        name.find("_qsv") != std::string::npos ||
                        name.find("_d3d11va") != std::string::npos;
            if (isHw || codec->id == codecpar->codec_id) {
                return const_cast<AVCodec*>(codec);
            }
        }
        SP_LOG_DEBUG("Specified decoder %s not supported, falling back to default decoder", name.c_str());
    }

    // 2. Automatic hardware decoder
    if (hwType != AV_HWDEVICE_TYPE_NONE) {
        std::string hwName = getHardwareDecoderName(codecpar->codec_id, hwType);
        if (!hwName.empty()) {
            const AVCodec* codec = avcodec_find_decoder_by_name(hwName.c_str());
            if (codec) return const_cast<AVCodec*>(codec);
        }
    }

    // 3. Default software decoder
    return const_cast<AVCodec*>(avcodec_find_decoder(codecpar->codec_id));
}
AVHWDeviceType Decoder::getBestHardwareType()
{
    static const AVHWDeviceType priority_list[] = {
        AV_HWDEVICE_TYPE_CUDA,
        AV_HWDEVICE_TYPE_QSV,
        AV_HWDEVICE_TYPE_D3D11VA,
        AV_HWDEVICE_TYPE_VAAPI,
        AV_HWDEVICE_TYPE_DXVA2,
        AV_HWDEVICE_TYPE_NONE
    };

    AVHWDeviceType current_type = AV_HWDEVICE_TYPE_NONE;
    while ((current_type = av_hwdevice_iterate_types(current_type)) != AV_HWDEVICE_TYPE_NONE)
    {
        for (int i = 0; priority_list[i] != AV_HWDEVICE_TYPE_NONE; i++)
        {
            if (current_type == priority_list[i])
            {
                return current_type;
            }
        }
    }

    return AV_HWDEVICE_TYPE_NONE;
}

std::string Decoder::getHardwareDecoderName(AVCodecID codecId, AVHWDeviceType hwType)
{
    auto formatName = [](AVCodecID id) -> const char* {
        switch (id) {
        case AV_CODEC_ID_H264: return "h264";
        case AV_CODEC_ID_HEVC: return "hevc";
        case AV_CODEC_ID_VP9:  return "vp9";
        case AV_CODEC_ID_AV1:  return "av1";
        default: return nullptr;
        }
    };
    const char* codec = formatName(codecId);
    if (!codec) return "";

    char buf[32];
    switch (hwType) {
    case AV_HWDEVICE_TYPE_CUDA:    std::snprintf(buf, sizeof(buf), "%s_cuvid", codec); return buf;
    case AV_HWDEVICE_TYPE_QSV:     std::snprintf(buf, sizeof(buf), "%s_qsv", codec); return buf;
    case AV_HWDEVICE_TYPE_D3D11VA: std::snprintf(buf, sizeof(buf), "%s_d3d11va", codec); return buf;
    case AV_HWDEVICE_TYPE_VAAPI:   std::snprintf(buf, sizeof(buf), "%s_vaapi", codec); return buf;
    case AV_HWDEVICE_TYPE_DXVA2:   std::snprintf(buf, sizeof(buf), "%s_dxva2", codec); return buf;
    default: return "";
    }
}

AVCodecContext* Decoder::codecCtx() const { std::shared_lock<std::shared_mutex> locker(lock_); return codecCtx_; }
AVMediaType Decoder::mediaType() const { std::shared_lock<std::shared_mutex> locker(lock_); return mediaType_; }
bool Decoder::isHardware() const { return useHardware_; }
bool Decoder::isOpen() const { return isOpened_; }
