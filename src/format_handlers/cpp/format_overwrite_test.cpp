/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

#include "image/SipiImage.h"
#include "test_paths.h"

namespace {

size_t file_size(const std::string &path)
{
  struct stat buf {};
  if (stat(path.c_str(), &buf) != 0) return 0;
  return static_cast<size_t>(buf.st_size);
}

// Compares sizes, never bytes: embedded ICC headers carry a wall-clock
// creation date, so two writes of the same image differ in content.
class FormatOverwrite : public ::testing::TestWithParam<std::string>
{
};

TEST_P(FormatOverwrite, OverwritingLargerFileTruncatesIt)
{
  const std::string format = GetParam();
  const std::string src = sipi::test::data_dir() + "/images/unit/MaoriFigure.jpg";
  const std::string fresh = sipi::test::tmp_dir() + "/_overwrite_fresh." + format;
  const std::string dst = sipi::test::tmp_dir() + "/_overwrite_dst." + format;

  Sipi::SipiImage img;
  ASSERT_TRUE(img.read(src).has_value());
  ASSERT_TRUE(img.write(format, fresh).has_value());
  const size_t fresh_size = file_size(fresh);
  ASSERT_GT(fresh_size, 0u);

  {
    int fd = ::open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    ASSERT_GE(fd, 0);
    const std::string junk(2 * fresh_size, 'x');
    ASSERT_EQ(::write(fd, junk.data(), junk.size()), static_cast<ssize_t>(junk.size()));
    ::close(fd);
  }

  ASSERT_TRUE(img.write(format, dst).has_value());
  EXPECT_EQ(file_size(dst), fresh_size);

  std::remove(fresh.c_str());
  std::remove(dst.c_str());
}

INSTANTIATE_TEST_SUITE_P(Handlers, FormatOverwrite, ::testing::Values("tif", "png", "jpx"));

}// namespace
