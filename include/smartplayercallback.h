#ifndef SMARTPLAYER_CALLBACK_H
#define SMARTPLAYER_CALLBACK_H

#include <cstdint>
#include <string>
#include "smartplayerdefs.h"

/**
 * Callback interface — users inherit this class, implement the events they need,
 * and register an instance with SmartPlayer.
 * All callbacks are invoked on the player's internal threads; do not perform
 * long-running or blocking work inside them.
 */
class SmartPlayerCallback {
public:
    virtual ~SmartPlayerCallback() = default;

    /// Playback state changed
    virtual void onStateChanged(SmartPlayerState /*state*/) {}

    /// Playback progress changed (ms)
    virtual void onPositionChanged(int64_t /*posMs*/) {}

    /// Total duration ready (ms)
    virtual void onDurationChanged(int64_t /*durationMs*/) {}

    /// open result
    virtual void onOpenResult(bool /*success*/, const std::string& /*errMsg*/) {}

    /// Media info ready
    virtual void onMediaInfoReady(const SmartMediaInfo& /*info*/) {}

    /// Playback finished
    virtual void onPlayFinished() {}

    /// Error
    virtual void onError(const std::string& /*msg*/) {}

    /// Screenshot completed
    virtual void onScreenshot(const std::string& /*path*/, bool /*success*/) {}

    /**
     * Video frame callback — render the frame yourself.
     *
     * Data layout depends on pixelFormat:
     *   YUV420P: data = [Y: w*h][U: w*h/4][V: w*h/4] stored contiguously
     *   NV12:    data = [Y: w*h][interleaved UV: w*h/2]
     *   RGBA:    data = 4 bytes per pixel, w*h*4 total
     *   BGRA:    data = 4 bytes per pixel, w*h*4 total
     *
     * Note: the data pointer is only valid during the callback; copy it
     * if you need to keep the frame.
     */
    virtual void onVideoFrame(const uint8_t* /*data*/, int /*width*/, int /*height*/,
                              SmartPixelFormat /*pixelFormat*/) {}
};

#endif // SMARTPLAYER_CALLBACK_H
