#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "Logger.h"
#include "jansson.h"

// https://stackoverflow.com/questions/17316506/strip-invalid-utf8-from-string-in-c-c
std::string correct_non_utf_8(const std::string *str)
{
  int i, f_size = str->size();
  unsigned char c, c2, c3, c4;
  std::string to;
  to.reserve(f_size);

  for (i = 0; i < f_size; i++) {
    c = (unsigned char)(*str)[i];
    if (c < 32) {// control char
      if (c == 9 || c == 10 || c == 13) {// allow only \t \n \r
        to.append(1, c);
      }
      continue;
    } else if (c < 127) {// normal ASCII
      to.append(1, c);
      continue;
    } else if (c < 160) {// control char (nothing should be defined here either ASCI, ISO_8859-1 or UTF8, so skipping)
      if (c2 == 128) {// fix microsoft mess, add euro
        to.append(1, 226);
        to.append(1, 130);
        to.append(1, 172);
      }
      if (c2 == 133) {// fix IBM mess, add NEL = \n\r
        to.append(1, 10);
        to.append(1, 13);
      }
      continue;
    } else if (c < 192) {// invalid for UTF8, converting ASCII
      to.append(1, (unsigned char)194);
      to.append(1, c);
      continue;
    } else if (c < 194) {// invalid for UTF8, converting ASCII
      to.append(1, (unsigned char)195);
      to.append(1, c - 64);
      continue;
    } else if (c < 224 && i + 1 < f_size) {// possibly 2byte UTF8
      c2 = (unsigned char)(*str)[i + 1];
      if (c2 > 127 && c2 < 192) {// valid 2byte UTF8
        if (c == 194 && c2 < 160) {// control char, skipping
          ;
        } else {
          to.append(1, c);
          to.append(1, c2);
        }
        i++;
        continue;
      }
    } else if (c < 240 && i + 2 < f_size) {// possibly 3byte UTF8
      c2 = (unsigned char)(*str)[i + 1];
      c3 = (unsigned char)(*str)[i + 2];
      if (c2 > 127 && c2 < 192 && c3 > 127 && c3 < 192) {// valid 3byte UTF8
        to.append(1, c);
        to.append(1, c2);
        to.append(1, c3);
        i += 2;
        continue;
      }
    } else if (c < 245 && i + 3 < f_size) {// possibly 4byte UTF8
      c2 = (unsigned char)(*str)[i + 1];
      c3 = (unsigned char)(*str)[i + 2];
      c4 = (unsigned char)(*str)[i + 3];
      if (c2 > 127 && c2 < 192 && c3 > 127 && c3 < 192 && c4 > 127 && c4 < 192) {// valid 4byte UTF8
        to.append(1, c);
        to.append(1, c2);
        to.append(1, c3);
        to.append(1, c4);
        i += 3;
        continue;
      }
    }
    // invalid UTF8, converting ASCII (c>245 || string too short for multi-byte))
    to.append(1, (unsigned char)195);
    to.append(1, c - 64);
  }
  return to;
}

// includes quotes
std::string to_escaped_json_string(const std::string &input)
{
  json_t *json_str = json_string_nocheck(correct_non_utf_8(&input).c_str());
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
  char *outfmt = "{\"level\": \"%s\", \"message\": %s}\n";
  fprintf(stderr, outfmt, LogLevelToString(ll), to_escaped_json_string(format).c_str());
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
