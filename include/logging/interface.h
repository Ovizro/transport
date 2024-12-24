#ifndef _INCLUDE_LOGGING_INTERFACE_
#define _INCLUDE_LOGGING_INTERFACE_

#include <assert.h>
#include <stdarg.h>
#include "level.h"

#ifdef __cplusplus
extern "C" {
#endif

void log_init(const char* level);
int log_level(void);
void log_set_level(int level);
void log_log(int level, const char *fmt, ...);
void log_vlog(int level, const char *fmt, va_list ap);

#define log_debug(...) log_log(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define log_info(...) log_log(LOG_LEVEL_INFO, __VA_ARGS__)
#define log_warn(...) log_log(LOG_LEVEL_WARN, __VA_ARGS__)
#define log_error(...) log_log(LOG_LEVEL_ERROR, __VA_ARGS__)
#define log_fatal(...) log_log(LOG_LEVEL_FATAL, __VA_ARGS__)

#define vlog_debug(...) log_vlog(LOG_LEVEL_DEBUG, __VA_ARGS__)
#define vlog_info(...) log_vlog(LOG_LEVEL_INFO, __VA_ARGS__)
#define vlog_warn(...) log_vlog(LOG_LEVEL_WARN, __VA_ARGS__)
#define vlog_error(...) log_vlog(LOG_LEVEL_ERROR, __VA_ARGS__)
#define vlog_fatal(...) log_vlog(LOG_LEVEL_FATAL, __VA_ARGS__)

#define log_with_source(level, msg) log_log(level,\
    "Trackback (most recent call last):\n  File \"%s\", line %d, in \"%s\"\n%s",\
    __FILE__, __LINE__, __ASSERT_FUNCTION, msg)
#define log_debug_with_source(msg) log_with_source(LOG_LEVEL_DEBUG, msg)
#define log_info_with_source(msg) log_with_source(LOG_LEVEL_INFO, msg)
#define log_warn_with_source(msg) log_with_source(LOG_LEVEL_WARN, msg)
#define log_error_with_source(msg) log_with_source(LOG_LEVEL_ERROR, msg)
#define log_fatal_with_source(msg) log_with_source(LOG_LEVEL_FATAL, msg)

#define log_from_errno(level, msg) do {\
    if (errno) log_log(level,"%s: %s", msg, strerror(errno));\
} while (0)
#define log_debug_from_errno(msg) log_from_errno(LOG_LEVEL_DEBUG, msg)
#define log_info_from_errno(msg) log_from_errno(LOG_LEVEL_INFO, msg)
#define log_warn_from_errno(msg) log_from_errno(LOG_LEVEL_WARN, msg)
#define log_error_from_errno(msg) log_from_errno(LOG_LEVEL_ERROR, msg)
#define log_fatal_from_errno(msg) log_from_errno(LOG_LEVEL_FATAL, msg)

#ifdef __cplusplus
}
#endif

#endif