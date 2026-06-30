# SmartPlayer SDK

仅依赖 **FFmpeg + SDL2 + C++17 标准库**的跨平台视频播放器 SDK。

通过回调推送解码后的视频帧数据，不绑定任何 UI 框架，可轻松集成到 Qt / SDL / OpenGL / DirectX 等渲染方案中。

## 特性

- 支持本地文件 & 网络流（RTSP / RTMP / HTTP / HLS）
- 软件解码 + 硬件解码自动回退（CUDA / QSV / D3D11VA / VAAPI）
- 倍速播放（0.5x / 1.0x / 1.5x / 2.0x）
- 音视频同步（音频为主时钟 / 系统时钟）
- 视频帧回调（YUV420P / NV12 / RGBA / BGRA）
- 截图（JPEG）
- 音量控制 & 静音
- 线程安全的对象池 & 缓冲队列
- PIMPL 封装，ABI 稳定

## 目录结构

```
smartplayer_sdk/
├── include/                       # 对外公开头文件
│   ├── smartplayer.h              # 主控制器 Facade
│   ├── smartplayerdefs.h          # 枚举/结构体定义
│   └── smartplayercallback.h      # 事件回调接口
├── src/
│   ├── smartplayer.cpp            # Facade 实现 (PIMPL)
│   ├── core/                      # 播放引擎核心 + 音视频同步时钟
│   ├── demuxer/                   # FFmpeg 解复用
│   ├── decoder/                   # FFmpeg 解码 (软解/硬解自动回退)
│   ├── converter/                 # 像素格式转换 (swscale)
│   ├── resampler/                 # 音频重采样 (swresample)
│   ├── filter/                    # 音频倍速滤镜 (avfilter atempo)
│   ├── queue/                     # 线程安全 Packet/Frame 缓冲队列
│   ├── pool/                      # AVPacket/AVFrame 对象池
│   ├── render/                    # SDL2 音频输出
│   └── utils/                     # 日志系统 / 音频环形缓冲
├── examples/
│   └── minimal_player.cpp         # SDL2 最小示例播放器
├── CMakeLists.txt
├── build.bat                      # Windows 一键编译脚本
├── run.bat                        # Windows 一键运行脚本
└── README.md
```

## 依赖

| 依赖 | 用途 |
|------|------|
| FFmpeg (avformat/avcodec/avutil/swscale/swresample/avfilter) | 解复用、解码、格式转换、重采样、音频滤镜 |
| SDL2 | 音频输出 |
| C++17 标准库 | 线程、互斥量、文件系统等 |

**不依赖**: Qt、Whisper、任何 AI 库、任何 UI 框架。

## 编译

### Windows 快速编译（推荐）

```bash
# 一键编译（自动检测 VS 2015~2026，优先使用 Ninja）
build.bat

# 运行示例
run.bat path/to/video.mp4

# 启用硬件解码 + 1.5 倍速 + 指定窗口大小
run.bat path/to/video.mp4 --hw --speed 1.5 --size 1280x720
```

### CMake 手动编译

```bash
# 使用项目自带的 dependencies 目录
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release

# 指定自定义 FFmpeg/SDL2 路径
cmake -B build -S . -DFFMPEG_DIR=/path/to/ffmpeg -DSDL2_DIR=/path/to/sdl2

# 编译为静态库
cmake -B build -S . -DSMARTPLAYER_STATIC=ON
cmake --build build
```

### CMake 选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `SMARTPLAYER_STATIC` | `OFF` | 编译为静态库 |
| `SMARTPLAYER_BUILD_EXAMPLE` | `ON` | 编译示例程序 |
| `FFMPEG_DIR` | `../dependencies` | FFmpeg 头文件和库路径 |
| `SDL2_DIR` | `../dependencies` | SDL2 头文件和库路径 |

## 快速上手

### 1. 包含头文件

```cpp
#include "smartplayer.h"
#include "smartplayercallback.h"
```

### 2. 实现回调

```cpp
class MyCallback : public SmartPlayerCallback {
public:
    void onOpenResult(bool ok, const std::string& err) override {
        printf("Open: %s\n", ok ? "success" : err.c_str());
    }

    void onStateChanged(SmartPlayerState state) override {
        const char* names[] = {"Stopped", "Running", "Paused"};
        printf("State: %s\n", names[state]);
    }

    void onVideoFrame(const uint8_t* data, int w, int h,
                      SmartPixelFormat fmt) override {
        // 在此用 SDL / OpenGL / D3D 等方式渲染视频帧
        // data 布局见 smartplayercallback.h 注释
    }

    void onPlayFinished() override {
        printf("Play finished\n");
    }
};
```

### 3. 使用播放器

```cpp
SmartPlayer player;
MyCallback cb;
player.setCallback(&cb);
player.setHardwareDecode(true);
player.open("D:/video.mp4");
player.play();

// ... 主循环 / 事件循环 ...

player.stop();
```

