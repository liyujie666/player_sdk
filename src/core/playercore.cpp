#include "playercore.h"
#include "pool/gloabalpool.h"
#include "utils/log.h"
#include <cstring>
#include <filesystem>
#include <chrono>
#include <thread>
#include <algorithm>
#include <ctime>
#include <cstdio>
#include <cmath>

extern "C" {
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

PlayerCore::PlayerCore()
    : state_(Stopped), is_exit_(false), is_seek_(false)
{
    audio_pkt_queue_ = new AVPacketQueue();
    video_pkt_queue_ = new AVPacketQueue();
    audio_frame_queue_ = new AVFrameQueue();
    video_frame_queue_ = new AVFrameQueue();
    sync_clock_ = new AVSyncClock();
}

PlayerCore::~PlayerCore()
{
    SP_LOG_DEBUG("~PlayerCore() 开始");
    stop();

    if (open_thread_ && open_thread_->joinable()) {
        SP_LOG_DEBUG("~PlayerCore() 等待 open 线程...");
        open_thread_->join();
    }

    delete audio_pkt_queue_; audio_pkt_queue_ = nullptr;
    delete video_pkt_queue_; video_pkt_queue_ = nullptr;
    delete audio_frame_queue_; audio_frame_queue_ = nullptr;
    delete video_frame_queue_; video_frame_queue_ = nullptr;

    if (sync_clock_) {
        sync_clock_->reset();
        delete sync_clock_;
        sync_clock_ = nullptr;
    }
    SP_LOG_DEBUG("~PlayerCore() 完成");
}


void PlayerCore::open(const std::string &url)
{
    // 等待上一次异步 open 完成
    if (open_thread_ && open_thread_->joinable()) {
        open_thread_->join();
    }
    open_thread_.reset();

    if (isNetworkUrl(url)) {
        open_thread_ = std::make_unique<std::thread>([this, url]() {
            stop();
            bool ret = openInternal(url);
            if (callback_) callback_->onOpenResult(ret, ret ? "" : "open failed");
        });
    } else {
        stop();
        bool ret = openInternal(url);
        if (callback_) callback_->onOpenResult(ret, ret ? "" : "open failed");
    }
}

bool PlayerCore::openInternal(const std::string &url)
{

    int ret=0;

    // 解复用器
    demuxer_ = new Demuxer();
    ret = demuxer_->open(url);
    if (ret < 0) {
        SP_LOG_ERROR("解复用器打开失败");
        releaseResources();
        if (callback_) callback_->onError(url + " 打开失败");
        return false;
    }

    file_url_ = url;

    hasAudio_ = demuxer_->hasStream(AVMEDIA_TYPE_AUDIO);
    hasVideo_ = demuxer_->hasStream(AVMEDIA_TYPE_VIDEO);

    // 视频解码器
    if(hasVideo_){
        video_decoder_ = new Decoder();
        video_decoder_->useHardware(hardware_enabled_);
        video_stream_idx_ = demuxer_->getStreamIndex(AVMEDIA_TYPE_VIDEO);
        AVStream* vStream = demuxer_->getStream(AVMEDIA_TYPE_VIDEO);
        ret = video_decoder_->init(vStream->codecpar,AVMEDIA_TYPE_VIDEO,decoder_type_);
        if(ret < 0){
            SP_LOG_ERROR("视频解码器初始化失败！");
            releaseResources();
            return false;
        }

        initVideoModule();

    }
    // 音频解码器
    if(hasAudio_){
        audio_decoder_ = new Decoder();
        audio_stream_idx_ = demuxer_->getStreamIndex(AVMEDIA_TYPE_AUDIO);

        AVStream* aStream = demuxer_->getStream(AVMEDIA_TYPE_AUDIO);
        ret = audio_decoder_->init(aStream->codecpar,AVMEDIA_TYPE_AUDIO);
        if(ret < 0){
            SP_LOG_ERROR("音频解码器初始化失败！");
            releaseResources();
            return false;
        }

        initAudioModule();
    }

    // 设置基准时钟
    AVSyncClock::SyncMode syncMode;
    if (hasAudio_ && hasVideo_) {
        syncMode = AVSyncClock::AUDIO_MASTER;
    } else if (hasAudio_) {
        syncMode = AVSyncClock::VIDEO_MASTER;
    } else if (hasVideo_) {
        syncMode = AVSyncClock::SYSTEM_MASTER;
    } else {
        syncMode = AVSyncClock::AUDIO_MASTER;
    }
    sync_clock_->setSyncMode(syncMode, hasAudio_, hasVideo_);

    duration_ms_ = demuxer_->getDuration();
    state_ = Stopped;

    // 填充媒体信息并通知
    fillMediaInfo();
    if (callback_) {
        callback_->onDurationChanged(duration_ms_);
        callback_->onMediaInfoReady(media_info_);
    }

    return true;
}

void PlayerCore::initAudioModule()
{
    if (!audio_decoder_ || !audio_decoder_->codecCtx()) {
        SP_LOG_ERROR("音频解码器上下文无效，初始化失败");
        return;
    }

    AVCodecContext* codec_ctx = audio_decoder_->codecCtx();
    Resampler::AudioSpec in_spec;

    in_spec.sampleRate  = codec_ctx->sample_rate;
    in_spec.sampleFmt   = codec_ctx->sample_fmt;
    in_spec.chs         = codec_ctx->ch_layout.nb_channels;
    in_spec.chLayout    = codec_ctx->ch_layout;
    in_spec.bytesPerSample = av_get_bytes_per_sample(in_spec.sampleFmt);

    Resampler::AudioSpec out_spec;
    out_spec.sampleRate  = 48000;
    out_spec.chs         = 2;
    out_spec.sampleFmt   = AV_SAMPLE_FMT_S16;
    out_spec.bytesPerSample = av_get_bytes_per_sample(out_spec.sampleFmt);
    av_channel_layout_from_string(&out_spec.chLayout, "stereo");

    // 初始化倍速滤镜
    audio_filter_ = new AudioFilter();
    if (audio_filter_->init(in_spec.sampleRate, in_spec.sampleFmt, in_spec.chs) < 0) {
        SP_LOG_ERROR("音频倍速滤镜初始化失败");
        delete audio_filter_; audio_filter_ = nullptr;
        return;
    }

    audio_output_ = new AudioOutput(in_spec, out_spec, audio_frame_queue_, sync_clock_);
    if (audio_output_->Init() < 0) {
        SP_LOG_ERROR("SDL 音频输出初始化失败");
        delete audio_filter_; audio_filter_ = nullptr;
        delete audio_output_; audio_output_ = nullptr;
        return;
    }

    audio_output_->setAudioTimebase(demuxer_->getStream(AVMEDIA_TYPE_AUDIO)->time_base);

    SP_LOG_DEBUG("=== 音频模块初始化完成 ===");
    SP_LOG_DEBUG("解码器输出: 采样率%dHz, 声道数%d",
                 in_spec.sampleRate, in_spec.chs);
    SP_LOG_DEBUG("SDL播放: 采样率%dHz, 声道数%d",
                 out_spec.sampleRate, out_spec.chs);
}


void PlayerCore::initVideoModule()
{
    if (!video_decoder_ || !video_decoder_->codecCtx()) {
        SP_LOG_ERROR("视频解码器初始化失败，无法初始化视频模块");
        return;
    }

    AVCodecContext* codec_ctx = video_decoder_->codecCtx();
    VideoConverter::VideoSpec in_spec;
    in_spec.width    = codec_ctx->width;
    in_spec.height   = codec_ctx->height;
    in_spec.pixFmt   = codec_ctx->pix_fmt;

    VideoConverter::VideoSpec out_spec;
    out_spec.width    = in_spec.width;
    out_spec.height   = in_spec.height;
    out_spec.pixFmt   = AV_PIX_FMT_RGBA;

    converter_ = new VideoConverter();
    if (converter_->init(in_spec, out_spec) < 0) {
        SP_LOG_ERROR("视频转换器初始化失败");
        delete converter_; converter_ = nullptr;
    }

    SP_LOG_DEBUG("=== 视频模块初始化完成 ===");
    SP_LOG_DEBUG("视频参数: %dx%d 格式: %s",
                 in_spec.width, in_spec.height,
                 av_get_pix_fmt_name(in_spec.pixFmt));
}


void PlayerCore::play()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == Running) {
        return;
    }

    if (state_ == Paused) {
        state_ = Running;
        if (audio_output_) {
            audio_output_->resume();
        }
        if(sync_clock_){
            sync_clock_->resume();
        }
        cond_.notify_all();
        notifyStateChanged();
        SP_LOG_DEBUG("播放器：恢复播放");
        return;
    }

    if (state_ == Stopped) {
        // 安全检查：确保 demuxer 已成功打开
        if (!demuxer_ || !demuxer_->isOpen()) {
            SP_LOG_ERROR("play() 失败：媒体未打开");
            return;
        }

        is_exit_ = false;
        is_seek_ = false;
        state_ = Running;

        demux_thread_ = std::make_unique<std::thread>(&PlayerCore::demuxThreadFunc, this);

        if (audio_decoder_) {
            audio_decode_thread_ = std::make_unique<std::thread>(&PlayerCore::audioDecodeThreadFunc, this);
        }

        if (video_decoder_) {
            video_decode_thread_ = std::make_unique<std::thread>(&PlayerCore::videoDecodeThreadFunc, this);

            video_render_thread_ = std::make_unique<std::thread>(&PlayerCore::videoRenderThreadFunc, this);
        }

        SP_LOG_DEBUG("播放器：开始播放");
    }

    notifyStateChanged();
}

