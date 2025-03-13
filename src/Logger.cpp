#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "Logger.h"

std::string escape_json_str(const std::string &s)
{
  std::ostringstream o;
  for (auto c = s.cbegin(); c != s.cend(); c++) {
    switch (*c) {
    case '"':
      o << "\\\"";
      break;
    case '\\':
      o << "\\\\";
      break;
    case '\b':
      o << "\\b";
      break;
    case '\f':
      o << "\\f";
      break;
    case '\n':
      o << "\\n";
      break;
    case '\r':
      o << "\\r";
      break;
    case '\t':
      o << "\\t";
      break;
    default:
      if ('\x00' <= *c && *c <= '\x1f') {
        o << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(*c);
      } else {
        o << *c;
      }
    }
  }
  return o.str();
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
  char *outfmt = "{\"level\": \"%s\", \"message\": %s}\n";
  fprintf(stderr, outfmt, LogLevelToString(ll), escape_json_str(format).c_str());
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
