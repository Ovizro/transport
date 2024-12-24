#include <time.h>
#include <string.h>
#include <chrono>
#include <iomanip>
#include <ctime>
#include <memory>
#include <sstream>
#include "logging/interface.h"
#include "logging/logger.hpp"

using namespace logging;

std::unique_ptr<Logger>& logging::_get_global_logger()
{
    static std::unique_ptr<Logger> global_logger(new Logger(LogLevel::INFO));
    return global_logger;
}

Logger& logging::get_global_logger()
{
    return *_get_global_logger();
}

void logging::set_global_logger(std::unique_ptr<Logger>&& logger)
{
    auto& global_logger = _get_global_logger();
    global_logger->move_children_to(*logger);
    global_logger = std::move(logger);
}

Logger::Logger(const std::string& name, Level level, Logger* parent)
    : _parent(parent), _level(level)
{
    if (parent && !parent->_name.empty()) {
        _name = parent->_name + namesep + name;
    } else {
        _name = name;
    }
}

void Logger::vlog(Level level, const char* fmt, va_list args)
{
    if (level < this->level())
        return;
        
    va_list args2;
    va_copy(args2, args);
    int size = vsnprintf(nullptr, 0, fmt, args2);
    va_end(args2);
    
    char* buffer = new char[size + 1];
    vsnprintf(buffer, size + 1, fmt, args);
    try {
        log_message(level, buffer);
    } catch (...) {
        delete[] buffer;
        throw;
    }
    delete[] buffer;
}

void Logger::log_message(Level level, const std::string& msg)
{
    if (level < this->level())
        return;
        
    auto record = Record(name(), level, msg);
    log_record(record);
}

void Logger::log_record(const Record& record)
{
    if (record.level < level()) {
        return;
    }
    if (!_streams.empty()) {
        for (auto& stream : _streams) {
            write_record(*stream, record);
        }
    } else if (_parent) {
        _parent->log_record(record);
    } else {
        write_record(default_stream, record);
    }
}

void Logger::write_record(std::ostream& os, const Record& record)
{
    auto now = record.time;

    // 转换为时间_t 类型
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);

    // 获取毫秒部分
    auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    // 格式化时间
    std::stringstream ss;
    std::tm *tm_now = std::localtime(&now_c);
    ss << std::put_time(tm_now, "%Y-%m-%d %H:%M:%S")
              << ',' << std::setfill('0') << std::setw(3) << milliseconds.count();
    if (record.name.empty()) {
        os << ss.str() << " [" << record.level << "] " << record.msg << std::endl;
    } else {
        os << ss.str() << " [" << record.name << "] [" << record.level << "] " << record.msg << std::endl;
    }
}

LoggerOStream Logger::operator[](Logger::Level level)
{
    return LoggerOStream(*this, level);
}

LoggerOStream Logger::operator[](int level)
{
    return (*this)[static_cast<Logger::Level>(level)];
}

void Logger::move_children_to(Logger& target_logger)
{
    if (logger_cache.empty()) return;
    for (auto it = logger_cache.begin(); it != logger_cache.end(); ) {
        // Transfer ownership of the child logger to the target_logger
        it->second->_parent = &target_logger;
        target_logger.logger_cache.insert(std::move(*it));
        it = logger_cache.erase(it); // Remove from current logger's cache
    }
}

LoggerOStream::LoggerOStream(Logger& logger, Logger::Level level)
    : std::ostream(&_streamBuf), _streamBuf(logger, level) {}

LoggerOStream::LoggerOStream(LoggerOStream&& loggerStream)
    : std::ostream(std::move(loggerStream)), _streamBuf(std::move(loggerStream._streamBuf))
{}

LoggerStreamBuf::LoggerStreamBuf(Logger& logger, Logger::Level level) 
    : _logger(logger), _level(level) {}

int LoggerStreamBuf::overflow(int c) {
    if (c != EOF) {
        // 处理换行符
        if (c == '\n') {
            flush_line(); // 刷新当前行
        } else {
            _lineBuffer += static_cast<char>(c); // 将字符添加到行缓存
        }
    }
    return c;
}

std::streamsize LoggerStreamBuf::xsputn(const char* s, std::streamsize n) {
    std::streamsize lineStart = 0; // 记录行的起始位置

    for (std::streamsize i = 0; i < n; ++i) {
        if (s[i] == '\n') {
            // 将当前行缓存中的内容添加到行缓存
            _lineBuffer.append(s + lineStart, i - lineStart + 1);
            flush_line(); // 刷新当前行
            lineStart = i + 1; // 更新行的起始位置
        }
    }

    // 处理剩余的字符
    if (lineStart < n) {
        _lineBuffer.append(s + lineStart, n - lineStart);
    }
    return n; // 返回写入的字符总数
}

int LoggerStreamBuf::sync() {
    return 0; // 不需要同步
}

void LoggerStreamBuf::flush_line() {
    if (!_lineBuffer.empty()) {
        // 调用 Logger 的 log 方法
        _logger.log_message(_level, _lineBuffer);
        _lineBuffer.clear(); // 清空行缓存
    }
}

Record::Record(const std::string& name, Logger::Level level, const std::string& msg)
    : name(name), time(std::chrono::system_clock::now()), level(level), msg(msg)
{}

LogLevel logging::str2level(const char* level)
{
    if (!level) return Logger::Level::INFO;
    else if (strcasecmp(level, "debug") == 0) return Logger::Level::DEBUG;
    else if (strcasecmp(level, "info") == 0) return Logger::Level::INFO;
    else if (strcasecmp(level, "warn") == 0) return Logger::Level::WARN;
    else if (strcasecmp(level, "error") == 0) return Logger::Level::ERROR;
    else if (strcasecmp(level, "fatal") == 0) return Logger::Level::FATAL;
    log_error("Unknown log level: %s", level);
    return Logger::Level::UNKNOWN;
}

std::ostream& logging::operator<<(std::ostream& stream, const LogLevel level)
{
    switch (level) {
    case Logger::Level::DEBUG:
        return stream << "DEBUG";
    case Logger::Level::INFO:
        return stream << "INFO";
    case Logger::Level::WARN:
        return stream << "WARN";
    case Logger::Level::ERROR:
        return stream << "ERROR";
    case Logger::Level::FATAL:
        return stream << "FATAL";
    default:
        return stream << "UNKNOWN";
    }
}

void log_init(const char* level)
{
    if (level == NULL || *level == '\0') {
        level = "info";
    } else if (strcasecmp(level, "env") == 0 || strcasecmp(level, "auto") == 0) {
        level = getenv("LOG_LEVEL");
    }
    set_global_logger(std::unique_ptr<Logger>(new Logger(str2level(level))));
}

int log_level()
{
    return static_cast<int>(_get_global_logger()->level());
}

void log_set_level(int level)
{
    _get_global_logger()->set_level(static_cast<Logger::Level>(level));
}

void log_log(int level, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_vlog(level, fmt, args);
    va_end(args);
}

void log_vlog(int level, const char* fmt, va_list args)
{
    _get_global_logger()->vlog(static_cast<Logger::Level>(level), fmt, args);
}