void PlayerCore::pause()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ == Running) {
        state_ = Paused;
        if (audio_output_) {
            audio_output_->pause();
        }
        if(sync_clock_){
            sync_clock_->pause();
        }
        SP_LOG_DEBUG("播放器：已暂停");
    }
    notifyStateChanged();
}

void PlayerCore::stop()
{
    SP_LOG_DEBUG("stop() 开始");
    // 等待异步 open 线程完成
    if (open_thread_ && open_thread_->joinable()) {
        SP_LOG_DEBUG("stop() 等待 open 线程...");
        open_thread_->join();
    }
    open_thread_.reset();

    if (state_ == Stopped) {
        SP_LOG_DEBUG("stop() 已是 Stopped 状态，直接返回");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        is_exit_ = true;
        is_seek_ = false;
        cond_.notify_all();
    }
    SP_LOG_DEBUG("stop() 已设置 is_exit_=true，通知所有线程");

    clearAllQueues();
    SP_LOG_DEBUG("stop() 队列已清空");

    if (demux_thread_ && demux_thread_->joinable()) {
        SP_LOG_DEBUG("stop() 等待 demux 线程...");
        demux_thread_->join();
    }
    demux_thread_.reset();
    SP_LOG_DEBUG("stop() demux 线程已退出");

    if (audio_decode_thread_ && audio_decode_thread_->joinable()) {
        SP_LOG_DEBUG("stop() 等待 audio decode 线程...");
        audio_decode_thread_->join();
    }
    audio_decode_thread_.reset();
    SP_LOG_DEBUG("stop() audio decode 线程已退出");

    if (video_decode_thread_ && video_decode_thread_->joinable()) {
        SP_LOG_DEBUG("stop() 等待 video decode 线程...");
        video_decode_thread_->join();
    }
    video_decode_thread_.reset();
    SP_LOG_DEBUG("stop() video decode 线程已退出");

    if (video_render_thread_ && video_render_thread_->joinable()) {
        SP_LOG_DEBUG("stop() 等待 video render 线程...");
        video_render_thread_->join();
    }
    video_render_thread_.reset();
    SP_LOG_DEBUG("stop() video render 线程已退出");

    releaseResources();
    SP_LOG_DEBUG("stop() 资源已释放");
    sync_clock_->reset();

    state_ = Stopped;
    duration_ms_ = 0;
    audio_stream_idx_ = -1;
    video_stream_idx_ = -1;
    hasAudio_ = false;
    hasVideo_ = false;
    notifyStateChanged();

    SP_LOG_DEBUG("播放器：已停止，资源已释放");
}

