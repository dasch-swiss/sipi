#include "gtest/gtest.h"

#include "SipiFilenameHash.h"
#include "shttps/Error.h"

TEST(SipiFilenameHash, ConsistentHashing)
{
    SipiFilenameHash::setLevels(3);
    SipiFilenameHash h1("test_image.jp2");
    SipiFilenameHash h2("test_image.jp2");
    EXPECT_EQ(h1.filepath(), h2.filepath());
}

TEST(SipiFilenameHash, DifferentInputsDifferentPaths)
{
    SipiFilenameHash::setLevels(3);
    SipiFilenameHash h1("image_a.jp2");
    SipiFilenameHash h2("image_b.jp2");
    EXPECT_NE(h1.filepath(), h2.filepath());
}

TEST(SipiFilenameHash, HashCharsInRange)
{
    SipiFilenameHash::setLevels(3);
    SipiFilenameHash h("some_file.tif");
    for (int i = 0; i < 6; ++i) {
        EXPECT_GE(h[i], 'A');
        EXPECT_LE(h[i], 'Z');
    }
}

TEST(SipiFilenameHash, FilepathContainsSubdirectories)
{
    SipiFilenameHash::setLevels(3);
    SipiFilenameHash h("myfile.jp2");
    std::string fp = h.filepath();

    // With 3 levels, filepath should have 3 single-char directory components
    // Format: "X/Y/Z/myfile.jp2"
    EXPECT_EQ(fp.back(), '2'); // ends with filename
    EXPECT_TRUE(fp.find("myfile.jp2") != std::string::npos);

    // Count slashes — should be exactly 3 (one per level)
    int slashes = 0;
    for (char c : fp) {
        if (c == '/') slashes++;
    }
    EXPECT_EQ(slashes, 3);
}

TEST(SipiFilenameHash, ZeroLevelsGivesFilenameOnly)
{
    SipiFilenameHash::setLevels(0);
    SipiFilenameHash h("plain.tif");
    EXPECT_EQ(h.filepath(), "plain.tif");
}

TEST(SipiFilenameHash, PathStrippedToFilename)
{
    // Constructor strips path prefix for hashing
    SipiFilenameHash::setLevels(0);
    SipiFilenameHash h("/some/path/to/file.jp2");
    EXPECT_EQ(h.filepath(), "file.jp2");
}

TEST(SipiFilenameHash, CopyConstructorCopiesHash)
{
    SipiFilenameHash::setLevels(2);
    SipiFilenameHash h1("test.jp2");
    SipiFilenameHash h2(h1);
    // Copy constructor copies hash values correctly
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(h1[i], h2[i]);
    }
    // BUG(DEV-6002): copy constructor does not copy 'path' and 'name' members,
    // so filepath() returns only directory components without filename.
    // h2.filepath() == "W/A/" instead of "W/A/test.jp2"
}

TEST(SipiFilenameHash, AssignmentOperator)
{
    SipiFilenameHash::setLevels(2);
    SipiFilenameHash h1("test.jp2");
    SipiFilenameHash h2("other.jp2");
    h2 = h1;
    // After assignment, hash values should match
    for (int i = 0; i < 6; ++i) {
        EXPECT_EQ(h1[i], h2[i]);
    }
    // CRITICAL: operator= leaks memory — it copies the raw hash array but does
    // not free h2's original 'path'/'name' allocations before overwriting them.
    // Same as copy constructor: 'path' and 'name' are not copied, so
    // h2.filepath() returns only directory components without filename.
    // TODO(DEV-6002): Fix operator= to properly manage 'path'/'name' members.
}

TEST(SipiFilenameHash, InvalidIndexThrows)
{
    SipiFilenameHash::setLevels(1);
    SipiFilenameHash h("test.jp2");
    EXPECT_THROW(h[-1], shttps::Error);
    EXPECT_THROW(h[6], shttps::Error);
}

TEST(SipiFilenameHash, LevelsStaticState)
{
    SipiFilenameHash::setLevels(5);
    EXPECT_EQ(SipiFilenameHash::getLevels(), 5);
    SipiFilenameHash::setLevels(0);
    EXPECT_EQ(SipiFilenameHash::getLevels(), 0);
}
