/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * \brief Implements an IIIF server with many features.
 *
 */
#include <csignal>
#include <cstdlib>
#include <dirent.h>
#include <execinfo.h>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <syslog.h>


#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include <curl/curl.h>
#include <jansson.h>
#include <CLI11.hpp>

#include "otel.hpp"

#include "shttps/LuaServer.h"
#include "shttps/LuaSqlite.h"
#include "shttps/Parsing.h"
#include "shttps/Server.h"

#include "SipiConf.h"
#include "SipiFilenameHash.h"
#include "SipiHttpServer.hpp"
#include "SipiIO.h"
#include "SipiImage.hpp"
#include "SipiImageError.hpp"
#include "SipiLua.h"
#include "formats/SipiIOTiff.h"

#include "generated/SipiVersion.h"

/*!
 * \mainpage
 *
 * # Sipi – Simple Image Presentation Interface #
 *
 * Sipi is a package that can be used to convert images from/to different formats while
 * preserving as much metadata thats embeded in the file headers a possible. Sipi is also
 * able to do some conversions, especially some common color space transformation using
 * ICC profiles. Currently Sipi supports the following file formats
 *
 * - TIFF
 * - JPEG2000
 * - PNG
 * - JPEG
 *
 * The following metadata "standards" are beeing preserved
 * - EXIF
 * - IPTC
 * - XMP
 *
 * ## Commandline Use ##
 *
 * For simple conversions, Sipi is being used from the command line (in a terminal window). The
 * format is usually
 *
 *     sipi [options] <infile> <outfile>
 *
 */


namespace {

void sipiConfGlobals(lua_State *L, shttps::Connection &conn, void *user_data)
{
  auto *conf = (Sipi::SipiConf *)user_data;

  lua_createtable(L, 0, 14);// table1

  lua_pushstring(L, "hostname");// table1 - "index_L1"
  lua_pushstring(L, conf->getHostname().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "port");// table1 - "index_L1"
  lua_pushinteger(L, conf->getPort());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "sslport");// table1 - "index_L1"
  lua_pushinteger(L, conf->getSSLPort());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "imgroot");// table1 - "index_L1"
  lua_pushstring(L, conf->getImgRoot().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "max_temp_file_age");// table1 - "index_L1"
  lua_pushinteger(L, conf->getMaxTempFileAge());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "prefix_as_path");// table1 - "index_L1"
  lua_pushboolean(L, conf->getPrefixAsPath());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "init_script");// table1 - "index_L1"
  lua_pushstring(L, conf->getInitScript().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "cache_dir");// table1 - "index_L1"
  lua_pushstring(L, conf->getCacheDir().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "cache_size");// table1 - "index_L1"
  lua_pushinteger(L, conf->getCacheSize());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "cache_hysteresis");// table1 - "index_L1"
  lua_pushnumber(L, conf->getCacheHysteresis());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "jpeg_quality");// table1 - "index_L1"
  lua_pushinteger(L, conf->getJpegQuality());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "keep_alive");// table1 - "index_L1"
  lua_pushinteger(L, conf->getKeepAlive());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "thumb_size");// table1 - "index_L1"
  lua_pushstring(L, conf->getThumbSize().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "cache_n_files");// table1 - "index_L1"
  lua_pushinteger(L, conf->getCacheNFiles());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "n_threads");// table1 - "index_L1"
  lua_pushinteger(L, conf->getNThreads());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "max_post_size");// table1 - "index_L1"
  lua_pushinteger(L, conf->getMaxPostSize());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "tmpdir");// table1 - "index_L1"
  lua_pushstring(L, conf->getTmpDir().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "ssl_certificate");// table1 - "index_L1"
  lua_pushstring(L, conf->getSSLCertificate().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "ssl_key");// table1 - "index_L1"
  lua_pushstring(L, conf->getSSLKey().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "scriptdir");// table1 - "index_L1"
  lua_pushstring(L, conf->getScriptDir().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "logfile");// table1 - "index_L1"
  lua_pushstring(L, conf->getLogfile().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "loglevel");// table1 - "index_L1"
  const std::string loglevel = conf->getLoglevel();
  if (loglevel == "LOG_EMERG") {
    lua_pushinteger(L, LOG_EMERG);
  } else if (loglevel == "LOG_ALERT") {
    lua_pushinteger(L, LOG_ALERT);
  } else if (loglevel == "LOG_CRIT") {
    lua_pushinteger(L, LOG_CRIT);
  } else if (loglevel == "LOG_ERR") {
    lua_pushinteger(L, LOG_ERR);
  } else if (loglevel == "LOG_WARNING") {
    lua_pushinteger(L, LOG_WARNING);
  } else if (loglevel == "LOG_NOTICE") {
    lua_pushinteger(L, LOG_NOTICE);
  } else if (loglevel == "LOG_INFO") {
    lua_pushinteger(L, LOG_INFO);
  } else if (loglevel == "LOG_DEBUG") {
    lua_pushinteger(L, LOG_DEBUG);
  } else {
    lua_pushinteger(L, -1);
  }
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "knora_path");// table1 - "index_L1"
  lua_pushstring(L, conf->getKnoraPath().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "knora_port");// table1 - "index_L1"
  lua_pushstring(L, conf->getKnoraPort().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "adminuser");// table1 - "index_L1"
  lua_pushstring(L, conf->getAdminUser().c_str());
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "password");// table1 - "index_L1"
  lua_pushstring(L, conf->getPassword().c_str());
  lua_rawset(L, -3);// table1

  // TODO: in the sipi config file, there are different namespaces that are unified here (danger of collision)
  lua_pushstring(L, "docroot");// table1 - "index_L1"
  lua_pushstring(L, conf->getDocRoot().c_str());
  lua_rawset(L, -3);// table1

  lua_setglobal(L, "config");
}

// small function to check if file exist
bool exists_file(const std::string &name)
{
  struct stat buffer;
  return (stat(name.c_str(), &buffer) == 0);
}

/*!
 * A singleton that does global initialisation and cleanup of libraries used by Sipi. This class is used only
 * in main().
 *
 * Some libraries such as the ones used for processing XMP or TIFF need some global initialization. These
 * are performed in the constructor of this singleton class
 */
class LibraryInitialiser
{
public:
  /*!
   * @return the singleton instance.
   */
  static LibraryInitialiser &instance()
  {
    // In C++11, initialization of this static local variable happens once and is thread-safe.
    static LibraryInitialiser sipi_init;
    return sipi_init;
  }

private:
  LibraryInitialiser()
  {
    // Initialise libcurl.
    curl_global_init(CURL_GLOBAL_ALL);

    // Initialise Exiv2, registering namespace sipi. Since this is not thread-safe, it must
    // be done here in the main thread.
    if (!Exiv2::XmpParser::initialize(Sipi::xmplock_func, &Sipi::xmp_mutex)) {
      throw shttps::Error("Exiv2::XmpParser::initialize failed");
    }

    // Inititalise the TIFF library.
    Sipi::SipiIOTiff::initLibrary();
  }