void PlayerCore::setSpeed(float speed)
{
    std::lock_guard<std::mutex> lock(mutex_);
    int speedIndex = speedToIndex(speed);
    if (audio_filter_) {
        audio_filter_->setSpeedIndex((AudioFilter::SpeedIndex)speedIndex);
    }
    if (sync_clock_) {
        double ratio = speedToRatio(speed);
        sync_clock_->setSpeedRatio(ratio);
    }
}

void PlayerCore::useHardware(bool isUse)
{
    hardware_enabled_ = isUse;
    if(video_decoder_){
        video_decoder_->useHardware(isUse);
    }
}

void PlayerCore::setDecodeType(const std::string &decoder)
{
    decoder_type_ = decoder;
}

void PlayerCore::seek(int64_t pos_ms)
{
    if (duration_ms_ <= 0 || pos_ms < 0) return;

    int64_t pos_us = pos_ms * 1000;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        is_seek_ = true;
        state_ = Paused;
        cond_.notify_all();
    }

    clearAllQueues();
    if (video_decoder_) video_decoder_->flush();
    if (audio_decoder_) audio_decoder_->flush();

    int ret = demuxer_->seek(pos_us);
    if (ret < 0) SP_LOG_WARN("seek failed");

    sync_clock_->reset();
    sync_clock_->set_audio_clock(pos_us);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        is_seek_ = false;
        state_ = Running;
        cond_.notify_all();
    }
    SP_LOG_DEBUG("seek to %lld ms", (long long)pos_ms);
}

