#include "gtest/gtest.h"

#include "../../../include/SipiImage.h"

//small function to check if file exist
inline bool exists_file(const std::string &name) {
    struct stat buffer{};
    return (stat(name.c_str(), &buffer) == 0);
}

inline bool image_identical(const std::string &name1, const std::string &name2) {
    Sipi::SipiImage img1;
    Sipi::SipiImage img2;
    img1.read(name1);
    img2.read(name2);

    return (img1 == img2);
}

std::string leavesSmallWithAlpha = "../../../../test/_test_data/images/knora/Leaves-small-alpha.tif";
std::string leavesSmallNoAlpha = "../../../../test/_test_data/images/knora/Leaves-small-no-alpha.tif";
std::string png16bit = "../../../../test/_test_data/images/knora/png_16bit.png";
std::string pngPaletteAlpha = "../../../../test/_test_data/images/unit/mario.png";
std::string leaves8tif = "../../../../test/_test_data/images/knora/Leaves8.tif";
std::string cielab = "../../../../test/_test_data/images/unit/cielab.tif";
std::string cmyk = "../../../../test/_test_data/images/unit/cmyk.tif";
std::string cielab16 = "../../../../test/_test_data/images/unit/CIELab16.tif";
std::string palette = "../../../../test/_test_data/images/unit/palette.tif";
std::string grayicc = "../../../../test/_test_data/images/unit/gray_with_icc.jp2";
std::string wrongrotation = "../../../../test/_test_data/images/unit/image_orientation.jpg";
std::string watermark_correct = "../../../../test/_test_data/images/unit/watermark_correct.tif";
std::string watermark_incorrect = "../../../../test/_test_data/images/unit/watermark_incorrect.tif";

// Check if configuration file can be found
TEST(SipiImage, CheckIfTestImagesCanBeFound)
{
    EXPECT_TRUE(exists_file(leavesSmallWithAlpha));
    EXPECT_TRUE(exists_file(leavesSmallNoAlpha));
    EXPECT_TRUE(exists_file(png16bit));
    EXPECT_TRUE(exists_file(pngPaletteAlpha));
    EXPECT_TRUE(exists_file(leaves8tif));
    EXPECT_TRUE(exists_file(cielab));
    EXPECT_TRUE(exists_file(cmyk));
    EXPECT_TRUE(exists_file(cielab16));
    EXPECT_TRUE(exists_file(palette));
    EXPECT_TRUE(exists_file(grayicc));
    EXPECT_TRUE(exists_file(wrongrotation));
    EXPECT_TRUE(exists_file(watermark_correct));
    EXPECT_TRUE(exists_file(watermark_incorrect));
}

TEST(SipiImage, ImageComparison)
{
    Sipi::SipiIOTiff::initLibrary();
    EXPECT_TRUE(image_identical(leaves8tif, leaves8tif));
}

// Convert Tiff with alpha channel to JPG
TEST(SipiImage, ConvertTiffWithAlphaToJPG)
{
    Sipi::SipiIOTiff::initLibrary();
    const std::shared_ptr<Sipi::SipiRegion> region;
    const auto size = std::make_shared<Sipi::SipiSize>("!128,128");

    Sipi::SipiImage img;

    ASSERT_NO_THROW(img.read(leavesSmallWithAlpha, region, size));

    ASSERT_NO_THROW(img.write("jpg", "../../../../test/_test_data/images/thumbs/Leaves-small-with-alpha.jpg"));
}

// Convert Tiff with no alpha channel to JPG
TEST(SipiImage, ConvertTiffWithNoAlphaToJPG)
{
    Sipi::SipiIOTiff::initLibrary();
    const std::shared_ptr<Sipi::SipiRegion> region;
    const auto size = std::make_shared<Sipi::SipiSize>("!128,128");

    Sipi::SipiImage img;

    ASSERT_NO_THROW(img.read(leavesSmallNoAlpha, region, size));

    ASSERT_NO_THROW(img.write("jpg", "../../../../test/_test_data/images/thumbs/Leaves-small-no-alpha.jpg"));
}

