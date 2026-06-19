/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "formats/output_sink.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace {

// A CallbackSink ctx that records the bytes it receives and replays a
// caller-chosen return code (0 = ok, non-zero = write failure).
struct CaptureCtx
{
  std::vector<uint8_t> bytes;
  int return_code{ 0 };
};

extern "C" int capture_write(void *ctx, const uint8_t *data, size_t len)
{
  auto *c = static_cast<CaptureCtx *>(ctx);
  c->bytes.insert(c->bytes.end(), data, data + len);
  return c->return_code;
}

std::vector<uint8_t> read_all(const std::string &path)
{
  std::ifstream in(path, std::ios::binary);
  return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

std::string temp_path(const std::string &name)
{
  const char *dir = std::getenv("TEST_TMPDIR");
  return (dir != nullptr ? std::string(dir) : std::string("/tmp")) + "/" + name;
}

const std::vector<uint8_t> kHello{ 'h', 'e', 'l', 'l', 'o' };
const std::vector<uint8_t> kWorld{ 'w', 'o', 'r', 'l', 'd' };

TEST(OutputSink, IsStreamingSink)
{
  EXPECT_FALSE(Sipi::is_streaming_sink(Sipi::FilePath{ "/tmp/x" }));
  EXPECT_TRUE(Sipi::is_streaming_sink(Sipi::CallbackSink{ &capture_write, nullptr }));
  EXPECT_TRUE(Sipi::is_streaming_sink(Sipi::TeeSink{ { Sipi::FilePath{ "/tmp/x" } } }));
}

TEST(OutputSink, CallbackSinkReceivesAllChunks)
{
  CaptureCtx ctx;
  Sipi::SinkStream stream{ Sipi::CallbackSink{ &capture_write, &ctx } };

  EXPECT_EQ(0, stream.write(kHello.data(), kHello.size()));
  EXPECT_EQ(0, stream.write(kWorld.data(), kWorld.size()));

  const std::vector<uint8_t> expected{ 'h', 'e', 'l', 'l', 'o', 'w', 'o', 'r', 'l', 'd' };
  EXPECT_EQ(expected, ctx.bytes);
}

TEST(OutputSink, CallbackSinkFailureIsFatal)
{
  CaptureCtx ctx;
  ctx.return_code = 7;
  Sipi::SinkStream stream{ Sipi::CallbackSink{ &capture_write, &ctx } };

  EXPECT_NE(0, stream.write(kHello.data(), kHello.size()));
}

TEST(OutputSink, TeeBroadcastsToCallbackAndFile)
{
  CaptureCtx ctx;
  const std::string path = temp_path("output_sink_tee.bin");

  Sipi::TeeSink tee{ { Sipi::CallbackSink{ &capture_write, &ctx }, Sipi::FilePath{ path } } };
  {
    Sipi::SinkStream stream{ tee };
    EXPECT_EQ(0, stream.write(kHello.data(), kHello.size()));
    EXPECT_EQ(0, stream.write(kWorld.data(), kWorld.size()));
  }// SinkStream destruction flushes/closes the file leaf

  const std::vector<uint8_t> expected{ 'h', 'e', 'l', 'l', 'o', 'w', 'o', 'r', 'l', 'd' };
  EXPECT_EQ(expected, ctx.bytes);
  EXPECT_EQ(expected, read_all(path));
}

TEST(OutputSink, CacheFileFailureIsBestEffortNotFatal)
{
  CaptureCtx ctx;
  // An unopenable cache path (missing directory) must not abort the response:
  // the socket callback still succeeds and write() returns 0.
  Sipi::TeeSink tee{ { Sipi::CallbackSink{ &capture_write, &ctx },
    Sipi::FilePath{ "/nonexistent-dir-xyz/cache.bin" } } };
  Sipi::SinkStream stream{ tee };

  EXPECT_EQ(0, stream.write(kHello.data(), kHello.size()));
  EXPECT_EQ(kHello, ctx.bytes);
}

}// namespace