void PlayerCore::setScreenshotSavePath(const std::string& savePath)
{
    screenshot_save_path_ = savePath;
}

void PlayerCore::takeScreenshot()
{
    need_screenshot_ = true;
}

void PlayerCore::setVolume(int val)
{
    if (audio_output_) {
        audio_output_->setVolume(val);
    }
}

void PlayerCore::setMute(bool mute)
{
    if (audio_output_) {
        audio_output_->setMute(mute);
    }
}

bool PlayerCore::isMute() const
{
    if (audio_output_) {
        return audio_output_->isMute();
    }

    return false;
}


void PlayerCore::demuxThreadFunc()
{
    SP_LOG_DEBUG("解复用线程启动");
    AVPacket* pkt = av_packet_alloc();

    while (!is_exit_)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cond_.wait(lock, [this] {
                return (state_ != Paused && !is_seek_) || is_exit_;
            });
        }

        if (is_exit_) break;

        const bool hasA = demuxer_->hasStream(AVMEDIA_TYPE_AUDIO);
        const bool hasV = demuxer_->hasStream(AVMEDIA_TYPE_VIDEO);
        while (!is_exit_ && !is_seek_)
        {
            const bool audioFull = !hasA || audio_pkt_queue_->Size() >= MAX_AUDIO_PKT;
            const bool videoFull = !hasV || video_pkt_queue_->Size() >= MAX_VIDEO_PKT;
            if (!videoFull || !audioFull) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }

        av_packet_unref(pkt);
        int ret = demuxer_->readPacket(pkt);
        if (ret < 0)
        {
            if (ret == AVERROR_EOF){
                SP_LOG_DEBUG("解复用完成：媒体文件读取完毕 (EOF)");
                if (callback_) {
                    SP_LOG_DEBUG("调用 onPlayFinished...");
                    callback_->onPlayFinished();
                    SP_LOG_DEBUG("onPlayFinished 返回");
                }
            }
            else{
                SP_LOG_DEBUG("解复用读取包失败，错误码：%d", ret);
            }
            is_exit_ = true;
            cond_.notify_all();
            break;
        }

        const int audio_idx = demuxer_->getStreamIndex(AVMEDIA_TYPE_AUDIO);
        const int video_idx = demuxer_->getStreamIndex(AVMEDIA_TYPE_VIDEO);

        if (pkt->stream_index == audio_idx && audio_pkt_queue_)
        {
            audio_pkt_queue_->Push(pkt);
        }
        else if (pkt->stream_index == video_idx && video_pkt_queue_)
        {
            video_pkt_queue_->Push(pkt);
        }
        else
        {
            av_packet_unref(pkt);
        }
    }
    av_packet_free(&pkt);
    SP_LOG_DEBUG("解复用线程退出");
}


void PlayerCore::audioDecodeThreadFunc()
{
    SP_LOG_DEBUG("音频解码线程启动");

    AVFrame* decoded_frame = av_frame_alloc();
    AVFrame* filtered_frame = av_frame_alloc();
    AVStream* as = demuxer_->getStream(AVMEDIA_TYPE_AUDIO);

    while (!is_exit_)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cond_.wait(lock, [this] {
                return (state_ != Paused && !is_seek_) || is_exit_;
            });
        }

        if (is_exit_) break;

        while (audio_frame_queue_->Size() >= MAX_AUDIO_FRAME && !is_exit_ && !is_seek_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        AVPacket* pkt = audio_pkt_queue_->Pop(10);
        if (!pkt) continue;


        int ret = audio_decoder_->decode(pkt, decoded_frame);
        GlobalPool::getPacketPool().recycle(pkt);

        if (ret == AVERROR(EAGAIN)) {
            continue;
        }

        if (ret == AVERROR_EOF) {
            SP_LOG_DEBUG("音频解码完成（EOF）");
            is_exit_ = true;
            cond_.notify_all();
            break;
        }
        if (ret < 0) {
            SP_LOG_DEBUG("音频解码失败，错误码：%d", ret);
            continue;
        }

        int64_t pts = (decoded_frame->pts == AV_NOPTS_VALUE) ? NAN : decoded_frame->pts;
        double duration = av_q2d(AVRational{ decoded_frame->nb_samples, decoded_frame->sample_rate });

        if (audio_filter_ && audio_filter_->isInitialized())
        {
            ret = audio_filter_->process(decoded_frame, filtered_frame);
            if (ret < 0)
            {
                av_frame_unref(filtered_frame);
                av_frame_move_ref(filtered_frame, decoded_frame);
            }
        }
        else
        {
            av_frame_move_ref(filtered_frame, decoded_frame);
        }

        filtered_frame->pts = pts;
        filtered_frame->duration = duration;

        audio_frame_queue_->Push(filtered_frame);
        if(!hasVideo_) {
            notifyPosition(currentPos());
        }
        av_frame_unref(decoded_frame);
        av_frame_unref(filtered_frame);
    }

    av_frame_free(&decoded_frame);
    av_frame_free(&filtered_frame);
    SP_LOG_DEBUG("音频解码线程退出");
}


