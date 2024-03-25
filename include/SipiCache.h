/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef __defined_sipi_cache_h
#define __defined_sipi_cache_h

#include <algorithm>
#include <ctime>
#include <mutex>
#include <string>
#include <sys/time.h>
#include <unordered_map>
#include <unordered_set>

#include "SipiConfig.h"

namespace Sipi {

/*!
 * SipiCache handles all the caching of files. Whenever a request to the IIIF server is made
 * SIPI first looks in its cache if this version of the file is available. IF not, the file
 * is generated (= decoded) and simultaneously written to the server socket and the chache file.
 * If the file has already been cached, the cached version is sent (but only, if the "original"
 * is older than the cached file. In order to identify the different versions, the
 * canonocal URL according to the IIIF 2.0 standard is used.
 */
class SipiCache
{
public:
  typedef enum {
    SORT_ATIME_ASC,
    SORT_ATIME_DESC,
    SORT_FSIZE_ASC,
    SORT_FSIZE_DESC,
  } SortMethod;

  /*!
   * A struct which is used to read/write the file containing all cache information on
   * server start or server shutdown.
   */
  typedef struct
  {
    size_t img_w, img_h;
    size_t tile_w, tile_h;
    int clevels;
    int numpages;
    char canonical[256];
    char origpath[256];
    char cachepath[256];
#if defined(HAVE_ST_ATIMESPEC)
    struct timespec mtime;//!< entry time into cache
#else
    time_t mtime;
#endif
    time_t access_time;//!< last access in seconds
    off_t fsize;
  } FileCacheRecord;

  /*!
   * SipiRecord is used to form a in-memory list of all cached files. On startup of the server,
   * the cached files are read from a file (being a serialization of FileCacheRecord). On
   * server shutdown, the in-memory representation is again ritten to a file.
   */
  typedef struct _CacheRecord
  {
    size_t img_w, img_h;
    size_t tile_w, tile_h;
    int clevels;
    int numpages;
    std::string origpath;
    std::string cachepath;
#if defined(HAVE_ST_ATIMESPEC)
    struct timespec mtime;//!< entry time into cache
#else
    time_t mtime;
#endif
    time_t access_time;//!< last access in seconds
    off_t fsize;
  } CacheRecord;

  /*!
   * SizeRecord is used to create a map of the original filenames in the image
   * directory and the sizes of the full images.
   */
  typedef struct
  {
    size_t img_w;
    size_t img_h;
    size_t tile_w;
    size_t tile_h;
    int clevels;
    int numpages;
#if defined(HAVE_ST_ATIMESPEC)
    struct timespec mtime;//!< entry time into cache
#else
    time_t mtime;
#endif
  } SizeRecord;


  /*!
   * This is the prototype function to used as parameter for the method SipiCache::loop
   * which is applied to all cached files.
   */
  typedef void (*ProcessOneCacheFile)(int index, const std::string &, const SipiCache::CacheRecord &, void *userdata);

private:
  std::mutex locking;
  std::string _cachedir;//!< path to the cache directory
  std::unordered_map<std::string, CacheRecord> cachetable;//!< Internal map of all cached files
  std::unordered_map<std::string, SizeRecord> sizetable;//!< Internal map of original file paths and image size
  std::unordered_map<std::string, int> blocked_files;
  unsigned long long cachesize;//!< number of bytes in the cache
  unsigned long long max_cachesize;//!< maximum number of bytes that can be cached
  unsigned nfiles;//!< number of files in cache
  unsigned max_nfiles;//!< maximum number of files that can be cached
  float cache_hysteresis;//!< If files are purged, what percentage we go below the maximum
public:
  /*!
   * Create a Cache instance an initialized the cache.
   *
   * Create the cache, read if available, the cache file containing all the files that
   * are already in the cache directory. The size of the cache can be limited to a maximum number
   * of files and a maxi,um size in bytes. The first limit that is readed will purge the cache.
   *
   * \param[in] cachedir_p Path to the cache directory. The directory must exist!
   * \param[in] max_cachesize_p Maximum size of the cache in bytes.
   * \param[in] max_nfiles_p Maximum number of files in the cache.
   * \param[in] cache_hysteresis_p If the maximum size of the cache is reached, some of the files that
   * have not been accessed recently will be deleted. The cache_hysteresis (between 0.0 and 1.0) defines the
   * amount of bytes that have to be cleared in relation to the max_cachesize_p.
   */
  SipiCache(const std::string &cachedir_p,
    long long max_cachesize_p = 0,
    unsigned max_nfiles_p = 0,
    float cache_hysteresis_p = 0.1);

