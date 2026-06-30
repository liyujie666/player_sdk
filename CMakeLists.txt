cmake_minimum_required(VERSION 3.16)
project(SmartPlayerSDK LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC: treat source files as UTF-8
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ========== 库类型 (默认 SHARED) ==========
option(SMARTPLAYER_STATIC "Build as static library" OFF)
if(NOT SMARTPLAYER_STATIC)
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
endif()

# ========== 用户可配置的第三方依赖路径 ==========
# 编译时通过 -DFFMPEG_DIR=... -DSDL2_DIR=... 指定
if(NOT DEFINED FFMPEG_DIR)
    set(FFMPEG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../dependencies")
endif()
if(NOT DEFINED SDL2_DIR)
    set(SDL2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../dependencies")
endif()

set(FFMPEG_INCLUDE_DIR "${FFMPEG_DIR}/include")
set(FFMPEG_LIB_DIR     "${FFMPEG_DIR}/lib")
set(SDL2_INCLUDE_DIR   "${SDL2_DIR}/include")
set(SDL2_LIB_DIR       "${SDL2_DIR}/lib")

# ========== SDK 源文件 ==========
set(SDK_SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src)

file(GLOB_RECURSE SDK_SOURCES
    ${SDK_SRC_DIR}/core/*.cpp
    ${SDK_SRC_DIR}/demuxer/*.cpp
    ${SDK_SRC_DIR}/decoder/*.cpp
    ${SDK_SRC_DIR}/converter/*.cpp
    ${SDK_SRC_DIR}/resampler/*.cpp
    ${SDK_SRC_DIR}/filter/*.cpp
    ${SDK_SRC_DIR}/queue/*.cpp
    ${SDK_SRC_DIR}/pool/*.cpp
    ${SDK_SRC_DIR}/render/*.cpp
    ${SDK_SRC_DIR}/utils/*.cpp
    ${SDK_SRC_DIR}/smartplayer.cpp
)

# ========== 创建库 ==========
if(SMARTPLAYER_STATIC)
    add_library(SmartPlayerSDK STATIC ${SDK_SOURCES})
    target_compile_definitions(SmartPlayerSDK PUBLIC SMARTPLAYER_STATIC)
else()
    add_library(SmartPlayerSDK SHARED ${SDK_SOURCES})
endif()

# ========== include 路径 ==========
target_include_directories(SmartPlayerSDK
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${SDK_SRC_DIR}
        ${FFMPEG_INCLUDE_DIR}
        ${SDL2_INCLUDE_DIR}
)

# ========== FFmpeg 库 ==========
set(FFMPEG_LIBS
    ${FFMPEG_LIB_DIR}/avformat.lib
    ${FFMPEG_LIB_DIR}/avcodec.lib
    ${FFMPEG_LIB_DIR}/avutil.lib
    ${FFMPEG_LIB_DIR}/swscale.lib
    ${FFMPEG_LIB_DIR}/swresample.lib
    ${FFMPEG_LIB_DIR}/avfilter.lib
)

# ========== SDL2 库 ==========
if(WIN32)
    set(SDL2_LIB ${SDL2_LIB_DIR}/SDL2.lib)
else()
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(SDL2 IMPORTED_TARGET sdl2)
        set(SDL2_LIB PkgConfig::SDL2)
    else()
        set(SDL2_LIB ${SDL2_LIB_DIR}/libSDL2.a)
    endif()
endif()

target_link_libraries(SmartPlayerSDK PRIVATE ${FFMPEG_LIBS} ${SDL2_LIB})



# ========== DLL 导出宏 ==========
if(NOT SMARTPLAYER_STATIC)
    target_compile_definitions(SmartPlayerSDK PRIVATE SMARTPLAYER_EXPORTS)
endif()

# ========== 示例程序 (可选) ==========
option(SMARTPLAYER_BUILD_EXAMPLE "Build minimal SDL2 player example" ON)
if(SMARTPLAYER_BUILD_EXAMPLE)
    add_executable(SmartPlayerExample examples/minimal_player.cpp)
    target_link_libraries(SmartPlayerExample PRIVATE SmartPlayerSDK ${SDL2_LIB})
    target_include_directories(SmartPlayerExample PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${SDL2_INCLUDE_DIR}
    )
endif()

# ========== 安装 ==========
install(TARGETS SmartPlayerSDK
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION include
)
