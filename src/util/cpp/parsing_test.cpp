#include "gtest/gtest.h"

#include "util/Parsing.h"
#include "util/Error.h"

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

TEST(Parsing, ParseIntValid)
{
    std::string s = "42";
    EXPECT_EQ(shttps::Parsing::parse_int(s), 42u);
}

TEST(Parsing, ParseIntZero)
{
    std::string s = "0";
    EXPECT_EQ(shttps::Parsing::parse_int(s), 0u);
}

TEST(Parsing, ParseIntLarge)
{
    std::string s = "1000000";
    EXPECT_EQ(shttps::Parsing::parse_int(s), 1000000u);
}

TEST(Parsing, ParseIntInvalidThrows)
{
    std::string s = "abc";
    EXPECT_THROW(shttps::Parsing::parse_int(s), shttps::Error);
}

TEST(Parsing, ParseIntNegativeThrows)
{
    std::string s = "-5";
    EXPECT_THROW(shttps::Parsing::parse_int(s), shttps::Error);
}

TEST(Parsing, ParseIntDecimalThrows)
{
    std::string s = "3.14";
    EXPECT_THROW(shttps::Parsing::parse_int(s), shttps::Error);
}

TEST(Parsing, ParseFloatInteger)
{
    std::string s = "42";
    EXPECT_FLOAT_EQ(shttps::Parsing::parse_float(s), 42.0f);
}

TEST(Parsing, ParseFloatDecimal)
{
    std::string s = "3.14";
    EXPECT_FLOAT_EQ(shttps::Parsing::parse_float(s), 3.14f);
}

TEST(Parsing, ParseFloatZero)
{
    std::string s = "0";
    EXPECT_FLOAT_EQ(shttps::Parsing::parse_float(s), 0.0f);
}

TEST(Parsing, ParseFloatInvalidThrows)
{
    std::string s = "abc";
    EXPECT_THROW(shttps::Parsing::parse_float(s), shttps::Error);
}

TEST(Parsing, ParseMimetypeSimple)
{
    auto [mime, charset] = shttps::Parsing::parseMimetype("text/html");
    EXPECT_EQ(mime, "text/html");
    EXPECT_EQ(charset, "");
}

TEST(Parsing, ParseMimetypeWithCharset)
{
    auto [mime, charset] = shttps::Parsing::parseMimetype("text/html; charset=utf-8");
    EXPECT_EQ(mime, "text/html");
    EXPECT_EQ(charset, "utf-8");
}

TEST(Parsing, ParseMimetypeWithQuotedCharset)
{
    auto [mime, charset] = shttps::Parsing::parseMimetype("text/html; charset=\"utf-8\"");
    EXPECT_EQ(mime, "text/html");
    EXPECT_EQ(charset, "utf-8");
}

TEST(Parsing, ParseMimetypeUpperCaseNormalized)
{
    auto [mime, charset] = shttps::Parsing::parseMimetype("TEXT/HTML");
    EXPECT_EQ(mime, "text/html");
}

TEST(Parsing, ParseMimetypeInvalidThrows)
{
    EXPECT_THROW(shttps::Parsing::parseMimetype(""), shttps::Error);
}

TEST(Parsing, GetFileMimetypeFromShortLivedThreadsDoesNotLeak)
{
    namespace fs = std::filesystem;

    const char *tmp_env = std::getenv("TEST_TMPDIR");
    fs::path tmp_dir = tmp_env != nullptr ? fs::path(tmp_env) : fs::temp_directory_path();
    fs::path png_path = tmp_dir / "parsing_test_minimal.png";

    // Minimal valid PNG: 8-byte signature + a single 1x1 8-bit truecolor IHDR chunk.
    static constexpr std::array<uint8_t, 33> png_bytes = { 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00,
        0x00, 0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x02, 0x00, 0x00,
        0x00, 0x90, 0x77, 0x53, 0xDE };

    {
        std::ofstream out(png_path, std::ios::binary);
        out.write(reinterpret_cast<const char *>(png_bytes.data()), png_bytes.size());
    }

    constexpr int kThreadCount = 64;
    std::vector<std::string> results(kThreadCount);
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    // Spawn every thread before joining any of them so each hits a fresh
    // thread_local handle concurrently, rather than one thread at a time.
    for (int i = 0; i < kThreadCount; ++i) {
        threads.emplace_back([&results, i, path = png_path.string()]() {
            try {
                results[i] = shttps::Parsing::getFileMimetype(path).first;
            } catch (const std::exception &e) {
                results[i] = e.what();
            } catch (...) {
                results[i] = "unknown error";
            }
        });
    }

    for (auto &t : threads) { t.join(); }

    for (int i = 0; i < kThreadCount; ++i) { EXPECT_EQ(results[i], "image/png"); }
}