  ~LibraryInitialiser()
  {
    // Clean up libcurl.
    curl_global_cleanup();

    // Clean up Exiv2.
    Exiv2::XmpParser::terminate();
  }
};

/*!
 * Print a stack trace.
 *
 * This function is called when a signal is received that would normally terminate the program
 * with a core dump. It logs out a stack trace.
 */
auto get_stack_trace() -> std::string
{
  void *array[15];

  const auto size = backtrace(array, 15);
  char **strings = backtrace_symbols(array, size);

  std::ostringstream errStream;
  errStream << "Obtained " << size << " stack frames.\n";

  for (auto i = 0; i < size; i++) { errStream << strings[i] << '\n'; }

  return errStream.str();
}

/*!
 * Handle a signal.
 *
 * This function is called when a signal is received that would normally terminate the program
 * with a core dump. It prints a stack trace to stderr and exits with an error code.
 *
 * @param sig the signal number.
 */
void sig_handler(const int sig)
{
  std::string msg;
  if (sig == SIGSEGV) {
    msg = "SIGSEGV: segmentation fault.";
  } else if (sig == SIGABRT) {
    msg = "SIGABRT: abort.";
  } else {
    msg = "Caught signal " + std::to_string(sig);
  }

  msg += "\n" + get_stack_trace();
  // TODO: add sig_handler metric: "sig_handler", msg.c_str()
  syslog(LOG_ERR, "%s", msg.c_str());

  exit(1);
}

/*!
 * Handle any unhandled exceptions.
 *
 * This function is called when an unhandled exception is thrown. It logs out the exception
 * and aborts the program.
 */
void my_terminate_handler()
{
  std::string msg;
  try {
    // Rethrow the current exception to identify it
    throw;
  } catch (const std::exception &e) {
    msg = "Unhandled exception caught: " + std::string(e.what());
  } catch (...) {
    msg = "Unhandled unknown exception caught";
  }

  msg += "\n" + get_stack_trace();
  // TODO: add my_terminate_handler metric: "my_terminate_handler", msg.c_str()
  syslog(LOG_ERR, "%s", msg.c_str());

  std::abort();// Abort the program or perform other cleanup
}

enum class OptFormat : int { jpx, jpg, tif, png };
enum class OptIcc : int { none, sRGB, AdobeRGB, GRAY };
enum class OptMirror { none, horizontal, vertical };
enum class LogLevel { DEBUG, INFO, NOTICE, WARNING, ERR, CRIT, ALERT, EMERG };
struct CLIArgumentOptions
{
  std::shared_ptr<CLI::App> sipiopt;
  std::string optConfigfile;
  std::string optInFile;
  std::string optOutFile;
  OptFormat optFormat = OptFormat::jpx;
  OptIcc optIcc = OptIcc::none;
  int optJpegQuality;
  std::string j2k_Sprofile;
  std::vector<std::string> j2k_rates;
  int j2k_Clayers;
  int j2k_Clevels;
  std::string j2k_Corder;
  std::string j2k_Stiles;
  std::string j2k_Cprecincts;
  std::string j2k_Cblk;
  bool j2k_Cuse_sop;
  int optPagenum = 0;
  std::vector<int> optRegion;
  int optReduce = 0;
  std::string optSize;
  int optScale;
  bool optSkipMeta = false;
  OptMirror optMirror = OptMirror::none;
  float optRotate = 0.0;
  bool optSetTopleft = false;
  std::vector<std::string> optCompare;
  std::string optWatermark;
  bool optQuery = false;
  bool optSalsah = false;
  int optServerport = 80;
  int optSSLport = 443;
  std::string optHostname = "localhost";
  int optKeepAlive = 5;
  unsigned int optNThreads = std::thread::hardware_concurrency();
  std::string optMaxPostSize = "300M";
  std::string optImgroot = "./images";
  std::string optDocroot = "./server";
  std::string optWWWRoute = "/server";
  std::string optScriptDir = "./scripts";
  std::string optTmpdir = "./tmp";
  int optMaxTmpAge = 86400;
  bool optPathprefix = false;
  int optSubdirLevels = 0;
  std::vector<std::string> optSubdirExcludes = { "tmp", "thumb" };
  std::string optInitscript = "./config/sipi.init.lua";
  std::string optCachedir = "./cache";
  std::string optCacheSize = "200M";
  int optCacheNFiles = 200;
  double optCacheHysteresis = 0.15;
  std::string optThumbSize = "!128,128";
  std::string optSSLCertificatePath = "./certificate/certificate.pem";
  std::string optSSLKeyPath = "./certificate/key.pem";
  std::string optJWTKey = "UP 4888, nice 4-8-4 steam engine";
  std::string optAdminUser = "admin";
  std::string optAdminPassword = "Sipi-Admin";
  std::string optKnoraPath = "localhost";
  std::string optKnoraPort = "3434";
  std::string optLogfilePath = "Sipi";
  LogLevel optLogLevel = LogLevel::DEBUG;
  std::string optSipiSentryDsn;
  std::string optSipiSentryRelease;
  std::string optSipiSentryEnvironment;
};

/**
 * return CLIArgumentOptionsResult containing an instance of the CLI::App class that is used to parse the command line
 * arguments. Note: shared_ptr is used because the CLI::App class does not implement move semantics.
 */
