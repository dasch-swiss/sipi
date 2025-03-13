#ifndef LOGGER_H
#define LOGGER_H

enum LogLevel { LL_DEBUG, LL_INFO, LL_WARN, LL_ERROR };

void log_format(LogLevel logl, const char *message, ...);
void log_debug(const char *message, ...);
void log_info(const char *message, ...);
void log_warn(const char *message, ...);
void log_err(const char *message, ...);

#endif
