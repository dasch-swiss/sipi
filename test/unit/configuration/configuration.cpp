#include "gtest/gtest.h"

#include "../../../include/SipiConf.h"
#include "../../../shttps/LuaServer.h"

#include <stdio.h>
#include <unistd.h>
#define GetCurrentDir getcwd
#include <iostream>

// helper function to find out what the current working directory is
std::string GetCurrentWorkingDir(void)
{
  char buff[FILENAME_MAX];
  GetCurrentDir(buff, FILENAME_MAX);
  std::string current_working_dir(buff);
  return current_working_dir;
}

// helper function to check if file exist
inline bool exists_file(const std::string &name)
{
  struct stat buffer;
  return (stat(name.c_str(), &buffer) == 0);
}

std::string configfile = "../../../../config/sipi.config.lua";

// Check if configuration file can be found
TEST(Configuration, CheckIfConfigurationFileCanBeFound)
{
  // std::cout << GetCurrentWorkingDir() << std::endl;

  EXPECT_TRUE(exists_file(configfile));
}

// Load the configuration file
TEST(Configuration, LoadConfigurationFile)
{
  // read and parse the config file (config file is a lua script)
  shttps::LuaServer luacfg(configfile);

  // store the config option in a SipiConf obj
  Sipi::SipiConf sipiConf(luacfg);

  EXPECT_EQ(sipiConf.getHostname(), "localhost");
  EXPECT_EQ(sipiConf.getPort(), 1024);
  EXPECT_EQ(sipiConf.getSSLPort(), 1025);
  EXPECT_EQ(sipiConf.getNThreads(), 8);
  EXPECT_EQ(sipiConf.getJpegQuality(), 60);
  EXPECT_EQ(sipiConf.getKeepAlive(), 5);
  EXPECT_EQ(sipiConf.getMaxPostSize(), 300 * 1024 * 1024);
  EXPECT_EQ(sipiConf.getImgRoot(), "./images");
  EXPECT_EQ(sipiConf.getPrefixAsPath(), false);
  EXPECT_EQ(sipiConf.getSubdirLevels(), 0);
  EXPECT_EQ(sipiConf.getSubdirExcludes().size(), 2);
  EXPECT_EQ(sipiConf.getInitScript(), "./config/sipi.init.lua");
  EXPECT_EQ(sipiConf.getCacheDir(), "./cache");
  EXPECT_EQ(sipiConf.getCacheSize(), 20 * 1024 * 1024);
  EXPECT_EQ(sipiConf.getCacheNFiles(), 8);
  EXPECT_EQ(sipiConf.getCacheHysteresis(), (float)0.15);
  EXPECT_EQ(sipiConf.getScriptDir(), "./scripts");
  EXPECT_EQ(sipiConf.getThumbSize(), "!128,128");
  EXPECT_EQ(sipiConf.getTmpDir(), "/tmp");
  EXPECT_EQ(sipiConf.getLoglevel(), "DEBUG");
}
