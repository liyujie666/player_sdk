#ifndef SMARTPLAYER_H
#define SMARTPLAYER_H

#include <memory>
#include <string>
#include "smartplayerdefs.h"
#include "smartplayercallback.h"

/**
 * SmartPlayer —— 视频播放器 SDK 主入口 (Facade)
 *
 * 仅依赖 FFmpeg + SDL2 + C++17 标准库。
 * 使用 PIMPL 模式隐藏内部实现，外部只需 #include "smartplayer.h"。
 *
 * 典型用法:
 *   SmartPlayer player;
 *   player.setCallback(&myCallback);
 *   player.open("D:/video.mp4");
 *   player.play();
 *   // ... 事件循环
 *   player.stop();
 */
class SMARTPLAYER_API SmartPlayer {
public:
    SmartPlayer();
    ~SmartPlayer();

    SmartPlayer(const SmartPlayer&) = delete;
    SmartPlayer& operator=(const SmartPlayer&) = delete;

    // ==================== 生命周期 ====================

    /// 打开媒体 (本地文件或网络流)。网络流异步打开，结果通过 onOpenResult 回调。
    void open(const std::string& url);

    /// 开始播放（从 Stopped 或 Paused 状态）
    void play();

    /// 暂停
    void pause();

    /// 停止并释放资源
    void stop();

    /// 跳转 (posMs 单位毫秒)
    void seek(int64_t posMs);

    // ==================== 播放控制 ====================

    /// 设置倍速 (0.5 / 1.0 / 1.5 / 2.0)
    void setSpeed(float speed);

    /// 设置音量 (0~100)
    void setVolume(int volume);

    /// 静音/取消静音
    void setMute(bool mute);

    // ==================== 视频 ====================

    /// 启用/禁用硬件解码
    void setHardwareDecode(bool enable);

    /// 指定解码器名称 (如 "h264_cuvid")，空字符串表示自动选择
    void setDecoderType(const std::string& decoder);

    /// 截图并保存到 savePath (JPEG 格式)，结果通过 onScreenshot 回调
    void takeScreenshot(const std::string& savePath);

    // ==================== 状态查询 ====================

    SmartPlayerState state() const;
    int64_t duration() const;       // ms
    int64_t position() const;       // ms
    bool    hasAudio() const;
    bool    hasVideo() const;
    SmartMediaType mediaType() const;
    const SmartMediaInfo& mediaInfo() const;

    // ==================== 回调 ====================

    /// 注册回调 (不持有所有权，调用方需确保回调对象生命周期长于播放器或在使用期间有效)
    void setCallback(SmartPlayerCallback* callback);

private:
    class Impl;
    std::unique_ptr<Impl> d;
};

#endif // SMARTPLAYER_H
