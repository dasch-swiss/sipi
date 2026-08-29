/*
 * Copyright © 2026 Swiss National Data and Service Center for the Humanities
 * and/or DaSCH Service Platform contributors. SPDX-License-Identifier:
 * AGPL-3.0-or-later
 */

// Regression test for Sipi::Icc::parse (DEV-7078): an embedded ICC profile
// need not carry a description tag. cmsGetProfileInfoASCII then reports
// len == 0, and parse() must classify the profile as icc_unknown without
// reading the (unallocated) description buffer.

#include "metadata/icc.h"

#include <lcms2.h>

#include "gtest/gtest.h"

#include <memory>
#include <sstream>
#include <vector>

namespace {

struct ProfileCloser
{
  void operator()(cmsHPROFILE p) const
  {
    if (p != nullptr) cmsCloseProfile(p);
  }
};
using ProfilePtr = std::unique_ptr<std::remove_pointer_t<cmsHPROFILE>, ProfileCloser>;

std::vector<unsigned char> serialize(cmsHPROFILE profile)
{
  cmsUInt32Number len = 0;
  cmsSaveProfileToMem(profile, nullptr, &len);
  std::vector<unsigned char> buf(len);
  cmsSaveProfileToMem(profile, buf.data(), &len);
  return buf;
}

}// namespace

TEST(SipiIccParse, DescriptionLessProfileParsesAsUnknown)
{
  ProfilePtr profile(cmsCreate_sRGBProfile());
  ASSERT_NE(profile, nullptr);

  // Erase the description tag: lcms2 removes a tag when the data pointer is null.
  ASSERT_TRUE(cmsWriteTag(profile.get(), cmsSigProfileDescriptionTag, nullptr));

  // Sanity: the erase actually produced a zero-length description, i.e. this
  // test really drives the len == 0 path in Icc::parse.
  ASSERT_EQ(cmsGetProfileInfoASCII(profile.get(), cmsInfoDescription, cmsNoLanguage, cmsNoCountry, nullptr, 0), 0u);

  auto buf = serialize(profile.get());

  auto icc = Sipi::Icc::parse(buf.data(), static_cast<int>(buf.size()));
  ASSERT_TRUE(icc.has_value());
  EXPECT_EQ((*icc)->getProfileType(), Sipi::icc_unknown);
}

TEST(SipiIccParse, SRgbProfileParsesAsSRgb)
{
  ProfilePtr profile(cmsCreate_sRGBProfile());
  ASSERT_NE(profile, nullptr);

  // lcms2's built-in sRGB description is "sRGB built-in", not the string
  // Icc::parse classifies against; set the description it expects so this
  // case exercises the normal (len > 0) classification path.
  cmsMLU *mlu = cmsMLUalloc(nullptr, 1);
  ASSERT_NE(mlu, nullptr);
  cmsMLUsetASCII(mlu, "en", "US", "sRGB IEC61966-2.1");
  ASSERT_TRUE(cmsWriteTag(profile.get(), cmsSigProfileDescriptionTag, mlu));
  cmsMLUfree(mlu);

  auto buf = serialize(profile.get());

  auto icc = Sipi::Icc::parse(buf.data(), static_cast<int>(buf.size()));
  ASSERT_TRUE(icc.has_value());
  EXPECT_EQ((*icc)->getProfileType(), Sipi::icc_sRGB);
}

// Regression test for operator<<(std::ostream &, Icc &) (DEV-7078): the
// manufacturer/model/copyright tags are optional and routinely absent from
// real-world profiles. Streaming a profile with none of the four ASCII info
// tags must not read a zero-length, non-NUL-terminated buffer as a C string.
TEST(SipiIccStream, ProfileWithoutOptionalTagsStreamsSafely)
{
  ProfilePtr profile(cmsCreate_sRGBProfile());
  ASSERT_NE(profile, nullptr);

  // Erase whichever of these tags cmsCreate_sRGBProfile() set (description is
  // always present; manufacturer/model/copyright are not, on this built-in
  // profile, so cmsWriteTag's delete-if-present has nothing to do for them).
  ASSERT_TRUE(cmsWriteTag(profile.get(), cmsSigProfileDescriptionTag, nullptr));
  cmsWriteTag(profile.get(), cmsSigDeviceMfgDescTag, nullptr);
  cmsWriteTag(profile.get(), cmsSigDeviceModelDescTag, nullptr);
  cmsWriteTag(profile.get(), cmsSigCopyrightTag, nullptr);

  // Sanity: all four reads really are len == 0, i.e. this test drives the
  // buffer-underflow path in operator<<.
  ASSERT_EQ(cmsGetProfileInfoASCII(profile.get(), cmsInfoDescription, cmsNoLanguage, cmsNoCountry, nullptr, 0), 0u);
  ASSERT_EQ(cmsGetProfileInfoASCII(profile.get(), cmsInfoManufacturer, cmsNoLanguage, cmsNoCountry, nullptr, 0), 0u);
  ASSERT_EQ(cmsGetProfileInfoASCII(profile.get(), cmsInfoModel, cmsNoLanguage, cmsNoCountry, nullptr, 0), 0u);
  ASSERT_EQ(cmsGetProfileInfoASCII(profile.get(), cmsInfoCopyright, cmsNoLanguage, cmsNoCountry, nullptr, 0), 0u);

  auto buf = serialize(profile.get());

  auto icc = Sipi::Icc::parse(buf.data(), static_cast<int>(buf.size()));
  ASSERT_TRUE(icc.has_value());

  std::ostringstream out;
  out << **icc;

  EXPECT_NE(out.str().find("ICC-Description   :"), std::string::npos);
  EXPECT_NE(out.str().find("ICC-Manufacturer  :"), std::string::npos);
  EXPECT_NE(out.str().find("ICC-Model         :"), std::string::npos);
  EXPECT_NE(out.str().find("ICC-Copyright     :"), std::string::npos);
}