CLIArgumentOptions get_cli_args()
{
  const auto app = std::make_shared<CLI::App>(
    "SIPI is a image format converter and - if started in server mode - a high performance IIIF server");

  CLIArgumentOptions result;

  app->add_option("-c,--config", result.optConfigfile, "Configuration file for web server.")
    ->envname("SIPI_CONFIGFILE")
    ->check(CLI::ExistingFile);
  ;

  app->add_option("-f,--file,--inf,infile", result.optInFile, "Input file to be converted.")->check(CLI::ExistingFile);
  ;

  app->add_option("-z,--outf,outfile", result.optOutFile, "Output file to be converted.");

  std::vector<std::pair<std::string, OptFormat>> optFormatMap{ { "jpx", OptFormat::jpx },
    { "jp2", OptFormat::jpx },
    { "jpg", OptFormat::jpg },
    { "tif", OptFormat::tif },
    { "png", OptFormat::png } };
  app->add_option("-F,--format", result.optFormat, "Output format.")
    ->transform(CLI::CheckedTransformer(optFormatMap, CLI::ignore_case));


  std::vector<std::pair<std::string, OptIcc>> optIccMap{
    { "none", OptIcc::none }, { "sRGB", OptIcc::sRGB }, { "AdobeRGB", OptIcc::AdobeRGB }, { "GRAY", OptIcc::GRAY }
  };
  app->add_option("-I,--icc", result.optIcc, "Convert to ICC profile.")
    ->transform(CLI::CheckedTransformer(optIccMap, CLI::ignore_case));

  app->add_option("-q,--quality", result.optJpegQuality, "Quality (compression).")
    ->check(CLI::Range(1, 100))
    ->envname("SIPI_JPEGQUALITY");

  //
  // Parameters for JPEG2000 compression (see kakadu kdu_compress for details!)
  //
  app
    ->add_option(
      "--Sprofile", result.j2k_Sprofile, "Restricted profile to which the code-stream conforms [Default: PART2].")
    ->check(CLI::IsMember({ "PROFILE0",
                            "PROFILE1",
                            "PROFILE2",
                            "PART2",
                            "CINEMA2K",
                            "CINEMA4K",
                            "BROADCAST",
                            "CINEMA2S",
                            "CINEMA4S",
                            "CINEMASS",
                            "IMF" },
      CLI::ignore_case));

  app->add_option("--rates",
    result.j2k_rates,
    "One or more bit-rates (see kdu_compress help!). A value "
    "\"-1\" may be used in place of the first bit-rate in the list to indicate "
    "that the final quality layer should include all compressed bits.");

  app->add_option("--Clayers", result.j2k_Clayers, "J2K: Number of quality layers [Default: 8].");

  app->add_option(
    "--Clevels", result.j2k_Clevels, "J2K: Number of wavelet decomposition levels, or stages [default: 8].");

  app
    ->add_option("--Corder",
      result.j2k_Corder,
      "J2K: Progression order. The four character identifiers have the following interpretation: "
      "L=layer; R=resolution; C=component; P=position. The first character in the identifier refers to the "
      "index which progresses most slowly, while the last refers to the index which progresses most quickly [Default: "
      "RPCL].")
    ->check(CLI::IsMember({ "LRCP", "RLCP", "RPCL", "PCRL", "CPRL" }, CLI::ignore_case));

  app->add_option("--Stiles", result.j2k_Stiles, "J2K: Tiles dimensions \"{tx,ty} [Default: {256,256}]\".");

  app->add_option("--Cprecincts",
    result.j2k_Cprecincts,
    "J2K: Precinct dimensions \"{px,py}\" (must be powers of 2) [Default: {256,256}].");

  app->add_option("--Cblk",
    result.j2k_Cblk,
    "J2K: Nominal code-block dimensions (must be powers of 2, no less than 4 and "
    "no greater than 1024, whose product may not exceed 4096) [Default: {64,64}].");

  app->add_option(
    "--Cuse_sop", result.j2k_Cuse_sop, "J2K Cuse_sop: Include SOP markers (i.e., resync markers) [Default: yes].");

  //
  // used for rendering only one page of multipage PDF or TIFF (NYI for tif...)
  //
  app->add_option("-n,--pagenum", result.optPagenum, "Pagenumber for PDF documents or multipage TIFFs.");

  app->add_option("-r,--region", result.optRegion, "Select region of interest, where x y w h are integer values.")
    ->expected(4);

  app->add_option(
    "-R,--reduce", result.optReduce, "Reduce image size by factor  (cannot be used together with --size and --scale)");

  app->add_option(
    "-s,--size", result.optSize, "Resize image to given size (cannot be used together with --reduce and --scale)");

  app->add_option("-S,--scale",
    result.optScale,
    "Resize image by the given percentage Value (cannot be used together with --size and --reduce)");

  app->add_flag("-k,--skipmeta", result.optSkipMeta, "Skip metadata of original file if flag is present.");


  std::vector<std::pair<std::string, OptMirror>> optMirrorMap{
    { "none", OptMirror::none }, { "horizontal", OptMirror::horizontal }, { "vertical", OptMirror::vertical }
  };
  app->add_option("-m,--mirror", result.optMirror, "Mirror the image. Value can be: 'none', 'horizontal', 'vertical'.")
    ->transform(CLI::CheckedTransformer(optMirrorMap, CLI::ignore_case));

  app->add_option("-o,--rotate", result.optRotate, "Rotate the image. by degree Value, angle between (0.0 - 360.0).");

  app->add_flag("--topleft", result.optSetTopleft, "Enforce orientation TOPLEFT.");

  app->add_option("-C,--compare", result.optCompare, "Compare two files.")->expected(2);

  app->add_option("-w,--watermark", result.optWatermark, "Add a watermark to the image.");

  app->add_flag("-x,--query", result.optQuery, "Dump all information about the given file.");

  app->add_flag("-a,--salsah", result.optSalsah, "Special optioons for conversions in old salsah.");

  //
  // below are server options
  //
  app->add_option("--serverport", result.optServerport, "Port of SIPI web server.")->envname("SIPI_SERVERPORT");

  app->add_option("--sslport", result.optSSLport, "SSL-port of the SIPI server.")->envname("SIPI_SSLPORT");

  app->add_option("--hostname", result.optHostname, "Hostname to use for HTTP server.")->envname("SIPI_HOSTNAME");

  app->add_option("--keepalive", result.optKeepAlive, "Number of seconds for the keeop-alive optioon of HTTP 1.1.")
    ->envname("SIPI_KEEPALIVE");

  app->add_option("-t,--nthreads", result.optNThreads, "Number of threads for SIPI server")->envname("SIPI_NTHREADS");

  app
    ->add_option(
      "--maxpost", result.optMaxPostSize, "A string indicating the maximal size of a POST request, e.g. '300M'.")
    ->envname("SIPI_MAXPOSTSIZE");

  app->add_option("--imgroot", result.optImgroot, "Root directory containing the images for the web server.")
    ->envname("SIPI_IMGROOT")
    ->check(CLI::ExistingDirectory);

  app->add_option("--docroot", result.optDocroot, "Path to document root for normal webserver.")
    ->envname("SIPI_DOCROOT")
    ->check(CLI::ExistingDirectory);

  app->add_option("--wwwroute", result.optWWWRoute, "URL route for standard webserver.")->envname("SIPI_WWWROUTE");

  app->add_option("--scriptdir", result.optScriptDir, "Path to directory containing Lua scripts to implement routes.")
    ->envname("SIPI_SCRIPTDIR")
    ->check(CLI::ExistingDirectory);

  app->add_option("--tmpdir", result.optTmpdir, "Path to the temporary directory (e.g. for uploads etc.).")
    ->envname("SIPI_TMPDIR")
    ->check(CLI::ExistingDirectory);

  app
    ->add_option("--maxtmpage",
      result.optMaxTmpAge,
      "The maximum allowed age of temporary files (in seconds) before they are deleted.")
    ->envname("SIPI_MAXTMPAGE");

  app
    ->add_flag("--pathprefix",
      result.optPathprefix,
      "Flag, if set indicates that the IIIF prefix is part of the path to the image file (deprecated).")
    ->envname("SIPI_PATHPREFIX");

  app->add_option("--subdirlevels", result.optSubdirLevels, "Number of subdir levels (deprecated).")
    ->envname("SIPI_SUBDIRLEVELS");

  app->add_option("--subdirexcludes", result.optSubdirExcludes, "Directories not included in subdir calculations.")
    ->envname("SIPI_SUBDIREXCLUDES");

  app->add_option("--initscript", result.optInitscript, "Path to init script (Lua).")
    ->envname("SIPI_INITSCRIPT")
    ->check(CLI::ExistingFile);

  app->add_option("--cachedir", result.optCachedir, "Path to cache folder.")->envname("SIPI_CACHEDIR");

  app->add_option("--cachesize", result.optCacheSize, "Maximal size of cache, e.g. '500M'.")->envname("SIPI_CACHESIZE");

  app->add_option("--cachenfiles", result.optCacheNFiles, "The maximal number of files to be cached.")
    ->envname("SIPI_CACHENFILES");

  app
    ->add_option("--cachehysteresis",
      result.optCacheHysteresis,
      "If the cache becomes full, the given percentage of file space is marked for reuse (0.0 - 1.0).")
    ->envname("SIPI_CACHEHYSTERESIS");

  app->add_option("--thumbsize", result.optThumbSize, "Size of the thumbnails (to be used within Lua).")
    ->envname("SIPI_THUMBSIZE");

  app->add_option("--sslcert", result.optSSLCertificatePath, "Path to SSL certificate.")
    ->envname("SIPI_SSLCERTIFICATE");

  app->add_option("--sslkey", result.optSSLKeyPath, "Path to the SSL key file.")->envname("SIPI_SSLKEY");

  app
    ->add_option(
      "--jwtkey", result.optJWTKey, "The secret for generating JWT's (JSON Web Tokens) (exactly 42 characters).")
    ->envname("SIPI_JWTKEY");

  app->add_option("--adminuser", result.optAdminUser, "Username for SIPI admin user.")->envname("SIPI_ADMIINUSER");

  app->add_option("--adminpasswd", result.optAdminPassword, "Password of the admin user.")->envname("SIPI_ADMINPASSWD");

  app->add_option("--knorapath", result.optKnoraPath, "Path to Knora server.")->envname("SIPI_KNORAPATH");

  app->add_option("--knoraport", result.optKnoraPort, "Portnumber for Knora.")->envname("SIPI_KNORAPORT");

  app->add_option("--logfile", result.optLogfilePath, "Name of the logfile (NYI).")->envname("SIPI_LOGFILE");


  std::vector<std::pair<std::string, LogLevel>> logLevelMap{ { "DEBUG", LogLevel::DEBUG },
    { "INFO", LogLevel::INFO },
    { "NOTICE", LogLevel::NOTICE },
    { "WARNING", LogLevel::WARNING },
    { "ERR", LogLevel::ERR },
    { "CRIT", LogLevel::CRIT },
    { "ALERT", LogLevel::ALERT },
    { "EMERG", LogLevel::EMERG } };
  app
    ->add_option("--loglevel",
      result.optLogLevel,
      "Logging level Value can be: 'DEBUG', 'INFO', 'WARNING', 'ERR', 'CRIT', 'ALERT', 'EMERG'.")
    ->transform(CLI::CheckedTransformer(logLevelMap, CLI::ignore_case))
    ->envname("SIPI_LOGLEVEL");

  // sentry configuration
  app->add_option("--sentry-dsn", result.optSipiSentryDsn)->envname("SIPI_SENTRY_DSN");

  app->add_option("--sentry-release", result.optSipiSentryRelease)->envname("SIPI_SENTRY_RELEASE");

  app->add_option("--sentry-environment", result.optSipiSentryEnvironment)->envname("SIPI_SENTRY_ENVIRONMENT");

  result.sipiopt = app;

  return result;
}
/**
 * Query the image file for all information.
 */
