#include "audiooutput.h"
#include "core/syncclock.h"
#include "pool/gloabalpool.h"
#include "utils/log.h"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <SDL2/SDL.h>

AudioOutput::AudioOutput(const Resampler::AudioSpec &inSpec,
                         const Resampler::AudioSpec &outSpec,
                         AVFrameQueue *frameQueue,
                         AVSyncClock* sync_clock)
    : sync_clock_(sync_clock),in_spec_(inSpec), out_spec_(outSpec), frame_queue_(frameQueue)
{
    need_resample_ = !(
        in_spec_.sampleRate == out_spec_.sampleRate &&
        in_spec_.sampleFmt == out_spec_.sampleFmt &&
        in_spec_.chs == out_spec_.chs &&
        av_channel_layout_compare(&in_spec_.chLayout, &out_spec_.chLayout) == 0
        );

    if(need_resample_){
        resampler_ = new Resampler();
        resampler_->init(inSpec, outSpec);
        SP_LOG_DEBUG("[AudioOutput] 重采样器初始化完成");
    } else {
        resampler_ = nullptr;
    }

    sample_rate_ = outSpec.sampleRate;

    audio_buf_ = nullptr;
    audio_buf_size_ = 0;
    audio_buf_index_ = 0;
}

AudioOutput::~AudioOutput()
{
    DeInit();
    if (resampler_) {
        delete resampler_;
        resampler_ = nullptr;
    }

}

int AudioOutput::Init()
{
    if (is_audio_init_) {
        return 0;
    }

    // 如果 AUDIO 子系统尚未初始化，则初始化它
    if (!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO)) {
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            SP_LOG_ERROR("SDL音频子系统初始化失败: %s", SDL_GetError());
            return -1;
        }
    }

    SDL_AudioSpec spec{};
    spec.freq = out_spec_.sampleRate;
    spec.channels = out_spec_.chs;
    spec.samples = 1024;
    spec.callback = fill_audio_pcm;
    spec.userdata = this;

    if (out_spec_.sampleFmt == AV_SAMPLE_FMT_S16) {
        spec.format = AUDIO_S16LSB;
    } else if (out_spec_.sampleFmt == AV_SAMPLE_FMT_FLT) {
        spec.format = AUDIO_F32LSB;
    } else {
        SP_LOG_ERROR("不支持的采样格式: %d", out_spec_.sampleFmt);
        return -1;
    }

    if (SDL_OpenAudio(&spec, nullptr) != 0) {
        SP_LOG_ERROR("SDL打开音频设备失败: %s", SDL_GetError());
        return -1;
    }

    SDL_PauseAudio(0);
    is_audio_init_ = true;
    return 0;
}

int AudioOutput::DeInit()
{
    if (!is_audio_init_) {
        return 0;
    }

    SDL_PauseAudio(1);
    SDL_CloseAudio();
    // 注意：不再调用 SDL_QuitSubSystem(SDL_INIT_AUDIO)，
    // 因为 AUDIO 子系统可能是外部应用初始化的，由外部负责释放

    if (audio_buf_) {
        free(audio_buf_);
        audio_buf_ = nullptr;
    }

    audio_buf_size_ = 0;
    audio_buf_index_ = 0;
    is_audio_init_ = false;
    return 0;
}

void AudioOutput::pause()
{
    SDL_PauseAudio(1);
}

void AudioOutput::resume()
{
    SDL_PauseAudio(0);
}

void AudioOutput::setAudioTimebase(AVRational tb)
{
    audio_timebase_ = tb;
}


void AudioOutput::setVolume(int volume) {
    volume_ = std::clamp(volume, 0, 100);
}

int AudioOutput::volume() const {
    return volume_;
}

void AudioOutput::setMute(bool mute) {
    mute_ = mute;
}

bool AudioOutput::isMute() const {
    return mute_;
}
int64_t AudioOutput::audioClock() const
{
    std::lock_guard<std::mutex> locker(mutex_);
    return audio_clock_us_;
}