  /*!
   * Cleans up the cache, serializes the actual cache content into a file and closes all caching
   * activities.
   */
  ~SipiCache();

  /*!
   * Function used to compare last access times (for sorting the list of cache files).
   *
   * \param[in] t1 First timestamp
   * \param[in] t2 Second timestamp
   *
   * \freturns true or false,
   */
#if defined(HAVE_ST_ATIMESPEC)

  int tcompare(struct timespec &t1, struct timespec &t2);

#else
  int tcompare(time_t &t1, time_t &t2);
#endif

  /*!
   * Purge the cache to make room for more files. Uses the cache_hysteresis, max_cachesize and max_nfiles values
   * for the amount of files that should be purged.
   *
   * \param[in] Use the cache-lock mutex
   *
   * \returns Number of files being purged.
   */
  int purge(bool use_lock = true);

  /*!
   * check if a file is already in the cache and up-to-date
   *
   * \param[in] origpath_p The original path to the master file
   * \param[in] canonical_p The canonical URL according to the IIIF standard
   *
   * \returns Returns an empty string if the file is not in the cache or if the file needs to be replaced.
   *          Otherwise returns tha path to the cached file.
   */
  std::string check(const std::string &origpath_p, const std::string &canonical_p, bool block_file = false);

  void deblock(std::string res);


  /*!
   * Creates a new cache file with a unique name.
   *
   * \return the name of the file.
   */
  std::string getNewCacheFileName(void);

  /*!
   * Add (or replace) a file to the cache.
   *
   * \param[in] origpath_p Path to the original master file
   * \param[in] canonical_p Canonical IIIF URL
   * \param[in] cachepath_p Path of the cache file
   */
  void add(const std::string &origpath_p,
    const std::string &canonical_p,
    const std::string &cachepath_p,
    size_t img_w_p,
    size_t img_h_p,
    size_t tile_w_p = 0,
    size_t tile_h_p = 0,
    int clevels_p = 0,
    int numpages_p = 0);

  /*!
   * Remove one file from the cache
   *
   * \param[in] canonical_p IIIF canonical URL of the file to remove from the cache
   */
  bool remove(const std::string &canonical_p);

  /*!
   * Get the current size of the cache in bytes
   * \returns Size of cache in bytes
   */
  inline unsigned long long getCachesize(void) { return cachesize; }

  /*!
   * Get the maximal size of the cache
   * \returns Maximal size of cache in bytes
   */
  inline unsigned long long getMaxCachesize(void) { return max_cachesize; }

  /*!
   * get the number of cached files
   * \returns Number of cached files
   */
  inline unsigned getNfiles(void) { return nfiles; }

  /*!
   * Get the maximal number of cached files
   * \returns The maximal number of files that can be cached.
   */
  inline unsigned getMaxNfiles(void) { return max_nfiles; }

  /*!
   * get the path to the cache directory
   * \returns Path of the cache directory
   */
  inline std::string getCacheDir(void) { return _cachedir; }

  /*!
   * Loop over all cached files and apply the worker function (e.g. to collect furter
   * information about the cached files.
   *
   * \param[in] worker Function to be called for each cached file
   * \param[in] userdata Arbitrary pointer to userdata gived as parameter to each call of worker
   * \param[in] Sort method used to determine the sequence how the cache is processed.
   */
  void loop(ProcessOneCacheFile worker, void *userdata, SortMethod sm = SORT_ATIME_ASC);

  /*!
   * Returns the sie of the image, if the file has ben cached
   *
   * \param[in] original filename
   * \param[out] img_w Width of original image in pixels
   * \param[out] img_h Height of original image in pixels
   */
  bool getSize(const std::string &origname_p,
    size_t &img_w,
    size_t &img_h,
    size_t &tile_w,
    size_t &tile_h,
    int &clevels,
    int &numpages);
};
}// namespace Sipi

#endif
