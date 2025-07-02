/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*!
 * \brief Implements an IIIF server with many features.
 *
 */
#include <csignal>
#include <dirent.h>
#include <execinfo.h>
#include <iostream>
#include <sstream>
#include <string>
#include <sys/stat.h>

#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include <curl/curl.h>
#include <jansson.h>
#include <sentry.h>

#include "shttps/LuaServer.h"
#include "shttps/LuaSqlite.h"
#include "shttps/Parsing.h"
#include "shttps/Server.h"


#include "CLI11.hpp"
#include "Logger.h"
#include "SipiConf.h"
#include "SipiFilenameHash.h"
#include "SipiHttpServer.hpp"
#include "SipiIO.h"
#include "SipiImage.hpp"
#include "SipiImageError.hpp"
#include "SipiLua.h"
#include "formats/SipiIOTiff.h"

#include "generated/SipiVersion.h"

// A macro for silencing incorrect compiler warnings about unused variables.
#define _unused(x) ((void)(x))

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

static void sipiConfGlobals(lua_State *L, shttps::Connection &conn, void *user_data)
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
    lua_pushinteger(L, LL_EMERG);
  } else if (loglevel == "LOG_ALERT") {
    lua_pushinteger(L, LL_ALERT);
  } else if (loglevel == "LOG_CRIT") {
    lua_pushinteger(L, LL_CRIT);
  } else if (loglevel == "LOG_ERR") {
    lua_pushinteger(L, LL_ERR);
  } else if (loglevel == "LOG_WARNING") {
    lua_pushinteger(L, LL_WARNING);
  } else if (loglevel == "LOG_NOTICE") {
    lua_pushinteger(L, LL_NOTICE);
  } else if (loglevel == "LOG_INFO") {
    lua_pushinteger(L, LL_INFO);
  } else if (loglevel == "LOG_DEBUG") {
    lua_pushinteger(L, LL_DEBUG);
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
inline bool exists_file(const std::string &name)
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
  sentry_capture_event(sentry_value_new_message_event(SENTRY_LEVEL_FATAL, "sig_handler", msg.c_str()));
  log_err("%s", msg.c_str());
  sentry_flush(2000);

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
  sentry_capture_event(sentry_value_new_message_event(SENTRY_LEVEL_FATAL, "my_terminate_handler", msg.c_str()));
  log_err("%s", msg.c_str());
  sentry_flush(2000);

  std::abort();// Abort the program or perform other cleanup
}

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

  // Attempt to read the environment variable
  const char *sipi_sentry_dsn = getenv("SIPI_SENTRY_DSN");
  std::string sentry_dsn{};
  if (sipi_sentry_dsn != nullptr) { sentry_dsn = sipi_sentry_dsn; }

  const char *sipi_sentry_release = getenv("SIPI_SENTRY_RELEASE");
  std::string sentry_release{};
  if (sipi_sentry_release != nullptr) { sentry_release = sipi_sentry_release; }

  const char *sipi_sentry_environment = getenv("SIPI_SENTRY_ENVIRONMENT");
  std::string sentry_environment{};
  if (sipi_sentry_environment != nullptr) { sentry_environment = sipi_sentry_environment; }


  // At this point the config is loaded and we can initialize sentry
  if (!sentry_dsn.empty()) {
    sentry_options_t *options = sentry_options_new();
    sentry_options_set_dsn(options, sentry_dsn.c_str());
    sentry_options_set_database_path(options, "/tmp/.sentry-native");

    sentry_options_set_symbolize_stacktraces(options, true);

    if (!sentry_release.empty()) {
      std::string sentryReleaseTag = std::string(BUILD_SCM_TAG);
      sentry_options_set_release(options, sentryReleaseTag.c_str());
    }

    if (!sentry_environment.empty()) {
      sentry_options_set_environment(options, sentry_environment.c_str());
    } else {
      sentry_options_set_environment(options, "development");
    }

    sentry_options_set_debug(options, 0);

    // configures the sampling rate for transactions
    sentry_options_set_traces_sample_rate(options, 0.1);

    sentry_init(options);
  }


  //
  // first we initialize the libraries that sipi uses
  //
  try {
    LibraryInitialiser &sipi_init = LibraryInitialiser::instance();
    _unused(sipi_init);// Silence compiler warning about unused variable.
  } catch (shttps::Error &e) {
    std::cerr << e.to_string() << '\n';
    return EXIT_FAILURE;
  }

  CLI::App sipiopt("SIPI is a image format converter and - if started in server mode - a high performance IIIF server");

  std::string optConfigfile;
  sipiopt.add_option("-c,--config", optConfigfile, "Configuration file for web server.")
    ->envname("SIPI_CONFIGFILE")
    ->check(CLI::ExistingFile);
  ;

  std::string optInFile;
  sipiopt.add_option("-f,--file,--inf,infile", optInFile, "Input file to be converted.")->check(CLI::ExistingFile);
  ;

  std::string optOutFile;
  sipiopt.add_option("-z,--outf,outfile", optOutFile, "Output file to be converted.");

  enum class OptFormat : int { jpx, jpg, tif, png };
  OptFormat optFormat = OptFormat::jpx;
  std::vector<std::pair<std::string, OptFormat>> optFormatMap{ { "jpx", OptFormat::jpx },
    { "jp2", OptFormat::jpx },
    { "jpg", OptFormat::jpg },
    { "tif", OptFormat::tif },
    { "png", OptFormat::png } };
  sipiopt.add_option("-F,--format", optFormat, "Output format.")
    ->transform(CLI::CheckedTransformer(optFormatMap, CLI::ignore_case));

  enum class OptIcc : int { none, sRGB, AdobeRGB, GRAY };
  OptIcc optIcc = OptIcc::none;
  std::vector<std::pair<std::string, OptIcc>> optIccMap{
    { "none", OptIcc::none }, { "sRGB", OptIcc::sRGB }, { "AdobeRGB", OptIcc::AdobeRGB }, { "GRAY", OptIcc::GRAY }
  };
  sipiopt.add_option("-I,--icc", optIcc, "Convert to ICC profile.")
    ->transform(CLI::CheckedTransformer(optIccMap, CLI::ignore_case));

  int optJpegQuality = 60;
  sipiopt.add_option("-q,--quality", optJpegQuality, "Quality (compression).")
    ->check(CLI::Range(1, 100))
    ->envname("SIPI_JPEGQUALITY");

  //
  // Parameters for JPEG2000 compression (see kakadu kdu_compress for details!)
  //
  std::string j2k_Sprofile;
  sipiopt
    .add_option("--Sprofile", j2k_Sprofile, "Restricted profile to which the code-stream conforms [Default: PART2].")
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

  std::vector<std::string> j2k_rates;
  sipiopt.add_option("--rates",
    j2k_rates,
    "One or more bit-rates (see kdu_compress help!). A value "
    "\"-1\" may be used in place of the first bit-rate in the list to indicate "
    "that the final quality layer should include all compressed bits.");

  int j2k_Clayers;
  sipiopt.add_option("--Clayers", j2k_Clayers, "J2K: Number of quality layers [Default: 8].");

  int j2k_Clevels;
  sipiopt.add_option("--Clevels", j2k_Clevels, "J2K: Number of wavelet decomposition levels, or stages [default: 8].");

  std::string j2k_Corder;
  sipiopt
    .add_option("--Corder",
      j2k_Corder,
      "J2K: Progression order. The four character identifiers have the following interpretation: "
      "L=layer; R=resolution; C=component; P=position. The first character in the identifier refers to the "
      "index which progresses most slowly, while the last refers to the index which progresses most quickly [Default: "
      "RPCL].")
    ->check(CLI::IsMember({ "LRCP", "RLCP", "RPCL", "PCRL", "CPRL" }, CLI::ignore_case));

  std::string j2k_Stiles;
  sipiopt.add_option("--Stiles", j2k_Stiles, "J2K: Tiles dimensions \"{tx,ty} [Default: {256,256}]\".");

  std::string j2k_Cprecincts;
  sipiopt.add_option(
    "--Cprecincts", j2k_Cprecincts, "J2K: Precinct dimensions \"{px,py}\" (must be powers of 2) [Default: {256,256}].");

  std::string j2k_Cblk;
  sipiopt.add_option("--Cblk",
    j2k_Cblk,
    "J2K: Nominal code-block dimensions (must be powers of 2, no less than 4 and "
    "no greater than 1024, whose product may not exceed 4096) [Default: {64,64}].");

  bool j2k_Cuse_sop;
  sipiopt.add_option(
    "--Cuse_sop", j2k_Cuse_sop, "J2K Cuse_sop: Include SOP markers (i.e., resync markers) [Default: yes].");

  bool tiff_Pyramid;
  sipiopt.add_option(
    "--Ctiff_pyramid", tiff_Pyramid, "TIFF: store in Pyramidal TIFF format [Default: no].");

  //
  // used for rendering only one page of multipage PDF or TIFF (NYI for tif...)
  //
  int optPagenum = 0;
  sipiopt.add_option("-n,--pagenum", optPagenum, "Pagenumber for PDF documents or multipage TIFFs.");

  std::vector<int> optRegion;
  sipiopt.add_option("-r,--region", optRegion, "Select region of interest, where x y w h are integer values.")
    ->expected(4);

  int optReduce = 0;
  sipiopt.add_option(
    "-R,--reduce", optReduce, "Reduce image size by factor  (cannot be used together with --size and --scale)");

  std::string optSize;
  sipiopt.add_option(
    "-s,--size", optSize, "Resize image to given size (cannot be used together with --reduce and --scale)");

  int optScale;
  sipiopt.add_option("-S,--scale",
    optScale,
    "Resize image by the given percentage Value (cannot be used together with --size and --reduce)");

  bool optSkipMeta = false;
  sipiopt.add_flag("-k,--skipmeta", optSkipMeta, "Skip metadata of original file if flag is present.");

  enum class OptMirror { none, horizontal, vertical };
  OptMirror optMirror = OptMirror::none;
  std::vector<std::pair<std::string, OptMirror>> optMirrorMap{
    { "none", OptMirror::none }, { "horizontal", OptMirror::horizontal }, { "vertical", OptMirror::vertical }
  };
  sipiopt.add_option("-m,--mirror", optMirror, "Mirror the image. Value can be: 'none', 'horizontal', 'vertical'.")
    ->transform(CLI::CheckedTransformer(optMirrorMap, CLI::ignore_case));

  float optRotate = 0.0;
  sipiopt.add_option("-o,--rotate", optRotate, "Rotate the image. by degree Value, angle between (0.0 - 360.0).");

  bool optSetTopleft = false;
  sipiopt.add_flag("--topleft", optSetTopleft, "Enforce orientation TOPLEFT.");

  std::vector<std::string> optCompare;
  sipiopt.add_option("-C,--compare", optCompare, "Compare two files.")->expected(2);

  std::string optWatermark;
  sipiopt.add_option("-w,--watermark", optWatermark, "Add a watermark to the image.");

  bool optQuery = false;
  sipiopt.add_flag("-x,--query", optQuery, "Dump all information about the given file.");

  bool optSalsah = false;
  sipiopt.add_flag("-a,--salsah", optSalsah, "Special optioons for conversions in old salsah.");

  //
  // below are server options
  //
  int optServerport = 80;
  sipiopt.add_option("--serverport", optServerport, "Port of SIPI web server.")->envname("SIPI_SERVERPORT");

  int optSSLport = 443;
  sipiopt.add_option("--sslport", optSSLport, "SSL-port of the SIPI server.")->envname("SIPI_SSLPORT");

  std::string optHostname = "localhost";
  sipiopt.add_option("--hostname", optHostname, "Hostname to use for HTTP server.")->envname("SIPI_HOSTNAME");

  int optKeepAlive = 5;
  sipiopt.add_option("--keepalive", optKeepAlive, "Number of seconds for the keeop-alive optioon of HTTP 1.1.")
    ->envname("SIPI_KEEPALIVE");

  unsigned int optNThreads = std::thread::hardware_concurrency();
  sipiopt.add_option("-t,--nthreads", optNThreads, "Number of threads for SIPI server")->envname("SIPI_NTHREADS");

  std::string optMaxPostSize = "300M";
  sipiopt
    .add_option("--maxpost", optMaxPostSize, "A string indicating the maximal size of a POST request, e.g. '300M'.")
    ->envname("SIPI_MAXPOSTSIZE");

  std::string optImgroot = "./images";
  sipiopt.add_option("--imgroot", optImgroot, "Root directory containing the images for the web server.")
    ->envname("SIPI_IMGROOT")
    ->check(CLI::ExistingDirectory);

  std::string optDocroot = "./server";
  sipiopt.add_option("--docroot", optDocroot, "Path to document root for normal webserver.")
    ->envname("SIPI_DOCROOT")
    ->check(CLI::ExistingDirectory);

  std::string optWWWRoute = "/server";
  sipiopt.add_option("--wwwroute", optWWWRoute, "URL route for standard webserver.")->envname("SIPI_WWWROUTE");

  std::string optScriptDir = "./scripts";
  sipiopt.add_option("--scriptdir", optScriptDir, "Path to directory containing Lua scripts to implement routes.")
    ->envname("SIPI_SCRIPTDIR")
    ->check(CLI::ExistingDirectory);

  std::string optTmpdir = "./tmp";
  sipiopt.add_option("--tmpdir", optTmpdir, "Path to the temporary directory (e.g. for uploads etc.).")
    ->envname("SIPI_TMPDIR")
    ->check(CLI::ExistingDirectory);

  int optMaxTmpAge = 86400;
  sipiopt
    .add_option(
      "--maxtmpage", optMaxTmpAge, "The maximum allowed age of temporary files (in seconds) before they are deleted.")
    ->envname("SIPI_MAXTMPAGE");

  bool optPathprefix = false;
  sipiopt
    .add_flag("--pathprefix",
      optPathprefix,
      "Flag, if set indicates that the IIIF prefix is part of the path to the image file (deprecated).")
    ->envname("SIPI_PATHPREFIX");

  int optSubdirLevels = 0;
  sipiopt.add_option("--subdirlevels", optSubdirLevels, "Number of subdir levels (deprecated).")
    ->envname("SIPI_SUBDIRLEVELS");

  std::vector<std::string> optSubdirExcludes = { "tmp", "thumb" };
  sipiopt.add_option("--subdirexcludes", optSubdirExcludes, "Directories not included in subdir calculations.")
    ->envname("SIPI_SUBDIREXCLUDES");

  std::string optInitscript = "./config/sipi.init.lua";
  sipiopt.add_option("--initscript", optInitscript, "Path to init script (Lua).")
    ->envname("SIPI_INITSCRIPT")
    ->check(CLI::ExistingFile);

  std::string optCachedir = "./cache";
  sipiopt.add_option("--cachedir", optCachedir, "Path to cache folder.")->envname("SIPI_CACHEDIR");

  std::string optCacheSize = "200M";
  sipiopt.add_option("--cachesize", optCacheSize, "Maximal size of cache, e.g. '500M'.")->envname("SIPI_CACHESIZE");

  int optCacheNFiles = 200;
  sipiopt.add_option("--cachenfiles", "The maximal number of files to be cached.")->envname("SIPI_CACHENFILES");

  double optCacheHysteresis = 0.15;
  sipiopt
    .add_option("--cachehysteresis",
      optCacheHysteresis,
      "If the cache becomes full, the given percentage of file space is marked for reuse (0.0 - 1.0).")
    ->envname("SIPI_CACHEHYSTERESIS");

  std::string optThumbSize = "!128,128";
  sipiopt.add_option("--thumbsize", optThumbSize, "Size of the thumbnails (to be used within Lua).")
    ->envname("SIPI_THUMBSIZE");

  std::string optSSLCertificatePath = "./certificate/certificate.pem";
  sipiopt.add_option("--sslcert", optSSLCertificatePath, "Path to SSL certificate.")->envname("SIPI_SSLCERTIFICATE");

  std::string optSSLKeyPath = "./certificate/key.pem";
  sipiopt.add_option("--sslkey", optSSLKeyPath, "Path to the SSL key file.")->envname("SIPI_SSLKEY");

  std::string optJWTKey = "UP 4888, nice 4-8-4 steam engine";
  sipiopt
    .add_option("--jwtkey", optJWTKey, "The secret for generating JWT's (JSON Web Tokens) (exactly 42 characters).")
    ->envname("SIPI_JWTKEY");

  std::string optAdminUser = "admin";
  sipiopt.add_option("--adminuser", optAdminUser, "Username for SIPI admin user.")->envname("SIPI_ADMIINUSER");

  std::string optAdminPassword = "Sipi-Admin";
  sipiopt.add_option("--adminpasswd", optAdminPassword, "Password of the admin user.")->envname("SIPI_ADMINPASSWD");

  std::string optKnoraPath = "localhost";
  sipiopt.add_option("--knorapath", optKnoraPath, "Path to Knora server.")->envname("SIPI_KNORAPATH");

  std::string optKnoraPort = "3434";
  sipiopt.add_option("--knoraport", optKnoraPort, "Portnumber for Knora.")->envname("SIPI_KNORAPORT");

  std::string optLogfilePath = "Sipi";
  sipiopt.add_option("--logfile", optLogfilePath, "Name of the logfile (NYI).")->envname("SIPI_LOGFILE");

  LogLevel optLogLevel = LL_DEBUG;
  std::vector<std::pair<std::string, LogLevel>> logLevelMap{ { "DEBUG", LL_DEBUG },
    { "INFO", LL_INFO },
    { "NOTICE", LL_NOTICE },
    { "WARNING", LL_WARNING },
    { "ERR", LL_ERR },
    { "CRIT", LL_CRIT },
    { "ALERT", LL_ALERT },
    { "EMERG", LL_EMERG } };
  sipiopt
    .add_option("--loglevel",
      optLogLevel,
      "Logging level Value can be: 'DEBUG', 'INFO', 'WARNING', 'ERR', 'CRIT', 'ALERT', 'EMERG'.")
    ->transform(CLI::CheckedTransformer(logLevelMap, CLI::ignore_case))
    ->envname("SIPI_LOGLEVEL");

  // sentry configuration
  std::string optSipiSentryDsn;
  sipiopt.add_option("--sentry-dsn", optSipiSentryDsn)->envname("SIPI_SENTRY_DSN");

  std::string optSipiSentryRelease;
  sipiopt.add_option("--sentry-release", optSipiSentryRelease)->envname("SIPI_SENTRY_RELEASE");

  std::string optSipiSentryEnvironment;
  sipiopt.add_option("--sentry-environment", optSipiSentryEnvironment)->envname("SIPI_SENTRY_ENVIRONMENT");

  CLI11_PARSE(sipiopt, argc, argv);

  /*
  argc -= (argc > 0);
  argv += (argc > 0); // skip program name argv[0] if present

  option::Stats stats(usage, argc, argv);
  std::vector<option::Option> options(stats.options_max);
  std::vector<option::Option> buffer(stats.buffer_max);
  option::Parser parse(usage, argc, argv, &options[0], &buffer[0]);
*/
  if (!sipiopt.get_option("--query")->empty()) {
    //
    // we query all information from just one file
    //
    Sipi::SipiImage img;
    img.read(optInFile);
    std::cout << img << std::endl;
    return (0);
  } else if (!sipiopt.get_option("--compare")->empty()) {
    //
    // command line function: we want to compare pixelwise to files. After having done this, we exit
    //
    if (!exists_file(optCompare[0])) {
      std::cerr << "File not found: " << optCompare[0] << std::endl;
      return EXIT_FAILURE;
    }

    if (!exists_file(optCompare[1])) {
      std::cerr << "File not found: " << optCompare[1] << std::endl;
      return EXIT_FAILURE;
    }

    Sipi::SipiImage img1, img2;
    img1.read(optCompare[0]);
    img2.read(optCompare[1]);
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
  } else if (!(sipiopt.get_option("--file")->empty() || sipiopt.get_option("--outf")->empty())) {
    //
    // Commandline conversion with input and output file given
    //

    //
    // get the output format
    //
    std::string format("jpg");
    if (!sipiopt.get_option("--format")->empty()) {
      switch (optFormat) {
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
      size_t pos = optOutFile.rfind('.');
      if (pos != std::string::npos) {
        std::string ext = optOutFile.substr(pos + 1);
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
    if (!sipiopt.get_option("--region")->empty()) {
      region = std::make_shared<Sipi::SipiRegion>(optRegion.at(0), optRegion.at(1), optRegion.at(2), optRegion.at(3));
    }

    //
    // get the reduce parameter
    // "reduce" is a special feature of the JPEG2000 format. It is possible (given the JPEG2000 format
    // is written a resolution pyramid). reduce=0 results in full resolution, reduce=1 is half the resolution
    // etc.
    //
    std::shared_ptr<Sipi::SipiSize> size = nullptr;
    if (optReduce > 0) {
      size = std::make_shared<Sipi::SipiSize>(optReduce);
    } else if (!sipiopt.get_option("--size")->empty()) {
      try {
        size = std::make_shared<Sipi::SipiSize>(optSize);
      } catch (std::exception &e) {
        log_err("Error in size parameter: %s", e.what());
        return EXIT_FAILURE;
      }
    } else if (!sipiopt.get_option("--scale")->empty()) {
      try {
        size = std::make_shared<Sipi::SipiSize>(optScale);
      } catch (std::exception &e) {
        log_err("Error in scale parameter: %s", e.what());
        return EXIT_FAILURE;
      }
    }

    //
    // read the input image
    //
    Sipi::SipiImage img;
    try {
      img.readOriginal(optInFile, region, size,
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
    if (!sipiopt.get_option("--topleft")->empty()) {
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

    if (!sipiopt.get_option("--skipmeta")->empty()) { img.setSkipMetadata(Sipi::SkipMetadata::SKIP_ALL); }

    //
    // color profile processing
    //
    if (!sipiopt.get_option("--icc")->empty()) {
      Sipi::SipiIcc icc;
      switch (optIcc) {
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
    if (!(sipiopt.get_option("--mirror")->empty() && sipiopt.get_option("--rotate")->empty())) {
      switch (optMirror) {
      case OptMirror::vertical: {
        img.rotate(optRotate + 180.0F, true);
        break;
      }
      case OptMirror::horizontal: {
        img.rotate(optRotate, true);
        break;
      }
      case OptMirror::none: {
        if (optRotate != 0.0F) { img.rotate(optRotate, false); }
        break;
      }
      }
    }

    if (!sipiopt.get_option("--watermark")->empty()) { img.add_watermark(optWatermark); }

    //
    // write the output file
    //
    // int quality = 80
    Sipi::SipiCompressionParams comp_params;
    if (!sipiopt.get_option("--quality")->empty()) comp_params[Sipi::JPEG_QUALITY] = optJpegQuality;
    if (!sipiopt.get_option("--Sprofile")->empty()) comp_params[Sipi::J2K_Sprofile] = j2k_Sprofile;
    if (!sipiopt.get_option("--Clayers")->empty()) comp_params[Sipi::J2K_Clayers] = std::to_string(j2k_Clayers);
    if (!sipiopt.get_option("--Clevels")->empty()) comp_params[Sipi::J2K_Clevels] = std::to_string(j2k_Clevels);
    if (!sipiopt.get_option("--Corder")->empty()) comp_params[Sipi::J2K_Corder] = j2k_Corder;
    if (!sipiopt.get_option("--Cprecincts")->empty()) comp_params[Sipi::J2K_Cprecincts] = j2k_Cprecincts;
    if (!sipiopt.get_option("--Cblk")->empty()) comp_params[Sipi::J2K_Cblk] = j2k_Cblk;
    if (!sipiopt.get_option("--Cuse_sop")->empty()) comp_params[Sipi::J2K_Cuse_sop] = j2k_Cuse_sop ? "yes" : "no";
    if (!sipiopt.get_option("--Stiles")->empty()) comp_params[Sipi::J2K_Stiles] = j2k_Stiles;
    if (!sipiopt.get_option("--Ctiff_pyramid")->empty()) comp_params[Sipi::TIFF_Pyramid] = tiff_Pyramid ? "yes" : "no";

    if (!sipiopt.get_option("--rates")->empty()) {
      std::stringstream ss;
      for (auto &rate : j2k_rates) {
        if (rate == "X") {
          ss << "-1.0 ";
        } else {
          ss << rate << " ";
        }
      }
      comp_params[Sipi::J2K_rates] = ss.str();
    }

    try {
      img.write(format, optOutFile, &comp_params);
    } catch (Sipi::SipiImageError &err) {
      std::cerr << err << '\n';
    }

    if (!sipiopt.get_option("--salsah")->empty()) { std::cout << img.getNx() << " " << img.getNy() << std::endl; }
  } else if (!(sipiopt.get_option("--config")->empty() && sipiopt.get_option("--serverport")->empty())) {
    //
    // there is a configuration file given on the command line. Thus we try to start SIPI in
    // server mode
    //

    try {
      std::cout << std::endl << "Ivan was here" << std::endl;

      Sipi::SipiConf sipiConf;
      bool config_loaded = false;
      if (!sipiopt.get_option("--config")->empty()) {
        // read and parse the config file (config file is a lua script)
        shttps::LuaServer luacfg(optConfigfile);

        // store the config option in a SipiConf obj
        sipiConf = Sipi::SipiConf(luacfg);
        config_loaded = true;
      }

      //
      // now we check for all commandline/environment variables and update sipiConf.
      //
      if (!config_loaded) {
        sipiConf.setPort(optServerport);
      } else {
        if (!sipiopt.get_option("--serverport")->empty()) sipiConf.setPort(optServerport);
      }

      if (!config_loaded) {
        sipiConf.setSSLPort(optSSLport);
      } else {
        if (!sipiopt.get_option("--sslport")->empty()) sipiConf.setSSLPort(optSSLport);
      }

      if (!config_loaded) {
        sipiConf.setHostname(optHostname);
      } else {
        if (!sipiopt.get_option("--hostname")->empty()) sipiConf.setHostname(optHostname);
      }

      if (!config_loaded) {
        sipiConf.setKeepAlive(optKeepAlive);
      } else {
        if (!sipiopt.get_option("--keepalive")->empty()) sipiConf.setKeepAlive(optKeepAlive);
      }

      if (!config_loaded) {
        sipiConf.setNThreads(optNThreads);
      } else {
        if (!sipiopt.get_option("--nthreads")->empty()) sipiConf.setNThreads(optNThreads);
      }

      size_t l = optMaxPostSize.length();
      char c = optMaxPostSize[l - 1];
      tsize_t maxpost_size;
      if (c == 'M') {
        maxpost_size = stoll(optMaxPostSize.substr(0, l - 1)) * 1024 * 1024;
      } else if (c == 'G') {
        maxpost_size = stoll(optMaxPostSize.substr(0, l - 1)) * 1024 * 1024 * 1024;
      } else {
        maxpost_size = stoll(optMaxPostSize);
      }
      if (!config_loaded) {
        sipiConf.setMaxPostSize(maxpost_size);
      } else {
        if (!sipiopt.get_option("--maxpost")->empty()) sipiConf.setMaxPostSize(maxpost_size);
      }

      if (!config_loaded) {
        sipiConf.setImgRoot(optImgroot);
      } else {
        if (!sipiopt.get_option("--imgroot")->empty()) sipiConf.setImgRoot(optImgroot);
      }

      if (!config_loaded) {
        sipiConf.setDocRoot(optDocroot);
      } else {
        if (!sipiopt.get_option("--docroot")->empty()) sipiConf.setDocRoot(optDocroot);
      }

      if (!config_loaded) {
        sipiConf.setWWWRoute(optWWWRoute);
      } else {
        if (!sipiopt.get_option("--wwwroute")->empty()) sipiConf.setWWWRoute(optWWWRoute);
      }

      if (!config_loaded) {
        sipiConf.setScriptDir(optScriptDir);
      } else {
        if (!sipiopt.get_option("--scriptdir")->empty()) sipiConf.setScriptDir(optScriptDir);
      }

      if (!config_loaded) {
        sipiConf.setTmpDir(optTmpdir);
      } else {
        if (!sipiopt.get_option("--tmpdir")->empty()) sipiConf.setTmpDir(optTmpdir);
      }

      if (!config_loaded) {
        sipiConf.setMaxTempFileAge(optMaxTmpAge);
      } else {
        if (!sipiopt.get_option("--maxtmpage")->empty()) sipiConf.setMaxTempFileAge(optMaxTmpAge);
      }

      if (!config_loaded) {
        sipiConf.setPrefixAsPath(optPathprefix);
      } else {
        if (!sipiopt.get_option("--pathprefix")->empty()) sipiConf.setPrefixAsPath(optPathprefix);
      }

      if (!config_loaded) {
        sipiConf.setSubdirLevels(optSubdirLevels);
      } else {
        if (!sipiopt.get_option("--subdirlevels")->empty()) sipiConf.setSubdirLevels(optSubdirLevels);
      }

      if (!config_loaded) {
        sipiConf.setSubdirExcludes(optSubdirExcludes);
      } else {
        if (!sipiopt.get_option("--subdirexcludes")->empty()) sipiConf.setSubdirExcludes(optSubdirExcludes);
      }

      if (!config_loaded) {
        sipiConf.setInitScript(optInitscript);
      } else {
        if (!sipiopt.get_option("--initscript")->empty()) sipiConf.setInitScript(optInitscript);
      }

      if (!config_loaded) {
        sipiConf.setCacheDir(optCachedir);
      } else {
        if (!sipiopt.get_option("--cachedir")->empty()) sipiConf.setCacheDir(optCachedir);
      }

      l = optCacheSize.length();
      c = optCacheSize[l - 1];
      tsize_t cache_size;
      if (c == 'M') {
        cache_size = stoll(optCacheSize.substr(0, l - 1)) * 1024 * 1024;
      } else if (c == 'G') {
        cache_size = stoll(optCacheSize.substr(0, l - 1)) * 1024 * 1024 * 1024;
      } else {
        cache_size = stoll(optCacheSize);
      }
      if (!config_loaded) {
        sipiConf.setCacheSize(cache_size);
      } else {
        if (!sipiopt.get_option("--cachesize")->empty()) sipiConf.setCacheSize(cache_size);
      }

      if (!config_loaded) {
        sipiConf.setCacheNFiles(optCacheNFiles);
      } else {
        if (!sipiopt.get_option("--cachenfiles")->empty()) sipiConf.setCacheNFiles(optCacheNFiles);
      }

      if (!config_loaded) {
        sipiConf.setCacheHysteresis(optCacheHysteresis);
      } else {
        if (!sipiopt.get_option("--cachehysteresis")->empty()) sipiConf.setCacheHysteresis(optCacheHysteresis);
      }

      if (!config_loaded) {
        sipiConf.setThumbSize(optThumbSize);
      } else {
        if (!sipiopt.get_option("--thumbsize")->empty()) sipiConf.setThumbSize(optThumbSize);
      }

      if (!config_loaded) {
        sipiConf.setSSLCertificate(optSSLCertificatePath);
      } else {
        if (!sipiopt.get_option("--sslcert")->empty()) sipiConf.setSSLCertificate(optSSLCertificatePath);
      }

      if (!config_loaded) {
        sipiConf.setSSLKey(optSSLKeyPath);
      } else {
        if (!sipiopt.get_option("--sslkey")->empty()) sipiConf.setSSLKey(optSSLKeyPath);
      }

      if (!config_loaded) {
        sipiConf.setJwtSecret(optJWTKey);
      } else {
        if (!sipiopt.get_option("--jwtkey")->empty()) sipiConf.setJwtSecret(optJWTKey);
      }

      if (!config_loaded) {
        sipiConf.setAdminUser(optAdminUser);
      } else {
        if (!sipiopt.get_option("--adminuser")->empty()) sipiConf.setAdminUser(optAdminUser);
      }

      if (!config_loaded) {
        sipiConf.setPasswort(optAdminPassword);
      } else {
        if (!sipiopt.get_option("--adminpasswd")->empty()) sipiConf.setPasswort(optAdminPassword);
      }

      if (!config_loaded) {
        sipiConf.setKnoraPath(optKnoraPath);
      } else {
        if (!sipiopt.get_option("--knorapath")->empty()) sipiConf.setKnoraPath(optKnoraPath);
      }

      if (!config_loaded) {
        sipiConf.setKnoraPort(optKnoraPort);
      } else {
        if (!sipiopt.get_option("--knoraport")->empty()) sipiConf.setKnoraPort(optKnoraPort);
      }

      if (!config_loaded) {
        sipiConf.setLogfile(optLogfilePath);
      } else {
        if (!sipiopt.get_option("--logfile")->empty()) sipiConf.setLogfile(optLogfilePath);
      }

      std::string loglevelstring;
      switch (optLogLevel) {
      case LL_DEBUG:
        loglevelstring = "DEBUG";
        break;
      case LL_INFO:
        loglevelstring = "INFO";
        break;
      case LL_NOTICE:
        loglevelstring = "NOTICE";
        break;
      case LL_WARNING:
        loglevelstring = "WARNING";
        break;
      case LL_ERR:
        loglevelstring = "ERR";
        break;
      case LL_CRIT:
        loglevelstring = "CRIT";
        break;
      case LL_ALERT:
        loglevelstring = "ALERT";
        break;
      case LL_EMERG:
        loglevelstring = "EMERG";
        break;
      }
      if (!config_loaded) {
        sipiConf.setLogLevel(loglevelstring);
      } else {
        if (!sipiopt.get_option("--loglevel")->empty()) sipiConf.setLogLevel(loglevelstring);
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
      if (!optSipiSentryDsn.empty()) log_info("SIPI_SENTRY_DSN: %s", optSipiSentryDsn.c_str());

      if (!optSipiSentryEnvironment.empty()) log_info("SIPI_SENTRY_ENVRONMENT: %s", optSipiSentryEnvironment.c_str());

      if (!optSipiSentryRelease.empty()) log_info("SIPI_SENTRY_Release: %s", optSipiSentryRelease.c_str());

      // Create object SipiHttpServer
      auto nthreads = sipiConf.getNThreads();
      if (nthreads < 1) { nthreads = std::thread::hardware_concurrency(); }
      Sipi::SipiHttpServer server(
        sipiConf.getPort(), nthreads, sipiConf.getUseridStr(), sipiConf.getLogfile(), sipiConf.getLoglevel());

      log_info("BUILD_TIMESTAMP: %s", BUILD_TIMESTAMP);
      log_info("BUILD_SCM_TAG: %s", BUILD_SCM_TAG);
      log_info("BUILD_SCM_REVISION: %s", BUILD_SCM_REVISION);

      server.ssl_port(sipiConf.getSSLPort());// set the secure connection port (-1 means no ssl socket)
      std::string tmps = sipiConf.getSSLCertificate();
      server.ssl_certificate(tmps);
      tmps = sipiConf.getSSLKey();
      server.ssl_key(tmps);
      server.jwt_secret(sipiConf.getJwtSecret());

      // set tmpdir for uploads (defined in sipi.config.lua)
      server.tmpdir(sipiConf.getTmpDir());
      server.max_post_size(sipiConf.getMaxPostSize());
      server.scriptdir(
        sipiConf.getScriptDir());// set the directory where the Lua scripts are found for the "Lua"-routes
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
    } catch (shttps::Error &err) {
      log_err("Error starting server: %s", err.what());
      std::cerr << err << '\n';
    }
  }
  // make sure everything flushes.
  sentry_close();
  return EXIT_SUCCESS;
}
