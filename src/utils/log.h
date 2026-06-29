#pragma once

#include <string>
#include <fstream>
#include <mutex>
#include <cstdarg>

namespace sp {

enum class LogLevel : int {
    LOG_DEBUG,
    LOG_INFO,
    LOG_WARN,
    LOG_ERROR
};

class Log {
private:
    LogLevel m_curLevel;
    std::ofstream m_file;
    bool m_isFileOpen;
    std::mutex m_mutex;

    Log();
    ~Log();

    std::string getTime();
    static const char* level2Str(LogLevel level);
    static std::string getFileName(const char* filePath);

public:
    static Log& getInstance();
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    void setLevel(LogLevel level);
    void setFile(const std::string& path);

    void print(LogLevel level, const char* file, int line, const char* fmt, ...);
};

} // namespace sp

#define SP_LOG_DEBUG(fmt, ...)  sp::Log::getInstance().print(sp::LogLevel::LOG_DEBUG, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define SP_LOG_INFO(fmt, ...)   sp::Log::getInstance().print(sp::LogLevel::LOG_INFO,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define SP_LOG_WARN(fmt, ...)   sp::Log::getInstance().print(sp::LogLevel::LOG_WARN,  __FILE__, __LINE__, fmt, ##__VA_ARGS__)
#define SP_LOG_ERROR(fmt, ...)  sp::Log::getInstance().print(sp::LogLevel::LOG_ERROR, __FILE__, __LINE__, fmt, ##__VA_ARGS__)
