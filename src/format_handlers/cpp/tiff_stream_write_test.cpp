/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "format_handlers/SipiIOTiff.h"
#include "format_handlers/output_sink.h"
#include "image/SipiImage.h"
#include "test_paths.h"

namespace {

extern "C" int capture_write(void *ctx, const uint8_t *data, size_t len)
{
  auto *bytes = static_cast<std::vector<uint8_t> *>(ctx);
  bytes->insert(bytes->end(), data, data + len);
  return 0;
}

std::vector<uint8_t> read_all(const std::string &path)
{
  std::ifstream in(path, std::ios::binary);
  return { std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
}

}// namespace

// A streamed TIFF is built in an in-memory file whose seek-past-end holes ship
// in the response body. It must match the TIFF libtiff writes to a real file,
// whose holes read as zeros, byte for byte. The source carries EXIF, so the
// writer appends a second IFD0 after the EXIF directory, seeking to the end.
TEST(TiffStreamWrite, StreamedTiffMatchesTheFileWrite)
{
  Sipi::SipiIOTiff::initLibrary();
  Sipi::SipiImage img;
  ASSERT_TRUE(img.read(sipi::test::data_dir() + "/images/unit/gray_with_icc.jp2").has_value());
  ASSERT_NE(img.getExif(), nullptr);

  const std::string dst = sipi::test::tmp_dir() + "/_tiff_stream_write.tif";
  ASSERT_TRUE(img.write("tif", dst).has_value());
  const std::vector<uint8_t> file_bytes = read_all(dst);
  std::remove(dst.c_str());

  std::vector<uint8_t> streamed;
  const Sipi::OutputSink sink{ Sipi::CallbackSink{ &capture_write, &streamed } };
  ASSERT_TRUE(img.write("tif", sink).has_value());

  ASSERT_FALSE(file_bytes.empty());
  EXPECT_EQ(streamed.size(), file_bytes.size());
  EXPECT_TRUE(streamed == file_bytes);
}