void query_command(CLIArgumentOptions const &cli_args)
{
  //
  // we query all information from just one file
  //
  Sipi::SipiImage img;
  img.read(cli_args.optInFile);
  std::cout << img << std::endl;
}

/**
 * Compare two image files.
 */
int compare_command(CLIArgumentOptions const &cli_args)
{
  //
  // command line function: we want to compare pixelwise to files. After having done this, we exit
  //
  if (!exists_file(cli_args.optCompare[0])) {
    std::cerr << "File not found: " << cli_args.optCompare[0] << std::endl;
    return EXIT_FAILURE;
  }

  if (!exists_file(cli_args.optCompare[1])) {
    std::cerr << "File not found: " << cli_args.optCompare[1] << std::endl;
    return EXIT_FAILURE;
  }

  Sipi::SipiImage img1, img2;
  img1.read(cli_args.optCompare[0]);
  img2.read(cli_args.optCompare[1]);
  bool result = img1 == img2;

  if (!result) {
    img1 -= img2;
    img1.write("tif", "diff.tif");
  }

  if (result) {
    std::cerr << "Files identical!" << std::endl;
  } else {
    double diffval = 0.;
    size_t maxdiff = 0;
    size_t max_x, max_y;
    for (size_t y = 0; y < img1.getNy(); y++) {
      for (size_t x = 0; x < img1.getNx(); x++) {
        for (size_t c = 0; c < img1.getNc(); c++) {
          size_t dv = img1.getPixel(x, y, c) - img2.getPixel(x, y, c);
          if (dv > maxdiff) {
            maxdiff = dv;
            max_x = x;
            max_y = y;
          }
          diffval += static_cast<float>(dv);
        }
      }
    }
    diffval /= static_cast<double>(img1.getNy() * img1.getNx() * img1.getNc());
    std::cerr << "Files differ: avg: " << diffval << " max: " << maxdiff << "(" << max_x << ", " << max_y
              << ") See diff.tif" << std::endl;
  }

  return (result) ? 0 : -1;
}

/**
 * Convert an image file.
 */