void PlayerCore::videoDecodeThreadFunc()
{
    SP_LOG_DEBUG("视频解码线程启动");
    AVFrame* decoded_frame = av_frame_alloc();
    AVFrame* rgb_frame = av_frame_alloc();
    while (!is_exit_)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cond_.wait(lock, [this] {
                return (state_ != Paused && !is_seek_) || is_exit_;
            });
        }

        if (is_exit_) break;

        while (video_frame_queue_->Size() >= MAX_VIDEO_FRAME && !is_exit_ && !is_seek_) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        AVPacket* pkt = video_pkt_queue_->Pop(10);
        if (!pkt) {
            continue;
        }
        int ret = video_decoder_->decode(pkt, decoded_frame);
        GlobalPool::getPacketPool().recycle(pkt);

        if (ret == AVERROR(EAGAIN)) {
            continue;
        }
        if (ret == AVERROR_EOF) {
            SP_LOG_DEBUG("视频解码完成（EOF）");
            break;
        }
        if (ret < 0) {
            SP_LOG_DEBUG("视频解码失败，错误码：%d", ret);
            continue;
        }

        video_frame_queue_->Push(decoded_frame);
        av_frame_unref(decoded_frame);
    }

    av_frame_free(&decoded_frame);
    av_frame_free(&rgb_frame);
    SP_LOG_DEBUG("视频解码线程退出");

}