// Convert PNG 16 bit with alpha channel and ICC profile to TIFF and back
TEST(SipiImage, ConvertPng16BitToJpxToPng) {
    Sipi::SipiIOTiff::initLibrary();
    Sipi::SipiImage img1;
    ASSERT_NO_THROW(img1.read(png16bit));
    ASSERT_NO_THROW(img1.write("tif", "../../../../test/_test_data/images/knora/png_16bit.tif"));

    Sipi::SipiImage img2;
    ASSERT_NO_THROW(img2.read("../../../../test/_test_data/images/knora/png_16bit.tif"));
    ASSERT_NO_THROW(img2.write("png", "../../../../test/_test_data/images/knora/png_16bit_X.png"));
    //EXPECT_TRUE(image_identical(png16bit, "../../../../test/_test_data/images/knora/png_16bit_X.png"));
}

// Convert PNG 16 bit with alpha channel and ICC profile to JPX
TEST(SipiImage, ConvertPng16BitToJpx) {
    Sipi::SipiIOTiff::initLibrary();
    Sipi::SipiImage img1;

    ASSERT_NO_THROW(img1.read(png16bit));

    ASSERT_NO_THROW(img1.write("jpx", "../../../../test/_test_data/images/knora/png_16bit.jpx"));

    EXPECT_TRUE(image_identical(png16bit, "../../../../test/_test_data/images/knora/png_16bit.jpx"));
}


// Convert PNG 16 bit with alpha channel and ICC profile to TIFF
TEST(SipiImage, ConvertPng16BitToTiff)
{
    Sipi::SipiIOTiff::initLibrary();
    Sipi::SipiImage img1;

    ASSERT_NO_THROW(img1.read(png16bit));

    ASSERT_NO_THROW(img1.write("tif", "../../../../test/_test_data/images/knora/png_16bit.tif"));

    EXPECT_TRUE(image_identical(png16bit, "../../../../test/_test_data/images/knora/png_16bit.tif"));
}

// Convert PNG 16 bit with alpha channel and ICC profile to JPEG
TEST(SipiImage, ConvertPng16BitToJpg)
{
    Sipi::SipiIOTiff::initLibrary();
    Sipi::SipiImage img1;

    ASSERT_NO_THROW(img1.read(png16bit));

    ASSERT_NO_THROW(img1.write("jpg", "../../../../test/_test_data/images/knora/png_16bit.jpg"));
}

TEST(SipiImage, ConvertPNGPaletteAlphaToTiff)
{
    Sipi::SipiIOTiff::initLibrary();
    Sipi::SipiImage img1;

    ASSERT_NO_THROW(img1.read(pngPaletteAlpha));
    ASSERT_NO_THROW(img1.write("tif", "../../../../test/_test_data/images/unit/_mario.tif"));
    EXPECT_TRUE(image_identical("../../../../test/_test_data/images/unit/mario.tif", "../../../../test/_test_data/images/unit/_mario.tif"));

}

TEST(SipiImage, CIELab_Conversion)
{
    Sipi::SipiIOTiff::initLibrary();
    Sipi::SipiImage img1;
    Sipi::SipiImage img2;
    Sipi::SipiImage img3;

    ASSERT_NO_THROW(img1.read(cielab));
    ASSERT_NO_THROW(img1.write("jpx", "../../../../test/_test_data/images/unit/cielab.jpx"));
    ASSERT_NO_THROW(img2.read("../../../../test/_test_data/images/unit/cielab.jpx"));
    ASSERT_NO_THROW(img2.write("tif", "../../../../test/_test_data/images/unit/cielab_2.tif"));

    // now test if conversion back to TIFF gives an identical image
    EXPECT_TRUE(image_identical(cielab, "../../../../test/_test_data/images/unit/cielab_2.tif"));
    ASSERT_NO_THROW(img3.read("../../../../test/_test_data/images/unit/cielab.jpx"));
    ASSERT_NO_THROW(img3.write("png", "../../../../test/_test_data/images/unit/cielab.png"));
}

