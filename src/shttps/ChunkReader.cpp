/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <algorithm>
#include <cctype>
#include <fcntl.h>
#include <functional>
#include <iomanip>
#include <iostream>
#include <locale>
#include <netinet/in.h>
#include <regex>
#include <sstream>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <unistd.h>//write

#include "ChunkReader.h"
#include "Connection.h"
#include "Error.h"

using namespace std;

namespace shttps {

ChunkReader::ChunkReader(std::istream *ins_p, size_t post_maxsize_p) : ins(ins_p), post_maxsize(post_maxsize_p)
{
  chunk_size = 0;
  chunk_pos = 0;
};
//=========================================================================

size_t ChunkReader::read_chunk(istream &ins, char **buf, size_t offs)
{
  string line;
  (void)safeGetline(ins, line, max_headerline_len);// read chunk size

  if (ins.fail() || ins.eof()) { throw INPUT_READ_FAIL; }

  size_t n;

  try {
    n = stoul(line, 0, 16);
  } catch (const std::invalid_argument &ia) {
    throw Error(ia.what());
  }

  //
  // Check for post_maxsize
  //
  if ((post_maxsize > 0) && (n > post_maxsize)) {
    stringstream ss;
    ss << "Chunksize (" << n << ") to big (maxsize=" << post_maxsize << ")";
    throw Error(ss.str());
  }

  if (n == 0) return 0;

  if (*buf == nullptr) {
    if ((*buf = (char *)malloc((n + 1) * sizeof(char))) == nullptr) {
      throw Error("malloc failed", errno);
    }
  } else {
    if ((*buf = (char *)realloc(*buf, (offs + n + 1) * sizeof(char))) == nullptr) {
      throw Error("realloc failed", errno);
    }
  }

  ins.read(*buf + offs, n);
  if (ins.fail() || ins.eof()) { throw -1; }

  (*buf)[offs + n] = '\0';
  (void)safeGetline(ins, line, max_headerline_len);// read "\r\n" at end of chunk...

  if (ins.fail() || ins.eof()) { throw INPUT_READ_FAIL; }

  return n;
}
//=========================================================================


size_t ChunkReader::readAll(char **buf)
{
  size_t n, nbytes = 0;
  *buf = nullptr;

  while ((n = read_chunk(*ins, buf, nbytes)) > 0) {
    nbytes += n;
    //
    // check for post_maxsize
    //
    if ((post_maxsize > 0) && (nbytes > post_maxsize)) {
      stringstream ss;
      ss << "Chunksize (" << nbytes << ") to big (maxsize=" << post_maxsize << ")";
      throw Error(ss.str());
    }
  }

  return nbytes;
}
//=========================================================================

size_t ChunkReader::getline(std::string &t)
{
  t.clear();

  size_t n = 0;
  while (true) {
    if (chunk_pos >= chunk_size) {
      string line;

      (void)safeGetline(*ins, line, max_headerline_len);// read chunk size
      if (ins->fail() || ins->eof()) { throw INPUT_READ_FAIL; }

      try {
        chunk_size = stoul(line, 0, 16);
      } catch (const std::invalid_argument &ia) {
        throw Error(ia.what());
      }

      //
      // check for post_maxsize
      //
      if ((post_maxsize > 0) && (chunk_size > post_maxsize)) {
        stringstream ss;
        ss << "Chunksize (" << chunk_size << ") to big (maxsize=" << post_maxsize << ")";
        throw Error(ss.str());
      }

      if (chunk_size == 0) {
        (void)safeGetline(*ins, line, max_headerline_len);// get last "\r\n"....
        if (ins->fail() || ins->eof()) { throw INPUT_READ_FAIL; }
        return n;
      }
      chunk_pos = 0;
    }

    std::istream::sentry se(*ins, true);
    std::streambuf *sb = ins->rdbuf();
    int c = sb->sbumpc();
    chunk_pos++;

    if (chunk_pos >= chunk_size) {
      string line;
      (void)safeGetline(*ins, line, max_headerline_len);// read "\r\n" at end of  chunk...
      if (ins->fail() || ins->eof()) { throw INPUT_READ_FAIL; }
    }

    n++;

    switch (c) {
    case '\n':
      return n;
    case '\r':
      if (sb->sgetc() == '\n') {
        sb->sbumpc();
        chunk_pos++;
        if (chunk_pos >= chunk_size) {
          string line;
          (void)safeGetline(*ins, line, max_headerline_len);// read "\r\n" at end of  chunk...
          if (ins->fail() || ins->eof()) { throw INPUT_READ_FAIL; }
        }
        n++;
      }
      return n;
    case EOF:
      // Also handle the case when the last line has no line ending
      if (t.empty()) ins->setstate(std::ios::eofbit);
      return n;
    default:
      t += (char)c;
    }
  }
}
//=========================================================================

int ChunkReader::getc(void)
{
  if (chunk_pos >= chunk_size) {
    string line;
    (void)safeGetline(*ins, line, max_headerline_len);// read the size of the new chunk

    if (ins->fail() || ins->eof()) { throw INPUT_READ_FAIL; }

    try {
      chunk_size = stoul(line, 0, 16);
    } catch (const std::invalid_argument &ia) {
      throw Error(ia.what() + line);
    }

    //
    // check for post_maxsize
    //
    if ((post_maxsize > 0) && (chunk_size > post_maxsize)) {
      stringstream ss;
      ss << "Chunksize (" << chunk_size << ") to big (maxsize=" << post_maxsize << ")";
      throw Error(ss.str());
    }

    if (chunk_size == 0) {
      (void)safeGetline(*ins, line, max_headerline_len);// get last "\r\n"....
      if (ins->fail() || ins->eof()) { throw INPUT_READ_FAIL; }
      return EOF;
    }
    chunk_pos = 0;
  }

  std::istream::sentry se(*ins, true);
  std::streambuf *sb = ins->rdbuf();
  int c = sb->sbumpc();
  chunk_pos++;

  if (chunk_pos >= chunk_size) {
    string line;
    (void)safeGetline(*ins, line, max_headerline_len);// read "\r\n" at end of chunk...
    if (ins->fail() || ins->eof()) { throw INPUT_READ_FAIL; }
  }
  return c;
}
//=========================================================================


}// namespace shttps
