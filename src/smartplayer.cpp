#include "smartplayer.h"
#include "core/playercore.h"

class SmartPlayer::Impl {
public:
    PlayerCore core_;
    SmartMediaInfo media_info_;
    bool media_info_valid_ = false;

    static SmartPlayerState toSpState(PlayerCore::State s) {
        switch (s) {
        case PlayerCore::Running: return SP_STATE_RUNNING;
        case PlayerCore::Paused:  return SP_STATE_PAUSED;
        default:                  return SP_STATE_STOPPED;
        }
    }

    static SmartMediaType toSpMediaType(Demuxer::MediaType t) {
        switch (t) {
        case Demuxer::MediaType::RTSP_TYPE:  return SP_MEDIA_RTSP;
        case Demuxer::MediaType::RTMP_TYPE:  return SP_MEDIA_RTMP;
        case Demuxer::MediaType::HTTP_TYPE:  return SP_MEDIA_HTTP;
        case Demuxer::MediaType::HTTPS_TYPE: return SP_MEDIA_HTTPS;
        case Demuxer::MediaType::HLS_TYPE:   return SP_MEDIA_HLS;
        default:                             return SP_MEDIA_FILE;
        }
    }
};

SmartPlayer::SmartPlayer()
    : d(std::make_unique<Impl>())
{
}

SmartPlayer::~SmartPlayer() = default;

void SmartPlayer::open(const std::string& url)
{
    d->core_.open(url);
}

void SmartPlayer::play()
{
    d->core_.play();
}

void SmartPlayer::pause()
{
    d->core_.pause();
}

void SmartPlayer::stop()
{
    d->core_.stop();
}

void SmartPlayer::seek(int64_t posMs)
{
    d->core_.seek(posMs);
}

void SmartPlayer::setSpeed(float speed)
{
    d->core_.setSpeed(speed);
}

void SmartPlayer::setVolume(int volume)
{
    d->core_.setVolume(volume);
}

void SmartPlayer::setMute(bool mute)
{
    d->core_.setMute(mute);
}

void SmartPlayer::setHardwareDecode(bool enable)
{
    d->core_.useHardware(enable);
}

void SmartPlayer::setDecoderType(const std::string& decoder)
{
    d->core_.setDecodeType(decoder);
}

void SmartPlayer::takeScreenshot(const std::string& savePath)
{
    d->core_.setScreenshotSavePath(savePath);
    d->core_.takeScreenshot();
}

SmartPlayerState SmartPlayer::state() const
{
    return Impl::toSpState(d->core_.state());
}

int64_t SmartPlayer::duration() const
{
    return d->core_.duration();
}

int64_t SmartPlayer::position() const
{
    return d->core_.currentPos();
}

bool SmartPlayer::hasAudio() const
{
    return d->core_.hasAudio();
}

bool SmartPlayer::hasVideo() const
{
    return d->core_.hasVideo();
}

SmartMediaType SmartPlayer::mediaType() const
{
    return Impl::toSpMediaType(d->core_.mediaType());
}

const SmartMediaInfo& SmartPlayer::mediaInfo() const
{
    return d->core_.mediaInfo();
}

void SmartPlayer::setCallback(SmartPlayerCallback* callback)
{
    d->core_.setCallback(callback);
}