TEST(SipiImage, CIELab16_Conversion)
{
    Sipi::SipiIOTiff::initLibrary();
    Sipi::SipiImage img1;
    Sipi::SipiImage img2;
    Sipi::SipiImage img3;
    Sipi::SipiImage img4;

    ASSERT_NO_THROW(img1.read(cielab16));
    ASSERT_NO_THROW(img1.write("jpx", "../../../../test/_test_data/images/unit/CIELab16.jpx"));
    ASSERT_NO_THROW(img2.read("../../../../test/_test_data/images/unit/CIELab16.jpx"));
    ASSERT_NO_THROW(img2.write("tif", "../../../../test/_test_data/images/unit/CIELab_2.tif"));

    // now test if conversion back to TIFF gives an identical image
    EXPECT_TRUE(image_identical(cielab16, "../../../../test/_test_data/images/unit/CIELab_2.tif"));
    ASSERT_NO_THROW(img3.read("../../../../test/_test_data/images/unit/CIELab16.jpx"));
    ASSERT_NO_THROW(img3.write("png", "../../../../test/_test_data/images/unit/CIELab16.png"));
    ASSERT_NO_THROW(img4.read("../../../../test/_test_data/images/unit/CIELab16.jpx"));
    ASSERT_NO_THROW(img4.write("jpg", "../../../../test/_test_data/images/unit/CIELab16.jpg"));
}

TEST(SipiImage, CMYK_Conversion)
{
    Sipi::SipiIOTiff::initLibrary();
    Sipi::SipiImage img1;
    Sipi::SipiImage img2;
    Sipi::SipiImage img3;
    Sipi::SipiImage img4;

    ASSERT_NO_THROW(img1.read(cmyk));
    ASSERT_NO_THROW(img1.write("jpx", "../../../../test/_test_data/images/unit/_cmyk.jpx"));
    ASSERT_NO_THROW(img2.read("../../../../test/_test_data/images/unit/_cmyk.jpx"));
    ASSERT_NO_THROW(img2.write("tif", "../../../../test/_test_data/images/unit/_cmyk_2.tif"));

    // now test if conversion back to TIFF gives an identical image
    EXPECT_TRUE(image_identical(cmyk, "../../../../test/_test_data/images/unit/_cmyk_2.tif"));
    ASSERT_NO_THROW(img3.read("../../../../test/_test_data/images/unit/_cmyk.jpx"));
    ASSERT_NO_THROW(img3.write("png", "../../../../test/_test_data/images/unit/_cmyk.png"));
    ASSERT_NO_THROW(img4.read("../../../../test/_test_data/images/unit/_cmyk.jpx"));
    ASSERT_NO_THROW(img4.write("jpg", "../../../../test/_test_data/images/unit/_cmyk.jpg"));
}

TEST(SipiImage, PALETTE_Conversion)
{
    Sipi::SipiIOTiff::initLibrary();
    Sipi::SipiImage img1;
    ASSERT_NO_THROW(img1.read(palette));
    ASSERT_NO_THROW(img1.write("jpx", "../../../../test/_test_data/images/unit/_palette.jpx"));
}

TEST(SipiImage, GRAYICC_Conversion)
{
    Sipi::SipiIOTiff::initLibrary();
    Sipi::SipiImage img;
    ASSERT_NO_THROW(img.read(grayicc));
    ASSERT_NO_THROW(img.write("jpg", "../../../../test/_test_data/images/unit/_grayicc.jpg"));
}

