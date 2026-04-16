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
void set_cli_mode(bool cli);
bool is_cli_mode();
void set_log_level(LogLevel level);
LogLevel get_log_level();

/*!
 * Enable JSON-output mode. When true, every log level (info, warn, err) is
 * routed to stderr so that stdout stays reserved for the single JSON document
 * produced by `--json`. Backed by `std::atomic<bool>`: writers use
 * `memory_order_relaxed` since the flag is set once at CLI startup
 * (immediately after CLI11 parses `--json`) and never re-set, and readers
 * only need to observe the value published by that single store.
 */
void set_json_mode(bool enabled);
bool is_json_mode();

#endif