int AudioOutput::resampleFrameToBuffer(uint8_t* stream, int len)
{
    if (!frame_queue_) {
        memset(stream, 0, len);
        return -1;
    }
    if (!is_audio_init_) {
        memset(stream, 0, len);
        return -1;
    }

    memset(stream, 0, len);
    int len_remaining = len;

    while (len_remaining > 0) {
        if (audio_buf_index_ >= audio_buf_size_) {
            AVFrame* frame = frame_queue_->Pop(10);
            if (!frame) {
                return 0;
            }

            if (frame->nb_samples <= 0 || frame->format == AV_SAMPLE_FMT_NONE) {
                GlobalPool::getFramePool().recycle(frame);
                continue;
            }
            if (need_resample_) {
                if(!resampler_){
                    GlobalPool::getFramePool().recycle(frame);
                    return 0;
                }
                int out_samples = 0;

                // 正确估算重采样后的最大输出样本数（考虑采样率转换比率 + 内部延迟）
                int max_out_samples = av_rescale_rnd(
                    frame->nb_samples + 256,  // 加额外余量应对 swr 内部延迟
                    out_spec_.sampleRate,
                    in_spec_.sampleRate,
                    AV_ROUND_UP
                );
                int buf_size = resampler_->outputBufferSize(max_out_samples);

                uint8_t* tmp = (uint8_t*)realloc(audio_buf_, buf_size);
                if(!tmp){
                    free(audio_buf_);
                    audio_buf_ = nullptr;
                    GlobalPool::getFramePool().recycle(frame);
                    return 0;
                }
                audio_buf_ = tmp;

                resampler_->resample(frame, &audio_buf_, &out_samples);
                audio_buf_size_ = resampler_->outputBufferSize(out_samples);
            } else {
                // 不需要重采样，直接拷贝帧数据
                int planar = av_sample_fmt_is_planar((AVSampleFormat)frame->format);
                int channels = frame->ch_layout.nb_channels;

                audio_buf_size_ = av_samples_get_buffer_size(
                    nullptr, channels,
                    frame->nb_samples, (AVSampleFormat)frame->format, 1
                    );
                if(audio_buf_size_ <=0){
                    GlobalPool::getFramePool().recycle(frame);
                    return 0;
                }

                uint8_t* tmp = (uint8_t*)realloc(audio_buf_, audio_buf_size_);
                if(!tmp){
                    free(audio_buf_);
                    audio_buf_ = nullptr;
                    GlobalPool::getFramePool().recycle(frame);
                    return 0;
                }
                audio_buf_ = tmp;

                if(!frame->data[0]){
                    GlobalPool::getFramePool().recycle(frame);
                    return 0;
                }

                if (planar && channels > 1) {
                    // planar 格式：需要将各声道数据交织拷贝
                    int bytes_per_sample = av_get_bytes_per_sample((AVSampleFormat)frame->format);
                    int plane_size = frame->nb_samples * bytes_per_sample;
                    uint8_t* dst = audio_buf_;
                    for (int s = 0; s < frame->nb_samples; s++) {
                        for (int ch = 0; ch < channels; ch++) {
                            memcpy(dst, frame->data[ch] + s * bytes_per_sample, bytes_per_sample);
                            dst += bytes_per_sample;
                        }
                    }
                } else {
                    // packed 格式：data[0] 包含所有交织数据，直接拷贝
                    memcpy(audio_buf_, frame->data[0], audio_buf_size_);
                }
            }

            audio_buf_index_ = 0;
            audio_clock_us_ = av_rescale_q(frame->pts, audio_timebase_, {1, 1000000});
            sync_clock_->set_audio_clock(audio_clock_us_);


            GlobalPool::getFramePool().recycle(frame);
        }

        int copy_len = std::min(len_remaining, (int)(audio_buf_size_ - audio_buf_index_));
        if(!audio_buf_){
            return 0;
        }
        memcpy(stream, audio_buf_ + audio_buf_index_, copy_len);

        applyVolume(stream, copy_len);

        stream += copy_len;
        len_remaining -= copy_len;
        audio_buf_index_ += copy_len;
    }

    return 0;
}

void AudioOutput::applyVolume(uint8_t* data, int len) {
    std::lock_guard<std::mutex> locker(mutex_);
    if (!data || len <= 0) return;

    if (mute_) {
        memset(data, 0, len);
        return;
    }

    float factor = volume_ / 100.0f;
    if (factor >= 1.0f) return;

    int sample_count = len / 2;
    int16_t* samples = (int16_t*)data;

    for (int i = 0; i < sample_count; i++) {
        samples[i] = static_cast<int16_t>(samples[i] * factor);
    }
}

void AudioOutput::fill_audio_pcm(void *udata, Uint8 *stream, int len)
{
    AudioOutput* self = (AudioOutput*)udata;
    if (!self) {
        return;
    }
    if (!self->is_audio_init_) {
        return;
    }

    self->resampleFrameToBuffer(stream, len);
}
