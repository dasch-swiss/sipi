/*
 * Copyright © 2016 - 2026 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <exiv2/exiv2.hpp>

#include "metadata/exif.h"
#include "metadata/iptc.h"
#include "metadata/xmp.h"

namespace {

// Exiv2's XMP decode registers each unknown namespace in a process-global
// registry that only XmpParser::terminate() frees; without it LeakSanitizer
// reports the registrations as leaks.
struct Exiv2XmpTerminate : ::testing::Environment
{
  void TearDown() override { Exiv2::XmpParser::terminate(); }
};
[[maybe_unused]] auto *kExiv2XmpTerminate = ::testing::AddGlobalTestEnvironment(new Exiv2XmpTerminate);

TEST(XmpPixelDimensions, RewritesElementAndAttributeForms)
{
  Sipi::Xmp xmp(std::string(R"(<rdf:Description exif:PixelXDimension="4000" tiff:ImageLength='3000'>)"
                            "<exif:PixelYDimension>\n  3000\n</exif:PixelYDimension>"
                            "<tiff:ImageWidth>4000</tiff:ImageWidth>"
                            "<dc:rights>4000 x 3000</dc:rights>"
                            "<tiff:ImageWidthX>4000</tiff:ImageWidthX>"
                            "<myexif:PixelXDimension>4000</myexif:PixelXDimension>"));
  xmp.set_pixel_dimensions(128, 96);
  EXPECT_EQ(xmp.xmpBytes(),
    std::string(R"(<rdf:Description exif:PixelXDimension="128" tiff:ImageLength='96'>)"
                "<exif:PixelYDimension>\n  96\n</exif:PixelYDimension>"
                "<tiff:ImageWidth>128</tiff:ImageWidth>"
                "<dc:rights>4000 x 3000</dc:rights>"
                "<tiff:ImageWidthX>4000</tiff:ImageWidthX>"
                "<myexif:PixelXDimension>4000</myexif:PixelXDimension>"));
}

TEST(XmpPixelDimensions, ValueStartingWithDigitOneIsWrittenVerbatim)
{
  Sipi::Xmp xmp(std::string(R"(<x exif:PixelXDimension="4000"/><tiff:ImageWidth>4000</tiff:ImageWidth>)"));
  xmp.set_pixel_dimensions(128, 1);
  EXPECT_EQ(xmp.xmpBytes(), std::string(R"(<x exif:PixelXDimension="128"/><tiff:ImageWidth>128</tiff:ImageWidth>)"));
}

TEST(XmpPixelDimensions, RewritesAnyPrefixBoundToTheNamespaces)
{
  Sipi::Xmp xmp(std::string(R"(<rdf:Description xmlns:e="http://ns.adobe.com/exif/1.0/" )"
                            R"(xmlns:t.x='http://ns.adobe.com/tiff/1.0/' e:PixelXDimension="4000">)"
                            R"(<t.x:ImageLength rdf:datatype="xsd:integer">3000</t.x:ImageLength>)"
                            R"(<tAx:ImageLength>3000</tAx:ImageLength>)"));
  xmp.set_pixel_dimensions(128, 96);
  EXPECT_EQ(xmp.xmpBytes(),
    std::string(R"(<rdf:Description xmlns:e="http://ns.adobe.com/exif/1.0/" )"
                R"(xmlns:t.x='http://ns.adobe.com/tiff/1.0/' e:PixelXDimension="128">)"
                R"(<t.x:ImageLength rdf:datatype="xsd:integer">96</t.x:ImageLength>)"
                R"(<tAx:ImageLength>3000</tAx:ImageLength>)"));
}

// Unterminated start tags must not make the rewrite scan the rest of the
// packet once per tag.
TEST(XmpPixelDimensions, UnterminatedTagsStayCheap)
{
  std::string packet;
  // 4.4 MB: a scan to the end of the packet per tag would take minutes.
  for (int i = 0; i < 200000; ++i) { packet += "<exif:PixelXDimension "; }
  Sipi::Xmp xmp(packet);
  const auto start = std::chrono::steady_clock::now();
  xmp.set_pixel_dimensions(128, 96);
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5));
  EXPECT_EQ(xmp.xmpBytes(), packet);
}

TEST(XmpPixelDimensions, ManyPropertiesStayCheap)
{
  std::string packet;
  for (int i = 0; i < 200000; ++i) { packet += R"(<x exif:PixelXDimension="4000"/>)"; }
  Sipi::Xmp xmp(packet);
  const auto start = std::chrono::steady_clock::now();
  xmp.set_pixel_dimensions(96, 64);
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5));
  EXPECT_EQ(xmp.xmpBytes().size(), packet.size() - 200000U * 2);
}

TEST(ExifPixelDimensions, RewritesPresentTagsKeepingTheirType)
{
  Sipi::Exif exif;
  exif.addKeyVal("Exif.Photo.PixelXDimension", static_cast<std::uint16_t>(4000));
  exif.addKeyVal("Exif.Photo.PixelYDimension", static_cast<std::uint32_t>(3000));
  exif.addKeyVal("Exif.Image.Copyright", std::string("DaSCH"));
  ASSERT_TRUE(exif.set_pixel_dimensions(128, 96));

  std::uint16_t x = 0;
  std::uint32_t y = 0;
  std::string copyright;
  ASSERT_TRUE(exif.getValByKey("Exif.Photo.PixelXDimension", x));
  ASSERT_TRUE(exif.getValByKey("Exif.Photo.PixelYDimension", y));
  ASSERT_TRUE(exif.getValByKey("Exif.Image.Copyright", copyright));
  EXPECT_EQ(x, 128);
  EXPECT_EQ(y, 96U);
  EXPECT_EQ(copyright, "DaSCH");

  std::uint32_t absent = 0;
  EXPECT_FALSE(exif.getValByKey("Exif.Image.ImageWidth", absent));
  EXPECT_FALSE(exif.getValByKey("Exif.Image.ImageLength", absent));

  // The encoders serialise exifBytes(), not the in-memory entries.
  const auto bytes = exif.exifBytes();
  auto reparsed = Sipi::Exif::parse(bytes.data(), static_cast<unsigned int>(bytes.size()));
  ASSERT_TRUE(reparsed.has_value());
  ASSERT_TRUE((*reparsed)->getValByKey("Exif.Photo.PixelXDimension", x));
  EXPECT_EQ(x, 128);
}

TEST(XmpPixelDimensions, RewritesMaxAvailableSize)
{
  Sipi::Xmp xmp(std::string(R"(<x Iptc4xmpExt:MaxAvailWidth="4000"><Iptc4xmpExt:MaxAvailHeight>3000</Iptc4xmpExt:MaxAvailHeight>)"));
  xmp.set_pixel_dimensions(128, 96);
  EXPECT_EQ(xmp.xmpBytes(),
    std::string(R"(<x Iptc4xmpExt:MaxAvailWidth="128"><Iptc4xmpExt:MaxAvailHeight>96</Iptc4xmpExt:MaxAvailHeight>)"));
}

TEST(XmpThumbnails, ErasesEveryThumbnailsElementAndNothingElse)
{
  const std::string thumbnails = R"(<xmp:Thumbnails><rdf:Alt><rdf:li rdf:parseType="Resource">)"
                                 R"(<xmpGImg:width>256</xmpGImg:width><xmpGImg:height>192</xmpGImg:height>)"
                                 R"(<xmpGImg:image>/9j/4AAQSkZJRg==</xmpGImg:image></rdf:li></rdf:Alt></xmp:Thumbnails >)";
  Sipi::Xmp xmp(std::string(R"(<rdf:Description xmlns:b="http://ns.adobe.com/xap/1.0/">)") + thumbnails
                + R"(<dc:rights>DaSCH</dc:rights>)"
                + R"(<b:Thumbnails rdf:about=""><rdf:Alt/></b:Thumbnails>)"
                + R"(<xap:Thumbnails/>)"
                + R"(<xmp:ThumbnailsX>kept</xmp:ThumbnailsX>)"
                + R"(<xmp:CreatorTool>kept</xmp:CreatorTool></rdf:Description>)");
  ASSERT_TRUE(xmp.erase_thumbnails());
  EXPECT_EQ(xmp.xmpBytes(),
    std::string(R"(<rdf:Description xmlns:b="http://ns.adobe.com/xap/1.0/">)"
                R"(<dc:rights>DaSCH</dc:rights>)"
                R"(<xmp:ThumbnailsX>kept</xmp:ThumbnailsX>)"
                R"(<xmp:CreatorTool>kept</xmp:CreatorTool></rdf:Description>)"));
}

TEST(XmpThumbnails, ManyElementsStayCheap)
{
  std::string packet;
  for (int i = 0; i < 200000; ++i) { packet += "<xmp:Thumbnails/><x/>"; }
  Sipi::Xmp xmp(packet);
  const auto start = std::chrono::steady_clock::now();
  ASSERT_TRUE(xmp.erase_thumbnails());
  EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(5));
  EXPECT_EQ(xmp.xmpBytes().size(), 200000U * 4);
}

TEST(XmpThumbnails, ErasesEveryGImgImage)
{
  Sipi::Xmp xmp(std::string(R"(<xmpTPg:PageInfo><rdf:Seq><rdf:li rdf:parseType="Resource"><xmpGImg:width>64</xmpGImg:width>)"
                            R"(<xmpGImg:image>/9j/4AAQ</xmpGImg:image></rdf:li></rdf:Seq></xmpTPg:PageInfo>)"));
  ASSERT_TRUE(xmp.erase_thumbnails());
  EXPECT_EQ(xmp.xmpBytes(),
    std::string(R"(<xmpTPg:PageInfo><rdf:Seq><rdf:li rdf:parseType="Resource"><xmpGImg:width>64</xmpGImg:width>)"
                R"(</rdf:li></rdf:Seq></xmpTPg:PageInfo>)"));
}

// Bindings the text scan does not resolve fail instead of passing a preview.
TEST(XmpThumbnails, UnresolvedBindingFails)
{
  Sipi::Xmp default_ns(std::string(R"(<rdf:Description xmlns="http://ns.adobe.com/xap/1.0/"><Thumbnails>x</Thumbnails>)"));
  EXPECT_FALSE(default_ns.erase_thumbnails());
  std::string packet;
  for (int i = 0; i < 9; ++i) { packet += "xmlns:p" + std::to_string(i) + R"(="http://ns.adobe.com/xap/1.0/" )"; }
  Sipi::Xmp past_cap(packet + "<p8:Thumbnails>x</p8:Thumbnails>");
  EXPECT_FALSE(past_cap.erase_thumbnails());
}

TEST(XmpThumbnails, UnterminatedThumbnailsFails)
{
  Sipi::Xmp unclosed(std::string("<xmp:Thumbnails><rdf:Alt><xmpGImg:image>/9j/</xmpGImg:image>"));
  EXPECT_FALSE(unclosed.erase_thumbnails());
  Sipi::Xmp open_tag(std::string("<xmp:Thumbnails") + std::string(300, ' '));
  EXPECT_FALSE(open_tag.erase_thumbnails());
}

// Exif built with exiv2 and parsed back, as a decoder hands it to SIPI.
std::shared_ptr<Sipi::Exif> encoded(Exiv2::ExifData data)
{
  Exiv2::Blob blob;
  Exiv2::ExifParser::encode(blob, Exiv2::littleEndian, data);
  auto exif = Sipi::Exif::parse(blob.data(), static_cast<unsigned int>(blob.size()));
  EXPECT_TRUE(exif.has_value());
  return exif.value_or(nullptr);
}

// What an encoder writes: the decoded exifBytes().
Exiv2::ExifData decoded(const std::vector<unsigned char> &bytes)
{
  Exiv2::ExifData data;
  Exiv2::ExifParser::decode(data, bytes.data(), bytes.size());
  return data;
}

std::string value_of(const Exiv2::ExifData &data, const std::string &key)
{
  const auto pos = data.findKey(Exiv2::ExifKey(key));
  return pos == data.end() ? "<absent>" : pos->toString();
}

bool contains(const std::vector<unsigned char> &bytes, const std::string &needle)
{
  return std::search(bytes.begin(), bytes.end(), needle.begin(), needle.end()) != bytes.end();
}

TEST(ExifPixelDimensions, RewritesTheOtherSizeFieldsInTheirTypes)
{
  Exiv2::ExifData data;
  data["Exif.Photo.PixelXDimension"] = static_cast<std::uint32_t>(4000);
  data["Exif.Iop.RelatedImageWidth"] = static_cast<std::uint32_t>(4000);
  data["Exif.Iop.RelatedImageLength"] = static_cast<std::uint16_t>(3000);
  Exiv2::URationalValue crop;
  crop.read("4000/1 3000/1");
  data.add(Exiv2::ExifKey("Exif.Image.DefaultCropSize"), &crop);
  Exiv2::ULongValue area;
  area.read("8 8 3008 4008");
  data.add(Exiv2::ExifKey("Exif.Image.ActiveArea"), &area);
  data["Exif.Image.Copyright"] = "DaSCH";
  auto exif = encoded(data);
  ASSERT_NE(exif, nullptr);

  ASSERT_TRUE(exif->set_pixel_dimensions(128, 96));
  const auto out = decoded(exif->exifBytes());
  EXPECT_EQ(value_of(out, "Exif.Iop.RelatedImageWidth"), "128");
  EXPECT_EQ(value_of(out, "Exif.Iop.RelatedImageLength"), "96");
  EXPECT_EQ(value_of(out, "Exif.Image.DefaultCropSize"), "128/1 96/1");
  EXPECT_EQ(value_of(out, "Exif.Image.ActiveArea"), "0 0 96 128");
  EXPECT_EQ(value_of(out, "Exif.Image.Copyright"), "DaSCH");
}

TEST(ExifPreviews, ErasesTheThumbnailAndMakerNotePreviewKeepingTheRest)
{
  const std::string pixels = "SIPI-PREVIEW-PIXELS";
  std::vector<Exiv2::byte> jpeg = { 0xff, 0xd8 };
  jpeg.insert(jpeg.end(), pixels.begin(), pixels.end());
  jpeg.insert(jpeg.end(), { 0xff, 0xd9 });

  Exiv2::ExifData data;
  data["Exif.Image.Make"] = "OLYMPUS IMAGING CORP.";
  data["Exif.Image.Copyright"] = "DaSCH";
  data["Exif.Olympus2.ImageHeight"] = static_cast<std::uint32_t>(3000);
  Exiv2::DataValue preview(Exiv2::undefined);
  preview.read(jpeg.data(), jpeg.size());
  data.add(Exiv2::ExifKey("Exif.Olympus2.ThumbnailImage"), &preview);
  Exiv2::ExifThumb(data).setJpegThumbnail(jpeg.data(), jpeg.size());
  auto exif = encoded(data);
  ASSERT_NE(exif, nullptr);

  const auto before = exif->exifBytes();
  ASSERT_TRUE(contains(before, pixels));
  const auto in = decoded(before);
  ASSERT_NE(value_of(in, "Exif.Olympus2.ThumbnailImage"), "<absent>");
  ASSERT_NE(value_of(in, "Exif.Thumbnail.JPEGInterchangeFormat"), "<absent>");

  exif->erase_previews();
  ASSERT_TRUE(exif->set_pixel_dimensions(128, 96));
  const auto after = exif->exifBytes();
  EXPECT_FALSE(contains(after, pixels));
  const auto out = decoded(after);
  EXPECT_EQ(value_of(out, "Exif.Olympus2.ThumbnailImage"), "<absent>");
  EXPECT_EQ(value_of(out, "Exif.Thumbnail.JPEGInterchangeFormat"), "<absent>");
  EXPECT_EQ(value_of(out, "Exif.Olympus2.ImageHeight"), "96");
  EXPECT_EQ(value_of(out, "Exif.Image.Make"), "OLYMPUS IMAGING CORP.");
  EXPECT_EQ(value_of(out, "Exif.Image.Copyright"), "DaSCH");
}

TEST(ExifPreviews, ErasesEveryPreviewExiv2Reads)
{
  const std::vector<std::string> previews = { "Exif.SubImage1.JPEGInterchangeFormat",
    "Exif.SubThumb1.JPEGInterchangeFormat",
    "Exif.Image2.JPEGInterchangeFormat",
    "Exif.NikonPreview.JPEGInterchangeFormat",
    "Exif.SamsungPreview.JPEGInterchangeFormat",
    "Exif.Pentax.PreviewOffset",
    "Exif.PentaxDng.PreviewLength",
    "Exif.Minolta.ThumbnailOffset",
    "Exif.SonyMinolta.ThumbnailLength",
    "Exif.OlympusCs.PreviewImageStart" };
  Sipi::Exif exif;
  for (const auto &key : previews) { exif.addKeyVal(key, static_cast<std::uint32_t>(1)); }
  exif.addKeyVal("Exif.Image.Copyright", std::string("DaSCH"));
  exif.erase_previews();

  std::uint32_t v = 0;
  for (const auto &key : previews) { EXPECT_FALSE(exif.getValByKey(key, v)) << key; }
  std::string copyright;
  EXPECT_TRUE(exif.getValByKey("Exif.Image.Copyright", copyright));
}

// A raw maker note that still holds a preview must not be written back.
TEST(ExifPreviews, AMakerNotePreviewDropsTheRawMakerNote)
{
  for (const std::string key : { "Exif.NikonPreview.JPEGInterchangeFormat",
         "Exif.SamsungPreview.JPEGInterchangeFormat",
         "Exif.Pentax.PreviewOffset",
         "Exif.Olympus2.ThumbnailImage",
         "Exif.Olympus.Thumbnail",
         "Exif.Olympus.PreviewImage",
         "Exif.Sony1.PreviewImage",
         "Exif.Casio2.PreviewImage" }) {
    Sipi::Exif exif;
    exif.addKeyVal(key, static_cast<std::uint32_t>(1));
    exif.addKeyVal("Exif.Photo.MakerNote", static_cast<std::uint32_t>(1));
    exif.erase_previews();
    std::uint32_t v = 0;
    EXPECT_FALSE(exif.getValByKey("Exif.Photo.MakerNote", v)) << key;
  }
  Sipi::Exif ifd_preview_only;
  ifd_preview_only.addKeyVal("Exif.SubImage1.JPEGInterchangeFormat", static_cast<std::uint32_t>(1));
  ifd_preview_only.addKeyVal("Exif.Photo.MakerNote", static_cast<std::uint32_t>(1));
  ifd_preview_only.erase_previews();
  std::uint32_t v = 0;
  EXPECT_TRUE(ifd_preview_only.getValByKey("Exif.Photo.MakerNote", v));
}

TEST(IptcPreview, ErasesThePreviewKeepingTheRest)
{
  Exiv2::IptcData data;
  data["Iptc.Application2.Copyright"] = "DaSCH";
  data["Iptc.Application2.PreviewFormat"] = static_cast<std::uint16_t>(11);
  Exiv2::DataValue preview(Exiv2::undefined);
  const std::string pixels = "SIPI-PREVIEW-PIXELS";
  preview.read(reinterpret_cast<const Exiv2::byte *>(pixels.data()), pixels.size());
  data.add(Exiv2::IptcKey("Iptc.Application2.Preview"), &preview);
  const auto buf = Exiv2::IptcParser::encode(data);
  auto iptc = Sipi::Iptc::parse(buf.c_data(), static_cast<unsigned int>(buf.size()));
  ASSERT_TRUE(iptc.has_value());
  ASSERT_TRUE(contains((*iptc)->iptcBytes(), pixels));

  (*iptc)->erase_preview();
  const auto bytes = (*iptc)->iptcBytes();
  EXPECT_FALSE(contains(bytes, pixels));
  Exiv2::IptcData out;
  ASSERT_EQ(Exiv2::IptcParser::decode(out, bytes.data(), bytes.size()), 0);
  EXPECT_EQ(out.findKey(Exiv2::IptcKey("Iptc.Application2.PreviewFormat")), out.end());
  const auto copyright = out.findKey(Exiv2::IptcKey("Iptc.Application2.Copyright"));
  ASSERT_NE(copyright, out.end());
  EXPECT_EQ(copyright->toString(), "DaSCH");
}

}// namespace

namespace {

// One Photoshop image resource with an empty name, its data padded to even.
std::vector<unsigned char> resource(std::uint16_t id, const std::string &data)
{
  const std::size_t n = data.size();
  std::vector<unsigned char> r = { '8',
    'B',
    'I',
    'M',
    static_cast<unsigned char>(id >> 8),
    static_cast<unsigned char>(id),
    0,
    0,
    static_cast<unsigned char>(n >> 24),
    static_cast<unsigned char>(n >> 16),
    static_cast<unsigned char>(n >> 8),
    static_cast<unsigned char>(n) };
  r.insert(r.end(), data.begin(), data.end());
  if (n % 2 == 1) { r.push_back(0); }
  return r;
}

std::vector<unsigned char> concat(std::initializer_list<std::vector<unsigned char>> parts)
{
  std::vector<unsigned char> all;
  for (const auto &p : parts) { all.insert(all.end(), p.begin(), p.end()); }
  return all;
}

std::shared_ptr<Sipi::Exif> with_resources(const std::vector<unsigned char> &irb)
{
  Exiv2::ExifData data;
  data["Exif.Image.Copyright"] = "DaSCH";
  Exiv2::DataValue value(Exiv2::unsignedByte);
  value.read(irb.data(), irb.size());
  data.add(Exiv2::ExifKey("Exif.Image.ImageResources"), &value);
  return encoded(data);
}

std::vector<unsigned char> resources_of(const std::vector<unsigned char> &exif_bytes)
{
  const auto data = decoded(exif_bytes);
  const auto pos = data.findKey(Exiv2::ExifKey("Exif.Image.ImageResources"));
  if (pos == data.end()) { return {}; }
  std::vector<unsigned char> bytes(pos->size());
  pos->copy(bytes.data(), Exiv2::littleEndian);
  return bytes;
}

bool holds(const std::vector<unsigned char> &bytes, const std::vector<unsigned char> &part)
{
  return std::search(bytes.begin(), bytes.end(), part.begin(), part.end()) != bytes.end();
}

std::string as_string(const Exiv2::Blob &blob) { return { blob.begin(), blob.end() }; }

// EXIF with a native size, a rights field and an IFD1 thumbnail.
std::string nested_exif(const std::string &pixels)
{
  std::vector<Exiv2::byte> jpeg = { 0xff, 0xd8 };
  jpeg.insert(jpeg.end(), pixels.begin(), pixels.end());
  jpeg.insert(jpeg.end(), { 0xff, 0xd9 });
  Exiv2::ExifData data;
  data["Exif.Image.Copyright"] = "DaSCH-nested";
  data["Exif.Photo.PixelXDimension"] = static_cast<std::uint32_t>(4000);
  Exiv2::ExifThumb(data).setJpegThumbnail(jpeg.data(), jpeg.size());
  Exiv2::Blob blob;
  Exiv2::ExifParser::encode(blob, Exiv2::littleEndian, data);
  return as_string(blob);
}

const std::string kRights = R"(<dc:rights><rdf:Alt><rdf:li xml:lang="x-default">DaSCH</rdf:li></rdf:Alt></dc:rights>)";

std::string xmp_with_thumbnail(const std::string &pixels)
{
  return R"(<rdf:Description rdf:about="" xmlns:xmp="http://ns.adobe.com/xap/1.0/" tiff:ImageWidth="4000">)"
         R"(<xmp:Thumbnails><rdf:Alt><rdf:li><xmpGImg:image>)"
         + pixels + R"(</xmpGImg:image></rdf:li></rdf:Alt></xmp:Thumbnails>)" + kRights + "</rdf:Description>";
}

TEST(ExifImageResources, DropPreviewsAndRewriteTheNestedBlocksKeepingTheRest)
{
  const std::string pixels = "SIPI-PREVIEW-PIXELS";
  const std::string xmp_pixels = "U0lQSS1QUkVWSUVX";
  const std::string nested_pixels = "SIPI-NESTED-PREVIEW";
  const auto resolution = resource(0x03ed, "RESOLUTION-INFO!");
  const auto copyright_flag = resource(0x040a, std::string(1, '\1'));
  auto exif = with_resources(concat({ resolution,
    resource(0x040c, pixels),
    resource(0x0409, pixels),
    resource(0x0424, xmp_with_thumbnail(xmp_pixels)),
    resource(0x0422, nested_exif(nested_pixels)),
    copyright_flag }));
  ASSERT_NE(exif, nullptr);
  const auto before = resources_of(exif->exifBytes());
  ASSERT_TRUE(contains(before, pixels));
  ASSERT_TRUE(contains(before, xmp_pixels));
  ASSERT_TRUE(contains(before, nested_pixels));

  ASSERT_TRUE(exif->restrict_image_resources(128, 96));
  const auto after = resources_of(exif->exifBytes());
  EXPECT_FALSE(contains(after, pixels));
  EXPECT_FALSE(contains(after, xmp_pixels));
  EXPECT_FALSE(contains(after, nested_pixels));
  EXPECT_TRUE(contains(after, R"(tiff:ImageWidth="128")"));
  EXPECT_TRUE(contains(after, kRights));
  EXPECT_TRUE(holds(after, resolution));
  EXPECT_TRUE(holds(after, copyright_flag));

  // The nested EXIF is the last resource but one, the copyright flag (14 bytes) the last.
  const std::string tag = "8BIM\x04\x22";
  const auto at = std::search(after.begin(), after.end(), tag.begin(), tag.end());
  ASSERT_NE(at, after.end());
  const auto nested_begin = at + 12;
  const auto nested = decoded(std::vector<unsigned char>(nested_begin, after.end() - 14));
  EXPECT_EQ(value_of(nested, "Exif.Photo.PixelXDimension"), "128");
  EXPECT_EQ(value_of(nested, "Exif.Image.Copyright"), "DaSCH-nested");
  EXPECT_EQ(value_of(nested, "Exif.Thumbnail.JPEGInterchangeFormat"), "<absent>");
}

TEST(ExifImageResources, UntouchedResourcesStayByteIdentical)
{
  const auto irb = concat({ resource(0x03ed, "RESOLUTION-INFO!"), resource(0x040a, std::string(1, '\1')) });
  auto exif = with_resources(irb);
  ASSERT_NE(exif, nullptr);
  ASSERT_TRUE(exif->restrict_image_resources(128, 96));
  EXPECT_EQ(resources_of(exif->exifBytes()), irb);
}

// Resources the rewrite cannot read, or native sizes it does not rewrite,
// fail instead of passing through.
TEST(ExifImageResources, UnreadableOrUnrewrittenResourcesFail)
{
  auto truncated = resource(0x03ed, "RESOLUTION-INFO!");
  truncated.resize(truncated.size() - 4);
  auto unsigned_block = resource(0x03ed, "RESOLUTION-INFO!");
  unsigned_block[0] = 'X';
  const std::vector<std::pair<std::string, std::vector<unsigned char>>> cases = { { "truncated", truncated },
    { "no signature", unsigned_block },
    { "slices", resource(0x041a, "SLICES") },
    { "legacy header", resource(0x03e8, "HEADER") },
    { "unresolved XMP thumbnail",
      resource(0x0424, R"(<rdf:Description xmlns="http://ns.adobe.com/xap/1.0/"><Thumbnails>x</Thumbnails>)") },
    { "unparseable EXIF", resource(0x0422, "garbage") } };
  for (const auto &[name, irb] : cases) {
    Sipi::Exif exif;
    exif.addKeyVal(0x8649, "Image", irb.data(), irb.size());
    EXPECT_FALSE(exif.restrict_image_resources(128, 96)) << name;
  }
  // A second block would leave the rewrite behind.
  const auto resolution = resource(0x03ed, "RESOLUTION-INFO!");
  Exiv2::ExifData data;
  Exiv2::DataValue block(Exiv2::unsignedByte);
  block.read(resolution.data(), resolution.size());
  data.add(Exiv2::ExifKey("Exif.Image.ImageResources"), &block);
  data.add(Exiv2::ExifKey("Exif.Image.ImageResources"), &block);
  Exiv2::IptcData no_iptc;
  Exiv2::XmpData no_xmp;
  Exiv2::MemIo io;
  Exiv2::TiffParser::encode(io, nullptr, 0, Exiv2::littleEndian, data, no_iptc, no_xmp);
  auto twice = Sipi::Exif::parse(io.mmap(), static_cast<unsigned int>(io.size()));
  ASSERT_TRUE(twice.has_value());
  const auto blocks = decoded(std::vector<unsigned char>(io.mmap(), io.mmap() + io.size()));
  ASSERT_EQ(
    std::count_if(
      blocks.begin(), blocks.end(), [](const Exiv2::Exifdatum &d) { return d.key() == "Exif.Image.ImageResources"; }),
    2);
  EXPECT_FALSE((*twice)->restrict_image_resources(128, 96)) << "two blocks";
}

// The raw XMP and IPTC copies IFD0 can hold, and the IPTC resource, are
// never encoded, so restrict_image_resources leaves them alone. An Exiv2
// change that keeps them fails this test.
TEST(ExifImageResources, ExifBytesNeverWritesTheRawXmpOrIptcCopies)
{
  const std::string xmp_pixels = "U0lQSS1QUkVWSUVX";
  const std::string iptc_pixels = "SIPI-IPTC-PREVIEW";
  Exiv2::ExifData data;
  data["Exif.Image.Copyright"] = "DaSCH";
  const auto irb = resource(0x03ed, "RESOLUTION-INFO!");
  Exiv2::DataValue resources(Exiv2::unsignedByte);
  resources.read(irb.data(), irb.size());
  data.add(Exiv2::ExifKey("Exif.Image.ImageResources"), &resources);
  data["Exif.Image.IPTCNAA"] = static_cast<std::uint32_t>(0);
  Exiv2::IptcData iptc;
  iptc["Iptc.Application2.Copyright"] = "DaSCH";
  Exiv2::DataValue preview(Exiv2::undefined);
  preview.read(reinterpret_cast<const Exiv2::byte *>(iptc_pixels.data()), iptc_pixels.size());
  iptc.add(Exiv2::IptcKey("Iptc.Application2.Preview"), &preview);
  Exiv2::XmpData xmp;
  xmp.setPacket(xmp_with_thumbnail(xmp_pixels));
  xmp.usePacket(true);
  Exiv2::MemIo io;
  Exiv2::TiffParser::encode(io, nullptr, 0, Exiv2::littleEndian, data, iptc, xmp);
  const std::vector<unsigned char> source(io.mmap(), io.mmap() + io.size());
  const auto in = decoded(source);
  ASSERT_NE(value_of(in, "Exif.Image.XMLPacket"), "<absent>");
  ASSERT_NE(value_of(in, "Exif.Image.IPTCNAA"), "<absent>");
  ASSERT_TRUE(contains(source, xmp_pixels));
  ASSERT_TRUE(contains(source, iptc_pixels));

  auto exif = Sipi::Exif::parse(source.data(), static_cast<unsigned int>(source.size()));
  ASSERT_TRUE(exif.has_value());
  const auto bytes = (*exif)->exifBytes();
  const auto out = decoded(bytes);
  EXPECT_EQ(value_of(out, "Exif.Image.XMLPacket"), "<absent>");
  EXPECT_EQ(value_of(out, "Exif.Image.IPTCNAA"), "<absent>");
  EXPECT_FALSE(contains(bytes, xmp_pixels));
  EXPECT_FALSE(contains(bytes, iptc_pixels));
  EXPECT_EQ(value_of(out, "Exif.Image.Copyright"), "DaSCH");
}

}// namespace

namespace {

TEST(XmpSplitExtended, MovesTheLargestPropertiesWithTheirBindingsAndKeepsRightsLast)
{
  const std::string big = "<a:big>" + std::string(1000, 'b') + "</a:big>";
  const std::string empty = R"(<a:empty rdf:resource="urn:x"/>)";
  const std::string mid = "<c:mid>" + std::string(600, 'm') + "</c:mid>";
  const std::string rights =
    R"(<dc:rights><rdf:Alt><rdf:li xml:lang="x-default">)" + std::string(700, 'r') + "</rdf:li></rdf:Alt></dc:rights>";
  const std::string packet =
    R"(<x:xmpmeta xmlns:x="adobe:ns:meta/"><rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#">)"
    R"(<rdf:Description rdf:about="" xmlns:a="urn:a" xmlns:dc="http://purl.org/dc/elements/1.1/">)"
    + big + "<a:small>s</a:small>" + empty + rights
    + R"(</rdf:Description><rdf:Description rdf:about="" xmlns:c="urn:c" c:attr="1">)" + mid
    + "</rdf:Description></rdf:RDF></x:xmpmeta>";
  const Sipi::Xmp xmp(packet);

  const auto split = xmp.split_extended(packet.size() - 1200);
  ASSERT_TRUE(split.has_value()) << split.error().diagnostic_message();
  EXPECT_LE(split->standard.size(), packet.size() - 1200);
  EXPECT_EQ(split->standard.find(big), std::string::npos);
  EXPECT_EQ(split->standard.find(mid), std::string::npos);
  EXPECT_NE(split->standard.find(rights), std::string::npos);
  EXPECT_NE(split->standard.find("<a:small>s</a:small>" + empty), std::string::npos);
  EXPECT_NE(split->standard.find(R"(xmpNote:HasExtendedXMP=")" + split->guid + '"'), std::string::npos);
  EXPECT_EQ(split->guid.size(), 32U);
  EXPECT_EQ(split->guid.find_first_not_of("0123456789ABCDEF"), std::string::npos);
  EXPECT_NE(
    split->extended.find(R"(xmlns:a="urn:a")" + std::string(R"( xmlns:dc="http://purl.org/dc/elements/1.1/">)") + big),
    std::string::npos);
  EXPECT_NE(split->extended.find(R"(xmlns:c="urn:c">)" + mid), std::string::npos);

  Exiv2::XmpData standard;
  ASSERT_EQ(Exiv2::XmpParser::decode(standard, split->standard), 0);
  EXPECT_EQ(standard["Xmp.xmpNote.HasExtendedXMP"].toString(), split->guid);
  Exiv2::XmpData extended;
  ASSERT_EQ(Exiv2::XmpParser::decode(extended, split->extended), 0);

  // Rights move only when nothing else is left to move.
  const auto all = xmp.split_extended(packet.size() - 1900);
  ASSERT_TRUE(all.has_value());
  EXPECT_NE(all->extended.find(rights), std::string::npos);
}

TEST(XmpSplitExtended, FailsOnMalformedXmlOrWhenNothingCanMove)
{
  EXPECT_FALSE(Sipi::Xmp(std::string("<x:xmpmeta><rdf:RDF")).split_extended(10).has_value());
  const std::string attributes_only =
    R"(<rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#"><rdf:Description xmlns:a="urn:a" a:v=")"
    + std::string(500, 'v') + R"("/></rdf:RDF>)";
  EXPECT_FALSE(Sipi::Xmp(attributes_only).split_extended(400).has_value());
  const std::string description = R"(<rdf:Description><a:v>)" + std::string(100, 'v') + "</a:v></rdf:Description>";
  const auto packet = [&](const std::string &prolog, const std::string &bindings, int descriptions) {
    std::string p =
      prolog + R"(<rdf:RDF xmlns:rdf="http://www.w3.org/1999/02/22-rdf-syntax-ns#" xmlns:a="urn:a")" + bindings + ">";
    for (int i = 0; i < descriptions; ++i) { p += description; }
    return Sipi::Xmp(p + "</rdf:RDF>");
  };
  // Room for everything: only the parse can fail.
  constexpr std::size_t kRoom = 1 << 20;
  ASSERT_TRUE(packet("", "", 10).split_extended(kRoom).has_value());
  EXPECT_FALSE(packet("<!DOCTYPE x>", "", 10).split_extended(kRoom).has_value());
  std::string many;
  for (int i = 0; i < 62; ++i) { many += " xmlns:n" + std::to_string(i) + R"(="urn:n")"; }
  ASSERT_TRUE(packet("", many, 10).split_extended(kRoom).has_value());
  EXPECT_FALSE(packet("", many + R"( xmlns:last="urn:n")", 10).split_extended(kRoom).has_value());
  ASSERT_TRUE(packet("", "", 256).split_extended(kRoom).has_value());
  EXPECT_FALSE(packet("", "", 257).split_extended(kRoom).has_value());
}

// Exif::exifBytes(limit) tells a fitting encode from one that dropped
// fields to fit; this pins the Exiv2 behaviour it relies on.
TEST(ExifEncode, DropsLargeFieldsToFit)
{
  Exiv2::ExifData data;
  data["Exif.Image.Copyright"] = "DaSCH";
  data["Exif.Image.ImageDescription"] = std::string(70000, 'd');
  Exiv2::Blob blob;
  Exiv2::ExifParser::encode(blob, Exiv2::littleEndian, data);
  EXPECT_LE(blob.size(), 65527U);
  EXPECT_EQ(value_of(data, "Exif.Image.ImageDescription"), "<absent>");
  EXPECT_EQ(value_of(data, "Exif.Image.Copyright"), "DaSCH");
}

}// namespace
