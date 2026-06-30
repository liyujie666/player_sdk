// SmartPlayer SDK minimal example — renders video frames with SDL2
//
// Build: see the SmartPlayerExample target in CMakeLists.txt
// Run:   SmartPlayerExample <video_file_path>

#define SDL_MAIN_HANDLED  // Prevent SDL2 from redefining main as SDL_main

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
    std::atomic<bool> finished_{false};   // playback finished flag
    bool customSize_ = false;             // whether the user has specified the window size

    // Thread-safe frame buffer: written by the render thread, read by the main thread
    std::mutex frameMutex_;
    std::vector<uint8_t> frameBuf_;
    int frameW_ = 0;
    int frameH_ = 0;
    SmartPixelFormat frameFmt_ = SP_FMT_UNKNOWN;

    void onOpenResult(bool ok, const char* err) override {
        printf("[Player] Open: %s\n", ok ? "success" : err);
    }

    void onStateChanged(SmartPlayerState state) override {
        const char* names[] = {"Stopped", "Running", "Paused"};
        printf("[Player] State: %s\n", names[state]);
    }

    void onDurationChanged(int64_t durationMs) override {
        printf("[Player] Duration: %lld ms\n", (long long)durationMs);
    }

    void onMediaInfoReady(const SmartMediaInfo& info) override {
        printf("[Player] %s | %s | %.1f fps | audio: %d Hz ch: %d\n",
               info.fileName,
               info.videoPixelFormat,
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

    void onError(const char* msg) override {
        printf("[Player] Error: %s\n", msg);
    }

    // Called on the main thread — creates/updates the texture and renders
    void render() {
        if (!hasFrame_.load() || !ren_) return;

        std::lock_guard<std::mutex> lock(frameMutex_);
        if (frameBuf_.empty()) return;

        int w = frameW_;
        int h = frameH_;
        SmartPixelFormat fmt = frameFmt_;

        if (!tex_ || texW_ != w || texH_ != h || texFmt_ != fmt) {
            if (tex_) SDL_DestroyTexture(tex_);

            Uint32 sdlFmt = SDL_PIXELFORMAT_IYUV;  // I420 = YUV420P (Y-U-V order)
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
            if (!customSize_) {
                SDL_SetWindowSize(win_, w, h);
            }
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

// Command-line argument parser helper
static void printUsage(const char* prog) {
    printf("Usage: %s <video_file> [options]\n", prog);
    printf("Options:\n");
    printf("  --hw              Enable hardware decoding\n");
    printf("  --speed <val>     Set playback speed (0.5/1.0/1.5/2.0)\n");
    printf("  --size <WxH>      Set window size (e.g. 1920x1080)\n");
    printf("\nControls:\n");
    printf("  Space       Pause / Resume\n");
    printf("  Left/Right  Seek -/+ 10s\n");
    printf("  Up/Down     Volume +/- 5\n");
    printf("  M           Toggle mute\n");
    printf("  S           Cycle speed (1.0 -> 1.5 -> 2.0 -> 0.5 -> 1.0)\n");
    printf("  ESC         Quit\n");
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    // Parse command-line arguments
    const char* videoFile = nullptr;
    bool hwDecode = false;
    float initSpeed = 1.0f;
    int winW = 1280, winH = 720;
    bool hasCustomSize = false;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--hw") == 0) {
            hwDecode = true;
        } else if (strcmp(argv[i], "--speed") == 0 && i + 1 < argc) {
            initSpeed = (float)atof(argv[++i]);
        } else if (strcmp(argv[i], "--size") == 0 && i + 1 < argc) {
            if (sscanf(argv[++i], "%dx%d", &winW, &winH) == 2) {
                hasCustomSize = true;
            }
        } else if (argv[i][0] != '-') {
            videoFile = argv[i];
        }
    }

    if (!videoFile) {
        printf("Error: No video file specified.\n");
        printUsage(argv[0]);
        return 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDLPlayerCallback cb;
    cb.customSize_ = hasCustomSize;
    cb.win_ = SDL_CreateWindow("SmartPlayer SDK",
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               winW, winH, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!cb.win_) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    cb.ren_ = SDL_CreateRenderer(cb.win_, -1, SDL_RENDERER_ACCELERATED);
    if (!cb.ren_) {
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
    player.setHardwareDecode(hwDecode);
    player.open(videoFile);

    if (player.state() == SP_STATE_STOPPED && !player.hasVideo() && !player.hasAudio()) {
        printf("[Player] Open failed, no media streams found.\n");
        SDL_DestroyRenderer(cb.ren_);
        SDL_DestroyWindow(cb.win_);
        SDL_Quit();
        return 1;
    }

    // Set initial playback speed
    if (initSpeed != 1.0f) {
        player.setSpeed(initSpeed);
        printf("[Player] Speed: %.1fx\n", initSpeed);
    }

    int volume = 50;
    bool muted = false;
    float currentSpeed = initSpeed;
    // Speed cycle table
    const float speedTable[] = {1.0f, 1.5f, 2.0f, 0.5f};
    const int speedTableSize = 4;
    int speedIdx = 0;
    for (int i = 0; i < speedTableSize; i++) {
        if (speedTable[i] == currentSpeed) { speedIdx = i; break; }
    }

    player.play();

    // Disable SDL TextInput so the CJK IME does not intercept letter key presses
    SDL_StopTextInput();

    printf("[Player] Hardware decode: %s\n", hwDecode ? "ON" : "OFF");
    printf("[Player] Volume: %d, Speed: %.1fx\n", volume, currentSpeed);

    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_KEYDOWN) {
                SDL_Scancode sc = e.key.keysym.scancode;
                switch (sc) {
                case SDL_SCANCODE_SPACE:
                    if (player.state() == SP_STATE_RUNNING) {
                        player.pause();
                        printf("[Control] Pause\n");
                    } else if (player.state() == SP_STATE_PAUSED) {
                        player.play();
                        printf("[Control] Resume\n");
                    }
                    break;
                case SDL_SCANCODE_ESCAPE:
                    printf("[Control] Quit\n");
                    running = false;
                    break;
                case SDL_SCANCODE_LEFT: {
                    int64_t pos = player.position() - 10000;
                    if (pos < 0) pos = 0;
                    player.seek(pos);
                    printf("[Control] Seek -> %lld ms\n", (long long)pos);
                    break;
                }
                case SDL_SCANCODE_RIGHT: {
                    int64_t pos = player.position() + 10000;
                    if (pos > player.duration()) pos = player.duration();
                    player.seek(pos);
                    printf("[Control] Seek -> %lld ms\n", (long long)pos);
                    break;
                }
                case SDL_SCANCODE_UP:
                    volume = (volume + 5 > 100) ? 100 : volume + 5;
                    player.setVolume(volume);
                    printf("[Control] Volume: %d\n", volume);
                    break;
                case SDL_SCANCODE_DOWN:
                    volume = (volume - 5 < 0) ? 0 : volume - 5;
                    player.setVolume(volume);
                    printf("[Control] Volume: %d\n", volume);
                    break;
                case SDL_SCANCODE_M:
                    muted = !muted;
                    player.setMute(muted);
                    printf("[Control] Mute: %s\n", muted ? "ON" : "OFF");
                    break;
                case SDL_SCANCODE_S:
                    speedIdx = (speedIdx + 1) % speedTableSize;
                    currentSpeed = speedTable[speedIdx];
                    player.setSpeed(currentSpeed);
                    printf("[Control] Speed: %.1fx\n", currentSpeed);
                    break;
                default:
                    break;
                }
                fflush(stdout);
            }
        }

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
