#include "gtest/gtest.h"

#include "util/PathRedact.h"

TEST(PathRedact, QuotedPathIsRedactedWithBalancedQuotes)
{
    std::string msg = "Cannot read file \"/srv/images/sub/foo.jp2\": broken";
    EXPECT_EQ(Sipi::redact_paths(msg), "Cannot read file \"foo.jp2\": broken");
}

TEST(PathRedact, UnquotedPathIsRedactedToFilename)
{
    std::string msg = "Cannot read file /srv/images/sub/foo.jp2: broken";
    EXPECT_EQ(Sipi::redact_paths(msg), "Cannot read file foo.jp2: broken");
}

TEST(PathRedact, TokenWithoutSlashIsUnchanged)
{
    std::string msg = "broken: no such file";
    EXPECT_EQ(Sipi::redact_paths(msg), "broken: no such file");
}

TEST(PathRedact, WhitespaceSeparatorsArePreservedAcrossTokens)
{
    std::string msg = "Cannot  read\tfile \"/srv/images/sub/foo.jp2\":  broken";
    EXPECT_EQ(Sipi::redact_paths(msg), "Cannot  read\tfile \"foo.jp2\":  broken");
}

TEST(PathRedact, SingleQuotedPathIsRedactedWithBalancedQuotes)
{
    std::string msg = "Cannot read file '/srv/images/sub/foo.jp2': broken";
    EXPECT_EQ(Sipi::redact_paths(msg), "Cannot read file 'foo.jp2': broken");
}
