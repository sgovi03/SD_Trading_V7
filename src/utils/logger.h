#ifndef SDTRADING_LOGGER_H
#define SDTRADING_LOGGER_H

#include <string>
#include <fstream>
#include <sstream>
#include <iostream>
#include <mutex>
#include <memory>
#include <ctime>
#include <iomanip>

namespace SDTrading {
namespace Utils {

// ============================================================
// LOG LEVELS
// ============================================================


// Undefine Windows macros that conflict with our enum
#ifdef ERROR
#undef ERROR
#endif
enum class LogLevel {
    DEBUG = 0,
    INFO = 1,
    WARN = 2,
    ERROR = 3,
    NONE = 4
};

// ============================================================
// LOGGER CLASS (Singleton)
// ============================================================

class Logger {
public:
    // Get singleton instance
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }
    
    // Delete copy constructor and assignment operator
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    // ========================================
    // CONFIGURATION METHODS
    // ========================================
    
    void setLevel(LogLevel level) {
        std::lock_guard<std::mutex> lock(mutex_);
        current_level_ = level;
    }
    
    LogLevel getLevel() const {
        return current_level_;
    }
    
    void enableConsole(bool enable) {
        std::lock_guard<std::mutex> lock(mutex_);
        console_enabled_ = enable;
    }
    
    void enableFile(bool enable, const std::string& filename = "sd_trading.log") {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (enable && !file_enabled_) {
            log_file_.open(filename, std::ios::app);
            if (!log_file_.is_open()) {
                std::cerr << "⚠️  WARNING: Failed to open log file: " << filename << std::endl;
                file_enabled_ = false;
                return;
            }
            file_enabled_ = true;
            filename_ = filename;
        } else if (!enable && file_enabled_) {
            if (log_file_.is_open()) {
                log_file_.close();
            }
            file_enabled_ = false;
        }
    }
    
    bool isConsoleEnabled() const {
        return console_enabled_;
    }
    
    bool isFileEnabled() const {
        return file_enabled_;
    }
    
    std::string getFilename() const {
        return filename_;
    }
    
    // ========================================
    // LOGGING METHODS
    // ========================================
    
    void debug(const std::string& message) {
        log(LogLevel::DEBUG, message);
    }
    
    void info(const std::string& message) {
        log(LogLevel::INFO, message);
    }
    
    void warn(const std::string& message) {
        log(LogLevel::WARN, message);
    }
    
    void error(const std::string& message) {
        log(LogLevel::ERROR, message);
    }
    
    // Stream-style logging support
    template<typename T>
    std::ostringstream& stream(LogLevel level) {
        current_log_level_ = level;
        log_stream_.str("");  // Clear stream
        log_stream_.clear();  // Clear state flags
        return log_stream_;
    }
    
    void flush() {
        if (current_log_level_ >= current_level_) {
            log(current_log_level_, log_stream_.str());
        }
    }
    
private:
    Logger() 
        : current_level_(LogLevel::INFO),
          console_enabled_(true),
          file_enabled_(false),
          filename_("") {}
    
    ~Logger() {
        if (log_file_.is_open()) {
            log_file_.close();
        }
    }
    
    void log(LogLevel level, const std::string& message) {
        if (level < current_level_) {
            return;  // Message level below threshold
        }
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        std::string formatted_message = format_message(level, message);
        
        if (console_enabled_) {
            std::cout << formatted_message << std::endl;
        }
        
        if (file_enabled_ && log_file_.is_open()) {
            log_file_ << formatted_message << std::endl;
            log_file_.flush();  // Ensure immediate write
        }
    }
    
    std::string format_message(LogLevel level, const std::string& message) const {
        std::ostringstream oss;
        
        // Timestamp
        auto now = std::time(nullptr);
        std::tm tm;
        
        #ifdef _WIN32
            localtime_s(&tm, &now);  // Windows secure version
        #else
            tm = *std::localtime(&now);  // Unix version
        #endif
        
        oss << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S") << "] ";
        
        // Level
        oss << "[" << level_to_string(level) << "] ";
        
        // Message
        oss << message;
        
        return oss.str();
    }
    
    std::string level_to_string(LogLevel level) const {
        switch (level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO:  return "INFO ";
            case LogLevel::WARN:  return "WARN ";
            case LogLevel::ERROR: return "ERROR";
            case LogLevel::NONE:  return "NONE ";
            default: return "UNKNOWN";
        }
    }
    
    // Member variables
    LogLevel current_level_;
    bool console_enabled_;
    bool file_enabled_;
    std::string filename_;
    std::ofstream log_file_;
    std::mutex mutex_;
    std::ostringstream log_stream_;
    LogLevel current_log_level_;
};

// ============================================================
// CONVENIENCE MACROS
// ============================================================

#define LOG_DEBUG(msg) \
    do { \
        if (SDTrading::Utils::Logger::getInstance().getLevel() <= SDTrading::Utils::LogLevel::DEBUG) { \
            std::ostringstream _oss; \
            _oss << msg; \
            SDTrading::Utils::Logger::getInstance().debug(_oss.str()); \
        } \
    } while(0)

#define LOG_INFO(msg) \
    do { \
        if (SDTrading::Utils::Logger::getInstance().getLevel() <= SDTrading::Utils::LogLevel::INFO) { \
            std::ostringstream _oss; \
            _oss << msg; \
            SDTrading::Utils::Logger::getInstance().info(_oss.str()); \
        } \
    } while(0)

#define LOG_WARN(msg) \
    do { \
        if (SDTrading::Utils::Logger::getInstance().getLevel() <= SDTrading::Utils::LogLevel::WARN) { \
            std::ostringstream _oss; \
            _oss << msg; \
            SDTrading::Utils::Logger::getInstance().warn(_oss.str()); \
        } \
    } while(0)

#define LOG_ERROR(msg) \
    do { \
        if (SDTrading::Utils::Logger::getInstance().getLevel() <= SDTrading::Utils::LogLevel::ERROR) { \
            std::ostringstream _oss; \
            _oss << msg; \
            SDTrading::Utils::Logger::getInstance().error(_oss.str()); \
        } \
    } while(0)

} // namespace Utils
} // namespace SDTrading

#endif // SDTRADING_LOGGER_H