int convert_command(CLIArgumentOptions const &cli_args)
{
  //
  // Commandline conversion with input and output file given
  //

  //
  // get the output format
  //
  std::string format("jpg");
  if (!cli_args.sipiopt->get_option("--format")->empty()) {
    switch (cli_args.optFormat) {
    case OptFormat::jpx:
      format = "jpx";
      break;
    case OptFormat::jpg:
      format = "jpg";
      break;
    case OptFormat::tif:
      format = "tif";
      break;
    case OptFormat::png:
      format = "png";
      break;
    }
  } else {
    //
    // there is no format option given – we try to determine the format
    // from the output name extension
    //
    size_t pos = cli_args.optOutFile.rfind('.');
    if (pos != std::string::npos) {
      std::string ext = cli_args.optOutFile.substr(pos + 1);
      if ((ext == "jpx") || (ext == "jp2")) {
        format = "jpx";
      } else if ((ext == "tif") || (ext == "tiff")) {
        format = "tif";
      } else if ((ext == "jpg") || (ext == "jpeg")) {
        format = "jpg";
      } else if (ext == "png") {
        format = "png";
      } else {
        std::cerr << "Not a supported filename extension: '" << ext << "' !" << std::endl;
        return EXIT_FAILURE;
      }
    }
  }

  //
  // getting information about a region of interest
  //
  std::shared_ptr<Sipi::SipiRegion> region = nullptr;
  if (!cli_args.sipiopt->get_option("--region")->empty()) {
    region = std::make_shared<Sipi::SipiRegion>(
      cli_args.optRegion.at(0), cli_args.optRegion.at(1), cli_args.optRegion.at(2), cli_args.optRegion.at(3));
  }

  //
  // get the reduce parameter
  // "reduce" is a special feature of the JPEG2000 format. It is possible (given the JPEG2000 format
  // is written a resolution pyramid). reduce=0 results in full resolution, reduce=1 is half the resolution
  // etc.
  //
  std::shared_ptr<Sipi::SipiSize> size = nullptr;
  if (cli_args.optReduce > 0) {
    size = std::make_shared<Sipi::SipiSize>(cli_args.optReduce);
  } else if (!cli_args.sipiopt->get_option("--size")->empty()) {
    try {
      size = std::make_shared<Sipi::SipiSize>(cli_args.optSize);
    } catch (std::exception &e) {
      syslog(LOG_ERR, "Error in size parameter: %s", e.what());
      return EXIT_FAILURE;
    }
  } else if (!cli_args.sipiopt->get_option("--scale")->empty()) {
    try {
      size = std::make_shared<Sipi::SipiSize>(cli_args.optScale);
    } catch (std::exception &e) {
      syslog(LOG_ERR, "Error in scale parameter: %s", e.what());
      return EXIT_FAILURE;
    }
  }

  //
  // read the input image
  //
  Sipi::SipiImage img;
  try {
    img.readOriginal(cli_args.optInFile,
      region,
      size,
      shttps::HashType::sha256);// convert to bps=8 in case of JPG output
    // img.read(optInFile); //convert to bps=8 in case of JPG output
    if (format == "jpg") {
      img.to8bps();
      img.convertToIcc(Sipi::SipiIcc(Sipi::PredefinedProfiles::icc_sRGB), 8);
    }
  } catch (Sipi::SipiImageError &err) {
    std::cerr << err << std::endl;
  }

  //
  // enforce orientation topleft?
  //
  if (!cli_args.sipiopt->get_option("--topleft")->empty()) {
    Sipi::Orientation orientation = img.getOrientation();
    std::shared_ptr<Sipi::SipiExif> exif = img.getExif();
    if (exif != nullptr) {
      unsigned short ori;
      if (exif->getValByKey("Exif.Image.Orientation", ori)) { orientation = static_cast<Sipi::Orientation>(ori); }
    }
    switch (orientation) {
    case Sipi::TOPLEFT:// 1
      break;
    case Sipi::TOPRIGHT:// 2
      img.rotate(0., true);
      break;
    case Sipi::BOTRIGHT:// 3
      img.rotate(180., false);
      break;
    case Sipi::BOTLEFT:// 4
      img.rotate(180., true);
      break;
    case Sipi::LEFTTOP:// 5
      img.rotate(270., true);
      break;
    case Sipi::RIGHTTOP:// 6
      img.rotate(90., false);
      break;
    case Sipi::RIGHTBOT:// 7
      img.rotate(90., true);
      break;
    case Sipi::LEFTBOT:// 8
      img.rotate(270., false);
      break;
    default:;// nothing to do...
    }
    exif->addKeyVal("Exif.Image.Orientation", static_cast<unsigned short>(Sipi::TOPLEFT));
    img.setOrientation(Sipi::TOPLEFT);

    orientation = img.getOrientation();
    std::shared_ptr<Sipi::SipiExif> exif2 = img.getExif();
    if (exif2 != nullptr) {
      unsigned short ori;
      if (exif2->getValByKey("Exif.Image.Orientation", ori)) { orientation = static_cast<Sipi::Orientation>(ori); }
    }
  }

  //
  // if we want to remove all metadata from the file...
  //
  std::string skipmeta("none");

  if (!cli_args.sipiopt->get_option("--skipmeta")->empty()) { img.setSkipMetadata(Sipi::SkipMetadata::SKIP_ALL); }

  //
  // color profile processing
  //
  if (!cli_args.sipiopt->get_option("--icc")->empty()) {
    Sipi::SipiIcc icc;
    switch (cli_args.optIcc) {
    case OptIcc::sRGB:
      icc = Sipi::SipiIcc(Sipi::PredefinedProfiles::icc_sRGB);
      break;
    case OptIcc::AdobeRGB:
      icc = Sipi::SipiIcc(Sipi::PredefinedProfiles::icc_AdobeRGB);
      break;
    case OptIcc::GRAY:
      icc = Sipi::SipiIcc(Sipi::PredefinedProfiles::icc_GRAY_D50);
      break;
    case OptIcc::none:
      break;
    }
    img.convertToIcc(icc, img.getBps());
  }

  //
  // mirroring and rotation
  //
  if (!(cli_args.sipiopt->get_option("--mirror")->empty() && cli_args.sipiopt->get_option("--rotate")->empty())) {
    switch (cli_args.optMirror) {
    case OptMirror::vertical: {
      img.rotate(cli_args.optRotate + 180.0F, true);
      break;
    }
    case OptMirror::horizontal: {
      img.rotate(cli_args.optRotate, true);
      break;
    }
    case OptMirror::none: {
      if (cli_args.optRotate != 0.0F) { img.rotate(cli_args.optRotate, false); }
      break;
    }
    }
  }

  if (!cli_args.sipiopt->get_option("--watermark")->empty()) { img.add_watermark(cli_args.optWatermark); }

  //
  // write the output file
  //
  // int quality = 80
  Sipi::SipiCompressionParams comp_params;
  if (!cli_args.sipiopt->get_option("--quality")->empty()) {
    comp_params[Sipi::JPEG_QUALITY] = cli_args.optJpegQuality;
  }
  if (!cli_args.sipiopt->get_option("--Sprofile")->empty()) { comp_params[Sipi::J2K_Sprofile] = cli_args.j2k_Sprofile; }
  if (!cli_args.sipiopt->get_option("--Clayers")->empty()) {
    comp_params[Sipi::J2K_Clayers] = std::to_string(cli_args.j2k_Clayers);
  }
  if (!cli_args.sipiopt->get_option("--Clevels")->empty()) {
    comp_params[Sipi::J2K_Clevels] = std::to_string(cli_args.j2k_Clevels);
  }
  if (!cli_args.sipiopt->get_option("--Corder")->empty()) { comp_params[Sipi::J2K_Corder] = cli_args.j2k_Corder; }
  if (!cli_args.sipiopt->get_option("--Cprecincts")->empty()) {
    comp_params[Sipi::J2K_Cprecincts] = cli_args.j2k_Cprecincts;
  }
  if (!cli_args.sipiopt->get_option("--Cblk")->empty()) { comp_params[Sipi::J2K_Cblk] = cli_args.j2k_Cblk; }
  if (!cli_args.sipiopt->get_option("--Cuse_sop")->empty()) {
    comp_params[Sipi::J2K_Cuse_sop] = cli_args.j2k_Cuse_sop ? "yes" : "no";
  }
  if (!cli_args.sipiopt->get_option("--Stiles")->empty()) { comp_params[Sipi::J2K_Stiles] = cli_args.j2k_Stiles; }
  if (!cli_args.sipiopt->get_option("--rates")->empty()) {
    std::stringstream ss;
    for (auto &rate : cli_args.j2k_rates) {
      if (rate == "X") {
        ss << "-1.0 ";
      } else {
        ss << rate << " ";
      }
    }
    comp_params[Sipi::J2K_rates] = ss.str();
  }

  try {
    img.write(format, cli_args.optOutFile, &comp_params);
  } catch (Sipi::SipiImageError &err) {
    std::cerr << err << '\n';
  }

  if (!cli_args.sipiopt->get_option("--salsah")->empty()) {
    std::cout << img.getNx() << " " << img.getNy() << std::endl;
  }
  return EXIT_SUCCESS;
}

