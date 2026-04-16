#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "Logger.h"

static bool g_cli_mode = false;
// `g_json_mode` is read from every request worker thread in server mode and
// from the main thread in CLI mode. It is set once at startup (before any
// threads exist) and never re-set, so `memory_order_relaxed` is sufficient
// — but using `std::atomic` rather than a plain bool makes the code
// formally data-race-free per the C++ memory model.
static std::atomic<bool> g_json_mode{ false };
static LogLevel g_log_level = LL_INFO;

void set_cli_mode(bool cli) { g_cli_mode = cli; }
bool is_cli_mode() { return g_cli_mode; }
void set_log_level(LogLevel level) { g_log_level = level; }
LogLevel get_log_level() { return g_log_level; }
void set_json_mode(bool enabled) { g_json_mode.store(enabled, std::memory_order_relaxed); }
bool is_json_mode() { return g_json_mode.load(std::memory_order_relaxed); }

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
  case LL_NOTICE:
    return "NOTICE";
  case LL_WARNING:
    return "WARN";
  case LL_ERR:
    return "ERROR";
  case LL_CRIT:
    return "ALERT";
  case LL_ALERT:
    return "EMERG";
  case LL_EMERG:
    return "ERROR";
  default:
    return "Missing";
  }
}

std::string vformat(const char *format, va_list args)
{
  va_list copy;
  va_copy(copy, args);
  int len = std::vsnprintf(nullptr, 0, format, copy);
  va_end(copy);

  if (len >= 0) {
    std::string s(std::size_t(len) + 1, '\0');
    std::vsnprintf(&s[0], s.size(), format, args);
    s.resize(len);
    return s;
  }

  const auto err = errno;
  const auto ec = std::error_code(err, std::generic_category());
  throw std::system_error(ec);
}

std::string format(const char *message, ...)
{
  va_list args;
  va_start(args, message);
  auto r = vformat(message, args);
  va_end(args);
  return r;
}

//

std::string log_vsformat(LogLevel ll, const char *message, va_list args)
{
  std::string f = vformat(message, args);
  return format("{\"level\": \"%s\", \"message\": \"%s\"}\n", LogLevelToString(ll), escape_json_str(f).c_str());
}

std::string log_sformat(LogLevel ll, const char *message, ...)
{
  va_list args;
  va_start(args, message);
  auto r = log_vsformat(ll, message, args);
  va_end(args);
  return r;
}

void log_vformat(LogLevel ll, const char *message, va_list args)
{
  if (ll < g_log_level) return;

  if (g_cli_mode) {
    // CLI mode: plain text. Under --json every level goes to stderr so stdout
    // stays reserved for the single JSON document. Outside --json,
    // errors→stderr, others→stdout (legacy behaviour).
    std::string msg = vformat(message, args);
    if (g_json_mode.load(std::memory_order_relaxed) || ll >= LL_ERR) {
      fprintf(stderr, "%s\n", msg.c_str());
    } else {
      fprintf(stdout, "%s\n", msg.c_str());
      fflush(stdout);
    }
  } else {
    // Server mode: JSON format→stdout (container best practice)
    // fflush required because stdout is fully buffered when piped
    std::string outfmt = log_vsformat(ll, message, args);
    fprintf(stdout, "%s", outfmt.c_str());
    fflush(stdout);
  }
}

void log_format(LogLevel ll, const char *message, ...)
{
  va_list args;
  va_start(args, message);
  log_vformat(ll, message, args);
  va_end(args);
}

void log_debug(const char *message, ...)
{
  va_list args;
  va_start(args, message);
  log_vformat(LL_DEBUG, message, args);
  va_end(args);
}

void log_info(const char *message, ...)
{
  va_list args;
  va_start(args, message);
  log_vformat(LL_INFO, message, args);
  va_end(args);
}

void log_warn(const char *message, ...)
{
  va_list args;
  va_start(args, message);
  log_vformat(LL_WARNING, message, args);
  va_end(args);
}

void log_err(const char *message, ...)
{
  va_list args;
  va_start(args, message);
  log_vformat(LL_ERR, message, args);
  va_end(args);
}