void PlayerCore::videoRenderThreadFunc()
{
    SP_LOG_DEBUG("视频渲染线程启动");
    AVFrame* frame = nullptr;

    while (!is_exit_)
    {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cond_.wait(lock, [this] {
                return (state_ != Paused && !is_seek_) || is_exit_;
            });
        }

        if (is_exit_) break;

        frame = video_frame_queue_->Pop(10);
        if (!frame) continue;

        AVStream* vs = demuxer_->getStream(AVMEDIA_TYPE_VIDEO);
        if (!vs) {
            GlobalPool::getFramePool().recycle(frame);
            continue;
        }
        int64_t video_pts_us = av_rescale_q(frame->pts, vs->time_base, {1,1000000});

        int64_t delay = sync_clock_->calc_display_delay(video_pts_us);
        if (delay > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(delay));
        }

        if (sync_clock_->need_force_catch_up()) {
            GlobalPool::getFramePool().recycle(frame);
            continue;
        }

        int w = frame->width;
        int h = frame->height;
        AVPixelFormat fmt = (AVPixelFormat)frame->format;
        std::vector<uint8_t> frame_data;

        SmartPixelFormat spFmt = SP_FMT_UNKNOWN;

        if (fmt == AV_PIX_FMT_YUV420P || fmt == AV_PIX_FMT_YUVJ420P) {
            // 安全检查
            if (!frame->data[0] || !frame->data[1] || !frame->data[2]) {
                SP_LOG_WARN("render: YUV420P 帧数据指针为空，跳过");
                GlobalPool::getFramePool().recycle(frame);
                continue;
            }
            int y_size = w * h;
            int u_size = w * h / 4;
            int v_size = w * h / 4;
            frame_data.resize(y_size + u_size + v_size);

            // 按行复制，处理 linesize > width 的情况
            int y_stride = frame->linesize[0];
            int u_stride = frame->linesize[1];
            int v_stride = frame->linesize[2];
            size_t offset = 0;

            // Y plane
            const uint8_t* src = frame->data[0];
            for (int i = 0; i < h; i++) {
                memcpy(frame_data.data() + offset, src, w);
                offset += w;
                src += y_stride;
            }
            // U plane
            src = frame->data[1];
            for (int i = 0; i < h / 2; i++) {
                memcpy(frame_data.data() + offset, src, w / 2);
                offset += w / 2;
                src += u_stride;
            }
            // V plane
            src = frame->data[2];
            for (int i = 0; i < h / 2; i++) {
                memcpy(frame_data.data() + offset, src, w / 2);
                offset += w / 2;
                src += v_stride;
            }
            spFmt = SP_FMT_YUV420P;
        }
        else if (fmt == AV_PIX_FMT_NV12) {
            int buf_size = w * h * 3 / 2;
            frame_data.resize(buf_size);
            const uint8_t* y_buf = frame->data[0];
            int y_stride = frame->linesize[0];
            size_t offset = 0;
            for (int i = 0; i < h; i++) {
                memcpy(frame_data.data() + offset, y_buf, w);
                offset += w;
                y_buf += y_stride;
            }
            const uint8_t* uv_buf = frame->data[1];
            int uv_stride = frame->linesize[1];
            int uv_h = h / 2;
            int uv_w = w / 2;
            for (int i = 0; i < uv_h; i++) {
                memcpy(frame_data.data() + offset, uv_buf, uv_w * 2);
                offset += uv_w * 2;
                uv_buf += uv_stride;
            }
            spFmt = SP_FMT_NV12;
        }
        else if (fmt == AV_PIX_FMT_RGBA || fmt == AV_PIX_FMT_BGRA) {
            int buf_size = w * h * 4;
            frame_data.resize(buf_size);
            const uint8_t* data = frame->data[0];
            int stride = frame->linesize[0];
            size_t offset = 0;
            for (int i = 0; i < h; i++) {
                memcpy(frame_data.data() + offset, data, w * 4);
                offset += w * 4;
                data += stride;
            }
            spFmt = (fmt == AV_PIX_FMT_RGBA) ? SP_FMT_RGBA : SP_FMT_BGRA;
        }
        else {
            SP_LOG_WARN("不支持的视频格式：%s", av_get_pix_fmt_name(fmt));
            GlobalPool::getFramePool().recycle(frame);
            continue;
        }

        // 帧回调
        if (callback_ && !frame_data.empty()) {
            callback_->onVideoFrame(frame_data.data(), w, h, spFmt);
        }

        // 截图
        if (need_screenshot_ && !screenshot_busy_) {
            need_screenshot_ = false;
            screenshot_busy_ = true;

            auto frame_copy = frame_data;
            int w_copy = w;
            int h_copy = h;
            AVPixelFormat fmt_copy = fmt;
            std::string path_copy = screenshot_save_path_;
            auto* cb = callback_;
            auto* self = this;

            std::thread([self, cb, frame_copy, w_copy, h_copy, fmt_copy, path_copy]() {
                std::string savePath = path_copy;
                if (savePath.empty()) {
                    savePath = "./smartplayer_screenshot";
                }
                bool ok = self->saveFrameToJpeg(frame_copy.data(), w_copy, h_copy, fmt_copy, savePath);
                if (cb) cb->onScreenshot(savePath, ok);
                self->screenshot_busy_ = false;
            }).detach();
        }

        notifyPosition(currentPos());
        GlobalPool::getFramePool().recycle(frame);
    }
    SP_LOG_DEBUG("视频渲染线程退出");
}


