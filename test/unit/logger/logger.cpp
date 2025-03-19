#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>

#include "Logger.h"
#include "gtest/gtest.h"

bool valid_json(const char *text)
{
  json_error_t error;

  if (json_loads(text, 0, &error)) {
    return true;
  } else {
    fprintf(stderr, "json error on line %d: %s\n", error.line, error.text);
    return false;
  }
}

TEST(Logger, CheckSyntax)
{
  auto should = "{\"level\": \"DEBUG\", \"message\": \"The \\\"7\\\" cranks are turning!\"}\n";
  auto out = log_sformat(LL_DEBUG, "The \"%i\" cranks are turning!", 7);
  EXPECT_TRUE(out == should);
  EXPECT_TRUE(valid_json(out.c_str()));
}
