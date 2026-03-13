#include "gtest/gtest.h"

#include "shttps/Hash.h"

TEST(Hash, Md5KnownValue)
{
    shttps::Hash h(shttps::md5);
    std::string data = "hello world";
    h.add_data(data.c_str(), data.size());
    // MD5 of "hello world" = 5eb63bbbe01eeed093cb22bb8f5acdc3
    EXPECT_EQ(h.hash(), "5eb63bbbe01eeed093cb22bb8f5acdc3");
}

TEST(Hash, Sha256KnownValue)
{
    shttps::Hash h(shttps::sha256);
    std::string data = "hello world";
    h.add_data(data.c_str(), data.size());
    // SHA-256 of "hello world"
    EXPECT_EQ(h.hash(), "b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9");
}

TEST(Hash, Sha1KnownValue)
{
    shttps::Hash h(shttps::sha1);
    std::string data = "hello world";
    h.add_data(data.c_str(), data.size());
    // SHA-1 of "hello world"
    EXPECT_EQ(h.hash(), "2aae6c35c94fcfb415dbe95f408b9ce91ee846ed");
}

TEST(Hash, Sha512KnownValue)
{
    shttps::Hash h(shttps::sha512);
    std::string data = "hello world";
    h.add_data(data.c_str(), data.size());
    // SHA-512 produces 128 hex chars
    std::string result = h.hash();
    EXPECT_EQ(result.size(), 128u);
    // Known SHA-512 prefix
    EXPECT_EQ(result.substr(0, 16), "309ecc489c12d6eb");
}

TEST(Hash, EmptyInput)
{
    shttps::Hash h(shttps::md5);
    std::string data;
    h.add_data(data.c_str(), data.size());
    // MD5 of empty string = d41d8cd98f00b204e9800998ecf8427e
    EXPECT_EQ(h.hash(), "d41d8cd98f00b204e9800998ecf8427e");
}

TEST(Hash, IncrementalHashing)
{
    // Hash computed in two parts should equal hash of full string
    shttps::Hash h1(shttps::sha256);
    h1.add_data("hello ", 6);
    h1.add_data("world", 5);
    std::string incremental = h1.hash();

    shttps::Hash h2(shttps::sha256);
    h2.add_data("hello world", 11);
    std::string full = h2.hash();

    EXPECT_EQ(incremental, full);
}
