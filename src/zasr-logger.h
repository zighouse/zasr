#ifndef ZASR_ZASR_LOGGER_H_
#define ZASR_ZASR_LOGGER_H_

#include <iostream>
#include <sstream>
#include <chrono>
#include <iomanip>
#include <thread>
#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>

namespace zasr {

enum class LogLevel {
  ERROR = 0,
  WARN = 1,
  INFO = 2,
  DEBUG = 3
};

// 日志级别管理器
class LogLevelManager {
 public:
  static LogLevelManager& Instance() {
    static LogLevelManager instance;
    return instance;
  }

  LogLevel GetLevel() const { return level_; }

  void SetLevel(LogLevel level) { level_ = level; }

  void SetLevelFromEnv() {
    const char* env_level = std::getenv("ZASR_SERVER_LOG_LEVEL");
    if (env_level != nullptr) {
      std::string level_str(env_level);
      // 转换为小写
      for (auto& c : level_str) {
        c = std::tolower(c);
      }

      if (level_str == "error") {
        level_ = LogLevel::ERROR;
      } else if (level_str == "warn" || level_str == "warning") {
        level_ = LogLevel::WARN;
      } else if (level_str == "info") {
        level_ = LogLevel::INFO;
      } else if (level_str == "debug") {
        level_ = LogLevel::DEBUG;
      }
    }
  }

  bool ShouldLog(LogLevel msg_level) const {
    return static_cast<int>(msg_level) <= static_cast<int>(level_);
  }

 private:
  LogLevelManager() : level_(LogLevel::INFO) {
    SetLevelFromEnv();
  }

  LogLevel level_;
};

class Logger {
 public:
  Logger(LogLevel level, const char* file, int line, std::ostream& out = std::cout)
      : level_(level), file_(file), line_(line), out_(out),
        enabled_(LogLevelManager::Instance().ShouldLog(level)) {
  }

  ~Logger() {
    if (enabled_) {
      out_ << GetTimestamp() << " "
           << getpid() << ":" << std::this_thread::get_id() << " "
           << "[" << GetLevelString() << "] "
           << GetFileName() << ":" << line_ << " "
           << stream_.str() << std::endl;
    }
  }

  template<typename T>
  Logger& operator<<(const T& val) {
    if (enabled_) {
      stream_ << val;
    }
    return *this;
  }

  // 设置全局日志级别
  static void SetGlobalLevel(LogLevel level) {
    LogLevelManager::Instance().SetLevel(level);
  }

  // 获取当前全局日志级别
  static LogLevel GetGlobalLevel() {
    return LogLevelManager::Instance().GetLevel();
  }

 private:
  std::string GetTimestamp() const {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()) % 1000000;

    std::tm tm_now;
    localtime_r(&time_t_now, &tm_now);

    std::ostringstream ss;
    ss << std::setfill('0')
       << std::setw(4) << (tm_now.tm_year + 1900) << "-"
       << std::setw(2) << (tm_now.tm_mon + 1) << "-"
       << std::setw(2) << tm_now.tm_mday << " "
       << std::setw(2) << tm_now.tm_hour << ":"
       << std::setw(2) << tm_now.tm_min << ":"
       << std::setw(2) << tm_now.tm_sec << "."
       << std::setw(4) << (ms.count() / 100);
    return ss.str();
  }

  const char* GetLevelString() const {
    switch (level_) {
      case LogLevel::ERROR: return "ERROR";
      case LogLevel::WARN:  return "WARN";
      case LogLevel::INFO:  return "INFO";
      case LogLevel::DEBUG: return "DEBUG";
      default: return "UNKNOWN";
    }
  }

  const char* GetFileName() const {
    // 提取文件基本名（去掉路径）
    const char* slash = file_;
    const char* last_slash = file_;
    while (*slash) {
      if (*slash == '/') last_slash = slash + 1;
      slash++;
    }
    return last_slash;
  }

  LogLevel level_;
  const char* file_;
  int line_;
  std::ostream& out_;
  std::ostringstream stream_;
  bool enabled_;
};

// 日志宏 - 输出到 stdout
#define LOG_DEBUG() Logger(LogLevel::DEBUG, __FILE__, __LINE__, std::cout)
#define LOG_INFO()  Logger(LogLevel::INFO,  __FILE__, __LINE__, std::cout)
#define LOG_WARN()  Logger(LogLevel::WARN,  __FILE__, __LINE__, std::cout)
#define LOG_ERROR() Logger(LogLevel::ERROR, __FILE__, __LINE__, std::cerr)

// 文件日志宏 - 输出到指定文件流
#define LOG_FILE_DEBUG(out) Logger(LogLevel::DEBUG, __FILE__, __LINE__, out)
#define LOG_FILE_INFO(out)  Logger(LogLevel::INFO,  __FILE__, __LINE__, out)
#define LOG_FILE_WARN(out)  Logger(LogLevel::WARN,  __FILE__, __LINE__, out)
#define LOG_FILE_ERROR(out) Logger(LogLevel::ERROR, __FILE__, __LINE__, out)

}  // namespace zasr

#endif  // ZASR_ZASR_LOGGER_H_
