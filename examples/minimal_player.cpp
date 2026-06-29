// SmartPlayer SDK 最小示例 —— 用 SDL2 渲染视频帧
//
// 编译: 参见 CMakeLists.txt 中的 SmartPlayerExample target
// 运行: SmartPlayerExample <视频文件路径>

#define SDL_MAIN_HANDLED  // 防止 SDL2 重定义 main 为 SDL_main

#include "smartplayer.h"
#include "smartplayercallback.h"

#include <SDL2/SDL.h>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <vector>

class SDLPlayerCallback : public SmartPlayerCallback {
public:
    SDL_Window*   win_ = nullptr;
    SDL_Renderer* ren_ = nullptr;
    SDL_Texture*  tex_ = nullptr;
    std::atomic<bool> hasFrame_{false};
    std::atomic<bool> finished_{false};   // 播放结束标志

    // 线程安全帧缓冲：渲染线程写入，主线程读取
    std::mutex frameMutex_;
    std::vector<uint8_t> frameBuf_;
    int frameW_ = 0;
    int frameH_ = 0;
    SmartPixelFormat frameFmt_ = SP_FMT_UNKNOWN;

    void onOpenResult(bool ok, const std::string& err) override {
        printf("[Player] Open: %s\n", ok ? "success" : err.c_str());
    }

    void onStateChanged(SmartPlayerState state) override {
        const char* names[] = {"Stopped", "Running", "Paused"};
        printf("[Player] State: %s\n", names[state]);
    }

    void onDurationChanged(int64_t durationMs) override {
        printf("[Player] Duration: %lld ms\n", (long long)durationMs);
    }

    void onMediaInfoReady(const SmartMediaInfo& info) override {
        printf("[Player] %s | %s | %.1ffps | audio:%dHz ch:%d\n",
               info.fileName.c_str(),
               info.videoPixelFormat.c_str(),
               info.videoFrameRate,
               info.audioSampleRate,
               info.audioChannels);
    }

    void onVideoFrame(const uint8_t* data, int w, int h,
                      SmartPixelFormat fmt) override {
        size_t dataSize = 0;
        switch (fmt) {
        case SP_FMT_YUV420P:
        case SP_FMT_NV12:
            dataSize = (size_t)w * h * 3 / 2;
            break;
        case SP_FMT_RGBA:
        case SP_FMT_BGRA:
            dataSize = (size_t)w * h * 4;
            break;
        default:
            return;
        }

        std::lock_guard<std::mutex> lock(frameMutex_);
        frameBuf_.resize(dataSize);
        memcpy(frameBuf_.data(), data, dataSize);
        frameW_ = w;
        frameH_ = h;
        frameFmt_ = fmt;
        hasFrame_ = true;
    }

    void onPlayFinished() override {
        printf("[Player] Finished\n");
        finished_ = true;
    }

    void onError(const std::string& msg) override {
        printf("[Player] Error: %s\n", msg.c_str());
    }

    // 在主线程调用 —— 创建/更新纹理并渲染
    void render() {
        if (!hasFrame_.load() || !ren_) return;

        std::lock_guard<std::mutex> lock(frameMutex_);
        if (frameBuf_.empty()) return;

        int w = frameW_;
        int h = frameH_;
        SmartPixelFormat fmt = frameFmt_;

        if (!tex_ || texW_ != w || texH_ != h || texFmt_ != fmt) {
            if (tex_) SDL_DestroyTexture(tex_);

            Uint32 sdlFmt = SDL_PIXELFORMAT_IYUV;  // I420 = YUV420P (Y-U-V 顺序)
            if (fmt == SP_FMT_NV12) sdlFmt = SDL_PIXELFORMAT_NV12;
            else if (fmt == SP_FMT_RGBA) sdlFmt = SDL_PIXELFORMAT_RGBA32;
            else if (fmt == SP_FMT_BGRA) sdlFmt = SDL_PIXELFORMAT_BGRA32;

            tex_ = SDL_CreateTexture(ren_, sdlFmt,
                                     SDL_TEXTUREACCESS_STREAMING, w, h);
            if (!tex_) {
                printf("[Player] SDL_CreateTexture failed: %s\n", SDL_GetError());
                return;
            }
            texW_ = w;
            texH_ = h;
            texFmt_ = fmt;
            SDL_SetWindowSize(win_, w, h);
        }

        if (fmt == SP_FMT_YUV420P) {
            int ySize = w * h;
            int uvSize = w * h / 4;
            SDL_UpdateYUVTexture(tex_, nullptr,
                frameBuf_.data(), w,
                frameBuf_.data() + ySize, w / 2,
                frameBuf_.data() + ySize + uvSize, w / 2);
        } else if (fmt == SP_FMT_NV12) {
            SDL_UpdateTexture(tex_, nullptr, frameBuf_.data(), w);
        } else {
            SDL_UpdateTexture(tex_, nullptr, frameBuf_.data(), w * 4);
        }

        SDL_RenderClear(ren_);
        SDL_RenderCopy(ren_, tex_, nullptr, nullptr);
        SDL_RenderPresent(ren_);
    }

private:
    int texW_ = 0, texH_ = 0;
    SmartPixelFormat texFmt_ = SP_FMT_UNKNOWN;
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <video_file>\n", argv[0]);
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDLPlayerCallback cb;
    cb.win_ = SDL_CreateWindow("SmartPlayer SDK",
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               1280, 720, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!cb.win_) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    cb.ren_ = SDL_CreateRenderer(cb.win_, -1, SDL_RENDERER_ACCELERATED);
    if (!cb.ren_) {
        // 尝试软件渲染
        cb.ren_ = SDL_CreateRenderer(cb.win_, -1, SDL_RENDERER_SOFTWARE);
    }
    if (!cb.ren_) {
        printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(cb.win_);
        SDL_Quit();
        return 1;
    }

    SmartPlayer player;
    player.setCallback(&cb);
    player.setHardwareDecode(false);  // 禁用硬解避免 CUDA 报错
    player.open(argv[1]);

    // 等待 open 结果，防止在打开失败后调用 play 导致崩溃
    // open 本地文件是同步的，此时 onOpenResult 已回调
    if (player.state() == SP_STATE_STOPPED && !player.hasVideo() && !player.hasAudio()) {
        printf("[Player] Open failed, no media streams found.\n");
        SDL_DestroyRenderer(cb.ren_);
        SDL_DestroyWindow(cb.win_);
        SDL_Quit();
        return 1;
    }

    player.play();

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN) {
                switch (e.key.keysym.sym) {
                case SDLK_SPACE:
                    if (player.state() == SP_STATE_RUNNING) player.pause();
                    else if (player.state() == SP_STATE_PAUSED) player.play();
                    break;
                case SDLK_ESCAPE:
                    running = false;
                    break;
                case SDLK_LEFT:
                    player.seek(player.position() - 10000);
                    break;
                case SDLK_RIGHT:
                    player.seek(player.position() + 10000);
                    break;
                }
            }
        }

        // 播放结束后自动退出
        if (cb.finished_.load()) {
            running = false;
        }

        cb.render();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    printf("[Player] Stopping...\n");
    player.stop();
    printf("[Player] Stopped.\n");

    if (cb.tex_) SDL_DestroyTexture(cb.tex_);
    if (cb.ren_) SDL_DestroyRenderer(cb.ren_);
    if (cb.win_) SDL_DestroyWindow(cb.win_);
    SDL_Quit();

    return 0;
}
