#ifndef _INCLUDE_LOGGER_
#define _INCLUDE_LOGGER_

#include <stdint.h>
#include <stdarg.h>
#include <errno.h>
#include <string.h>
#include <vector>
#include <memory>
#include <iostream>
#include <chrono>
#include <unordered_map>
#include "level.h"

namespace logging {

struct Record;
class LoggerOStream;
class LoggerStreamBuf;
    
class Logger
{
public:
    enum class Level: uint8_t
    {
        UNKNOWN = 0,
        DEBUG = LOG_LEVEL_DEBUG,
        INFO = LOG_LEVEL_INFO,
        WARN = LOG_LEVEL_WARN,
        ERROR = LOG_LEVEL_ERROR,
        FATAL = LOG_LEVEL_FATAL
    };

    constexpr static const char* namesep = "::";

    explicit Logger(Level level = Level::INFO) : _parent(nullptr), _level(level) {}
    Logger(const Logger&) = delete;
    virtual ~Logger() = default;
    
    inline Level level() const
    {
        if (_level == Logger::Level::UNKNOWN) {
            if (_parent) {
                return _parent->level();
            }
            return Logger::Level::INFO;
        }
        return _level;
    }

    inline const std::string& name() const
    {
        return _name;
    }

    inline Logger* parent() const
    {
        return _parent;
    }

    inline size_t children_count() const
    {
        return logger_cache.size();
    }

    inline void set_level(Level level)
    {
        _level = level;
    }
    
    template<Level level>
    inline void log(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vlog(level, fmt, args);
        va_end(args);
    }
    inline void log(Level level, const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        vlog(level, fmt, args);
        va_end(args);
    }
    inline void debug(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        vlog(Logger::Level::DEBUG, fmt, args);
        va_end(args);
    }
    void info(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        vlog(Logger::Level::INFO, fmt, args);
        va_end(args);
    }
    void warn(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        vlog(Logger::Level::WARN, fmt, args);
        va_end(args);
    }
    void error(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        vlog(Logger::Level::ERROR, fmt, args);
        va_end(args);
    }
    void fatal(const char* fmt, ...)
    {
        va_list args;
        va_start(args, fmt);
        vlog(Logger::Level::FATAL, fmt, args);
        va_end(args);
    }
    template<Level level>
    inline void vlog(const char* fmt, va_list args) {
        vlog(level, fmt, args);
    }
    template<typename E = std::runtime_error>
    inline void raise_from_errno(const char* msg) {
        error("%s: %s", msg, strerror(errno));
        throw E(msg);
    }

    void vlog(Level level, const char* fmt, va_list args);
    virtual void log_message(Level level, const std::string& msg);

    LoggerOStream operator[](Level level);
    LoggerOStream operator[](int level);

    void move_children_to(Logger& other);
        
    template<class T_Logger = Logger>
    Logger* get_child(const std::string& name, Level level)
    {
        Logger* logger;
        auto sep_pos = name.find(namesep);
        do {
            if (name.empty() || sep_pos == 0) {
                logger = this;
                break;
            }
            std::string base_name;
            if (sep_pos == std::string::npos) {
                base_name = name;
            } else {
                base_name = name.substr(0, sep_pos);
            }
            auto iter = logger_cache.find(base_name);
            
            if (iter != logger_cache.end()) {
                logger = iter->second.get();
                break;
            }
            logger = new T_Logger(base_name, level, this);
            logger_cache[base_name] = std::unique_ptr<Logger>(logger);
        } while (0);
        auto seqlen = strlen(namesep);
        if (sep_pos == std::string::npos || sep_pos + seqlen >= name.size())
            return logger;
        return logger->get_child(name.substr(sep_pos + seqlen), level);
    }

    inline void add_stream(std::ostream& stream)
    {
        _streams.push_back(std::unique_ptr<std::ostream>(new std::ostream(stream.rdbuf())));
    }

    template <typename T>
    inline void add_stream(T&& stream)
    {
        _streams.push_back(std::unique_ptr<std::ostream>(new typename std::remove_reference<T>::type(std::move(stream))));
    }

    inline std::vector<std::ostream*> streams() const
    {
        std::vector<std::ostream*> streams;
        for (auto& stream : _streams)
            streams.push_back(stream.get());
        return streams;
    }

protected:
    explicit Logger(const std::string& name, Level level = Level::INFO, Logger* parent = nullptr);
    
    virtual void log_record(const Record& record);
    virtual void write_record(std::ostream& os, const Record& record);
    
    static constexpr std::ostream& default_stream = std::cerr;
    
    std::vector<std::unique_ptr<std::ostream>> _streams;
    Logger* _parent;

private:
    std::unordered_map<std::string, std::unique_ptr<Logger>> logger_cache;
    std::string _name;
    Level _level;
};

class LoggerStreamBuf : public std::streambuf {
public:
    LoggerStreamBuf(Logger& logger, Logger::Level level);
    LoggerStreamBuf(const LoggerStreamBuf&) = delete;
    LoggerStreamBuf(LoggerStreamBuf&& loggerStream) = default;

protected:
    int overflow(int c) override;
    std::streamsize xsputn(const char* s, std::streamsize n) override;
    int sync() override;

private:
    void flush_line(); // 将当前行写入日志

    Logger& _logger;
    Logger::Level _level;
    std::string _lineBuffer; // 行缓存
};

class LoggerOStream : public std::ostream {
public:
    LoggerOStream(Logger& logger, Logger::Level level);
    LoggerOStream(const LoggerOStream&) = delete;
    LoggerOStream(LoggerOStream&& loggerStream);
    
private:
    LoggerStreamBuf _streamBuf;
};

struct Record
{
    std::string name;
    std::chrono::system_clock::time_point time;
    Logger::Level level;
    std::string msg;

    Record(const std::string& name, Logger::Level level, const std::string& msg);
};

using LogLevel = Logger::Level;

std::ostream& operator<<(std::ostream& os, const Logger::Level level);
Logger::Level str2level(const char* level);
std::unique_ptr<Logger>& _get_global_logger();
Logger& get_global_logger();
void set_global_logger(std::unique_ptr<Logger>&& logger);

template<class T_Logger = Logger>
static inline Logger* get_logger(const std::string& name, LogLevel level = LogLevel::UNKNOWN)
{
    return _get_global_logger()->get_child<T_Logger>(name, level);
}

}

#endif