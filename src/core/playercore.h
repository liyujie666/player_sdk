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

    // Control functions
    void open(const std::string &url);
    void play();
    void pause();
    void stop();
    void setSpeed(float speed);          // 0.5 / 1.0 / 1.5 / 2.0

    // Hardware decoding
    void useHardware(bool isUse);
    void setDecodeType(const std::string& decoder);
    // seek (ms)
    void seek(int64_t pos_ms);
    // Screenshot
    void setScreenshotSavePath(const std::string& savePath);
    void takeScreenshot();

    // Volume
    void setVolume(int val);
    void setMute(bool mute);
    bool isMute() const;

    // Callback
    void setCallback(SmartPlayerCallback* callback) { callback_ = callback; }

    // Get info
    int64_t duration() const;          // total duration (ms)
    int64_t currentPos() const;        // current playback position (ms)
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
    void demuxThreadFunc();       // Demuxer thread
    void audioDecodeThreadFunc(); // Audio decode thread
    void videoDecodeThreadFunc(); // Video decode thread
    void videoRenderThreadFunc(); // Video render thread

    void releaseResources();      // Release all resources
    void clearAllQueues();        // Clear all queues (used by Seek/Stop)
    void initAudioModule();       // Init audio module (resampler / filter / output)
    void initVideoModule();       // Init video module (converter)
    bool saveFrameToJpeg(const uint8_t* frame_data, int width, int height,
                         AVPixelFormat format, const std::string& savePath);
    float speedToRatio(float speed) const;
    int speedToIndex(float speed) const;
    void fillMediaInfo();
    bool isNetworkUrl(const std::string& url) const;

    // Callback helpers
    void notifyStateChanged();
    void notifyPosition(int64_t posMs);

private:

    // State control
    std::atomic<State> state_;
    std::atomic<bool> is_exit_;       // Thread exit flag
    std::atomic<bool> is_seek_;       // Seek flag
    std::atomic<bool> need_screenshot_{false};
    std::atomic<bool> screenshot_busy_{false};
    std::mutex mutex_;
    std::condition_variable cond_;

    // Core modules
    Demuxer* demuxer_ = nullptr;
    Decoder* audio_decoder_ = nullptr;
    Decoder* video_decoder_ = nullptr;
    VideoConverter* converter_ = nullptr;
    Resampler* resampler_ = nullptr;
    AudioFilter* audio_filter_ = nullptr;
    AudioOutput* audio_output_ = nullptr;

    // Queues (demuxer -> decoder -> playback)
    AVPacketQueue* audio_pkt_queue_ = nullptr;
    AVPacketQueue* video_pkt_queue_ = nullptr;
    AVFrameQueue* audio_frame_queue_ = nullptr;
    AVFrameQueue* video_frame_queue_ = nullptr;

    static constexpr int MAX_AUDIO_PKT = 16;
    static constexpr int MAX_VIDEO_PKT = 30;
    static constexpr int MAX_AUDIO_FRAME = 8;
    static constexpr int MAX_VIDEO_FRAME = 15;

    // Threads
    std::unique_ptr<std::thread> demux_thread_;
    std::unique_ptr<std::thread> audio_decode_thread_;
    std::unique_ptr<std::thread> video_decode_thread_;
    std::unique_ptr<std::thread> video_render_thread_;
    std::unique_ptr<std::thread> open_thread_;
    AVSyncClock* sync_clock_ = nullptr;

    // Media params
    std::string file_url_;
    int64_t duration_ms_ = 0;
    int video_stream_idx_ = -1;
    int audio_stream_idx_ = -1;
    bool hardware_enabled_ = false;
    std::string decoder_type_ = "";
    std::string screenshot_save_path_ = "";
    bool hasAudio_ = false;
    bool hasVideo_ = false;

    // Callback
    SmartPlayerCallback* callback_ = nullptr;

    // Media info cache
    SmartMediaInfo media_info_;
};

#endif // PLAYERCORE_H