bool PlayerCore::saveFrameToJpeg(const uint8_t* frame_data, int width, int height,
                                 AVPixelFormat format, const std::string& savePath)
{
    if(!frame_data || width <= 0 || height <= 0)
        return false;

    // 创建目录
    std::error_code ec;
    std::filesystem::create_directories(savePath, ec);

    // 生成文件名
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    std::tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    char timeBuf[32];
    std::strftime(timeBuf, sizeof(timeBuf), "%Y-%m-%d_%H%M%S", &tmv);

    std::string fullPath = savePath;
    if (!fullPath.empty() && fullPath.back() != '/' && fullPath.back() != '\\') {
        fullPath += "/";
    }
    fullPath += std::string("screenshot_") + timeBuf + ".jpg";

    // 1. 用 sws_scale 转为 YUVJ420P
    SwsContext* sws_ctx = sws_getContext(
        width, height, format,
        width, height, AV_PIX_FMT_YUVJ420P,
        SWS_BILINEAR, nullptr, nullptr, nullptr
        );
    if (!sws_ctx) {
        SP_LOG_ERROR("saveFrameToJpeg: sws_getContext failed");
        return false;
    }

    uint8_t* dst_data[4] = {nullptr};
    int dst_linesize[4] = {0};
    int allocRet = av_image_alloc(dst_data, dst_linesize, width, height, AV_PIX_FMT_YUVJ420P, 1);
    if (allocRet < 0) {
        sws_freeContext(sws_ctx);
        SP_LOG_ERROR("saveFrameToJpeg: av_image_alloc failed");
        return false;
    }

    // 准备源数据
    const uint8_t* src_data[4] = {nullptr};
    int src_linesize[4] = {0};
    if (format == AV_PIX_FMT_NV12) {
        src_data[0] = (uint8_t*)frame_data;
        src_linesize[0] = width;
        src_data[1] = (uint8_t*)frame_data + width * height;
        src_linesize[1] = width;
    } else if (format == AV_PIX_FMT_YUV420P || format == AV_PIX_FMT_YUVJ420P) {
        src_data[0] = (uint8_t*)frame_data;
        src_linesize[0] = width;
        src_data[1] = (uint8_t*)frame_data + width * height;
        src_linesize[1] = width / 2;
        src_data[2] = (uint8_t*)frame_data + width * height * 5 / 4;
        src_linesize[2] = width / 2;
    } else {
        // RGBA / BGRA
        src_data[0] = (uint8_t*)frame_data;
        src_linesize[0] = width * 4;
    }

    sws_scale(sws_ctx, src_data, src_linesize, 0, height, dst_data, dst_linesize);
    sws_freeContext(sws_ctx);

    // 2. 查找 mjpeg 编码器
    const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!codec) {
        SP_LOG_ERROR("saveFrameToJpeg: mjpeg encoder not found");
        av_freep(&dst_data[0]);
        return false;
    }

    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    if (!codecCtx) {
        av_freep(&dst_data[0]);
        return false;
    }
    codecCtx->width = width;
    codecCtx->height = height;
    codecCtx->pix_fmt = AV_PIX_FMT_YUVJ420P;
    codecCtx->time_base = {1, 25};
    codecCtx->framerate = {25, 1};

    // 设置 JPEG 质量
    av_opt_set(codecCtx->priv_data, "qmin", "2", 0);
    av_opt_set(codecCtx->priv_data, "qmax", "5", 0);

    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        SP_LOG_ERROR("saveFrameToJpeg: avcodec_open2 failed");
        avcodec_free_context(&codecCtx);
        av_freep(&dst_data[0]);
        return false;
    }

    // 3. 创建帧并编码
    AVFrame* frame = av_frame_alloc();
    frame->width = width;
    frame->height = height;
    frame->format = AV_PIX_FMT_YUVJ420P;
    for (int i = 0; i < 4; i++) {
        frame->data[i] = dst_data[i];
        frame->linesize[i] = dst_linesize[i];
    }
    frame->pts = 0;

    int ret = avcodec_send_frame(codecCtx, frame);
    if (ret < 0) {
        SP_LOG_ERROR("saveFrameToJpeg: avcodec_send_frame failed: %d", ret);
        av_frame_free(&frame);
        avcodec_free_context(&codecCtx);
        av_freep(&dst_data[0]);
        return false;
    }

    bool success = false;
    AVPacket* pkt = av_packet_alloc();
    ret = avcodec_receive_packet(codecCtx, pkt);
    if (ret >= 0 && pkt->size > 0) {
        FILE* fp = nullptr;
#ifdef _WIN32
        fopen_s(&fp, fullPath.c_str(), "wb");
#else
        fp = fopen(fullPath.c_str(), "wb");
#endif
        if (fp) {
            fwrite(pkt->data, 1, pkt->size, fp);
            fclose(fp);
            SP_LOG_DEBUG("截图保存成功：%s", fullPath.c_str());
            success = true;
        } else {
            SP_LOG_ERROR("截图保存失败：无法打开文件 %s", fullPath.c_str());
        }
    } else {
        SP_LOG_ERROR("saveFrameToJpeg: encode failed: %d", ret);
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&codecCtx);
    av_freep(&dst_data[0]);
    return success;
}

float PlayerCore::speedToRatio(float speed) const
{
    return speed;
}

int PlayerCore::speedToIndex(float speed) const
{
    if (speed <= 0.6f)  return 1;  // 0.5x
    if (speed <= 1.2f)  return 2;  // 1.0x
    if (speed <= 1.7f)  return 3;  // 1.5x
    return 4;                       // 2.0x
}

