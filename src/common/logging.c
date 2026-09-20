#include "common.h"
#include <stdarg.h>
#include <pthread.h>

static pthread_mutex_t g_log_lock = PTHREAD_MUTEX_INITIALIZER;

static const char *level_to_str(log_level_t level) {
    switch (level) {
        case LOG_DEBUG: return "DEBUG";
        case LOG_INFO:  return "INFO ";
        case LOG_WARN:  return "WARN ";
        case LOG_ERROR: return "ERROR";
        case LOG_FATAL: return "FATAL";
        default:        return "UNK  ";
    }
}

static const char *level_to_color(log_level_t level) {
    switch (level) {
        case LOG_DEBUG: return "\033[90m";     /* Gray */
        case LOG_INFO:  return "\033[32m";     /* Green */
        case LOG_WARN:  return "\033[33m";     /* Yellow */
        case LOG_ERROR: return "\033[31m";     /* Red */
        case LOG_FATAL: return "\033[35;1m";   /* Bold Magenta */
        default:        return "\033[0m";
    }
}

void log_msg(log_level_t level, const char *component, const char *fmt, ...) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    
    struct tm tm_buf;
    localtime_r(&ts.tv_sec, &tm_buf);
    
    char time_str[32];
    snprintf(time_str, sizeof(time_str), "%02d:%02d:%02d.%03ld",
             tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec, ts.tv_nsec / 1000000);
    
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    pthread_mutex_lock(&g_log_lock);
    fprintf(stderr, "%s[%s] [%s] [%-12s]\033[0m %s\n",
            level_to_color(level),
            time_str,
            level_to_str(level),
            component ? component : "CORE",
            buffer);
    fflush(stderr);
    pthread_mutex_unlock(&g_log_lock);
}
