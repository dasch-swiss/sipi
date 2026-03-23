/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include "ApprovalTests.hpp"
#include "gtest/gtest.h"

#include "SipiImage.hpp"
#include "SipiImageError.hpp"
#include "metadata/SipiExif.h"
#include "metadata/SipiIcc.h"

#include <sstream>
#include <sys/stat.h>

// Approval tests run from build/test/approval/
static const std::string test_images = "../../../test/_test_data/images/";

static bool file_exists(const std::string &path)
{
  struct stat buf {};
  return stat(path.c_str(), &buf) == 0;
}

/// Helper: read image and dump all available metadata to a string.
static std::string dump_metadata(const std::string &path)
{
  std::ostringstream out;
  out << "=== " << path << " ===\n\n";

  Sipi::SipiImage img;
  try {
    img.read(path);
  } catch (const std::exception &e) {
    out << "READ ERROR: " << e.what() << "\n";
    return out.str();
  }

  out << "Dimensions: " << img.getNx() << "x" << img.getNy() << "\n";
  out << "Channels: " << img.getNc() << "\n";
  out << "BPS: " << img.getBps() << "\n";
  out << "Orientation: " << static_cast<int>(img.getOrientation()) << "\n\n";

  auto exif = img.getExif();
  if (exif) {
    out << "--- EXIF ---\n";
    out << *exif;
    out << "\n";
  } else {
    out << "--- EXIF: none ---\n\n";
  }

  auto icc = img.getIcc();
  if (icc) {
    out << "--- ICC ---\n";
    out << *icc;
    out << "\n";
  } else {
    out << "--- ICC: none ---\n\n";
  }

  return out.str();
}

// Golden test: MaoriFigure.jpg — rich EXIF metadata
TEST(MetadataGolden, MaoriFigure)
{
  std::string path = test_images + "unit/MaoriFigure.jpg";
  if (!file_exists(path)) GTEST_SKIP() << "Test image not found: " << path;
  ApprovalTests::Approvals::verify(dump_metadata(path));
}

// Golden test: img_exif_gps.jpg — EXIF with GPS data
TEST(MetadataGolden, ImgExifGps)
{
  std::string path = test_images + "unit/img_exif_gps.jpg";
  if (!file_exists(path)) GTEST_SKIP() << "Test image not found: " << path;
  ApprovalTests::Approvals::verify(dump_metadata(path));
}

// Golden test: gray_with_icc_another.jpg — grayscale with ICC profile
TEST(MetadataGolden, GrayWithIcc)
{
  std::string path = test_images + "unit/gray_with_icc_another.jpg";
  if (!file_exists(path)) GTEST_SKIP() << "Test image not found: " << path;
  ApprovalTests::Approvals::verify(dump_metadata(path));
}

// Golden test: HasCommentBlock.JPG — JPEG with comment block
TEST(MetadataGolden, HasCommentBlock)
{
  std::string path = test_images + "unit/HasCommentBlock.JPG";
  if (!file_exists(path)) GTEST_SKIP() << "Test image not found: " << path;
  ApprovalTests::Approvals::verify(dump_metadata(path));
}

// Golden test: image_orientation.jpg — EXIF with orientation tag
TEST(MetadataGolden, ImageOrientation)
{
  std::string path = test_images + "unit/image_orientation.jpg";
  if (!file_exists(path)) GTEST_SKIP() << "Test image not found: " << path;
  ApprovalTests::Approvals::verify(dump_metadata(path));
}