/**
 * Server Command
 */
int server_command(CLIArgumentOptions cli_args)
{

  std::cout << std::endl << "Ivan was here" << std::endl;

  Sipi::SipiConf sipiConf;
  bool config_loaded = false;
  if (!cli_args.sipiopt->get_option("--config")->empty()) {
    // read and parse the config file (config file is a lua script)
    shttps::LuaServer luacfg(cli_args.optConfigfile);

    // store the config option in a SipiConf obj
    sipiConf = Sipi::SipiConf(luacfg);
    config_loaded = true;
  }

  //
  // now we check for all commandline/environment variables and update sipiConf.
  //
  if (!config_loaded) {
    sipiConf.setPort(cli_args.optServerport);
  } else {
    if (!cli_args.sipiopt->get_option("--serverport")->empty()) { sipiConf.setPort(cli_args.optServerport); }
  }

  if (!config_loaded) {
    sipiConf.setSSLPort(cli_args.optSSLport);
  } else {
    if (!cli_args.sipiopt->get_option("--sslport")->empty()) { sipiConf.setSSLPort(cli_args.optSSLport); }
  }

  if (!config_loaded) {
    sipiConf.setHostname(cli_args.optHostname);
  } else {
    if (!cli_args.sipiopt->get_option("--hostname")->empty()) { sipiConf.setHostname(cli_args.optHostname); }
  }

  if (!config_loaded) {
    sipiConf.setKeepAlive(cli_args.optKeepAlive);
  } else {
    if (!cli_args.sipiopt->get_option("--keepalive")->empty()) sipiConf.setKeepAlive(cli_args.optKeepAlive);
  }

  if (!config_loaded) {
    sipiConf.setNThreads(cli_args.optNThreads);
  } else {
    if (!cli_args.sipiopt->get_option("--nthreads")->empty()) sipiConf.setNThreads(cli_args.optNThreads);
  }

  size_t l = cli_args.optMaxPostSize.length();
  char c = cli_args.optMaxPostSize[l - 1];
  tsize_t maxpost_size;
  if (c == 'M') {
    maxpost_size = stoll(cli_args.optMaxPostSize.substr(0, l - 1)) * 1024 * 1024;
  } else if (c == 'G') {
    maxpost_size = stoll(cli_args.optMaxPostSize.substr(0, l - 1)) * 1024 * 1024 * 1024;
  } else {
    maxpost_size = stoll(cli_args.optMaxPostSize);
  }
  if (!config_loaded) {
    sipiConf.setMaxPostSize(maxpost_size);
  } else {
    if (!cli_args.sipiopt->get_option("--maxpost")->empty()) sipiConf.setMaxPostSize(maxpost_size);
  }

  if (!config_loaded) {
    sipiConf.setImgRoot(cli_args.optImgroot);
  } else {
    if (!cli_args.sipiopt->get_option("--imgroot")->empty()) sipiConf.setImgRoot(cli_args.optImgroot);
  }

  if (!config_loaded) {
    sipiConf.setDocRoot(cli_args.optDocroot);
  } else {
    if (!cli_args.sipiopt->get_option("--docroot")->empty()) sipiConf.setDocRoot(cli_args.optDocroot);
  }

  if (!config_loaded) {
    sipiConf.setWWWRoute(cli_args.optWWWRoute);
  } else {
    if (!cli_args.sipiopt->get_option("--wwwroute")->empty()) sipiConf.setWWWRoute(cli_args.optWWWRoute);
  }

  if (!config_loaded) {
    sipiConf.setScriptDir(cli_args.optScriptDir);
  } else {
    if (!cli_args.sipiopt->get_option("--scriptdir")->empty()) sipiConf.setScriptDir(cli_args.optScriptDir);
  }

  if (!config_loaded) {
    sipiConf.setTmpDir(cli_args.optTmpdir);
  } else {
    if (!cli_args.sipiopt->get_option("--tmpdir")->empty()) sipiConf.setTmpDir(cli_args.optTmpdir);
  }

  if (!config_loaded) {
    sipiConf.setMaxTempFileAge(cli_args.optMaxTmpAge);
  } else {
    if (!cli_args.sipiopt->get_option("--maxtmpage")->empty()) sipiConf.setMaxTempFileAge(cli_args.optMaxTmpAge);
  }

  if (!config_loaded) {
    sipiConf.setPrefixAsPath(cli_args.optPathprefix);
  } else {
    if (!cli_args.sipiopt->get_option("--pathprefix")->empty()) sipiConf.setPrefixAsPath(cli_args.optPathprefix);
  }

  if (!config_loaded) {
    sipiConf.setSubdirLevels(cli_args.optSubdirLevels);
  } else {
    if (!cli_args.sipiopt->get_option("--subdirlevels")->empty()) sipiConf.setSubdirLevels(cli_args.optSubdirLevels);
  }

  if (!config_loaded) {
    sipiConf.setSubdirExcludes(cli_args.optSubdirExcludes);
  } else {
    if (!cli_args.sipiopt->get_option("--subdirexcludes")->empty())
      sipiConf.setSubdirExcludes(cli_args.optSubdirExcludes);
  }

  if (!config_loaded) {
    sipiConf.setInitScript(cli_args.optInitscript);
  } else {
    if (!cli_args.sipiopt->get_option("--initscript")->empty()) sipiConf.setInitScript(cli_args.optInitscript);
  }

  if (!config_loaded) {
    sipiConf.setCacheDir(cli_args.optCachedir);
  } else {
    if (!cli_args.sipiopt->get_option("--cachedir")->empty()) sipiConf.setCacheDir(cli_args.optCachedir);
  }

  l = cli_args.optCacheSize.length();
  c = cli_args.optCacheSize[l - 1];
  tsize_t cache_size;
  if (c == 'M') {
    cache_size = stoll(cli_args.optCacheSize.substr(0, l - 1)) * 1024 * 1024;
  } else if (c == 'G') {
    cache_size = stoll(cli_args.optCacheSize.substr(0, l - 1)) * 1024 * 1024 * 1024;
  } else {
    cache_size = stoll(cli_args.optCacheSize);
  }
  if (!config_loaded) {
    sipiConf.setCacheSize(cache_size);
  } else {
    if (!cli_args.sipiopt->get_option("--cachesize")->empty()) sipiConf.setCacheSize(cache_size);
  }

  if (!config_loaded) {
    sipiConf.setCacheNFiles(cli_args.optCacheNFiles);
  } else {
    if (!cli_args.sipiopt->get_option("--cachenfiles")->empty()) sipiConf.setCacheNFiles(cli_args.optCacheNFiles);
  }

  if (!config_loaded) {
    sipiConf.setCacheHysteresis(cli_args.optCacheHysteresis);
  } else {
    if (!cli_args.sipiopt->get_option("--cachehysteresis")->empty())
      sipiConf.setCacheHysteresis(cli_args.optCacheHysteresis);
  }

  if (!config_loaded) {
    sipiConf.setThumbSize(cli_args.optThumbSize);
  } else {
    if (!cli_args.sipiopt->get_option("--thumbsize")->empty()) sipiConf.setThumbSize(cli_args.optThumbSize);
  }

  if (!config_loaded) {
    sipiConf.setSSLCertificate(cli_args.optSSLCertificatePath);
  } else {
    if (!cli_args.sipiopt->get_option("--sslcert")->empty()) sipiConf.setSSLCertificate(cli_args.optSSLCertificatePath);
  }

  if (!config_loaded) {
    sipiConf.setSSLKey(cli_args.optSSLKeyPath);
  } else {
    if (!cli_args.sipiopt->get_option("--sslkey")->empty()) sipiConf.setSSLKey(cli_args.optSSLKeyPath);
  }

  if (!config_loaded) {
    sipiConf.setJwtSecret(cli_args.optJWTKey);
  } else {
    if (!cli_args.sipiopt->get_option("--jwtkey")->empty()) sipiConf.setJwtSecret(cli_args.optJWTKey);
  }

  if (!config_loaded) {
    sipiConf.setAdminUser(cli_args.optAdminUser);
  } else {
    if (!cli_args.sipiopt->get_option("--adminuser")->empty()) sipiConf.setAdminUser(cli_args.optAdminUser);
  }

  if (!config_loaded) {
    sipiConf.setPasswort(cli_args.optAdminPassword);
  } else {
    if (!cli_args.sipiopt->get_option("--adminpasswd")->empty()) sipiConf.setPasswort(cli_args.optAdminPassword);
  }

  if (!config_loaded) {
    sipiConf.setKnoraPath(cli_args.optKnoraPath);
  } else {
    if (!cli_args.sipiopt->get_option("--knorapath")->empty()) sipiConf.setKnoraPath(cli_args.optKnoraPath);
  }

  if (!config_loaded) {
    sipiConf.setKnoraPort(cli_args.optKnoraPort);
  } else {
    if (!cli_args.sipiopt->get_option("--knoraport")->empty()) sipiConf.setKnoraPort(cli_args.optKnoraPort);
  }

  if (!config_loaded) {
    sipiConf.setLogfile(cli_args.optLogfilePath);
  } else {
    if (!cli_args.sipiopt->get_option("--logfile")->empty()) sipiConf.setLogfile(cli_args.optLogfilePath);
  }

  std::string loglevelstring;
  switch (cli_args.optLogLevel) {
  case LogLevel::DEBUG:
    loglevelstring = "DEBUG";
    break;
  case LogLevel::INFO:
    loglevelstring = "INFO";
    break;
  case LogLevel::NOTICE:
    loglevelstring = "NOTICE";
    break;
  case LogLevel::WARNING:
    loglevelstring = "WARNING";
    break;
  case LogLevel::ERR:
    loglevelstring = "ERR";
    break;
  case LogLevel::CRIT:
    loglevelstring = "CRIT";
    break;
  case LogLevel::ALERT:
    loglevelstring = "ALERT";
    break;
  case LogLevel::EMERG:
    loglevelstring = "EMERG";
    break;
  }
  if (!config_loaded) {
    sipiConf.setLogLevel(loglevelstring);
  } else {
    if (!cli_args.sipiopt->get_option("--loglevel")->empty()) sipiConf.setLogLevel(loglevelstring);
  }

  //
  // here we check the levels... and migrate if necessary
  //
  if (sipiConf.getPrefixAsPath()) {
    std::vector<std::string> dirs_to_exclude = sipiConf.getSubdirExcludes();

    //
    // the prefix is used as part of the path
    //
    DIR *dirp = opendir(sipiConf.getImgRoot().c_str());
    if (dirp == nullptr) {
      throw shttps::Error(std::string("Couldn't read directory content! Path: ") + sipiConf.getImgRoot(), errno);
    }
    struct dirent *dp;
    while ((dp = readdir(dirp)) != nullptr) {
      if (dp->d_type == DT_DIR) {
        if (strcmp(dp->d_name, ".") == 0) continue;
        if (strcmp(dp->d_name, "..") == 0) continue;
        bool exclude = false;
        for (auto direx : dirs_to_exclude) {
          if (direx == dp->d_name) exclude = true;
        }
        if (exclude) continue;
        std::string path = sipiConf.getImgRoot() + "/" + dp->d_name;
        int levels = SipiFilenameHash::check_levels(path);
        int new_levels = sipiConf.getSubdirLevels();
        if (levels != new_levels) {
          std::cerr << "Subdir migration of " << path << "...." << std::endl;
          SipiFilenameHash::migrateToLevels(path, new_levels);
        }
      }
    }
  } else {
    //
    // the prefix is not used
    //
    int levels = SipiFilenameHash::check_levels(sipiConf.getImgRoot());
    int new_levels = sipiConf.getSubdirLevels();
    if (levels != new_levels) {
      std::cerr << "Subdir migration of " << sipiConf.getImgRoot() << "...." << std::endl;
      SipiFilenameHash::migrateToLevels(sipiConf.getImgRoot(), new_levels);
    }
  }
  SipiFilenameHash::setLevels(sipiConf.getSubdirLevels());


  // TODO: At this point the config is loaded and we can initialize metrics
  if (!cli_args.optSipiSentryDsn.empty()) syslog(LOG_INFO, "SIPI_SENTRY_DSN: %s", cli_args.optSipiSentryDsn.c_str());

  if (!cli_args.optSipiSentryEnvironment.empty())
    syslog(LOG_INFO, "SIPI_SENTRY_ENVRONMENT: %s", cli_args.optSipiSentryEnvironment.c_str());

  if (!cli_args.optSipiSentryRelease.empty())
    syslog(LOG_INFO, "SIPI_SENTRY_Release: %s", cli_args.optSipiSentryRelease.c_str());

  // Create object SipiHttpServer
  auto nthreads = sipiConf.getNThreads();
  if (nthreads < 1) { nthreads = std::thread::hardware_concurrency(); }
  Sipi::SipiHttpServer server(
    sipiConf.getPort(), nthreads, sipiConf.getUseridStr(), sipiConf.getLogfile(), sipiConf.getLoglevel());

  int old_ll = setlogmask(LOG_MASK(LOG_INFO));
  syslog(LOG_INFO, "BUILD_TIMESTAMP: %s", BUILD_TIMESTAMP);
  syslog(LOG_INFO, "BUILD_SCM_TAG: %s", BUILD_SCM_TAG);
  syslog(LOG_INFO, "BUILD_SCM_REVISION: %s", BUILD_SCM_REVISION);
  setlogmask(old_ll);

  server.ssl_port(sipiConf.getSSLPort());// set the secure connection port (-1 means no ssl socket)
  std::string tmps = sipiConf.getSSLCertificate();
  server.ssl_certificate(tmps);
  tmps = sipiConf.getSSLKey();
  server.ssl_key(tmps);
  server.jwt_secret(sipiConf.getJwtSecret());

  // set tmpdir for uploads (defined in sipi.config.lua)
  server.tmpdir(sipiConf.getTmpDir());
  server.max_post_size(sipiConf.getMaxPostSize());
  server.scriptdir(sipiConf.getScriptDir());// set the directory where the Lua scripts are found for the "Lua"-routes
  server.luaRoutes(sipiConf.getRoutes());
  server.add_lua_globals_func(sipiConfGlobals, &sipiConf);
  server.add_lua_globals_func(shttps::sqliteGlobals);// add new lua function "gaga"
  server.add_lua_globals_func(Sipi::sipiGlobals, &server);// add Lua SImage functions
  server.prefix_as_path(sipiConf.getPrefixAsPath());
  server.dirs_to_exclude(sipiConf.getSubdirExcludes());
  server.scaling_quality(sipiConf.getScalingQuality());
  server.jpeg_quality(sipiConf.getJpegQuality());

  //
  // cache parameter...
  //
  std::string emptystr;
  std::string cachedir = sipiConf.getCacheDir();

  if (!cachedir.empty()) {
    size_t cachesize = sipiConf.getCacheSize();
    size_t nfiles = sipiConf.getCacheNFiles();
    float hysteresis = sipiConf.getCacheHysteresis();
    server.cache(cachedir, cachesize, nfiles, hysteresis);
  }

  server.imgroot(sipiConf.getImgRoot());
  server.initscript(sipiConf.getInitScript());
  server.keep_alive_timeout(sipiConf.getKeepAlive());

  //
  // now we set the routes for the normal HTTP server file handling
  //
  std::string docroot = sipiConf.getDocRoot();
  std::string wwwroute = sipiConf.getWWWRoute();
  std::pair<std::string, std::string> filehandler_info;

  // here we add two additional routes for handling files.
  // (tip: click into add_route to see all the places where routes are added. there are a few places.)
  if (!(wwwroute.empty() || docroot.empty())) {
    filehandler_info.first = wwwroute;
    filehandler_info.second = docroot;
    server.add_route(shttps::Connection::GET, wwwroute, shttps::file_handler, &filehandler_info);
    server.add_route(shttps::Connection::POST, wwwroute, shttps::file_handler, &filehandler_info);
  }

  // start the server
  server.run();
  return EXIT_SUCCESS;
}
}// namespace
/*!
 * The main function.
 *
 * @param argc the number of command line arguments.
 * @param argv the command line arguments.
 * @return the exit code.
 */