void PlayerCore::fillMediaInfo()
{
    media_info_ = SmartMediaInfo{};
    media_info_.filePath = file_url_;

    // 文件名
    size_t pos1 = file_url_.find_last_of('/');
    size_t pos2 = file_url_.find_last_of('\\');
    size_t pos = std::string::npos;
    if (pos1 != std::string::npos) pos = pos1;
    if (pos2 != std::string::npos && pos2 > pos) pos = pos2;
    media_info_.fileName = (pos == std::string::npos) ? file_url_ : file_url_.substr(pos + 1);

    media_info_.durationMs = duration_ms_;
    media_info_.hasVideo = hasVideo_;
    media_info_.hasAudio = hasAudio_;

    AVFormatContext* fmtCtx = demuxer_->formatContext();
    if (fmtCtx) {
        media_info_.formatName = fmtCtx->iformat ? fmtCtx->iformat->name : "";
        media_info_.bitRate = fmtCtx->bit_rate;
    }

    if (hasVideo_) {
        AVStream* vs = demuxer_->getStream(AVMEDIA_TYPE_VIDEO);
        if (vs && vs->codecpar) {
            const char* fmtName = av_get_pix_fmt_name(static_cast<AVPixelFormat>(vs->codecpar->format));
            media_info_.videoPixelFormat = fmtName ? fmtName : "unknown";
            if (vs->avg_frame_rate.den > 0) {
                media_info_.videoFrameRate = av_q2d(vs->avg_frame_rate);
            }
        }
    }

    if (hasAudio_) {
        AVStream* as = demuxer_->getStream(AVMEDIA_TYPE_AUDIO);
        if (as && as->codecpar) {
            media_info_.audioChannels = as->codecpar->ch_layout.nb_channels;
            media_info_.audioSampleRate = as->codecpar->sample_rate;
        }
    }
}

bool PlayerCore::isNetworkUrl(const std::string& url) const
{
    std::string lower = url;
    for (auto& c : lower) c = (char)tolower((unsigned char)c);
    return lower.compare(0, 7, "rtsp://") == 0 ||
           lower.compare(0, 7, "rtmp://") == 0 ||
           lower.compare(0, 7, "http://") == 0 ||
           lower.compare(0, 8, "https://") == 0 ||
           lower.compare(0, 7, "hls://") == 0;
}

void PlayerCore::notifyStateChanged()
{
    if (callback_) {
        SmartPlayerState spState = SP_STATE_STOPPED;
        switch (state_) {
        case Running: spState = SP_STATE_RUNNING; break;
        case Paused:  spState = SP_STATE_PAUSED;  break;
        default:      spState = SP_STATE_STOPPED; break;
        }
        callback_->onStateChanged(spState);
    }
}

void PlayerCore::notifyPosition(int64_t posMs)
{
    if (callback_) {
        callback_->onPositionChanged(posMs);
    }
}


void PlayerCore::clearAllQueues()
{
    if (audio_pkt_queue_)   audio_pkt_queue_->clear();
    if (video_pkt_queue_)   video_pkt_queue_->clear();
    if (audio_frame_queue_) audio_frame_queue_->clear();
    if (video_frame_queue_) video_frame_queue_->clear();

}

void PlayerCore::releaseResources()
{
    if (audio_filter_) {
        audio_filter_->close();
        delete audio_filter_;
        audio_filter_ = nullptr;
    }
    if (audio_output_) {
        delete audio_output_;
        audio_output_ = nullptr;
    }
    if (audio_decoder_) {
        audio_decoder_->close();
        delete audio_decoder_;
        audio_decoder_ = nullptr;
    }

    if (converter_) {
        delete converter_;
        converter_ = nullptr;
    }
    if (video_decoder_) {
        video_decoder_->close();
        delete video_decoder_;
        video_decoder_ = nullptr;
    }

    if (demuxer_) {
        demuxer_->close();
        delete demuxer_;
        demuxer_ = nullptr;
    }

    SP_LOG_DEBUG("资源已释放");
}

int64_t PlayerCore::duration() const
{
    return duration_ms_;
}

int64_t PlayerCore::currentPos() const
{
    if (hasAudio_) {
        return sync_clock_->get_audio_clock() / 1000000;
    } else if (hasVideo_) {
        return sync_clock_->getCurrentSystemClock() / 1000000;
    }
    return 0;
}

double PlayerCore::currentTimeSec() const
{
    if (hasAudio_)
        return sync_clock_->get_audio_clock() / 1000000.0;
    if (hasVideo_)
        return sync_clock_->getCurrentSystemClock() / 1000000.0;
    return 0.0;
}

Demuxer::MediaType PlayerCore::mediaType() const
{
    if(demuxer_) return demuxer_->mediaType();
    return Demuxer::MediaType::FILE_TYPE;
}

PlayerCore::State PlayerCore::state() const
{
    return state_;
}

AVFormatContext *PlayerCore::avFormatContext() const
{
    if(!demuxer_) return nullptr;
    return demuxer_->formatContext();
}

std::string PlayerCore::fileUrl() const
{
    return file_url_;
}

bool PlayerCore::hasAudio() const
{
    return hasAudio_;
}

bool PlayerCore::hasVideo() const
{
    return hasVideo_;
}
