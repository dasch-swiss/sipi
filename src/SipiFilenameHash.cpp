//
// Created by Lukas Rosenthaler on 04.06.17.
//
#include <iostream>
#include <fstream>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>

#include "Logger.h"
#include "SipiFilenameHash.h"
#include "shttps/Error.h"

int SipiFilenameHash::__levels = 0;

SipiFilenameHash::SipiFilenameHash(const std::string &path_p)
  : path(path_p)
{
  unsigned int hashval = 0;

  size_t pos = path.rfind("/");
  if (pos != std::string::npos) { name = path.substr(pos + 1); } else { name = path; }

  for (auto c : name) { hashval = ((hashval * seed) + c) % modval; }

  hash = new std::vector<char>(hash_len, 0);

  for (int i = 0; i < hash_len; i++) {
    (*hash)[i] = 'A' + hashval % numchars;
    hashval /= numchars;
  }
}


SipiFilenameHash::SipiFilenameHash(const SipiFilenameHash &other) { hash = new std::vector<char>(*(other.hash)); }

SipiFilenameHash &SipiFilenameHash::operator=(const SipiFilenameHash &other)
{
  hash = new std::vector<char>(*(other.hash));
  return *this;
}

SipiFilenameHash::~SipiFilenameHash() { delete hash; }


char &SipiFilenameHash::operator[](int index)
{
  if ((index < 0) || (index >= hash_len)) { throw shttps::Error("Invalid hash index!"); }
  return (*hash)[index];
}


std::string SipiFilenameHash::filepath(void)
{

  std::string outfname = "";
  for (int i = 0; i < __levels; i++) {
    char tmp[3] = { (*hash)[i], '/', '\0' };
    outfname += tmp;
  }
  outfname += name;
  return outfname;
}

static int n_dirs[6];

static int scanDir(const std::string &path, int level)
{
  if (level == 0) { n_dirs[0] = n_dirs[1] = n_dirs[2] = n_dirs[3] = n_dirs[4] = n_dirs[5] = 0; }
  DIR *dirp = opendir(path.c_str());
  if (dirp == nullptr) {
    throw shttps::Error(std::string("Couldn't read directory content! Path: ") + path, errno);
  }
  struct dirent *dp;
  while ((dp = readdir(dirp)) != nullptr) {
    if (dp->d_type == DT_DIR) {
      if ((strlen(dp->d_name) == 1) && (dp->d_name[0] >= 'A' && dp->d_name[0] <= 'Z')) {
        n_dirs[level]++;
        char tmp[3] = { '/', dp->d_name[0], '\0' };
        (void)scanDir(path + tmp, level + 1);
      }
    }
  }
  closedir(dirp);

  int i = 0;
  int k = 26;
  if (level == 0) {
    for (i = 0; i < 6; i++) {
      if (n_dirs[i] == 0) break;
      if (n_dirs[i] != k) throw shttps::Error("Inconsistent directory tree!");
      k *= 26;
    }
  }
  return i;
}

int SipiFilenameHash::check_levels(const std::string &imgdir) { return scanDir(imgdir, 0); }