### 4. 控制播放

```cpp
player.seek(30000);          // 跳转到 30 秒
player.setSpeed(1.5);        // 1.5 倍速
player.setVolume(80);        // 音量 80%
player.setMute(false);       // 取消静音
player.takeScreenshot("./screenshots");  // 截图
```

## API 概览

| 方法 | 说明 |
|------|------|
| `open(url)` | 打开媒体（文件/RTSP/RTMP/HTTP/HLS） |
| `play()` / `pause()` / `stop()` | 播放控制 |
| `seek(posMs)` | 跳转（毫秒） |
| `setSpeed(0.5~2.0)` | 倍速 |
| `setVolume(0~100)` | 音量 |
| `setMute(bool)` | 静音 |
| `setHardwareDecode(bool)` | 硬件解码 |
| `setDecoderType(name)` | 指定解码器 |
| `takeScreenshot(path)` | 截图（JPEG） |
| `duration()` / `position()` | 时长/进度 |
| `state()` / `hasAudio()` / `hasVideo()` | 状态查询 |
| `setCallback(cb)` | 注册回调 |

## 回调事件

| 回调 | 说明 |
|------|------|
| `onStateChanged` | 播放状态变化 |
| `onPositionChanged` | 播放进度更新 |
| `onDurationChanged` | 总时长就绪 |
| `onOpenResult` | open 结果 |
| `onMediaInfoReady` | 媒体元数据就绪 |
| `onPlayFinished` | 播放结束 |
| `onError` | 错误 |
| `onVideoFrame` | 视频帧数据（自行渲染） |
| `onScreenshot` | 截图完成 |

## 帧数据格式

`onVideoFrame` 回调中 `data` 指针的内存布局：

| 格式 | 布局 | 总大小 |
|------|------|--------|
| `SP_FMT_YUV420P` | Y(w*h) + U(w*h/4) + V(w*h/4) | w*h*3/2 |
| `SP_FMT_NV12` | Y(w*h) + interleaved UV(w*h/2) | w*h*3/2 |
| `SP_FMT_RGBA` | 每像素 4 字节 RGBA | w*h*4 |
| `SP_FMT_BGRA` | 每像素 4 字节 BGRA | w*h*4 |

> `data` 指针仅在回调期间有效，如需保留请拷贝。

## 示例程序

`examples/minimal_player.cpp` 是一个完整的 SDL2 播放器示例。

### 命令行参数

```bash
run.bat <video_file> [options]
```

| 参数 | 说明 |
|------|------|
| `--hw` | 启用硬件解码（GPU 加速） |
| `--speed <val>` | 设置初始倍速（0.5 / 1.0 / 1.5 / 2.0） |
| `--size <WxH>` | 设置窗口大小（如 1920x1080，默认自适应视频分辨率） |
| `--help` | 显示帮助 |

示例：

```bash
run.bat video.mp4
run.bat video.mp4 --hw
run.bat video.mp4 --hw --speed 1.5
run.bat video.mp4 --size 1920x1080
```

### 快捷键

| 按键 | 功能 |
|------|------|
| Space | 暂停 / 恢复 |
| ← / → | 快退 / 快进 10 秒 |
| ↑ / ↓ | 音量 +/- 5 |
| M | 切换静音 |
| S | 循环切换倍速（1.0 → 1.5 → 2.0 → 0.5 → 1.0） |
| ESC | 退出 |

## 集成到其他项目

### 方式 1：作为 CMake 子项目

```cmake
add_subdirectory(smartplayer_sdk)
target_link_libraries(YourApp PRIVATE SmartPlayerSDK)
```

### 方式 2：使用已编译的库

将 `include/` 目录和编译产物（`.dll` / `.lib`）拷贝到你的项目中：

```cmake
target_include_directories(YourApp PRIVATE path/to/smartplayer_sdk/include)
target_link_libraries(YourApp PRIVATE path/to/SmartPlayerSDK.lib)
```

运行时需确保以下 DLL 在可执行文件同目录：

```
SmartPlayerSDK.dll
avcodec-63.dll    avformat-63.dll   avutil-61.dll
swscale-10.dll    swresample-7.dll  avfilter-12.dll
SDL2.dll
```

## 注意事项

- **回调线程**：`onVideoFrame` 在 SDK 内部渲染线程中调用，需自行做线程同步
- **帧数据生命周期**：`onVideoFrame` 中的 `data` 指针仅在回调期间有效，如需保留请拷贝
- **SDL 初始化**：如果调用方也使用 SDL，建议一次性 `SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)`，SDK 内部会自动检测避免重复初始化
- **open 语义**：本地文件同步返回；网络流（RTSP/RTMP/HTTP）异步执行，结果通过 `onOpenResult` 回调
- **生命周期**：`SmartPlayer` 析构时自动调用 `stop()`，回调对象必须在 player 生命周期内有效

## License

MIT
