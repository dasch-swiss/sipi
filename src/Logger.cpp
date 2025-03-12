#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "Logger.h"
#include "jansson.h"

// includes quotes
std::string to_escaped_json_string(const std::string &input)
{
  json_t *json_str = json_string(input.c_str());
  char *escaped_cstr = json_dumps(json_str, JSON_ENCODE_ANY);
  std::string escaped_string(escaped_cstr);

  free(escaped_cstr);
  json_decref(json_str);

  return escaped_string;
}

inline const char *LogLevelToString(LogLevel ll)
{
  switch (ll) {
  case LL_DEBUG:
    return "DEBUG";
  case LL_INFO:
    return "INFO";
  case LL_WARN:
    return "WARN";
  case LL_ERROR:
    return "ERROR";
  default:
    return "Missing";
  }
}

const int vsnprintf_buf_size = 1000;

void log_format(LogLevel ll, const char *message, va_list args)
{
  char format[vsnprintf_buf_size];
  vsnprintf(format, vsnprintf_buf_size, message, args);
  printf("{\"level\": \"%s\", \"message\": %s}\n", LogLevelToString(ll), to_escaped_json_string(format).c_str());
}

void log_debug(const char *message, ...)
{
  va_list args;
  va_start(args, message);
  log_format(LL_DEBUG, message, args);
  va_end(args);
}

void log_info(const char *message, ...)
{
  va_list args;
  va_start(args, message);
  log_format(LL_INFO, message, args);
  va_end(args);
}

void log_warn(const char *message, ...)
{
  va_list args;
  va_start(args, message);
  log_format(LL_WARN, message, args);
  va_end(args);
}

void log_err(const char *message, ...)
{
  va_list args;
  va_start(args, message);
  log_format(LL_ERROR, message, args);
  va_end(args);
}
