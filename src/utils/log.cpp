#include "log.h"
#include <chrono>
#include <ctime>
#include <cstdio>
#include <algorithm>

namespace sp {

Log::Log() : m_curLevel(LogLevel::LOG_DEBUG), m_isFileOpen(false) {}

Log::~Log() {
    if (m_file.is_open()) {
        m_file.close();
    }
}

Log& Log::getInstance() {
    static Log instance;
    return instance;
}

void Log::setLevel(LogLevel level) {
    m_curLevel = level;
}

void Log::setFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_file.is_open()) {
        m_file.close();
    }
    m_file.open(path, std::ios::out | std::ios::app);
    m_isFileOpen = m_file.is_open();
}

std::string Log::getTime() {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                  now.time_since_epoch()) % 1000;

    char buf[32];
    std::tm tmv;
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);

    char result[40];
    std::snprintf(result, sizeof(result), "%s.%03d", buf, (int)ms.count());
    return std::string(result);
}

const char* Log::level2Str(LogLevel level) {
    switch (level) {
    case LogLevel::LOG_DEBUG: return "DEBUG";
    case LogLevel::LOG_INFO:  return "INFO";
    case LogLevel::LOG_WARN:  return "WARN";
    case LogLevel::LOG_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

std::string Log::getFileName(const char* filePath) {
    if (!filePath) return "unknown";
    std::string path(filePath);
    size_t pos1 = path.find_last_of('/');
    size_t pos2 = path.find_last_of('\\');
    size_t pos = std::string::npos;
    if (pos1 != std::string::npos) pos = pos1;
    if (pos2 != std::string::npos && pos2 > pos) pos = pos2;
    return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

void Log::print(LogLevel level, const char* file, int line, const char* fmt, ...) {
    if (static_cast<int>(level) < static_cast<int>(m_curLevel)) {
        return;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    char msg[1024] = {0};
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    const char* color = "";
    switch (level) {
    case LogLevel::LOG_DEBUG: color = "\033[37m"; break;
    case LogLevel::LOG_INFO:  color = "\033[32m"; break;
    case LogLevel::LOG_WARN:  color = "\033[33m"; break;
    case LogLevel::LOG_ERROR: color = "\033[31m"; break;
    default: break;
    }

    std::string timeStr = getTime();
    std::string fileName = getFileName(file);

    fprintf(stderr, "%s[%s] [%s] [%s:%d] %s\033[0m\n",
            color, timeStr.c_str(), level2Str(level),
            fileName.c_str(), line, msg);

    if (m_isFileOpen) {
        m_file << "[" << timeStr << "] [" << level2Str(level)
               << "] [" << fileName << ":" << line << "] " << msg << std::endl;
        m_file.flush();
    }
}

} // namespace sp
