#include "gtest/gtest.h"

#include "shttps/Parsing.h"
#include "shttps/Error.h"

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