int main(int argc, char *argv[])
{

  // install signal handler
  signal(SIGSEGV, sig_handler);
  signal(SIGABRT, sig_handler);

  // set top level exception handler
  std::set_terminate(my_terminate_handler);

  //
  // first we initialize the libraries that sipi uses
  //
  try {
    LibraryInitialiser::instance();
  } catch (shttps::Error &e) {
    std::cerr << e.to_string() << '\n';
    return EXIT_FAILURE;
  }

  auto cli_args = get_cli_args();

  // parse the command line arguments or exit if not valid
  CLI11_PARSE(*cli_args.sipiopt, argc, argv);

  initTracer();
  auto tracer = get_tracer();

  initMeter();
  auto meter = get_meter();

  initLogger();
  auto logger = get_logger();


  //
  // Query the image file for all information.
  //
  if (!cli_args.sipiopt->get_option("--query")->empty()) {
    try {

      auto span = tracer->StartSpan("query_command");
      query_command(cli_args);
      span->End();
      return EXIT_SUCCESS;
    } catch (std::exception &e) {
      syslog(LOG_ERR, "Error in query command: %s", e.what());
      return EXIT_FAILURE;
    }
  }

  //
  // Compare two image files.
  //
  if (!cli_args.sipiopt->get_option("--compare")->empty()) {
    try {
      auto span = tracer->StartSpan("compare_command");
      auto counter = meter->CreateDoubleCounter("compare_command");
      counter->Add(1.0, { { "command", "compare" } });
      logger->Info("Starting compare command");
      int const ret_code = compare_command(cli_args);
      span->End();
      return ret_code;
    } catch (std::exception &e) {
      logger->Error("Error in compare command: {}", e.what());
      syslog(LOG_ERR, "Error in compare command: %s", e.what());
      return EXIT_FAILURE;
    }
  }

  //
  // Convert an image file.
  //
  if (!(cli_args.sipiopt->get_option("--file")->empty() || cli_args.sipiopt->get_option("--outf")->empty())) {
    try {
      int const ret_code = convert_command(cli_args);
      return ret_code;
    } catch (std::exception &e) {
      logger->Error("Error in convert command: {}", e.what());
      syslog(LOG_ERR, "Error in convert command: %s", e.what());
      return EXIT_FAILURE;
    }
  }

  //
  // there is a configuration file given on the command line. Thus we try to start SIPI in
  // server mode
  //
  if (!(cli_args.sipiopt->get_option("--config")->empty() && cli_args.sipiopt->get_option("--serverport")->empty())) {
    try {
      auto span = tracer->StartSpan("server_command");
      auto counter = meter->CreateDoubleCounter("server_command");
      counter->Add(1.0, { { "command", "server" } });
      logger->Info("Starting server command");
      const int ret_code = server_command(cli_args);
      return ret_code;
    } catch (shttps::Error &err) {
      logger->Error("Error starting server: {}", err.what());
      syslog(LOG_ERR, "Error starting server: %s", err.what());
      std::cerr << err << '\n';
      return EXIT_FAILURE;
    }
  }
  return EXIT_SUCCESS;
}
