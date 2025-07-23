#ifndef LOGGER_H
#define LOGGER_H

#include <string>

enum LogLevel { LL_DEBUG, LL_INFO, LL_NOTICE, LL_WARNING, LL_ERR, LL_CRIT, LL_ALERT, LL_EMERG };

std::string log_vsformat(LogLevel ll, const char *message, va_list args);
std::string log_sformat(LogLevel ll, const char *message, ...);
void log_vformat(LogLevel ll, const char *message, ...);
void log_format(LogLevel ll, const char *message, ...);
void log_debug(const char *message, ...);
void log_info(const char *message, ...);
void log_warn(const char *message, ...);
void log_err(const char *message, ...);

#endif
