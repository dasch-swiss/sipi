/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

// Round-trip tests for the typed constructors + flatten accessors added for the
// FFI seam (sipi_serve_image). serve_iiif parses an IIIF URL component into the
// C++ object, flattens it into the flat SipiServeRequest, and build_image_response
// reconstructs it via the typed constructor. The reconstructed object must be
// byte-identical to the original under canonical()/get_size()/crop_coords(),
// because the canonical URL is the cache key and the Link header.

#include "gtest/gtest.h"

#include "iiifparser/SipiQualityFormat.h"
#include "iiifparser/SipiRegion.h"
#include "iiifparser/SipiRotation.h"
#include "iiifparser/SipiSize.h"

namespace {

constexpr size_t kW = 512;
constexpr size_t kH = 512;

std::string region_canonical(Sipi::SipiRegion &r)
{
  int x = 0, y = 0;
  size_t w = 0, h = 0;
  if (r.getType() != Sipi::SipiRegion::FULL) { r.crop_coords(kW, kH, x, y, w, h); }
  char buf[128];
  r.canonical(buf, sizeof buf);
  return buf;
}

// Flatten -> reconstruct via the typed ctor, then compare canonical output.
void check_region(const std::string &iiif)
{
  Sipi::SipiRegion orig(iiif);
  float rx = 0, ry = 0, rw = 0, rh = 0;
  orig.get_coords(rx, ry, rw, rh);
  Sipi::SipiRegion rebuilt(orig.getType(), rx, ry, rw, rh);

  EXPECT_EQ(orig.getType(), rebuilt.getType()) << iiif;
  EXPECT_EQ(region_canonical(orig), region_canonical(rebuilt)) << iiif;
}

std::string size_canonical(Sipi::SipiSize &s)
{
  size_t w = 0, h = 0;
  int reduce = -1;
  bool redonly = false;
  s.get_size(kW, kH, w, h, reduce, redonly);
  char buf[128];
  s.canonical(buf, sizeof buf);
  return buf;
}

void check_size(const std::string &iiif)
{
  Sipi::SipiSize orig(iiif);
  bool upscaling = false;
  float percent = 0;
  int reduce = 0;
  size_t nx = 0, ny = 0;
  orig.get_params(upscaling, percent, reduce, nx, ny);
  Sipi::SipiSize rebuilt(orig.getType(), upscaling, percent, reduce, nx, ny);

  EXPECT_EQ(orig.getType(), rebuilt.getType()) << iiif;

  size_t ow = 0, oh = 0, nw = 0, nh = 0;
  int ored = -1, nred = -1;
  bool oro = false, nro = false;
  orig.get_size(kW, kH, ow, oh, ored, oro);
  rebuilt.get_size(kW, kH, nw, nh, nred, nro);
  EXPECT_EQ(ow, nw) << iiif;
  EXPECT_EQ(oh, nh) << iiif;
  EXPECT_EQ(ored, nred) << iiif;
  EXPECT_EQ(oro, nro) << iiif;

  char obuf[128], nbuf[128];
  orig.canonical(obuf, sizeof obuf);
  rebuilt.canonical(nbuf, sizeof nbuf);
  EXPECT_STREQ(obuf, nbuf) << iiif;
}

}// namespace

TEST(SeamRoundtrip, Region)
{
  for (const auto *iiif : { "full", "square", "0,0,100,100", "50,50,200,200", "pct:10,10,50,50" }) {
    check_region(iiif);
  }
}

TEST(SeamRoundtrip, Size)
{
  // Only cases that get_size() accepts on a 512x512 source (no forbidden upscale).
  for (const auto *iiif : { "max", "256,", ",256", "200,200", "!200,200", "pct:50", "^1000,", "^max" }) {
    check_size(iiif);
  }
}

TEST(SeamRoundtrip, Rotation)
{
  for (const auto *iiif : { "0", "90", "180", "270", "!0", "!90", "45" }) {
    Sipi::SipiRotation orig(iiif);
    float angle = 0;
    const bool mirror = orig.get_rotation(angle);

    Sipi::SipiRotation rebuilt(angle, mirror);
    float angle2 = 0;
    const bool mirror2 = rebuilt.get_rotation(angle2);
    EXPECT_FLOAT_EQ(angle, angle2) << iiif;
    EXPECT_EQ(mirror, mirror2) << iiif;
  }
}

TEST(SeamRoundtrip, QualityFormat)
{
  for (const auto *iiif : { "default.jpg", "color.png", "gray.tif", "bitonal.jp2", "default.jp2" }) {
    Sipi::SipiQualityFormat orig(iiif);
    Sipi::SipiQualityFormat rebuilt(orig.quality(), orig.format());
    EXPECT_EQ(orig.quality(), rebuilt.quality()) << iiif;
    EXPECT_EQ(orig.format(), rebuilt.format()) << iiif;
  }
}