TEST(SipiImage, CMYK_lossy_compression)
{
    Sipi::SipiIOTiff::initLibrary();
    Sipi::SipiImage img;
    const std::shared_ptr<Sipi::SipiRegion> region = nullptr;
    const std::shared_ptr<Sipi::SipiSize> size = nullptr;
    ASSERT_NO_THROW(img.readOriginal(cmyk, region, size, shttps::HashType::sha256));
        Sipi::SipiCompressionParams params =  {
            {
                Sipi::J2K_rates, "0.5 0.2 0.1 0.025"
            },
            {
                Sipi::J2K_Clayers, "4"
            },
            {
                Sipi::J2K_Clevels, "3"
            }
    };
    ASSERT_NO_THROW(img.write("jpx", "../../../../test/_test_data/images/unit/_cmyk_lossy.jp2", &params));
    EXPECT_TRUE(image_identical("../../../../test/_test_data/images/unit/cmyk_lossy.jp2", "../../../../test/_test_data/images/unit/_cmyk_lossy.jp2"));
}

TEST(SipiImage, WrongRotation)
{
    Sipi::SipiIOTiff::initLibrary();
    Sipi::SipiImage img;
    const std::shared_ptr<Sipi::SipiRegion> region = nullptr;
    const std::shared_ptr<Sipi::SipiSize> size = nullptr;
    ASSERT_NO_THROW(img.readOriginal(wrongrotation, region, size, shttps::HashType::sha256));
    //EXPECT_EQ(img.getNx(), 3264);
    //EXPECT_EQ(img.getNy(), 2448);
    //EXPECT_EQ(img.getNc(), 3);
    EXPECT_EQ(img.getOrientation(), Sipi::RIGHTTOP);
    ASSERT_NO_THROW(img.set_topleft());
    //EXPECT_EQ(img.getNx(), 2448);
    //EXPECT_EQ(img.getNy(), 3264);
    //EXPECT_EQ(img.getNc(), 3);
    //EXPECT_EQ(img.getOrientation(), Sipi::TOPLEFT);
    ASSERT_NO_THROW(img.write("tif", "../../../../test/_test_data/images/unit/_image_orientation.tif"));
    EXPECT_TRUE(image_identical("../../../../test/_test_data/images/unit/image_orientation.tif", "../../../../test/_test_data/images/unit/_image_orientation.tif"));
}

// Apply watermark to image
TEST(SipiImage, Watermark)
{
    Sipi::SipiIOTiff::initLibrary();
    Sipi::SipiImage img1;
    Sipi::SipiImage img2;
    Sipi::SipiImage img3;
    Sipi::SipiImage img4;

    ASSERT_NO_THROW(img1.read(cielab));
    EXPECT_NO_THROW(img1.add_watermark(watermark_correct));

    ASSERT_NO_THROW(img2.read(cielab));
    ASSERT_THROW(img2.add_watermark(watermark_incorrect), std::exception);

    ASSERT_NO_THROW(img3.read(cielab16));
    EXPECT_NO_THROW(img3.add_watermark(watermark_correct));

    ASSERT_NO_THROW(img4.read(cielab16));
    ASSERT_THROW(img4.add_watermark(watermark_incorrect), std::exception);

}

TEST(SipiImage, Dev3229)
{
    Sipi::SipiIOTiff::initLibrary();
    Sipi::SipiImage img1;
    Sipi::SipiImage img2;

    const std::string problematic_tif = "../../../../test/_test_data/images/unit/dev_3229.tif";
    const std::string problematic_tif_converted_to_jpx = "../../../../test/_test_data/images/unit/dev_3229.jpx";
    const std::string problematic_tif_converted_from_jpx_to_tif = "../../../../test/_test_data/images/unit/dev_3229_2.tif";

    ASSERT_NO_THROW(img1.read(problematic_tif));
    ASSERT_NO_THROW(img1.write("jpx", problematic_tif_converted_to_jpx));
    ASSERT_NO_THROW(img2.read(problematic_tif_converted_to_jpx));

    // now test if conversion back to TIFF gives an identical image
    ASSERT_NO_THROW(img2.write("tif", problematic_tif_converted_from_jpx_to_tif));
    EXPECT_TRUE(image_identical(problematic_tif, problematic_tif_converted_from_jpx_to_tif));
}