static void add_level(const std::string &path, int cur_level)
{
  DIR *dirp = opendir(path.c_str());
  if (dirp == nullptr) {
    throw shttps::Error(std::string("Couldn't read directory content! Path: ") + path, errno);
  }
  std::vector<std::string> filelist;
  struct dirent *dp;
  int n_dirs = 0;
  while ((dp = readdir(dirp)) != nullptr) {
    if (dp->d_type == DT_DIR) {
      if ((strlen(dp->d_name) == 1) && (dp->d_name[0] >= 'A' && dp->d_name[0] <= 'Z')) {
        char tmp[3] = { '/', dp->d_name[0], '\0' };
        add_level(path + tmp, cur_level + 1);
        n_dirs++;
      }
    } else if (dp->d_type == DT_REG) {
      if (strcmp(dp->d_name, ".") == 0) continue;
      if (strcmp(dp->d_name, "..") == 0) continue;
      if (dp->d_name[0] == '.') continue;
      filelist.push_back(std::string(dp->d_name));
    }
  }
  closedir(dirp);

  //
  // first create all new subdirs
  //
  if (n_dirs == 0) {
    for (char c = 'A'; c <= 'Z'; c++) {
      char tmp[3] = { '/', c, '\0' };
      std::string newdirname = path + tmp;
      if (mkdir(newdirname.c_str(), 0777)) {
        throw shttps::Error("Creating subdir failed!", errno);
      }
    }
  }

  if (filelist.size() > 0) {
    if (n_dirs != 0) { throw shttps::Error("inconsistency in directory tree!"); }
    for (auto fname : filelist) {
      SipiFilenameHash fhash(fname);
      char tmp[4] = { '/', fhash[cur_level], '/', '\0' };
      std::string newfname(path + tmp + fname);
      std::string oldfname(path + "/" + fname);
      if (std::rename(oldfname.c_str(), newfname.c_str())) {
        throw shttps::Error("Rename/move failed!", errno);
      }
    }
  }
}

static bool remove_level(const std::string &path, int cur_level)
{
  //
  // prepare scanning the directory
  //
  DIR *dirp = opendir(path.c_str());
  if (dirp == nullptr) {
    throw shttps::Error(std::string("Couldn't read directory content! Path: ") + path, errno);
  }
  std::vector<std::string> filelist;
  struct dirent *dp;
  int n_dirs = 0;
  int n_emptied_dirs = 0;
  //
  // start scanning the directory
  //
  while ((dp = readdir(dirp)) != nullptr) {
    if (dp->d_type == DT_DIR) {
      if ((strlen(dp->d_name) == 1) && (dp->d_name[0] >= 'A' && dp->d_name[0] <= 'Z')) {
        //
        // we have subdirs – let's enter them recursively
        //
        char tmp[3] = { '/', dp->d_name[0], '\0' };
        if (remove_level(path + tmp, cur_level + 1)) n_emptied_dirs++;
        n_dirs++;
      }
    } else if (dp->d_type == DT_REG) {
      //
      // we have a file - add it to the list of files to be moved up
      //
      filelist.push_back(std::string(dp->d_name));
    }
  }
  closedir(dirp);

  if (n_dirs == 0) {
    if (cur_level > 0) {
      //
      // we have no more subdirs, so we are down at the end. Let's move
      // all files one level up.
      //
      size_t pos = path.rfind("/");
      if (pos == std::string::npos) throw shttps::Error("Inconsistency!");
      for (auto fname : filelist) {
        std::string newfname = path.substr(0, pos) + "/" + fname;
        std::string oldfname = path + "/" + fname;
        log_debug("rename oldfname: %s newfname: %s", oldfname.c_str(), newfname.c_str());
        if (rename(oldfname.c_str(), newfname.c_str())) {
          throw shttps::Error("Rename/move failed!", errno);
        }
      }
    }
    return true;
  }
  if (n_emptied_dirs == 26) {
    //
    // we have emptied all subdirs – let's remove them
    //
    //size_t pos = path.rfind("/");
    for (char c = 'A'; c <= 'Z'; c++) {
      char tmp[3] = { '/', c, '\0' };
      //std::string dirname = path.substr(0, pos) + tmp;
      std::string dirname = path + tmp;
      if (rmdir(dirname.c_str())) {
        throw shttps::Error(std::string("rmdir failed! ") + dirname, errno);
      }
    }
  }
  return false;
}


void SipiFilenameHash::migrateToLevels(const std::string &imgdir, int levels)
{
  int act_levels = check_levels(imgdir);
  log_debug("act_levels=%d levels=%d", act_levels, levels);
  if (levels > act_levels) { for (int i = 0; i < (levels - act_levels); i++) { add_level(imgdir, 0); } } else if (
    levels < act_levels) {
    log_debug("Removing levels...");
    for (int i = 0; i < (act_levels - levels); i++) {
      log_debug("Remove a level...");
      (void)remove_level(imgdir, 0);
    }
  }
}
