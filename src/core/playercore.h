#ifndef PLAYERCORE_H
#define PLAYERCORE_H

#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <vector>
#include <cstdint>
#include <string>

#include "demuxer/demuxer.h"
#include "decoder/decoder.h"
#include "converter/videoconverter.h"
#include "resampler/resampler.h"
#include "filter/audiofilter.h"
#include "render/audiooutput.h"
#include "queue/avpacketqueue.h"
#include "queue/avframequeue.h"
#include "syncclock.h"
#include "smartplayercallback.h"

class PlayerCore
{
public:
    enum State {
        Stopped = 0,
        Running = 1,
        Paused = 2
    };

    explicit PlayerCore();
    ~PlayerCore();

    PlayerCore(const PlayerCore&) = delete;
    PlayerCore& operator=(const PlayerCore&) = delete;

    // 控制函数
    void open(const std::string &url);
    void play();
    void pause();
    void stop();
    void setSpeed(float speed);          // 0.5 / 1.0 / 1.5 / 2.0

    // 硬件解码
    void useHardware(bool isUse);
    void setDecodeType(const std::string& decoder);
    // seek (ms)
    void seek(int64_t pos_ms);
    // 截图
    void setScreenshotSavePath(const std::string& savePath);
    void takeScreenshot();

    // 音量
    void setVolume(int val);
    void setMute(bool mute);
    bool isMute() const;

    // 回调
    void setCallback(SmartPlayerCallback* callback) { callback_ = callback; }

    // 获取信息
    int64_t duration() const;          // 总时长(ms)
    int64_t currentPos() const;        // 当前播放位置(ms)
    double currentTimeSec() const;
    State state() const;
    Demuxer::MediaType mediaType() const;
    AVFormatContext* avFormatContext() const;
    std::string fileUrl() const;
    bool hasAudio() const;
    bool hasVideo() const;
    const SmartMediaInfo& mediaInfo() const { return media_info_; }

private:
    bool openInternal(const std::string &url);
    void demuxThreadFunc();       // 解复用线程
    void audioDecodeThreadFunc(); // 音频解码线程
    void videoDecodeThreadFunc(); // 视频解码线程
    void videoRenderThreadFunc(); // 视频渲染线程

    void releaseResources();      // 释放所有资源
    void clearAllQueues();        // 清空所有队列(Seek/Stop用)
    void initAudioModule();       // 初始化音频模块(重采样/滤镜/输出)
    void initVideoModule();       // 初始化视频模块(转换器)
    bool saveFrameToJpeg(const uint8_t* frame_data, int width, int height,
                         AVPixelFormat format, const std::string& savePath);
    float speedToRatio(float speed) const;
    int speedToIndex(float speed) const;
    void fillMediaInfo();
    bool isNetworkUrl(const std::string& url) const;

    // 回调助手
    void notifyStateChanged();
    void notifyPosition(int64_t posMs);

private:

    // 状态控制
    std::atomic<State> state_;
    std::atomic<bool> is_exit_;       // 线程退出标志
    std::atomic<bool> is_seek_;       // Seek标志
    std::atomic<bool> need_screenshot_{false};
    std::atomic<bool> screenshot_busy_{false};
    std::mutex mutex_;
    std::condition_variable cond_;

    // 核心模块
    Demuxer* demuxer_ = nullptr;
    Decoder* audio_decoder_ = nullptr;
    Decoder* video_decoder_ = nullptr;
    VideoConverter* converter_ = nullptr;
    Resampler* resampler_ = nullptr;
    AudioFilter* audio_filter_ = nullptr;
    AudioOutput* audio_output_ = nullptr;

    // 队列（解复用→解码→播放）
    AVPacketQueue* audio_pkt_queue_ = nullptr;
    AVPacketQueue* video_pkt_queue_ = nullptr;
    AVFrameQueue* audio_frame_queue_ = nullptr;
    AVFrameQueue* video_frame_queue_ = nullptr;

    static constexpr int MAX_AUDIO_PKT = 16;
    static constexpr int MAX_VIDEO_PKT = 30;
    static constexpr int MAX_AUDIO_FRAME = 8;
    static constexpr int MAX_VIDEO_FRAME = 15;

    // 线程
    std::unique_ptr<std::thread> demux_thread_;
    std::unique_ptr<std::thread> audio_decode_thread_;
    std::unique_ptr<std::thread> video_decode_thread_;
    std::unique_ptr<std::thread> video_render_thread_;
    std::unique_ptr<std::thread> open_thread_;
    AVSyncClock* sync_clock_ = nullptr;

    // 媒体参数
    std::string file_url_;
    int64_t duration_ms_ = 0;
    int video_stream_idx_ = -1;
    int audio_stream_idx_ = -1;
    bool hardware_enabled_ = false;
    std::string decoder_type_ = "";
    std::string screenshot_save_path_ = "";
    bool hasAudio_ = false;
    bool hasVideo_ = false;

    // 回调
    SmartPlayerCallback* callback_ = nullptr;

    // 媒体信息缓存
    SmartMediaInfo media_info_;
};

#endif // PLAYERCORE_H
