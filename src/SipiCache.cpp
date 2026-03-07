/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>


#ifdef HAVE_MALLOC_H
#include <malloc.h>
#else


#endif

#include <cassert>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>


#include "SipiCache.h"
#include "SipiError.hpp"
#include "shttps/Global.h"
#include "Logger.h"

namespace Sipi {

// Helper: remove all non-dotfiles from a directory, returns count removed
static int clearCacheDir(const std::string &dir)
{
  int removed = 0;
  struct dirent **namelist;
  int n = scandir(dir.c_str(), &namelist, nullptr, alphasort);
  if (n >= 0) {
    while (n--) {
      if (namelist[n]->d_name[0] != '.') {
        std::string ff = dir + "/" + namelist[n]->d_name;
        ::remove(ff.c_str());
        removed++;
      }
      free(namelist[n]);
    }
    free(namelist);
  }
  return removed;
}

typedef struct _AListEle
{
  std::string canonical;
  time_t access_time;
  off_t fsize;

  bool operator<(const _AListEle &str) const { return (difftime(access_time, str.access_time) < 0.); }

  bool operator>(const _AListEle &str) const { return (difftime(access_time, str.access_time) > 0.); }

  bool operator==(const _AListEle &str) const { return (difftime(access_time, str.access_time) == 0.); }
} AListEle;


SipiCache::SipiCache(const std::string &cachedir_p,
  long long max_cache_size_p,
  unsigned max_nfiles_p)
  : _cachedir(cachedir_p), cache_used_bytes(0), max_cache_size(max_cache_size_p), nfiles(0), max_nfiles(max_nfiles_p)
{

  if (access(_cachedir.c_str(), R_OK | W_OK | X_OK) != 0) {
    if (mkdir(_cachedir.c_str(), 0755) != 0) {
      throw SipiError("Cannot create cache directory: " + _cachedir, errno);
    }
    log_info("Created cache directory: %s", _cachedir.c_str());
  }

  std::string cachefilename = _cachedir + "/.sipicache";

  std::ifstream cachefile(cachefilename, std::ios::in | std::ios::binary);

  int skipped = 0;
  int orphans_removed = 0;

  if (cachefile.fail()) {
    //
    // No .sipicache file — crash recovery: clear all files in cache dir
    //
    orphans_removed = clearCacheDir(_cachedir);

    if (orphans_removed > 0) {
      log_warn("Cache index missing — cleared %d orphan files (crash recovery)", orphans_removed);
    }
  } else {
    //
    // .sipicache exists — validate and load
    //
    cachefile.seekg(0, cachefile.end);
    std::streampos length = cachefile.tellg();
    cachefile.seekg(0, cachefile.beg);

    // Check for corrupted index (size not divisible by record size)
    if (length > 0 && (static_cast<size_t>(length) % sizeof(SipiCache::FileCacheRecord) != 0)) {
      log_warn("Cache index corrupted (size %lld not divisible by record size %zu) — clearing cache",
        static_cast<long long>(length), sizeof(SipiCache::FileCacheRecord));
      cachefile.close();

      // Clear all files in cache dir
      orphans_removed = clearCacheDir(_cachedir);
      // Remove the corrupted index file itself
      ::remove(cachefilename.c_str());
    } else {
      int nrecords = static_cast<int>(length / sizeof(SipiCache::FileCacheRecord));

      for (int i = 0; i < nrecords; i++) {
        SipiCache::FileCacheRecord fr;
        cachefile.read((char *)&fr, sizeof(SipiCache::FileCacheRecord));
        std::string accesspath = _cachedir + "/" + fr.cachepath;

        if (access(accesspath.c_str(), R_OK) != 0) {
          log_debug("Cache file \"%s\" not on disk, skipping", fr.cachepath);
          skipped++;
          continue;
        }

        CacheRecord cr;
        cr.img_w = fr.img_w;
        cr.img_h = fr.img_h;
        cr.tile_w = fr.tile_w;
        cr.tile_h = fr.tile_h;
        cr.clevels = fr.clevels;
        cr.numpages = fr.numpages;
        cr.origpath = fr.origpath;
        cr.cachepath = fr.cachepath;
        cr.mtime = fr.mtime;
        cr.access_time = fr.access_time;
        cr.fsize = fr.fsize;
        cache_used_bytes += fr.fsize;
        nfiles++;
        cachetable[fr.canonical] = cr;
        log_debug("Cache loaded file \"%s\"", cr.cachepath.c_str());
      }

      cachefile.close();

      //
      // Scan for orphan files not in the loaded index and delete them
      // Build a set of known cache filenames for O(1) lookup
      //
      std::unordered_set<std::string> known_cache_files;
      known_cache_files.reserve(cachetable.size());
      for (const auto &ele : cachetable) {
        known_cache_files.insert(ele.second.cachepath);
      }

      struct dirent **namelist;
      int n = scandir(_cachedir.c_str(), &namelist, nullptr, alphasort);

      if (n >= 0) {
        while (n--) {
          if (namelist[n]->d_name[0] == '.') {
            free(namelist[n]);
            continue;
          }
          std::string file_on_disk = namelist[n]->d_name;

          if (known_cache_files.find(file_on_disk) == known_cache_files.end()) {
            std::string ff = _cachedir + "/" + file_on_disk;
            log_debug("Orphan file \"%s\" not in cache index, removing", file_on_disk.c_str());
            ::remove(ff.c_str());
            orphans_removed++;
          }

          free(namelist[n]);
        }
        free(namelist);
      }
    }
  }

  // Populate sizetable from cachetable
  for (const auto &ele : cachetable) {
    if (sizetable.find(ele.second.origpath) == sizetable.end()) {
      SipiCache::SizeRecord tmp_cr = { ele.second.img_w,
        ele.second.img_h,
        ele.second.tile_w,
        ele.second.tile_h,
        ele.second.clevels,
        ele.second.numpages,
        ele.second.mtime };
      sizetable[ele.second.origpath] = tmp_cr;
    }
  }

  // If over limits on startup, evict down to 80%
  int evicted = purge(false);

  // Single summary line at INFO level
  log_info("Cache loaded: %u files (%.1f MB), %d skipped, %d orphans removed, %d evicted",
    nfiles.load(), static_cast<double>(cache_used_bytes.load()) / (1024.0 * 1024.0), skipped, orphans_removed, evicted > 0 ? evicted : 0);
}

//============================================================================

SipiCache::~SipiCache()
{
  log_debug("Closing cache...");
  std::string cachefilename = _cachedir + "/.sipicache";
  std::ofstream cachefile(cachefilename, std::ofstream::out | std::ofstream::binary | std::ofstream::trunc);

  if (!cachefile.fail()) {
    for (const auto &ele : cachetable) {
      SipiCache::FileCacheRecord fr;
      fr.img_w = ele.second.img_w;
      fr.img_h = ele.second.img_h;
      fr.tile_w = ele.second.tile_w;
      fr.tile_h = ele.second.tile_h;
      fr.clevels = ele.second.clevels;
      fr.numpages = ele.second.numpages;
      (void)snprintf(fr.canonical, 256, "%s", ele.first.c_str());
      (void)snprintf(fr.origpath, 256, "%s", ele.second.origpath.c_str());
      (void)snprintf(fr.cachepath, 256, "%s", ele.second.cachepath.c_str());
      fr.mtime = ele.second.mtime;
      fr.fsize = ele.second.fsize;
      fr.access_time = ele.second.access_time;
      cachefile.write((char *)&fr, sizeof(SipiCache::FileCacheRecord));
      log_debug("Writing \"%s\" to cache file...", ele.second.cachepath.c_str());
    }
  }

  cachefile.close();
}

//============================================================================

#if defined(HAVE_ST_ATIMESPEC)

int SipiCache::tcompare(struct timespec &t1, struct timespec &t2)
#else
int SipiCache::tcompare(time_t &t1, time_t &t2)
#endif
{
#if defined(HAVE_ST_ATIMESPEC)
  if (t1.tv_sec > t2.tv_sec) {
    return 1;
  } else if (t1.tv_sec < t2.tv_sec) {
    return -1;
  } else {
    if (t1.tv_sec == t2.tv_sec) {
      if (t1.tv_nsec > t2.tv_nsec) {
        return 1;
      } else if (t1.tv_nsec < t2.tv_nsec) {
        return -1;
      } else {
        return 0;
      }
    }
  }
#else
  if (t1 > t2) {
    return 1;
  } else if (t1 < t2) {
    return -1;
  } else {
    return 0;
  }

#endif
  return 0;// should never reached – just for the compiler to suppress a warning...
}

//============================================================================

static bool _compare_access_time_asc(const AListEle &e1, const AListEle &e2)
{
  double d = difftime(e1.access_time, e2.access_time);
  return (d < 0.0);
}

//============================================================================

static bool _compare_access_time_desc(const AListEle &e1, const AListEle &e2)
{
  double d = difftime(e1.access_time, e2.access_time);
  return (d > 0.0);
}

//============================================================================

static bool _compare_fsize_asc(const AListEle &e1, const AListEle &e2) { return (e1.fsize < e2.fsize); }
//============================================================================

static bool _compare_fsize_desc(const AListEle &e1, const AListEle &e2) { return (e1.fsize > e2.fsize); }
//============================================================================

int SipiCache::purge(bool use_lock)
{
  // Acquire lock first so threshold checks are consistent
  std::unique_lock<std::mutex> locking_mutex_guard(locking, std::defer_lock);
  if (use_lock) locking_mutex_guard.lock();

  if ((max_cache_size < 0) && (max_nfiles == 0)) return 0;// unlimited cache, no file limit

  bool size_over = (max_cache_size > 0)
    && (cache_used_bytes >= static_cast<unsigned long long>(max_cache_size));
  bool nfiles_over = (max_nfiles > 0) && (nfiles >= max_nfiles);

  if (!size_over && !nfiles_over) return 0;

  int n = 0;

  // Low-water marks: 80% of configured limits
  unsigned long long size_low = (max_cache_size > 0)
    ? static_cast<unsigned long long>(max_cache_size * 0.8) : 0;
  unsigned nfiles_low = (max_nfiles > 0)
    ? static_cast<unsigned>(max_nfiles * 0.8) : 0;

  // Build sorted list — use partial_sort to only sort the portion we need to evict
  std::vector<AListEle> alist;
  alist.reserve(cachetable.size());
  for (const auto &ele : cachetable) {
    alist.push_back({ ele.first, ele.second.access_time, ele.second.fsize });
  }

  std::sort(alist.begin(), alist.end(), _compare_access_time_asc);

  for (const auto &ele : alist) {
    // Check if BOTH limits are at or below low-water
    bool size_ok = (max_cache_size <= 0) || (cache_used_bytes <= size_low);
    bool nfiles_ok = (max_nfiles == 0) || (nfiles <= nfiles_low);
    if (size_ok && nfiles_ok) break;

    auto ct_it = cachetable.find(ele.canonical);
    if (ct_it == cachetable.end()) continue;// already erased

    std::string delpath = _cachedir + "/" + ct_it->second.cachepath;

    auto blocked_it = blocked_files.find(delpath);
    if (blocked_it != blocked_files.end() && blocked_it->second > 0) {
      log_debug("Skipping blocked cache file for %s", ele.canonical.c_str());
      continue;
    }

    log_debug("Purging from cache \"%s\"...", ct_it->second.cachepath.c_str());
    ::unlink(delpath.c_str());
    cache_used_bytes -= ct_it->second.fsize;
    --nfiles;
    ++n;
    cachetable.erase(ct_it);
  }

  // Check if we couldn't free enough space (all remaining files blocked)
  bool size_ok = (max_cache_size <= 0) || (cache_used_bytes <= size_low);
  bool nfiles_ok = (max_nfiles == 0) || (nfiles <= nfiles_low);
  if (!size_ok || !nfiles_ok) {
    log_warn("Cache full and all remaining files are blocked. New file will not be cached.");
    return -1;
  }

  return n;
}

//============================================================================

std::string SipiCache::check(const std::string &origpath_p, const std::string &canonical_p, bool block_file)
{
  struct stat fileinfo;
  SipiCache::CacheRecord fr;

  if (stat(origpath_p.c_str(), &fileinfo) != 0) {
    throw SipiError("Couldn't stat file \"" + origpath_p + "\"!", errno);
  }
#if defined(HAVE_ST_ATIMESPEC)
  struct timespec mtime = fileinfo.st_mtimespec;
#else
  time_t mtime = fileinfo.st_mtime;
#endif

  std::string res;

  std::lock_guard<std::mutex> locking_mutex_guard(locking);
  auto it = cachetable.find(canonical_p);
  if (it == cachetable.end()) {
    return res;// return empty string, because we didn't find the file in cache
  }
  fr = it->second;

  //
  // get the current time (seconds since Epoch)
  //
  time_t at;
  time(&at);
  it->second.access_time = at;// update the access time!

  if (tcompare(mtime, fr.mtime) > 0) {
    // original file is newer than cache, we have to replace it...
    return res;// return empty string, means "replace the file in the cache!"
  } else {
    std::string res = _cachedir + "/" + fr.cachepath;
    if (block_file) { blocked_files[res]++; }
    return res;
  }
}

//============================================================================

void SipiCache::deblock(const std::string &res)
{
  std::lock_guard<std::mutex> locking_mutex_guard(locking);
  auto it = blocked_files.find(res);
  if (it == blocked_files.end()) return;
  --(it->second);
  if (it->second < 1) { blocked_files.erase(it); }
}

/*!
 * Creates a new cache file with a unique name.
 *
 * \return the name of the file.
 */
std::string SipiCache::getNewCacheFileName(void)
{
  std::string filename = _cachedir + "/cache_XXXXXXXXXX";
  char *c_filename = &filename[0];
  int tmp_fd = mkstemp(c_filename);

  if (tmp_fd == -1) {
    throw SipiError(std::string("Couldn't create cache file ") + filename, errno);
  }

  close(tmp_fd);
  return filename;
}

//============================================================================

void SipiCache::add(const std::string &origpath_p,
  const std::string &canonical_p,
  const std::string &cachepath_p,
  size_t img_w_p,
  size_t img_h_p,
  size_t tile_w_p,
  size_t tile_h_p,
  int clevels_p,
  int numpages_p)
{
  size_t pos = cachepath_p.rfind('/');
  std::string cachepath;

  if (pos != std::string::npos) {
    cachepath = cachepath_p.substr(pos + 1);
  } else {
    cachepath = cachepath_p;
  }

  struct stat fileinfo;
  SipiCache::CacheRecord fr;
  SipiCache::SizeRecord sr;

  fr.img_w = sr.img_w = img_w_p;
  fr.img_h = sr.img_h = img_h_p;
  fr.tile_w = sr.tile_w = tile_w_p;
  fr.tile_h = sr.tile_h = tile_h_p;
  fr.clevels = sr.clevels = clevels_p;
  fr.numpages = sr.numpages = numpages_p;
  fr.origpath = origpath_p;
  fr.cachepath = cachepath;

  if (stat(cachepath_p.c_str(), &fileinfo) != 0) {
    throw SipiError("Couldn't stat file \"" + origpath_p + "\"!", errno);
  }
#if defined(HAVE_ST_ATIMESPEC)
  fr.mtime = fileinfo.st_mtimespec;
#else
  fr.mtime = fileinfo.st_mtime;
#endif

  //
  // get the current time (seconds since Epoch)
  //
  time_t at;
  time(&at);
  fr.access_time = at;
  fr.fsize = fileinfo.st_size;

  //
  // we check if there is already a file with the same canonical name. If so,
  // we remove it
  //
  std::lock_guard<std::mutex> locking_mutex_guard(locking);
  auto existing = cachetable.find(canonical_p);
  if (existing != cachetable.end()) {
    std::string toremove = _cachedir + "/" + existing->second.cachepath;
    ::unlink(toremove.c_str());
    cache_used_bytes -= existing->second.fsize;
    --nfiles;
  }

  int purge_result = purge(false);

  if (purge_result == -1) {
    // All files are blocked and cache is full — don't add this file
    std::string toremove = _cachedir + "/" + cachepath;
    ::unlink(toremove.c_str());
    return;
  }

  cachetable[canonical_p] = fr;
  cache_used_bytes += fr.fsize;

  SipiCache::SizeRecord tmp_cr = { img_w_p, img_h_p, tile_w_p, tile_h_p, clevels_p, numpages_p, fr.mtime };
  sizetable[origpath_p] = tmp_cr;

  ++nfiles;
}

//============================================================================

bool SipiCache::remove(const std::string &canonical_p)
{
  std::lock_guard<std::mutex> locking_mutex_guard(locking);

  auto it = cachetable.find(canonical_p);
  if (it == cachetable.end()) {
    log_warn("Couldn't remove cache for %s: not existing!", canonical_p.c_str());
    return false;
  }

  std::string delpath = _cachedir + "/" + it->second.cachepath;
  auto blocked_it = blocked_files.find(delpath);
  if (blocked_it != blocked_files.end() && blocked_it->second > 0) {
    log_warn("Couldn't remove cache for %s: file in use (%d)!", canonical_p.c_str(), blocked_it->second);
    return false;
  }
  log_debug("Delete from cache \"%s\"...", it->second.cachepath.c_str());
  ::remove(delpath.c_str());
  cache_used_bytes -= it->second.fsize;
  cachetable.erase(it);
  --nfiles;

  return true;
}

//============================================================================

void SipiCache::loop(ProcessOneCacheFile worker, void *userdata, SortMethod sm)
{
  std::lock_guard<std::mutex> locking_mutex_guard(locking);
  std::vector<AListEle> alist;

  for (const auto &ele : cachetable) {
    AListEle al = { ele.first, ele.second.access_time, ele.second.fsize };
    alist.push_back(al);
  }

  switch (sm) {
  case SORT_ATIME_ASC: {
    sort(alist.begin(), alist.end(), _compare_access_time_asc);
    break;
  }

  case SORT_ATIME_DESC: {
    sort(alist.begin(), alist.end(), _compare_access_time_desc);
    break;
  }

  case SORT_FSIZE_ASC: {
    sort(alist.begin(), alist.end(), _compare_fsize_asc);
    break;
  }

  case SORT_FSIZE_DESC: {
    sort(alist.begin(), alist.end(), _compare_fsize_desc);
    break;
  }
  }

  int i = 1;

  for (const auto &ele : alist) {
    worker(i, ele.canonical, cachetable[ele.canonical], userdata);
    i++;
  }
}

//============================================================================

bool SipiCache::getSize(const std::string &origname_p,
  size_t &img_w,
  size_t &img_h,
  size_t &tile_w,
  size_t &tile_h,
  int &clevels,
  int &numpages)
{
  struct stat fileinfo;
  if (stat(origname_p.c_str(), &fileinfo) != 0) {
    throw SipiError("Couldn't stat file \"" + origname_p + "\"!", errno);
  }
#if defined(HAVE_ST_ATIMESPEC)
  struct timespec mtime = fileinfo.st_mtimespec;
#else
  time_t mtime = fileinfo.st_mtime;
#endif

  std::lock_guard<std::mutex> locking_mutex_guard(locking);
  auto it = sizetable.find(origname_p);
  if (it == sizetable.end()) {
    return false;
  }

  if (tcompare(mtime, it->second.mtime) > 0) {
    // original file is newer than cache, we have to replace it..
    sizetable.erase(it);
    return false;// means "replace the file in the cache"
  }

  img_w = it->second.img_w;
  img_h = it->second.img_h;
  tile_w = it->second.tile_w;
  tile_h = it->second.tile_h;
  clevels = it->second.clevels;
  numpages = it->second.numpages;

  return true;
}

//============================================================================
}
