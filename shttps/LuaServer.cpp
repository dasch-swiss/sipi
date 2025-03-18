/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>// Needed for memset
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>

// #include <sys/types.h>
#include <arpa/inet.h>
#include <dirent.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "Connection.h"
#include "Error.h"
#include "Global.h"
#include "LuaServer.h"
#include "Parsing.h"
#include "Server.h"
#include "SockStream.h"
#include "curl/curl.h"

#include "Logger.h"
#include "jwt.h"
#include "sole.hpp"
#include <jansson.h>

using ms = std::chrono::milliseconds;
using get_time = std::chrono::steady_clock;

static const char servertablename[] = "server";

namespace shttps {

char luaconnection[] = "__shttpsconnection";

/*!
 * Dumps the Lua stack to a string.
 * @param L the Lua interpreter.
 * @return a string containing the stack dump.
 */
static std::string stackDump(lua_State *L)
{
  std::ostringstream errorMsg;
  errorMsg << "Lua error. Stack dump: ";
  int top = lua_gettop(L);

  for (int i = 1; i <= top; i++) {
    if (i > 1) { errorMsg << " | "; }

    int t = lua_type(L, i);
    switch (t) {
    case LUA_TSTRING:
      errorMsg << lua_tostring(L, i);
      break;

    case LUA_TBOOLEAN:
      errorMsg << (lua_toboolean(L, i) ? "true" : "false");
      break;

    case LUA_TNUMBER:
      errorMsg << lua_tonumber(L, i);
      break;

    default:
      errorMsg << lua_typename(L, t);
      break;
    }
  }
  return errorMsg.str();
}

/*!
 * Error handler for Lua errors.
 */
static int dont_panic(lua_State *L)
{
  std::string errorMsg = stackDump(L);
  throw Error(errorMsg);
}

/*
 * base64.cpp and base64.h
 *
 * Copyright (C) 2004-2008 René Nyffenegger
 *
 * This source code is provided 'as-is', without any express or implied
 * warranty. In no event will the author be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this source code must not be misrepresented; you must not
 * claim that you wrote the original source code. If you use this source code
 * in a product, an acknowledgment in the product documentation would be
 * appreciated but is not required.
 *
 * 2. Altered source versions must be plainly marked as such, and must not be
 * misrepresented as being the original source code.
 *
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * René Nyffenegger rene.nyffenegger@adp-gmbh.ch
 *
 *
 */

static const std::string base64_chars =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
  "abcdefghijklmnopqrstuvwxyz"
  "0123456789+/";


static inline bool is_base64(unsigned char c) { return (isalnum(c) || (c == '+') || (c == '/')); }

/* NOT YET USED
    static std::string base64_encode(unsigned char const* bytes_to_encode, unsigned int in_len) {
        std::string ret;
        int i = 0;
        int j = 0;
        unsigned char char_array_3[3];
        unsigned char char_array_4[4];

        while (in_len--) {
            char_array_3[i++] = *(bytes_to_encode++);
            if (i == 3) {
                char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
                char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
                char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
                char_array_4[3] = char_array_3[2] & 0x3f;

                for(i = 0; (i <4) ; i++)
                    ret += base64_chars[char_array_4[i]];
                i = 0;
            }
        }

        if (i)
        {
            for(j = i; j < 3; j++)
                char_array_3[j] = '\0';

            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (j = 0; (j < i + 1); j++)
                ret += base64_chars[char_array_4[j]];

            while((i++ < 3))
                ret += '=';

        }

        return ret;

    }
*/

static std::string base64_decode(std::string const &encoded_string)
{
  int in_len = encoded_string.size();
  int i = 0;
  int j = 0;
  int in_ = 0;
  unsigned char char_array_4[4], char_array_3[3];
  std::string ret;

  while (in_len-- && (encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
    char_array_4[i++] = encoded_string[in_];
    in_++;
    if (i == 4) {
      for (i = 0; i < 4; i++) { char_array_4[i] = base64_chars.find(char_array_4[i]); }

      char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
      char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
      char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

      for (i = 0; (i < 3); i++) ret += char_array_3[i];
      i = 0;
    }
  }

  if (i) {
    for (j = i; j < 4; j++) { char_array_4[j] = 0; }

    for (j = 0; j < 4; j++) { char_array_4[j] = base64_chars.find(char_array_4[j]); }

    char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
    char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
    char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

    for (j = 0; j < i - 1; j++) ret += char_array_3[j];
  }

  return ret;
}

/*!
 * Instantiates a Lua server
 */
LuaServer::LuaServer()
{
  if ((L = luaL_newstate()) == nullptr) { throw Error("Couldn't start lua interpreter"); }
  lua_atpanic(L, dont_panic);
  luaL_openlibs(L);
}
//=========================================================================

/*!
 * Instantiates a Lua server
 */
LuaServer::LuaServer(Connection &conn)
{
  if ((L = luaL_newstate()) == nullptr) { throw Error("Couldn't start lua interpreter"); }

  lua_atpanic(L, dont_panic);
  luaL_openlibs(L);
  createGlobals(conn);
}
//=========================================================================

/*!
 * Instantiates a Lua server
 *
 * \param
 */
LuaServer::LuaServer(const std::string &luafile, bool iscode)
{
  if ((L = luaL_newstate()) == nullptr) { throw Error("Couldn't start lua interpreter"); }

  lua_atpanic(L, dont_panic);
  luaL_openlibs(L);

  if (!luafile.empty()) {
    if (iscode) {
      if (luaL_loadstring(L, luafile.c_str()) != 0) { lua_error(L); }
    } else {
      if (luaL_loadfile(L, luafile.c_str()) != 0) { lua_error(L); }
    }

    if (lua_pcall(L, 0, LUA_MULTRET, 0) != 0) { lua_error(L); }
  }
}
//=========================================================================


/*!
 * Instantiates a Lua server and directly executes the script file given
 *
 * \param[in] luafile A file containing a Lua script or a Lua code chunk
 */
LuaServer::LuaServer(Connection &conn, const std::string &luafile, bool iscode, const std::string &lua_scriptdir)
{
  if ((L = luaL_newstate()) == nullptr) { throw Error("Couldn't start lua interpreter"); }

  lua_atpanic(L, dont_panic);
  luaL_openlibs(L);
  createGlobals(conn);

  // add the script directory to the standard search path for lua packages
  // this allows for the inclusion of Lua scripts contained in the Lua script directory
  this->setLuaPath(lua_scriptdir);

  if (!luafile.empty()) {
    if (iscode) {
      if (luaL_loadstring(L, luafile.c_str()) != 0) { lua_error(L); }
    } else {
      if (luaL_loadfile(L, luafile.c_str()) != 0) { lua_error(L); }
    }

    if (lua_pcall(L, 0, LUA_MULTRET, 0) != 0) { lua_error(L); }
  }
}
//=========================================================================

/*!
 * Destroys the Lua server and free's all resources (garbage collectors are called here)
 */
LuaServer::~LuaServer()
{
  lua_close(L);
  L = nullptr;
}
//=========================================================================

/*!
 * Activates the the connection buffer. Optionally the buffer size and increment size can be given
 * LUA: server.setBuffer([bufsize][,incsize])
 */
static int lua_setbuffer(lua_State *L)
{
  size_t bufsize = 0;
  size_t incsize = 0;

  int top = lua_gettop(L);

  if (top > 0) {
    if (lua_isinteger(L, 1)) {
      bufsize = static_cast<size_t>(lua_tointeger(L, 1));
    } else {
      lua_settop(L, 0);// clear stack
      lua_pushboolean(L, false);
      lua_pushstring(L, "'server.setbuffer([bufize][, incsize])': requires bufsize size as integer");
      return 2;
    }
  }

  if (top > 1) {
    if (lua_isinteger(L, 2)) {
      incsize = static_cast<size_t>(lua_tointeger(L, 2));
    } else {
      lua_settop(L, 0);// clear stack
      lua_pushboolean(L, false);
      lua_pushstring(L, "'server.setbuffer([bufize][, incsize])': requires incsize size as integer");
      return 2;
    }
  }

  lua_pop(L, top);
  lua_getglobal(L, luaconnection);// push onto stack
  Connection *conn = (Connection *)lua_touserdata(L, -1);// does not change the stack
  lua_remove(L, -1);// remove from stack

  if ((bufsize > 0) && (incsize > 0)) {
    conn->setBuffer(bufsize, incsize);
  } else if (bufsize > 0) {
    conn->setBuffer(bufsize);
  } else {
    conn->setBuffer();
  }

  lua_pushboolean(L, true);
  lua_pushnil(L);

  return 2;
}
//=========================================================================

/*!
 * Checks the filetype of a given filepath
 * LUA: server.fs.ftype("path")
 * @return "FILE", "DIRECTORY", "CHARDEV", "BLOCKDEV", "LINK", "SOCKET" or "UNKNOWN"
 */
static int lua_fs_ftype(lua_State *L)
{
  struct stat s;
  int top = lua_gettop(L);

  if (top < 1) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.ftype()': parameter missing");
    return 2;
  }

  if (!lua_isstring(L, 1)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.ftype()': filename is not a string");
    return 2;
  }

  const char *filename = lua_tostring(L, 1);
  lua_settop(L, 0);// clear stack

  if (stat(filename, &s) != 0) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, strerror(errno));
    return 2;
  }

  lua_pushboolean(L, true);

  if (S_ISREG(s.st_mode)) {
    lua_pushstring(L, "FILE");
  } else if (S_ISDIR(s.st_mode)) {
    lua_pushstring(L, "DIRECTORY");
  } else if (S_ISCHR(s.st_mode)) {
    lua_pushstring(L, "CHARDEV");
  } else if (S_ISBLK(s.st_mode)) {
    lua_pushstring(L, "BLOCKDEV");
  } else if (S_ISLNK(s.st_mode)) {
    lua_pushstring(L, "LINK");
  } else if (S_ISFIFO(s.st_mode)) {
    lua_pushstring(L, "FIFO");
  } else if (S_ISSOCK(s.st_mode)) {
    lua_pushstring(L, "SOCKET");
  } else {
    lua_pushstring(L, "UNKNOWN");
  }

  return 2;
}
//=========================================================================

/*!
 * Returns the modification time of a given filepath (in s since epoch)
 * LUA: server.fs.modtime("path")
 * @return integer with seconds since epoch of last modification
 */
static int lua_fs_modtime(lua_State *L)
{
  struct stat s;
  int top = lua_gettop(L);

  if (top < 1) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.modtime()': parameter missing");
    return 2;
  }

  if (!lua_isstring(L, 1)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.modtime()': filename is not a string");
    return 2;
  }

  const char *filename = lua_tostring(L, 1);
  lua_settop(L, 0);// clear stack

  if (stat(filename, &s) != 0) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, strerror(errno));
    return 2;
  }

  lua_pushboolean(L, true);
  lua_pushinteger(L, (lua_Integer)s.st_mtime);

  return 2;
}
//=========================================================================

/*!
 * Returns a table of the names of the files in the specified directory.
 * LUA: server.fs.readdir("path")
 * @return a table of filenames in the directory.
 */
static int lua_fs_readdir(lua_State *L)
{
  int top = lua_gettop(L);

  if (top < 1) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.readdir()': parameter missing");
    return 2;
  }

  if (!lua_isstring(L, 1)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.readdir()': path is not a string");
    return 2;
  }

  const char *path = lua_tostring(L, 1);
  lua_settop(L, 0);// clear stack

  DIR *dir_ptr = opendir(path);

  if (dir_ptr == nullptr) {
    std::stringstream ss;
    ss << strerror(errno) << ": " << path;
    lua_pushboolean(L, false);
    lua_pushstring(L, ss.str().c_str());
    return 2;
  }

  struct dirent *dirent_ptr;
  std::vector<std::string> filenames;

  while ((dirent_ptr = readdir(dir_ptr)) != nullptr) {
    const char *entry_name = dirent_ptr->d_name;

    if (!(strncmp(entry_name, ".", 1) == 0 || strncmp(entry_name, "..", 2) == 0)) {
      filenames.emplace_back(entry_name);
    }
  }

  closedir(dir_ptr);
  lua_pushboolean(L, true);// we assume success

  lua_createtable(L, 0, 0);
  int table_index = 1;
  std::vector<std::string>::const_iterator filename_iter = filenames.begin();

  while (filename_iter != filenames.end()) {
    lua_pushinteger(L, table_index);
    lua_pushstring(L, (*filename_iter).c_str());
    lua_rawset(L, -3);
    ++filename_iter;
    ++table_index;
  }

  return 2;
}
//=========================================================================

/*!
 * check if a file is readable
 * LUA: server.fs.is_readable(filepath)
 */
static int lua_fs_is_readable(lua_State *L)
{
  int top = lua_gettop(L);

  if (top < 1) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.is_readable(filename)': parameter missing");
    return 2;
  }

  if (!lua_isstring(L, 1)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.is_readable(filename)': filename is not a string");
    return 2;
  }

  const char *filename = lua_tostring(L, 1);
  lua_settop(L, 0);// clear stack
  lua_pushboolean(L, true);
  lua_pushboolean(L, access(filename, R_OK) == 0);
  return 2;
}
//=========================================================================

/*!
 * check if a file is writeable
 * LUA: server.fs.is_writeable(filepath)
 */
static int lua_fs_is_writeable(lua_State *L)
{
  int top = lua_gettop(L);

  if (top < 1) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.is_writeable(filename)': parameter missing");
    return 2;
  }

  if (!lua_isstring(L, 1)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.is_writeable(filename)': filename is not a string");
    return 2;
  }

  const char *filename = lua_tostring(L, 1);
  lua_settop(L, 0);// clear stack
  lua_pushboolean(L, true);
  lua_pushboolean(L, access(filename, W_OK) == 0);
  return 2;
}
//=========================================================================

/*!
 * check if a file is executable
 * LUA: server.fs.is_executable(filepath)
 */
static int lua_fs_is_executable(lua_State *L)
{
  int top = lua_gettop(L);

  if (top < 1) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.is_executable(filename)': parameter missing");
    return 2;
  }

  if (!lua_isstring(L, 1)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.is_executable(filename)': filename is not a string");
    return 2;
  }

  const char *filename = lua_tostring(L, 1);
  lua_settop(L, 0);// clear stack
  lua_pushboolean(L, true);
  lua_pushboolean(L, access(filename, X_OK) == 0);
  return 2;
}
//=========================================================================

/*!
 * check if a file exists
 * LUA: server.fs.exists(filepath)
 */
static int lua_fs_exists(lua_State *L)
{
  int top = lua_gettop(L);

  if (top < 1) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.exists(filename)': parameter missing");
    return 2;
  }

  if (!lua_isstring(L, 1)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.exists(filename)': filename is not a string");
    return 2;
  }

  const char *filename = lua_tostring(L, 1);
  lua_settop(L, 0);// clear stack
  lua_pushboolean(L, true);
  lua_pushboolean(L, access(filename, F_OK) == 0);
  return 2;
}
//=========================================================================


/*!
 * deletes a file from the file system. The file must exist and the user must have write
 * access
 * LUA: server.fs.unlink(filename)
 */
static int lua_fs_unlink(lua_State *L)
{
  int top = lua_gettop(L);

  if (top < 1) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.unlink(filename)': parameter missing");
    return 2;
  }

  if (!lua_isstring(L, 1)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.unlink(filename)': filename is not a string");
    return 2;
  }

  const char *filename = lua_tostring(L, 1);
  lua_settop(L, 0);// clear stack

  if (unlink(filename) != 0) {
    std::stringstream ss;
    ss << strerror(errno) << " File to unlink: " << filename;
    lua_pushboolean(L, false);
    lua_pushstring(L, ss.str().c_str());
    return 2;
  }

  lua_pushboolean(L, true);
  lua_pushnil(L);
  return 2;
}
//=========================================================================

/*!
 * Creates a new directory
 * LUA: server.fs.mkdir(dirname, tonumber('0755', 8))
 */
static int lua_fs_mkdir(lua_State *L)
{
  int top = lua_gettop(L);

  if (top < 2) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.mkdir(dirname, mask)': parameter missing");
    return 2;
  }

  if (!lua_isstring(L, 1)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.mkdir(dirname, mask)': dirname is not a string");
    return 2;
  }

  if (!lua_isinteger(L, 2)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.mkdir(dirname, mask)': mask is not an integer");
    return 2;
  }

  const char *dirname = lua_tostring(L, 1);
  auto mode = static_cast<mode_t>(lua_tointeger(L, 2));
  lua_settop(L, 0);// clear stack

  if (mkdir(dirname, mode) != 0) {
    lua_pushboolean(L, false);
    lua_pushstring(L, strerror(errno));
    return 2;
  }

  lua_pushboolean(L, true);
  lua_pushnil(L);
  return 2;
}
//=========================================================================

/*!
 * Deletes a directory
 * LUA: server.fs.rmdir(dirname)
 */
static int lua_fs_rmdir(lua_State *L)
{
  int top = lua_gettop(L);

  if (top < 1) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.rmdir(dirname)': parameter missing");
    return 2;
  }

  if (!lua_isstring(L, 1)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.rmdir(dirname)': dirname is not a string");
    return 2;
  }

  const char *dirname = lua_tostring(L, 1);
  lua_settop(L, 0);// clear stack

  if (rmdir(dirname) != 0) {
    lua_pushboolean(L, false);
    lua_pushstring(L, strerror(errno));
    return 2;
  }

  lua_pushboolean(L, true);
  lua_pushnil(L);
  return 2;
}
//=========================================================================

/*!
 * gets the current working directory
 * LUA: curdir = server.fs.getcwd()
 */
static int lua_fs_getcwd(lua_State *L)
{
  char *dirname = getcwd(nullptr, 0);

  if (dirname != nullptr) {
    lua_pushboolean(L, true);
    lua_pushstring(L, dirname);
  } else {
    lua_pushboolean(L, false);
    lua_pushstring(L, strerror(errno));
    return 2;
  }

  free(dirname);
  return 2;
}
//=========================================================================

/*!
 * Change working directory
 * LUA: oldir = server.fs.chdir(newdir)
 */
static int lua_fs_chdir(lua_State *L)
{
  int top = lua_gettop(L);

  if (top < 1) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.chdir(dirname)': parameter missing");
    return 2;
  }

  if (!lua_isstring(L, 1)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.chdir(dirname)': dirname is not a string");
    return 2;
  }

  const char *dirname = lua_tostring(L, 1);
  lua_settop(L, 0);// clear stack
  char *olddirname = getcwd(nullptr, 0);

  if (olddirname == nullptr) {
    lua_pushboolean(L, false);
    lua_pushstring(L, strerror(errno));
    return 2;
  }

  if (chdir(dirname) != 0) {
    lua_pushboolean(L, false);
    lua_pushstring(L, strerror(errno));
    return 2;
  }

  lua_pushboolean(L, true);
  lua_pushstring(L, olddirname);
  free(olddirname);
  return 2;
}
//=========================================================================

/*!
 * Copy a file from one location to another.
 *
 * LUA: server.fs.copyFile(source, target)
 *
 */
static int lua_fs_copyfile(lua_State *L)
{
  int top = lua_gettop(L);

  if (top < 2) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'lua_fs_copyfile(from,to)': not enough parameters");
    return 2;
  }

  const char *infile = lua_tostring(L, 1);

  std::ifstream source(infile, std::ios::binary);

  // check if source is readable
  if (source.fail()) {
    lua_pushboolean(L, false);
    lua_pushstring(L, "'lua_fs_copyfile(from,to)': Couldn't open source file");
    return 2;
  }

  std::string outfile = lua_tostring(L, 2);
  lua_pop(L, top);// clear stack
  std::ofstream dest(outfile, std::ios::binary);

  if (dest.fail()) {
    lua_pushboolean(L, false);
    lua_pushstring(L, "'lua_fs_copyfile(from,to)': Couldn't open output file");
    return 2;
  }

  dest << source.rdbuf();

  if (dest.fail() || source.fail()) {
    lua_pushboolean(L, false);
    lua_pushstring(L, "'lua_fs_copyfile(from,to)': Copying data failed");
    return 2;
  }

  source.close();
  dest.close();
  lua_pushboolean(L, true);
  lua_pushnil(L);

  return 2;
}
//=========================================================================

/*!
 * Moves a file from one location to another.
 *
 * LUA: server.fs.moveFile(source, target)
 *
 */
static int lua_fs_mvfile(lua_State *L)
{
  lua_getglobal(L, shttps::luaconnection);
  shttps::Connection *conn = (shttps::Connection *)lua_touserdata(L, -1);
  lua_remove(L, -1);// remove from stacks
  int top = lua_gettop(L);

  if (top < 2) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.fs.moveFile(from,to)': not enough parameters");
    return 2;
  }

  std::string infile;
  if (lua_isinteger(L, 1)) {
    std::vector<shttps::Connection::UploadedFile> uploads = conn->uploads();
    int tmpfile_id = static_cast<int>(lua_tointeger(L, 1));
    try {
      infile = uploads.at(tmpfile_id - 1).tmpname;// In Lua, indexes are 1-based.
    } catch (const std::out_of_range &oor) {
      lua_settop(L, 0);// clear stack
      lua_pushboolean(L, false);
      lua_pushstring(L, "'server.fs.moveFile(from,to)': Could not read data of uploaded file. Invalid index?");
      return 2;
    }
  } else if (lua_isstring(L, 1)) {
    infile = lua_tostring(L, 1);
  } else {
    lua_pop(L, top);
    lua_pushboolean(L, false);
    lua_pushstring(L, "server.fs.moveFile(from,to): filename must be string or index");
    return 2;
  }
  std::string outfile = lua_tostring(L, 2);
  lua_pop(L, top);// clear stack

  if (std::rename(infile.c_str(), outfile.c_str())) {
    // an error occured
    switch (errno) {
    case EACCES: {
      lua_pushboolean(L, false);
      lua_pushstring(L, "'server.fs.moveFile(from,to)': no permission!");
      return 2;
    }
    case EXDEV: {
      lua_pushboolean(L, false);
      lua_pushstring(L, "'server.fs.moveFile(from,to)': move across file systems not allowd!");
      return 2;
    }
    default: {
      lua_pushboolean(L, false);
      lua_pushstring(L, "'server.fs.moveFile(from,to)': error moving file!");
      return 2;
    }
    }
  }

  lua_pushboolean(L, true);
  lua_pushnil(L);
  return 2;
}
//=========================================================================

static const luaL_Reg fs_methods[] = { { "ftype", lua_fs_ftype },
  { "modtime", lua_fs_modtime },
  { "readdir", lua_fs_readdir },
  { "is_readable", lua_fs_is_readable },
  { "is_writeable", lua_fs_is_writeable },
  { "is_executable", lua_fs_is_executable },
  { "exists", lua_fs_exists },
  { "unlink", lua_fs_unlink },
  { "mkdir", lua_fs_mkdir },
  { "rmdir", lua_fs_rmdir },
  { "getcwd", lua_fs_getcwd },
  { "chdir", lua_fs_chdir },
  { "copyFile", lua_fs_copyfile },
  { "moveFile", lua_fs_mvfile },
  { 0, 0 } };
//=========================================================================

/*!
 * Generates a random version 4 uuid string
 * LUA: uuid = server.uuid()
 */
static int lua_uuid(lua_State *L)
{
  sole::uuid u4 = sole::uuid4();
  std::string uuidstr = u4.str();
  lua_pushboolean(L, true);
  lua_pushstring(L, uuidstr.c_str());
  return 2;
}
//=========================================================================

/*!
 * Generate a base62-uuid string
 * LUA: uuid62 = server.uuid62()
 */
static int lua_uuid_base62(lua_State *L)
{
  sole::uuid u4 = sole::uuid4();
  std::string uuidstr62 = u4.base62();
  lua_pushboolean(L, true);
  lua_pushstring(L, uuidstr62.c_str());
  return 2;
}
//=========================================================================

/*!
 * Converts a uuid-string to a base62 uuid
 * LUA: uuid62 = server.uuid_to_base62(uuid)
 */
static int lua_uuid_to_base62(lua_State *L)
{
  int top = lua_gettop(L);

  if (top != 1) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.uuid_tobase62(uuid)': uuid parameter missing");
    return 2;
  }

  if (!lua_isstring(L, 1)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.uuid_tobase62(uuid)': uuid is not a string");
    return 2;
  }

  const char *uuidstr = lua_tostring(L, 1);
  lua_settop(L, 0);// clear stack

  sole::uuid u4 = sole::rebuild(uuidstr);
  std::string uuidb62str = u4.base62();

  lua_pushboolean(L, true);
  lua_pushstring(L, uuidb62str.c_str());

  return 2;
}
//=========================================================================

/*!
 * Converts a base62-uuid to a "normal" uuid
 * LUA: uuid = server.base62_to_uuid(uuid62)
 */
static int lua_base62_to_uuid(lua_State *L)
{
  int top = lua_gettop(L);
  if (top < 1) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.base62_to_uuid(uuid62)': uuid62 parameter missing");
    return 2;
  }

  if (!lua_isstring(L, 1)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.base62_to_uuid(uuid62)': uuid62 is not a string");
    return 2;
  }

  const char *uuidb62 = lua_tostring(L, 1);
  lua_settop(L, 0);// clear stack

  sole::uuid u4 = sole::rebuild(uuidb62);
  std::string uuidstr = u4.str();

  lua_pushboolean(L, true);
  lua_pushstring(L, uuidstr.c_str());

  return 2;
}
//=========================================================================

/*!
 * Prints variables and/or strings to the HTTP connection
 * LUA: server.print("string"|var1 [,"string|var]...)
 */
static int lua_print(lua_State *L)
{
  lua_getglobal(L, luaconnection);// push onto stack
  Connection *conn = (Connection *)lua_touserdata(L, -1);// does not change the stack
  lua_remove(L, -1);// remove from stack
  int top = lua_gettop(L);

  for (int i = 1; i <= top; i++) {
    size_t len;
    const char *str = lua_tolstring(L, i, &len);

    if (str != nullptr) {
      try {
        conn->send(str, len);
      } catch (int ierr) {
        lua_settop(L, 0);// clear stack
        lua_pushboolean(L, false);
        lua_pushstring(L, "Sending data to connection failed");
        return 2;
      } catch (Error &err) {
        lua_settop(L, 0);// clear stack
        lua_pushboolean(L, false);
        lua_pushstring(L, err.to_string().c_str());
        return 2;
      }
    }
  }

  lua_pop(L, top);
  lua_pushboolean(L, true);
  lua_pushnil(L);
  return 2;
}
//=========================================================================

static int lua_require_auth(lua_State *L)
{
  lua_getglobal(L, luaconnection);// push onto stack
  Connection *conn = (Connection *)lua_touserdata(L, -1);// does not change the stack
  lua_remove(L, -1);// remove from stack
  lua_pushboolean(L, true);

  std::string auth = conn->header("authorization");
  lua_createtable(L, 0, 3);// table

  if (auth.empty()) {
    lua_pushstring(L, "status");// table - "username"
    lua_pushstring(L, "NOAUTH");// table - "username" - <username>
    lua_rawset(L, -3);// table
  } else {
    size_t npos;

    if ((npos = auth.find(" ")) != std::string::npos) {
      std::string auth_type = auth.substr(0, npos);
      asciitolower(auth_type);

      if (auth_type == "basic") {
        std::string auth_secret = auth.substr(npos + 1);
        std::string auth_string = base64_decode(auth_secret);
        npos = auth_string.find(":");

        if (npos != std::string::npos) {
          std::string username = auth_string.substr(0, npos);
          std::string password = auth_string.substr(npos + 1);

          lua_pushstring(L, "status");// table - "username"
          lua_pushstring(L, "BASIC");// table - "username" - <username>
          lua_rawset(L, -3);// table

          lua_pushstring(L, "username");// table - "username"
          lua_pushstring(L, username.c_str());// table - "username" - <username>
          lua_rawset(L, -3);// table

          lua_pushstring(L, "password");// table - "password"
          lua_pushstring(L, password.c_str());// table - "password" - <password>
          lua_rawset(L, -3);// table
        } else {
          lua_pushstring(L, "status");// table - "username"
          lua_pushstring(L, "ERROR");// table - "username" - <username>
          lua_rawset(L, -3);// table

          lua_pushstring(L, "message");// table - "username"
          lua_pushstring(L, "Auth-string not valid");// table - "username" - <username>
          lua_rawset(L, -3);// table
        }
      } else if (auth_type == "bearer") {
        std::string jwt_token = auth.substr(npos + 1);

        lua_pushstring(L, "status");// table - "username"
        lua_pushstring(L, "BEARER");// table - "username" - <username>
        lua_rawset(L, -3);// table

        lua_pushstring(L, "token");// table - "username"
        lua_pushstring(L, jwt_token.c_str());// table - "username" - <username>
        lua_rawset(L, -3);// table
      }
    } else {
      lua_pushstring(L, "status");// table - "username"
      lua_pushstring(L, "ERROR");// table - "username" - <username>
      lua_rawset(L, -3);// table

      lua_pushstring(L, "message");// table - "username"
      lua_pushstring(L, "Auth-type not known");// table - "username" - <username>
      lua_rawset(L, -3);// table
    }
  }

  return 2;
}

//=========================================================================

/*!
 * Indicates an error in a client HTTP connection. Thrown and caught only
 * by lua_http_client() and CurlConnection.
 */
class HttpError
{
private:
  int line;
  std::string errorMsg;

public:
  inline HttpError(int line_p, const std::string &errormsg_p) : line(line_p), errorMsg(errormsg_p){};

  inline HttpError(int line_p, const char *errormsg_p) : line(line_p) { errorMsg = errormsg_p; };

  inline std::string what(void)
  {
    std::stringstream ss;
    ss << "Error #" << line << ": " << errorMsg;
    return ss.str();
  }
};

// libcurl HTTP response body callback function
static size_t curlWriterCallback(char *data, size_t size, size_t nitems, std::string *writerData)
{
  size_t length = size * nitems;
  writerData->append(data, size * nitems);
  return length;
}

// libcurl HTTP response header callback function
static size_t curlHeaderCallback(char *data,
  size_t size,
  size_t nitems,
  std::unordered_map<std::string, std::string> *responseHeaders)
{
  size_t length = size * nitems;
  std::string headerStr = std::string(data, length);
  size_t separatorPos = headerStr.find(":");

  if (separatorPos != std::string::npos) {
    std::string headerName = headerStr.substr(0, separatorPos);
    size_t headerValuePos = headerStr.find_first_not_of(' ', separatorPos + 1);

    if (headerValuePos != std::string::npos) {
      std::string headerValue = headerStr.substr(headerValuePos, std::string::npos);
      (*responseHeaders)[headerName] = headerValue;
    }
  }

  return length;
}

/*!
 * Represents a libcurl connection that can be used to make a single HTTP request.
 */
class CurlConnection
{
public:
  std::string responseBody;
  std::unordered_map<std::string, std::string> responseHeaders;

  CurlConnection(const std::string &url,
    const std::unordered_map<std::string, std::string> &requestHeaders,
    long timeout)
    : _url(url)
  {
    // Make a libcurl connection object.

    _conn = curl_easy_init();

    if (_conn == nullptr) { throw HttpError(__LINE__, "Failed to create libcurl connection"); }

    try {
      // Set the connection object's error message buffer.
      if (curl_easy_setopt(_conn, CURLOPT_ERRORBUFFER, _curlErrorBuffer) != CURLE_OK) {
        throw HttpError(__LINE__, "Failed to set libcurl error buffer");
      }

      // Tell Curl not to use signal handlers. This is required in multi-threaded applications.
      if (curl_easy_setopt(_conn, CURLOPT_NOSIGNAL, 1L) != CURLE_OK) {
        std::ostringstream errMsg;
        errMsg << "Failed to set CURLOPT_NOSIGNAL: " << _curlErrorBuffer;
        throw HttpError(__LINE__, errMsg.str());
      }

      // Set the connection URL.
      if (curl_easy_setopt(_conn, CURLOPT_URL, url.c_str()) != CURLE_OK) {
        std::ostringstream errMsg;
        errMsg << "Failed to set libcurl URL: " << _curlErrorBuffer;
        throw HttpError(__LINE__, errMsg.str());
      }

      // Set the connection timeout.
      if (curl_easy_setopt(_conn, CURLOPT_CONNECTTIMEOUT_MS, timeout) != CURLE_OK) {
        std::ostringstream errMsg;
        errMsg << "Failed to set connection timeout: " << _curlErrorBuffer;
        throw HttpError(__LINE__, errMsg.str());
      }

      // Set the HTTP request headers.

      struct curl_slist *chunk = nullptr;

      for (const auto &header : requestHeaders) {
        std::string headerStr = header.first + ": " + header.second;
        chunk = curl_slist_append(chunk, headerStr.c_str());
      }

      if (curl_easy_setopt(_conn, CURLOPT_HTTPHEADER, chunk) != CURLE_OK) {
        std::ostringstream errMsg;
        errMsg << "Failed to set HTTP headers: " << _curlErrorBuffer;
        throw HttpError(__LINE__, errMsg.str());
      }

      // Tell the connection to follow redirects.
      if (curl_easy_setopt(_conn, CURLOPT_FOLLOWLOCATION, 1L) != CURLE_OK) {
        std::ostringstream errMsg;
        errMsg << "Failed to set libcurl redirect option: " << _curlErrorBuffer;
        throw HttpError(__LINE__, errMsg.str());
      }

      // Register a function for handling the connection's response data.
      if (curl_easy_setopt(_conn, CURLOPT_WRITEFUNCTION, curlWriterCallback) != CURLE_OK) {
        std::ostringstream errMsg;
        errMsg << "Failed to set libcurl writer callback: " << _curlErrorBuffer;
        throw HttpError(__LINE__, errMsg.str());
      }

      // Set the connection's response data buffer.
      if (curl_easy_setopt(_conn, CURLOPT_WRITEDATA, &responseBody) != CURLE_OK) {
        std::ostringstream errMsg;
        errMsg << "Failed to set libcurl response data buffer: " << _curlErrorBuffer;
        throw HttpError(__LINE__, errMsg.str());
      }

      // Register a fiunction for handling the connection's response headers.
      if (curl_easy_setopt(_conn, CURLOPT_HEADERFUNCTION, curlHeaderCallback) != CURLE_OK) {
        std::ostringstream errMsg;
        errMsg << "Failed to set libcurl response header callback: " << _curlErrorBuffer;
        throw HttpError(__LINE__, errMsg.str());
      }

      // Set the object that will collect the respnse headers.
      if (curl_easy_setopt(_conn, CURLOPT_HEADERDATA, &responseHeaders) != CURLE_OK) {
        std::ostringstream errMsg;
        errMsg << "Failed to set libcurl response header object: " << _curlErrorBuffer;
        throw HttpError(__LINE__, errMsg.str());
      }
    } catch (HttpError &err) {
      curl_easy_cleanup(_conn);
      throw err;
    }
  }

  void doGetRequest()
  {
    if (curl_easy_perform(_conn) != CURLE_OK) {
      std::ostringstream errMsg;
      errMsg << "HTTP GET request to " << _url << " failed: " << _curlErrorBuffer;
      throw HttpError(__LINE__, errMsg.str());
    }
  }

  long getStatusCode()
  {
    long status_code;
    curl_easy_getinfo(_conn, CURLINFO_RESPONSE_CODE, &status_code);
    return status_code;
  }

  ~CurlConnection() { curl_easy_cleanup(_conn); }

private:
  CURL *_conn;
  std::string _url;
  char _curlErrorBuffer[CURL_ERROR_SIZE];
};


/*!
 * Get data from a http server
 * LUA: result = server.http("GET", "http://server.domain/path/file" [, header] [, timeout])
 * where header is an associative array (key-value pairs) of header variables.
 * Parameters:
 *  - method: "GET" (only method allowed so far
 *  - url: complete url including optional port, but no authorization yet
 *  - header: optional table with HTTP-header key-value pairs
 *  - timeout: option number of milliseconds until the connect timeouts
 *
 * result = {
 *    header {
 *       name = value [, name = value, ...]
 *    },
 *    body = data
 * }
 */
static int lua_http_client(lua_State *L)
{
  int top = lua_gettop(L);

  if (top < 2) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.http(method, url [, header] [, timeout])' requires at least 2 parameters");
    return 2;
  }

  // Get the first parameter: HTTP method (only "GET" is supported at the moment)
  std::string method = lua_tostring(L, 1);

  // Get the second parameter: URL
  std::string url = lua_tostring(L, 2);
  ;

  // the next parameters are either the header values and/or the timeout
  // header: table of key/value pairs of additional HTTP-headers to be sent
  // timeout: number of milliseconds any operation of the socket may take at maximum
  std::unordered_map<std::string, std::string> requestHeaders;
  int timeout = 2000;// default is 2000 ms

  if (lua_istable(L, 3)) {// process header table
    lua_pushnil(L);

    while (lua_next(L, 3) != 0) {
      const char *key = lua_tostring(L, -2);
      const char *value = lua_tostring(L, -1);
      requestHeaders[key] = value;
      lua_pop(L, 1);
    }
  } else if (lua_isinteger(L, 3)) {// process timeout
    timeout = static_cast<int>(lua_tointeger(L, 1));
    lua_pop(L, 1);
  }

  lua_settop(L, 0);// clear stack

  try {
    if (method == "GET") {// the only method we support so far...
      // Perform the HTTP request using libcurl.
      CurlConnection curlConnection(url, requestHeaders, timeout);
      auto start = get_time::now();
      curlConnection.doGetRequest();

      // Calculate how long the request took.
      auto end = get_time::now();
      auto diff = end - start;
      int duration = std::chrono::duration_cast<ms>(diff).count();

      // Return true to indicate that this function call succeeded.
      lua_pushboolean(L, true);

      // Construct a Lua table containing the HTTP response.
      lua_createtable(L, 0, 0);// table

      lua_pushstring(L, "status_code");// table - "success"
      lua_pushinteger(L, curlConnection.getStatusCode());// table - "status_code" - status_code
      lua_rawset(L, -3);// table

      lua_pushstring(L, "body");// table - "body"
      std::string &responseBody = curlConnection.responseBody;
      lua_pushlstring(L, responseBody.c_str(), responseBody.length());// table - "body" - curlResponseBuffer
      lua_rawset(L, -3);// table

      lua_pushstring(L, "duration");// table - "duration"
      lua_pushinteger(L, duration);// table - "duration" - duration
      lua_rawset(L, -3);// table

      lua_pushstring(L, "header");// table1 - "header"
      std::unordered_map<std::string, std::string> &responseHeaders = curlConnection.responseHeaders;
      lua_createtable(L, 0, responseHeaders.size());// table - "header" - table2
      for (auto const &iterator : responseHeaders) {
        lua_pushstring(L, iterator.first.c_str());// table - "header" - table2 - headername
        lua_pushstring(L, iterator.second.c_str());// table - "header" - table2 - headername - headervalue
        lua_rawset(L, -3);// table - "header" - table2
      }
      lua_rawset(L, -3);// table

      return 2;// we return success and one table...
    } else {
      std::string errorMsg = std::string("'server.http(method, url, [header])': unknown method ") + method;
      throw HttpError(__LINE__, errorMsg);
    }
  } catch (HttpError &err) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, err.what().c_str());// table - "errmsg" - errorMsg
    return 2;
  }

  lua_settop(L, 0);// clear stack
  lua_pushboolean(L, false);
  lua_pushstring(L, "Unknown error");// table - "errmsg" - errorMsg
  return 2;
}
//=========================================================================


static json_t *subtable(lua_State *L, int index)
{
  std::string table_error("server.table_to_json(table): datatype inconsistency");
  json_t *tableobj = nullptr;
  json_t *arrayobj = nullptr;
  json_t *tmp_luanumber = nullptr;
  const char *skey;
  lua_pushnil(L); /* first key */

  while (lua_next(L, index) != 0) {
    // key is at index -2  // index + 1
    // value is at index -1 // index + 2

    if (lua_type(L, index + 1) == LUA_TSTRING) {
      // we have a string as key
      skey = lua_tostring(L, index + 1);

      if (arrayobj != nullptr) {
        throw std::string("'server.table_to_json(table)': Cannot mix int and strings as key");
      }

      if (tableobj == nullptr) { tableobj = json_object(); }
    } else if (lua_type(L, index + 1) == LUA_TNUMBER) {
      (void)lua_tointeger(L, index + 1);

      if (tableobj != nullptr) {
        throw std::string("'server.table_to_json(table)': Cannot mix int and strings as key");
      }

      if (arrayobj == nullptr) { arrayobj = json_array(); }
    } else {
      // something else as key....
      throw std::string("'server.table_to_json(table)': Cannot convert key to JSON object field");
    }

    //
    // process values
    //
    // Check for number first, because every number will be accepted as a string.
    // The lua-functions do not check for a variable's actual type, but if they are convertable to the expected type.
    //
    if (lua_type(L, index + 2) == LUA_TNUMBER) {
      // a number value
      double val = lua_tonumber(L, index + 2);

      if (floor(val) == val) {
        // the lua number is actually an integer
        tmp_luanumber = json_integer(static_cast<long>(floor(val)));
      } else {
        // the lua number is a double
        tmp_luanumber = json_real(val);
      }

      if (tableobj != nullptr) {
        json_object_set_new(tableobj, skey, tmp_luanumber);
      } else if (arrayobj != nullptr) {
        json_array_append_new(arrayobj, tmp_luanumber);
      } else {
        throw table_error;
      }
    } else if (lua_type(L, index + 2) == LUA_TSTRING) {
      // a string value
      const char *val = lua_tostring(L, index + 2);

      if (tableobj != nullptr) {
        json_object_set_new(tableobj, skey, json_string(val));
      } else if (arrayobj != nullptr) {
        json_array_append_new(arrayobj, json_string(val));
      } else {
        throw table_error;
      }
    } else if (lua_type(L, index + 2) == LUA_TBOOLEAN) {
      // a boolean value
      bool val = lua_toboolean(L, index + 2);

      if (tableobj != nullptr) {
        json_object_set_new(tableobj, skey, json_boolean(val));
      } else if (arrayobj != nullptr) {
        json_array_append_new(arrayobj, json_boolean(val));
      } else {
        throw table_error;
      }
    } else if (lua_type(L, index + 2) == LUA_TTABLE) {
      if (tableobj != nullptr) {
        json_object_set_new(tableobj, skey, subtable(L, index + 2));
      } else if (arrayobj != nullptr) {
        json_array_append_new(arrayobj, subtable(L, index + 2));
      } else {
        throw table_error;
      }
    } else {
      throw table_error;
    }
    lua_pop(L, 1);
  }
  return tableobj != nullptr ? tableobj : (arrayobj != nullptr ? arrayobj : nullptr);
}
//=========================================================================

/*!
 * Converts a Lua table into a JSON string
 * LUA: jsonstr = server.table_to_json(table)
 */
static int lua_table_to_json(lua_State *L)
{
  int top = lua_gettop(L);
  if (top < 1) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.table_to_json(table)': table parameter missing");
    return 2;
  }

  if (!lua_istable(L, 1)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.table_to_json(table)': table is not a lua-table");
    return 2;
  }

  json_t *root = nullptr;

  try {
    root = subtable(L, 1);
  } catch (std::string &errmsg) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, errmsg.c_str());
  }

  lua_pushboolean(L, true);// we are successful...
  char *jsonstr = json_dumps(root, JSON_INDENT(3));
  lua_pushstring(L, jsonstr);
  free(jsonstr);
  json_decref(root);
  return 2;
}
//=========================================================================

//
// forward declaration is needed here
//
static void lua_jsonarr(lua_State *L, json_t *obj);

static void lua_jsonobj(lua_State *L, json_t *obj)
{
  if (!json_is_object(obj)) { throw std::string("'lua_jsonobj expects object"); }

  lua_createtable(L, 0, 0);
  const char *key;
  json_t *value;

  json_object_foreach(obj, key, value)
  {
    lua_pushstring(L, key);// index
    switch (json_typeof(value)) {
    case JSON_NULL: {
      lua_pushnil(L);// ToDo: we should create a custom Lua object named NULL!
      break;
    }
    case JSON_FALSE: {
      lua_pushboolean(L, false);
      break;
    }
    case JSON_TRUE: {
      lua_pushboolean(L, true);
      break;
    }
    case JSON_REAL: {
      lua_pushnumber(L, json_real_value(value));
      break;
    }
    case JSON_INTEGER: {
      lua_pushinteger(L, json_integer_value(value));
      break;
    }
    case JSON_STRING: {
      lua_pushstring(L, json_string_value(value));
      break;
    }
    case JSON_ARRAY: {
      lua_jsonarr(L, value);
      break;
    }
    case JSON_OBJECT: {
      lua_jsonobj(L, value);
      break;
    }
    }

    lua_rawset(L, -3);
  }
}
//=========================================================================


static void lua_jsonarr(lua_State *L, json_t *arr)
{
  if (!json_is_array(arr)) { throw std::string("'lua_jsonarr expects array"); }

  lua_createtable(L, 0, 0);
  size_t index;
  json_t *value;

  json_array_foreach(arr, index, value)
  {
    lua_pushinteger(L, index);
    switch (json_typeof(value)) {
    case JSON_NULL: {
      lua_pushnil(L);// ToDo: we should create a custom Lua object named NULL!
      break;
    }
    case JSON_FALSE: {
      lua_pushboolean(L, false);
      break;
    }
    case JSON_TRUE: {
      lua_pushboolean(L, true);
      break;
    }
    case JSON_REAL: {
      lua_pushnumber(L, json_real_value(value));
      break;
    }
    case JSON_INTEGER: {
      lua_pushinteger(L, json_integer_value(value));
      break;
    }
    case JSON_STRING: {
      lua_pushstring(L, json_string_value(value));
      break;
    }
    case JSON_ARRAY: {
      lua_jsonarr(L, value);
      break;
    }
    case JSON_OBJECT: {
      lua_jsonobj(L, value);
      break;
    }
    }

    lua_rawset(L, -3);
  }
}
//=========================================================================


/*!
 * Converts a json string into a possibly nested Lua table
 * LUA: table = server.json_to_table(jsonstr)
 */
static int lua_json_to_table(lua_State *L)
{
  int top = lua_gettop(L);

  if (top < 1) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.json_to_table(jsonstr)': jsonstr parameter missing");
    return 2;
  }

  if (!lua_isstring(L, 1)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.json_to_table(jsonstr)': jsonstr is not a string");
    return 2;
  }

  const char *jsonstr = lua_tostring(L, 1);
  lua_pop(L, top);
  json_error_t jsonerror;
  json_t *jsonobj = json_loads(jsonstr, JSON_REJECT_DUPLICATES, &jsonerror);

  if (jsonobj == nullptr) {
    lua_pushboolean(L, false);
    std::stringstream ss;
    ss << "'server.json_to_table(jsonstr)': Error parsing JSON: " << jsonerror.text << std::endl;
    ss << "JSON-source: " << jsonerror.source << std::endl;
    ss << "Line: " << jsonerror.line << " Column: " << jsonerror.column << " Pos: " << jsonerror.position << std::endl;
    lua_pushstring(L, ss.str().c_str());
    return 2;
  }

  lua_pushboolean(L, true);// we assume success

  try {
    if (json_is_object(jsonobj)) {
      lua_jsonobj(L, jsonobj);
    } else if (json_is_array(jsonobj)) {
      lua_jsonarr(L, jsonobj);
    } else {
      lua_settop(L, 0);// clear stack
      lua_pushboolean(L, false);
      lua_pushstring(L, "'server.json_to_table(jsonstr)': Not a valid json string");
      return 2;
    }
  } catch (std::string &errmsg) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, errmsg.c_str());
  }

  json_decref(jsonobj);

  return 2;
}
//=========================================================================


/*!
 * Stops the HTTP server and exits everything
 * TODO: There must be some authorization control here. This function should only be available for admins
 * TODO: Add the possibility to give a message which is printed on the console and logfiles
 * LUA: server.shutdown()
 */
static int lua_exitserver(lua_State *L)
{
  lua_getglobal(L, luaconnection);// push onto stack
  Connection *conn = (Connection *)lua_touserdata(L, -1);// does not change the stack
  lua_remove(L, -1);// remove from stack
  conn->server()->stop();
  return 0;
}
//=========================================================================

/*!
 * Adds a new HTTP header field
 * LUA: server.sendHeader(key, value)
 */
static int lua_send_header(lua_State *L)
{
  lua_getglobal(L, luaconnection);
  Connection *conn = (Connection *)lua_touserdata(L, -1);
  lua_remove(L, -1);// remove from stack
  int top = lua_gettop(L);

  if (top != 2) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.sendHeader(key,val)': Invalid number of parameters");
    return 2;
  }

  const char *hkey = lua_tostring(L, 1);
  const char *hval = lua_tostring(L, 2);
  lua_pop(L, top);
  conn->header(hkey, hval);
  lua_pushboolean(L, true);
  lua_pushnil(L);
  return 2;
}
//=========================================================================

/*!
 * Adds Set-Cookie header field
 * LUA: server.sendCookie(name, value [, options-table])
 * options-table: {
 *    path = "path allowd",
 *    domain = "domain allowed",
 *    expires = seconds,
 *    secure = true | false,
 *    http_only = true | false
 * }
 */
static int lua_send_cookie(lua_State *L)
{
  lua_getglobal(L, luaconnection);
  Connection *conn = (Connection *)lua_touserdata(L, -1);
  lua_remove(L, -1);// remove from stack

  int top = lua_gettop(L);

  if ((top < 2) || (top > 3)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.sendCookie(name, value[, options])': Invalid number of parameters");
    return 2;
  }

  const char *ckey = lua_tostring(L, 1);
  const char *cval = lua_tostring(L, 2);

  Cookie cookie(ckey, cval);

  if (top == 3) {
    if (!lua_istable(L, 3)) {
      lua_settop(L, 0);// clear stack
      lua_pushboolean(L, false);
      lua_pushstring(L, "'server.sendCookie(name, value[, options])': options is not a lua-table");
      return 2;
    }

    lua_pushnil(L); /* first key */
    int index = 3;
    std::string optname;

    while (lua_next(L, index) != 0) {
      // key is at index -2
      // value is at index -1
      try {
        if (lua_isstring(L, -2)) {
          // we have a string as key
          optname = lua_tostring(L, -2);
        } else {
          lua_settop(L, 0);// clear stack
          lua_pushboolean(L, false);
          lua_pushstring(L, "'server.sendCookie(name, value[, options])': option name is not a string");
          return 2;
        }

        if (optname == "path") {
          if (lua_isstring(L, -1)) {
            std::string path = lua_tostring(L, -1);
            cookie.path(path);
          } else {
            throw std::string("'server.sendCookie(name, value[, options])': path is not string");
          }
        } else if (optname == "domain") {
          if (lua_isstring(L, -1)) {
            std::string domain = lua_tostring(L, -1);
            cookie.domain(domain);
          } else {
            throw std::string("'server.sendCookie(name, value[, options])': domain is not string");
          }
        } else if (optname == "expires") {
          if (lua_isinteger(L, -1)) {
            int expires = lua_tointeger(L, -1);
            cookie.expires(expires);
          } else {
            throw std::string("'server.sendCookie(name, value[, options])': expires is not integer");
          }
        } else if (optname == "secure") {
          if (lua_isboolean(L, -1)) {
            bool secure = lua_toboolean(L, -1);
            if (secure) cookie.secure(secure);
          } else {
            throw std::string("'server.sendCookie(name, value[, options])': secure is not boolean");
          }
        } else if (optname == "http_only") {
          if (lua_isboolean(L, -1)) {
            bool http_only = lua_toboolean(L, -1);
            if (http_only) cookie.httpOnly(http_only);
          } else {
            throw std::string("'server.sendCookie(name, value[, options])': http_only is not boolean");
          }
        } else {
          throw std::string("'server.sendCookie(name, value[, options])': unknown option: ") + optname;
        }
      } catch (std::string &errmsg) {
        lua_settop(L, 0);// clear stack
        lua_pushboolean(L, false);
        lua_pushstring(L, errmsg.c_str());
        return 2;
      }

      lua_pop(L, 1);
    }
  }

  lua_settop(L, 0);// clear stack
  conn->cookies(cookie);
  lua_pushboolean(L, true);
  lua_pushnil(L);
  return 2;
}
//=========================================================================

static int lua_send_status(lua_State *L)
{
  lua_getglobal(L, luaconnection);
  Connection *conn = (Connection *)lua_touserdata(L, -1);
  lua_remove(L, -1);// remove from stack

  int top = lua_gettop(L);
  int istatus = 200;

  if (top == 1) {
    istatus = static_cast<int>(lua_tointeger(L, 1));
    lua_pop(L, 1);
  }

  Connection::StatusCodes status = static_cast<Connection::StatusCodes>(istatus);
  conn->status(status);
  return 0;
}
//=========================================================================


/*!
 * Copy an uploaded file to another location
 *
 * shttp saves uploaded files in a temporary location (given by the config variable "tmpdir")
 * and deletes it after the request has been served. This function is used to copy the file
 * to another location where it can be used/retrieved by shttps/sipi.
 *
 * LUA: server.copyTmpfile(from, target)
 * from:    an index (integer value) of array server.uploads.
 * target:  an absolute path
 */
static int lua_copytmpfile(lua_State *L)
{
  lua_getglobal(L, luaconnection);
  Connection *conn = (Connection *)lua_touserdata(L, -1);
  lua_remove(L, -1);// remove from stack
  int top = lua_gettop(L);

  if (top < 2) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'lua_copytmpfile(from,to)': not enough parameters");
    return 2;
  }

  if (!lua_isinteger(L, 1)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'lua_copytmpfile(from,to)': parameter 'from' must be an integer");
    return 2;
  }

  std::vector<Connection::UploadedFile> uploads = conn->uploads();
  int tmpfile_id = static_cast<int>(lua_tointeger(L, 1));
  std::string infile;

  try {
    infile = uploads.at(tmpfile_id - 1).tmpname;// In Lua, indexes are 1-based.
  } catch (const std::out_of_range &oor) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'lua_copytmpfile(from,to)': parameter 'from' is not a valid index.");
    return 2;
  }

  const char *outfile = lua_tostring(L, 2);
  lua_settop(L, 0);// clear stack
  std::ifstream source(infile, std::ios::binary);

  if (source.fail()) {
    lua_pushboolean(L, false);
    lua_pushstring(L, "'lua_copytmpfile(from,to)': Couldn't open input file");
    return 2;
  }

  std::ofstream dest(outfile, std::ios::binary);

  if (dest.fail()) {
    lua_pushboolean(L, false);
    lua_pushstring(L, "'lua_copytmpfile(from,to)': Couldn't open output file");
    return 2;
  }

  dest << source.rdbuf();

  if (dest.fail() || source.fail()) {
    lua_pushboolean(L, false);
    lua_pushstring(L, "'lua_copytmpfile(from,to)': Copying data failed");
    return 2;
  }

  source.close();
  dest.close();

  lua_pushboolean(L, true);
  lua_pushnil(L);
  return 2;
}
//=========================================================================


//
// reserved claims (IntDate: The number of seconds from 1970-01-01T0:0:0Z):
// - iss (string => StringOrURI) OPT: principal that issued the JWT.
// - exp (number => IntDate) OPT: expiration time on or after which the token
//   MUST NOT be accepted for processing.
// - nbf  (number => IntDate) OPT: identifies the time before which the token
//   MUST NOT be accepted for processing.
// - iat (number => IntDate) OPT: identifies the time at which the JWT was issued.
// - aud (string => StringOrURI) OPT: identifies the audience that the JWT is intended for.
//   The audience value is a string -- typically, the base address of the resource
//   being accessed, such as "https://contoso.com"
// - prn (string => StringOrURI) OPT: identifies the subject of the JWT.
// - jti (string => String) OPT: provides a unique identifier for the JWT.
//
// The following SIPI/Knora claims are supported
//
static int lua_generate_jwt(lua_State *L)
{
  lua_getglobal(L, luaconnection);
  Connection *conn = (Connection *)lua_touserdata(L, -1);
  lua_remove(L, -1);// remove from stack

  jwt_t *jwt;
  int top = lua_gettop(L);

  if (top < 1) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.table_to_json(table)': table parameter missing");
    return 2;
  }

  if (!lua_istable(L, 1)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.table_to_json(table)': table is not a lua-table");
    return 2;
  }

  json_t *root = subtable(L, 1);
  char *jsonstr = json_dumps(root, JSON_INDENT(3));
  json_decref(root);

  if (jwt_new(&jwt) != 0) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.table_to_json(table)': Creating token failed");
    return 2;
  }

  jwt_set_alg(
    jwt, JWT_ALG_HS256, (unsigned char *)conn->server()->jwt_secret().c_str(), conn->server()->jwt_secret().size());
  jwt_add_grants_json(jwt, jsonstr);

  char *token = jwt_encode_str(jwt);

  lua_pushboolean(L, true);
  lua_pushstring(L, token);
  free(jsonstr);

  return 2;
}
//=========================================================================

static int lua_decode_jwt(lua_State *L)
{
  lua_getglobal(L, luaconnection);
  Connection *conn = (Connection *)lua_touserdata(L, -1);
  lua_remove(L, -1);// remove from stack

  int top = lua_gettop(L);

  if (top != 1) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.decode_jwt(token)': error in parameter list");
    return 2;
  }

  if (!lua_isstring(L, 1)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.decode_jwt(token)': error in parameter list: is not string");
    return 2;
  }
  std::string token = lua_tostring(L, 1);
  lua_pop(L, 1);

  jwt_t *jwt;
  int err;

  if ((err = jwt_decode(&jwt,
         token.c_str(),
         (unsigned char *)conn->server()->jwt_secret().c_str(),
         conn->server()->jwt_secret().size()))
      != 0) {
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.decode_jwt(token)': Error in decoding token! (1)");
    return 2;
  }

  char *tokendata;

  if ((tokendata = jwt_dump_str(jwt, false)) == nullptr) {
    jwt_free(jwt);
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.decode_jwt(token)': Error in decoding token! (2)");
    return 2;
  }
  jwt_free(jwt);

  std::string tokenstr = tokendata;
  free(tokendata);
  size_t pos = tokenstr.find(".");
  std::string jsonstr = tokenstr.substr(pos + 1);
  json_error_t jsonerror;
  json_t *jsonobj = json_loads(jsonstr.c_str(), JSON_REJECT_DUPLICATES, &jsonerror);
  lua_pushboolean(L, true);

  try {
    lua_jsonobj(L, jsonobj);
  } catch (std::string &errorMsg) {

    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    std::string tmpstr = std::string("'server.decode_jwt(token)': Error in decoding token: ") + errorMsg;
    lua_pushstring(L, tmpstr.c_str());
    return 2;
  }

  json_decref(jsonobj);

  return 2;
}
//=========================================================================

/*!
 * Lua: logger(message, level)
 */
static int lua_logger(lua_State *L)
{
  std::string message;
  int level = LL_ERR;
  int top = lua_gettop(L);

  if (top < 1) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.log()': no message given");
    return 2;
  }

  if (!lua_isstring(L, 1)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "'server.log()': message is not a string");
    return 2;
  }

  message = lua_tostring(L, 1);

  if (top > 1) {
    if (!lua_isinteger(L, 2)) {
      lua_settop(L, 0);// clear stack
      lua_pushboolean(L, false);
      lua_pushstring(L, "'server.log()': level is not integer");
      return 2;
    }

    level = static_cast<int>(lua_tointeger(L, 2));
  }

  if (!message.empty()) {
    log_format(static_cast<LogLevel>(level), "%s", message.c_str());
  }

  lua_pop(L, top);
  lua_pushboolean(L, true);
  lua_pushnil(L);
  return 2;
}
//=========================================================================

static int return_mimetype_to_lua(lua_State *L, const std::pair<std::string, std::string> &mimetype)
{
  lua_pushboolean(L, true);
  lua_createtable(L, 0, 2);// table

  lua_pushstring(L, "mimetype");// table - "mimetype"
  lua_pushstring(L, mimetype.first.c_str());// table - "mimetype" - <mimetype.first>
  lua_rawset(L, -3);// table

  lua_pushstring(L, "charset");// table - "charset"

  if (!mimetype.second.empty()) {
    lua_pushstring(L, mimetype.second.c_str());// table - "charset" - <mimetype.second>
  } else {
    lua_pushnil(L);
  }

  lua_rawset(L, -3);// table
  return 2;
}
//=========================================================================

/*!
 * Lua: success, mimetype = server.parse_mimetype(str)
 */
static int lua_parse_mimetype(lua_State *L)
{
  int top = lua_gettop(L);

  if (top < 1) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "server.parse_mimetype(): no argument given");
    return 2;
  }

  if (!lua_isstring(L, 1)) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "server.parse_mimetype(): argument is not a string");
    return 2;
  }

  std::string mimestr = lua_tostring(L, 1);
  lua_pop(L, top);

  try {
    std::pair<std::string, std::string> mimetype =
      Parsing::parseMimetype(mimestr);// returns a pair of strings: mimetype and charset
    return return_mimetype_to_lua(L, mimetype);
  } catch (Error &err) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    std::ostringstream error_msg;
    error_msg << "server.parse_mimetype() failed: " << err.to_string();
    lua_pushstring(L, error_msg.str().c_str());
    return 2;
  }
}
//=========================================================================

/*!
 * Lua: success, mimetype = server.file_mimetype(path)
 */
static int lua_file_mimetype(lua_State *L)
{
  lua_getglobal(L, luaconnection);
  Connection *conn = (Connection *)lua_touserdata(L, -1);
  lua_remove(L, -1);// remove from stack
  int top = lua_gettop(L);

  if (top < 1) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "server.file_mimetype(): no path given");
    return 2;
  }

  std::string path;
  if (lua_isinteger(L, 1)) {
    std::vector<shttps::Connection::UploadedFile> uploads = conn->uploads();
    int tmpfile_id = static_cast<int>(lua_tointeger(L, 1));
    try {
      path = uploads.at(tmpfile_id - 1).tmpname;// In Lua, indexes are 1-based.
    } catch (const std::out_of_range &oor) {
      lua_settop(L, 0);// clear stack
      lua_pushboolean(L, false);
      lua_pushstring(L, "'server.file_mimetype()': Could not read data of uploaded file. Invalid index?");
      return 2;
    }
  } else if (lua_isstring(L, 1)) {
    path = lua_tostring(L, 1);
  } else {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "server.file_mimetype(): path is not a string");
    return 2;
  }

  lua_pop(L, top);

  try {
    std::pair<std::string, std::string> mimetype =
      Parsing::getFileMimetype(path);// returns a pair of strings: mimetype and charset
    return return_mimetype_to_lua(L, mimetype);
  } catch (Error &err) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    std::ostringstream error_msg;
    error_msg << "server.file_mimetype() failed: " << err.to_string();
    lua_pushstring(L, error_msg.str().c_str());
    return 2;
  }
}
//=========================================================================

/*!
 * LUA: success, mimetype = server.file_mimeconsistency(path)
 *      success, mimetype = server.file_mimconsistency(index)
 */
static int lua_file_mimeconsistency(lua_State *L)
{
  lua_getglobal(L, luaconnection);
  Connection *conn = (Connection *)lua_touserdata(L, -1);
  lua_remove(L, -1);// remove from stack
  int top = lua_gettop(L);

  if (top < 1) {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "server.file_mimetype(): no path given");
    return 2;
  }

  std::string path;
  std::string filename;
  std::string expected_mimetype = "";
  if (lua_isinteger(L, 1)) {
    std::vector<shttps::Connection::UploadedFile> uploads = conn->uploads();
    int tmpfile_id = static_cast<int>(lua_tointeger(L, 1));
    try {
      path = uploads.at(tmpfile_id - 1).tmpname;// In Lua, indexes are 1-based.
      filename = uploads.at(tmpfile_id - 1).origname;
      expected_mimetype = uploads.at(tmpfile_id - 1).mimetype;
    } catch (const std::out_of_range &oor) {
      lua_settop(L, 0);// clear stack
      lua_pushboolean(L, false);
      lua_pushstring(L, "'server.file_mimeconsistency()': Could not read data of uploaded file. Invalid index?");
      return 2;
    }
  } else if (lua_isstring(L, 1)) {
    path = lua_tostring(L, 1);
    filename = path;
  } else {
    lua_settop(L, 0);// clear stack
    lua_pushboolean(L, false);
    lua_pushstring(L, "server.file_mimeconsistency(): path is not a string");
    return 2;
  }

  lua_pop(L, top);
  lua_settop(L, 0);// clear stack

  bool consistency;
  try {
    consistency = Parsing::checkMimeTypeConsistency(path, filename, expected_mimetype);
  } catch (Error &err) {
    lua_pushboolean(L, false);
    std::ostringstream error_msg;
    error_msg << "server.file_mimeconsistency() failed: " << err.to_string();
    lua_pushstring(L, error_msg.str().c_str());
    return 2;
  }

  lua_pushboolean(L, true);
  lua_pushboolean(L, consistency);
  return 2;
}
//=========================================================================

static int lua_systime(lua_State *L)
{
  lua_settop(L, 0);// clear stack

  std::time_t result = std::time(nullptr);
  lua_pushinteger(L, (lua_Integer)result);
  return 1;
}
//=========================================================================

void LuaServer::setLuaPath(const std::string &path)
{
  lua_getglobal(L, "package");
  lua_getfield(L, -1, "path");// get field "path" from table at top of stack (-1)
  std::string cur_path = lua_tostring(L, -1);// grab path string from top of stack
  cur_path.append(";");// do your path magic here
  cur_path.append(path);
  lua_pop(L, 1);// get rid of the string on the stack we just pushed on line 5
  lua_pushstring(L, cur_path.c_str());// push the new one
  lua_setfield(L, -2, "path");// set the field "path" in table at -2 with value at top of stack
  lua_pop(L, 1);// get rid of package table from top of stack
  return;// all done!
}
//=========================================================================


/*!
 * This function registers all variables and functions in the server table
 */
void LuaServer::createGlobals(Connection &conn)
{
  lua_createtable(L, 0, 33);// table1
  // lua_newtable(L); // table1

  Connection::HttpMethod method = conn.method();
  lua_pushstring(L, "method");// table1 - "index_L1"
  switch (method) {
  case Connection::OPTIONS:
    lua_pushstring(L, "OPTIONS");
    break;// table1 - "index_L1" - "value_L1"
  case Connection::GET:
    lua_pushstring(L, "GET");
    break;
  case Connection::HEAD:
    lua_pushstring(L, "HEAD");
    break;
  case Connection::POST:
    lua_pushstring(L, "POST");
    break;
  case Connection::PUT:
    lua_pushstring(L, "PUT");
    break;
  case Connection::DELETE:
    lua_pushstring(L, "DELETE");
    break;
  case Connection::TRACE:
    lua_pushstring(L, "TRACE");
    break;
  case Connection::CONNECT:
    lua_pushstring(L, "CONNECT");
    break;
  case Connection::OTHER:
    lua_pushstring(L, "OTHER");
    break;
  }

  lua_rawset(L, -3);// table1


  lua_pushstring(L, "has_openssl");// table1 - "index_L1"
  lua_pushboolean(L, true);// table1 - "index_L1" - "value_L1"
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "client_ip");// table1 - "index_L1"
  lua_pushstring(L, conn.peer_ip().c_str());// table1 - "index_L1" - "value_L1"
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "client_port");// table1 - "index_L1"
  lua_pushinteger(L, conn.peer_port());// table1 - "index_L1" - "value_L1"
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "secure");// table1 - "index_L1"
  lua_pushboolean(L, conn.secure());// table1 - "index_L1" - "value_L1"
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "header");// table1 - "index_L1"
  std::vector<std::string> headers = conn.header();
  lua_createtable(L, 0, headers.size());// table1 - "index_L1" - table2

  for (unsigned i = 0; i < headers.size(); i++) {
    lua_pushstring(L, headers[i].c_str());// table1 - "index_L1" - table2 - "index_L2"
    lua_pushstring(L,
      conn.header(headers[i]).c_str());// table1 - "index_L1" - table2 - "index_L2" - "value_L2"
    lua_rawset(L, -3);// table1 - "index_L1" - table2
  }

  lua_rawset(L, -3);// table1

  std::unordered_map<std::string, std::string> cookies = conn.cookies();
  lua_pushstring(L, "cookies");// table1 - "index_L1"
  lua_createtable(L, 0, cookies.size());// table1 - "index_L1" - table2

  for (auto cookie : cookies) {
    lua_pushstring(L, cookie.first.c_str());
    lua_pushstring(L, cookie.second.c_str());
    lua_rawset(L, -3);
  }

  lua_rawset(L, -3);// table1

  std::string host = conn.host();
  lua_pushstring(L, "host");// table1 - "index_L1"
  lua_pushstring(L, host.c_str());// table1 - "index_L1" - "value_L1"
  lua_rawset(L, -3);// table1

  std::string uri = conn.uri();
  lua_pushstring(L, "uri");// table1 - "index_L1"
  lua_pushstring(L, uri.c_str());// table1 - "index_L1" - "value_L1"
  lua_rawset(L, -3);// table1

  std::vector<std::string> get_params = conn.getParams();

  if (get_params.size() > 0) {
    lua_pushstring(L, "get");// table1 - "index_L1"
    lua_createtable(L, 0, get_params.size());// table1 - "index_L1" - table2

    for (unsigned i = 0; i < get_params.size(); i++) {
      (void)lua_pushstring(L, get_params[i].c_str());// table1 - "index_L1" - table2 - "index_L2"
      (void)lua_pushstring(
        L, conn.getParams(get_params[i]).c_str());// table1 - "index_L1" - table2 - "index_L2" - "value_L2"
      lua_settable(L, -3);// table1 - "index_L1" - table2
    }

    lua_settable(L, -3);// table1
  }

  std::vector<std::string> post_params = conn.postParams();

  if (post_params.size() > 0) {
    lua_pushstring(L, "post");// table1 - "index_L1"
    lua_createtable(L, 0, post_params.size());// table1 - "index_L1" - table2

    for (unsigned i = 0; i < post_params.size(); i++) {
      lua_pushstring(L, post_params[i].c_str());// table1 - "index_L1" - table2 - "index_L2"
      lua_pushstring(
        L, conn.postParams(post_params[i]).c_str());// table1 - "index_L1" - table2 - "index_L2" - "value_L2"
      lua_settable(L, -3);// table1 - "index_L1" - table2
    }
    lua_settable(L, -3);// table1
  }

  std::vector<Connection::UploadedFile> uploads = conn.uploads();

  if (uploads.size() > 0) {
    lua_pushstring(L, "uploads");// table1 - "index_L1"
    lua_createtable(L, 0, uploads.size());// table1 - "index_L1" - table2

    for (unsigned i = 0; i < uploads.size(); i++) {
      // In Lua, indexes are 1-based.
      lua_pushinteger(L, i + 1);// table1 - "index_L1" - table2 - "index_L2"
      lua_createtable(L, 0, uploads.size());// "table1" - "index_L1" - "table2" - "index_L2" - "table3"

      lua_pushstring(L,
        "fieldname");// "table1" - "index_L1" - "table2" - "index_L2" - "table3" - "index_L3"
      lua_pushstring(L,
        uploads[i]
          .fieldname.c_str());// "table1" - "index_L1" - "table2" - "index_L2" - "table3" - "index_L3" - "value_L3"
      lua_rawset(L, -3);// "table1" - "index_L1" - "table2" - "index_L2" - "table3"

      lua_pushstring(L,
        "origname");// "table1" - "index_L1" - "table2" - "index_L2" - "table3" - "index_L3"
      lua_pushstring(L,
        uploads[i]
          .origname.c_str());// "table1" - "index_L1" - "table2" - "index_L2" - "table3" - "index_L3" - "value_L3"
      lua_rawset(L, -3);// "table1" - "index_L1" - "table2" - "index_L2" - "table3"

      lua_pushstring(L,
        "tmpname");// "table1" - "index_L1" - "table2" - "index_L2" - "table3" - "index_L3"
      lua_pushstring(L,
        uploads[i]
          .tmpname.c_str());// "table1" - "index_L1" - "table2" - "index_L2" - "table3" - "index_L3" - "value_L3"
      lua_rawset(L, -3);// "table1" - "index_L1" - "table2" - "index_L2" - "table3"

      lua_pushstring(L,
        "mimetype");// "table1" - "index_L1" - "table2" - "index_L2" - "table3" - "index_L3"
      lua_pushstring(L,
        uploads[i]
          .mimetype.c_str());// "table1" - "index_L1" - "table2" - "index_L2" - "table3" - "index_L3" - "value_L3"
      lua_rawset(L, -3);// "table1" - "index_L1" - "table2" - "index_L2" - "table3"

      lua_pushstring(L,
        "filesize");// "table1" - "index_L1" - "table2" - "index_L2" - "table3" - "index_L3"
      lua_pushinteger(L,
        uploads[i].filesize);// "table1" - "index_L1" - "table2" - "index_L2" - "table3" - "index_L3" - "value_L3"
      lua_rawset(L, -3);// "table1" - "index_L1" - "table2" - "index_L2" - "table3"

      lua_rawset(L, -3);// table1 - "index_L1" - table2
    }
    lua_rawset(L, -3);// table1
  }

  std::vector<std::string> request_params = conn.requestParams();

  if (request_params.size() > 0) {
    lua_pushstring(L, "request");// table1 - "index_L1"
    lua_createtable(L, 0, request_params.size());// table1 - "index_L1" - table2

    for (unsigned i = 0; i < request_params.size(); i++) {
      lua_pushstring(L, request_params[i].c_str());// table1 - "index_L1" - table2 - "index_L2"
      lua_pushstring(
        L, conn.requestParams(request_params[i]).c_str());// table1 - "index_L1" - table2 - "index_L2" - "value_L2"
      lua_rawset(L, -3);// table1 - "index_L1" - table2
    }

    lua_rawset(L, -3);// table1
  }

  if (conn.contentLength() > 0) {
    lua_pushstring(L, "content");
    lua_pushlstring(L, conn.content(), conn.contentLength());
    lua_rawset(L, -3);// table1 - "index_L1" - table2

    lua_pushstring(L, "content_type");
    lua_pushstring(L, conn.contentType().c_str());
    lua_rawset(L, -3);// table1 - "index_L1" - table2
  }

  //
  // filesystem functions
  //
  lua_pushstring(L, "fs");// table1 - "fs"
  lua_newtable(L);// table1 - "fs" - table2
  luaL_setfuncs(L, fs_methods, 0);
  lua_rawset(L, -3);// table1

  //
  // jsonstr = server.table_to_json(table)
  //
  lua_pushstring(L, "table_to_json");// table1 - "index_L1"
  lua_pushcfunction(L, lua_table_to_json);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "json_to_table");// table1 - "index_L1"
  lua_pushcfunction(L, lua_json_to_table);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1

  //
  // server.print (var[, var...])
  //
  lua_pushstring(L, "print");// table1 - "index_L1"
  lua_pushcfunction(L, lua_print);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "uuid");// table1 - "index_L1"
  lua_pushcfunction(L, lua_uuid);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "uuid62");// table1 - "index_L1"
  lua_pushcfunction(L, lua_uuid_base62);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "uuid_to_base62");// table1 - "index_L1"
  lua_pushcfunction(L, lua_uuid_to_base62);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "base62_to_uuid");// table1 - "index_L1"
  lua_pushcfunction(L, lua_base62_to_uuid);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "setBuffer");// table1 - "index_L1"
  lua_pushcfunction(L, lua_setbuffer);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1


  lua_pushstring(L, "sendHeader");// table1 - "index_L1"
  lua_pushcfunction(L, lua_send_header);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1


  lua_pushstring(L, "sendCookie");// table1 - "index_L1"
  lua_pushcfunction(L, lua_send_cookie);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "copyTmpfile");// table1 - "index_L1"
  lua_pushcfunction(L, lua_copytmpfile);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "shutdown");// table1 - "index_L1"
  lua_pushcfunction(L, lua_exitserver);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "http");// table1 - "index_L1"
  lua_pushcfunction(L, lua_http_client);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "sendStatus");// table1 - "index_L1"
  lua_pushcfunction(L, lua_send_status);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "requireAuth");// table1 - "index_L1"
  lua_pushcfunction(L, lua_require_auth);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "systime");// table1 - "index_L1"
  lua_pushcfunction(L, lua_systime);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "generate_jwt");// table1 - "index_L1"
  lua_pushcfunction(L, lua_generate_jwt);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1 lua_decode_jwt

  lua_pushstring(L, "decode_jwt");// table1 - "index_L1"
  lua_pushcfunction(L, lua_decode_jwt);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "parse_mimetype");// table1 - "index_L1"
  lua_pushcfunction(L, lua_parse_mimetype);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "file_mimetype");// table1 - "index_L1"
  lua_pushcfunction(L, lua_file_mimetype);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "file_mimeconsistency");// table1 - "index_L1"
  lua_pushcfunction(L, lua_file_mimeconsistency);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1

  lua_pushstring(L, "log");// table1 - "index_L1"
  lua_pushcfunction(L, lua_logger);// table1 - "index_L1" - function
  lua_rawset(L, -3);// table1 lua_decode_jwt

  lua_pushstring(L, "loglevel");// table1 - "index_L1"
  lua_createtable(L, 0, 9);// table1 - "index_L1" - table2

  lua_pushstring(L, "LOG_EMERG");// table1 - "index_L1" - table2 - "index_L2"
  lua_pushinteger(L, LL_EMERG);
  lua_rawset(L, -3);// table1 - "index_L1" - table2

  lua_pushstring(L, "LOG_ALERT");// table1 - "index_L1" - table2 - "index_L2"
  lua_pushinteger(L, LL_ALERT);
  lua_rawset(L, -3);// table1 - "index_L1" - table2

  lua_pushstring(L, "LOG_CRIT");// table1 - "index_L1" - table2 - "index_L2"
  lua_pushinteger(L, LL_CRIT);
  lua_rawset(L, -3);// table1 - "index_L1" - table2

  lua_pushstring(L, "LOG_ERR");// table1 - "index_L1" - table2 - "index_L2"
  lua_pushinteger(L, LL_ERR);
  lua_rawset(L, -3);// table1 - "index_L1" - table2

  lua_pushstring(L, "LOG_WARNING");// table1 - "index_L1" - table2 - "index_L2"
  lua_pushinteger(L, LL_WARNING);
  lua_rawset(L, -3);// table1 - "index_L1" - table2

  lua_pushstring(L, "LOG_NOTICE");// table1 - "index_L1" - table2 - "index_L2"
  lua_pushinteger(L, LL_NOTICE);
  lua_rawset(L, -3);// table1 - "index_L1" - table2

  lua_pushstring(L, "LOG_INFO");// table1 - "index_L1" - table2 - "index_L2"
  lua_pushinteger(L, LL_INFO);
  lua_rawset(L, -3);// table1 - "index_L1" - table2

  lua_pushstring(L, "LOG_DEBUG");// table1 - "index_L1" - table2 - "index_L2"
  lua_pushinteger(L, LL_DEBUG);
  lua_rawset(L, -3);// table1 - "index_L1" - table2

  lua_rawset(L, -3);// table1

  lua_setglobal(L, servertablename);

  lua_pushlightuserdata(L, &conn);
  lua_setglobal(L, luaconnection);
}
//=========================================================================


void LuaServer::add_servertableentry(const std::string &name, const std::string &value)
{
  lua_getglobal(L, servertablename);// "table1"

  lua_pushstring(L, name.c_str());// table1 - "index_L1"
  lua_pushstring(L, value.c_str());// table1 - "index_L1" - "value_L1"
  lua_rawset(L, -3);// table1
  lua_pop(L, 1);
}
//=========================================================================

/*!
 * Reads a string item from a Lua config file. A lua config file is always a table with
 * a name (e.g. sipi which contains configuration parameters
 *
 * @param table Table name for config parameters
 * @param variable Variable name
 * @param defval Default value to take if variable is not defined
 * @return Configuartion parameter value
 */
std::string LuaServer::configString(const std::string table, const std::string variable, const std::string defval)
{
  if (lua_getglobal(L, table.c_str()) != LUA_TTABLE) {
    lua_pop(L, 1);
    return defval;
  }

  lua_getfield(L, -1, variable.c_str());

  if (lua_isnil(L, -1)) {
    lua_pop(L, 2);
    return defval;
  }

  if (!lua_isstring(L, -1)) { throw Error("String expected for " + table + "." + variable); }

  std::string retval = lua_tostring(L, -1);
  lua_pop(L, 2);
  return retval;
}
//=========================================================================

/*!
 * Reads a boolean item from a Lua config file. A lua config file is always a table with
 * a name (e.g. sipi which contains configuration parameters
 *
 * @param table Table name for config parameters
 * @param variable Variable name
 * @param defval Default value to take if variable is not defined
 * @return Configuartion parameter value
 */
bool LuaServer::configBoolean(const std::string table, const std::string variable, const bool defval)
{
  if (lua_getglobal(L, table.c_str()) != LUA_TTABLE) {
    lua_pop(L, 1);
    return defval;
  }

  lua_getfield(L, -1, variable.c_str());

  if (lua_isnil(L, -1)) {
    lua_pop(L, 2);
    return defval;
  }

  if (!lua_isboolean(L, -1)) { throw Error("Integer expected for " + table + "." + variable); }

  bool retval = lua_toboolean(L, -1) == 1;
  lua_pop(L, 2);
  return retval;
}
//=========================================================================

/*!
 * Reads a boolean item from a Lua config file. A lua config file is always a table with
 * a name (e.g. sipi which contains configuration parameters
 *
 * @param table Table name for config parameters
 * @param variable Variable name
 * @param defval Default value to take if variable is not defined
 * @return Configuartion parameter value
 */
int LuaServer::configInteger(const std::string table, const std::string variable, const int defval)
{
  if (lua_getglobal(L, table.c_str()) != LUA_TTABLE) {
    lua_pop(L, 1);
    return defval;
  }

  lua_getfield(L, -1, variable.c_str());

  if (lua_isnil(L, -1)) {
    lua_pop(L, 2);
    return defval;
  }

  if (!lua_isinteger(L, -1)) { throw Error("Integer expected for " + table + "." + variable); }

  int retval = static_cast<int>(lua_tointeger(L, -1));
  lua_pop(L, 2);
  return retval;
}
//=========================================================================

/*!
 * Reads a float item from a Lua config file. A lua config file is always a table with
 * a name (e.g. sipi which contains configuration parameters
 *
 * @param table Table name for config parameters
 * @param variable Variable name
 * @param defval Default value to take if variable is not defined
 * @return Configuartion parameter value
 */
float LuaServer::configFloat(const std::string table, const std::string variable, const float defval)
{
  if (lua_getglobal(L, table.c_str()) != LUA_TTABLE) {
    lua_pop(L, 1);
    return defval;
  }

  lua_getfield(L, -1, variable.c_str());

  if (lua_isnil(L, -1)) {
    lua_pop(L, 2);
    return defval;
  }

  if (!lua_isnumber(L, -1)) { throw Error("Number expected for " + table + "." + variable); }

  lua_Number num = lua_tonumber(L, -1);
  lua_pop(L, 2);
  return (float)num;
}
//=========================================================================

/*!
 * Reads a table of values and returns the values as string vector
 *
 * @param table Table name for config parameters
 * @param variable Variable name (which must be a table/array)
 * @return Vector of table entries
 */
const std::vector<std::string> LuaServer::configStringList(const std::string table, const std::string variable)
{
  std::vector<std::string> strings;
  if (lua_getglobal(L, table.c_str()) != LUA_TTABLE) {
    lua_pop(L, 1);
    return strings;
  }

  lua_getfield(L, -1, variable.c_str());

  if (lua_isnil(L, -1)) {
    lua_pop(L, 2);
    return strings;
  }

  if (!lua_istable(L, -1)) { throw Error("Value '" + variable + "' in config file must be a table"); }

  for (int i = 1;; i++) {
    lua_rawgeti(L, -1, i);

    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      break;
    }
    if (lua_isstring(L, -1)) {
      std::string tmpstr = lua_tostring(L, -1);
      strings.push_back(tmpstr);
    }
    lua_pop(L, 1);
  }
  return strings;
}
//=========================================================================

/*!
 * Reads a table with key value pairs
 * @param table Table name for config parameters
 * @param variable Variable name (which must be a table with key value pairs)
 * @return Map of key-value pairs
 */
const std::map<std::string, std::string> LuaServer::configStringTable(const std::string &table,
  const std::string &variable,
  const std::map<std::string, std::string> &defval)
{
  std::map<std::string, std::string> subtable;

  int top = lua_gettop(L);

  if (lua_getglobal(L, table.c_str()) != LUA_TTABLE) {
    lua_pop(L, top);
    return defval;
  }

  try {
    lua_getfield(L, 1, variable.c_str());
  } catch (...) {
    return defval;
  }

  if (lua_isnil(L, -1)) {
    lua_pop(L, lua_gettop(L));
    return defval;
  }

  if (!lua_istable(L, -1)) { throw Error("Value '" + variable + "' in config file must be a table"); }

  lua_pushvalue(L, -1);
  lua_pushnil(L);

  while (lua_next(L, -2)) {
    std::string valstr, keystr;
    lua_pushvalue(L, -2);
    if (lua_isstring(L, -1)) {
      keystr = lua_tostring(L, -1);
    } else {
      throw Error("Key element of '" + variable + "' in config file must be a string");
    }
    if (lua_isstring(L, -2)) {
      valstr = lua_tostring(L, -2);
    } else {
      throw Error("Value element of '" + variable + "' in config file must be a string");
    }
    lua_pop(L, 2);
    subtable[keystr] = valstr;
  }

  return subtable;
};
//=========================================================================

/*!
 * Read a route definition (containing "method", "route", "script" keys)
 *
 * @param routetable A table name containing route info
 * @return Route info
 */
std::vector<LuaRoute> LuaServer::configRoute(const std::string &routetable) const
{
  static struct
  {
    const char *name;
    int type;
  } fields[] = { { "method", LUA_TSTRING }, { "route", LUA_TSTRING }, { "script", LUA_TSTRING }, { nullptr, 0 } };

  std::vector<LuaRoute> routes;

  lua_getglobal(L, routetable.c_str());

  if (!lua_istable(L, -1)) { throw Error("Value '" + routetable + "' in config file must be a table"); }

  for (int i = 1;; i++) {
    lua_rawgeti(L, -1, i);

    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      break;
    }

    // an element of the 'listen' table should now be at the top of the stack
    luaL_checktype(L, -1, LUA_TTABLE);
    int field_index;
    LuaRoute route;

    for (field_index = 0; fields[field_index].name != nullptr; field_index++) {
      lua_getfield(L, -1, fields[field_index].name);
      luaL_checktype(L, -1, fields[field_index].type);

      std::string method;
      // you should probably use a function pointer in the fields table.
      // I am using a simple switch/case here
      switch (field_index) {
      case 0:
        method = lua_tostring(L, -1);
        if (method == "GET") {
          route.method = Connection::HttpMethod::GET;
        } else if (method == "PUT") {
          route.method = Connection::HttpMethod::PUT;
        } else if (method == "POST") {
          route.method = Connection::HttpMethod::POST;
        } else if (method == "DELETE") {
          route.method = Connection::HttpMethod::DELETE;
        } else if (method == "OPTIONS") {
          route.method = Connection::HttpMethod::OPTIONS;
        } else if (method == "CONNECT") {
          route.method = Connection::HttpMethod::CONNECT;
        } else if (method == "HEAD") {
          route.method = Connection::HttpMethod::HEAD;
        } else if (method == "OTHER") {
          route.method = Connection::HttpMethod::OTHER;
        } else {
          throw Error(std::string("Unknown HTTP method") + method);
        }
        break;
      case 1:
        route.route = lua_tostring(L, -1);
        break;
      case 2:
        route.script = lua_tostring(L, -1);
        break;
      }
      // remove the field value from the top of the stack
      lua_pop(L, 1);
    }

    lua_pop(L, 1);
    routes.push_back(route);
  }

  return routes;
}
//=========================================================================

/*
const std::map<std::string,LuaKeyValStore> LuaServer::configKeyValueStores(const std::string table) {
    std::map<std::string,LuaKeyValStore> keyvalstores;

    int top = lua_gettop(L);

    lua_getglobal(L, table.c_str());

    if (!lua_istable(L, -1)) {
        //return keyvalstores;
        throw Error(thisSourceFile, __LINE__, "Value '" + table + "' in config file must be a table");
    }

    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
        const char *profile_name = lua_tostring(L, -2);

        if (!lua_istable(L, -1)) {
            throw Error(thisSourceFile, __LINE__, "Value '" + std::string(profile_name) + "' in config file must be a
table");
        }
        LuaKeyValStore kvstore;
        lua_pushnil(L);
        while (lua_next(L, -2) != 0) {
            std::shared_ptr<LuaValstruct> tmplv = std::make_shared<LuaValstruct>();
            const char *param_name = lua_tostring(L, -2);
            if (lua_isstring(L, -1)) {
                tmplv->type = LuaValstruct::STRING_TYPE;
                tmplv->value.s = std::string(lua_tostring(L, -1));
            } else if (lua_isinteger(L, -1)) {
                tmplv->type = LuaValstruct::INT_TYPE;
                tmplv->value.i = lua_tointeger(L, -1);
            } else if (lua_isnumber(L, -1)) {
                tmplv->type = LuaValstruct::FLOAT_TYPE;
                tmplv->value.f = (float) lua_tonumber(L, -1);
            } else if (lua_isboolean(L, -1)) {
                tmplv->type = LuaValstruct::BOOLEAN_TYPE;
                tmplv->value.b = (bool) lua_toboolean(L, -1);
            } else if (lua_isnil(L, -1)) {
                std::string luaTypeName = std::string(lua_typename(L, -1));
                std::ostringstream errStream;
                errStream << "Table contained returned nil";
                std::string errorMsg = errStream.str();
                throw Error(thisSourceFile, __LINE__, errorMsg);
            } else {
                std::string luaTypeName = std::string(lua_typename(L, -1));
                std::ostringstream errMsg;
                errMsg << "Table contained a value of type " << luaTypeName
                       << ", which is not supported";
                throw Error(thisSourceFile, __LINE__, errMsg.str());
            }
            kvstore[param_name] = tmplv;
            lua_pop(L, 1);
        }
        keyvalstores[profile_name] = kvstore;
        lua_pop(L, 1);
    }
    lua_pop(L, top);

    return keyvalstores;
}
*/
int LuaServer::executeChunk(const std::string &luastr, const std::string &scriptname)
{
  if (luaL_dostring(L, luastr.c_str()) != LUA_OK) {
    const char *errorMsg = nullptr;

    if (lua_gettop(L) > 0) {
      errorMsg = lua_tostring(L, 1);
      lua_pop(L, 1);
      throw Error(std::string("LuaServer::executeChunk failed: ") + errorMsg + ", scriptname: " + scriptname);
    } else {
      throw Error("LuaServer::executeChunk failed");
    }
  }

  int top = lua_gettop(L);

  if (top == 1) {
    int status = static_cast<int>(lua_tointeger(L, 1));
    lua_pop(L, 1);
    return status;
  }

  return 1;
}
//=========================================================================

static std::shared_ptr<LuaValstruct> getLuaValue(lua_State *L, int index, const std::string &funcname)
{
  std::shared_ptr<LuaValstruct> tmplv = std::make_shared<LuaValstruct>();
  if (lua_isstring(L, index)) {
    tmplv->type = LuaValstruct::STRING_TYPE;
    tmplv->value.s = std::string(lua_tostring(L, index));
  } else if (lua_isinteger(L, index)) {
    tmplv->type = LuaValstruct::INT_TYPE;
    tmplv->value.i = lua_tointeger(L, index);
  } else if (lua_isnumber(L, index)) {
    tmplv->type = LuaValstruct::FLOAT_TYPE;
    tmplv->value.f = (float)lua_tonumber(L, index);
  } else if (lua_isboolean(L, index)) {
    tmplv->type = LuaValstruct::BOOLEAN_TYPE;
    tmplv->value.b = (bool)lua_toboolean(L, index);
  } else if (lua_istable(L, index)) {
    tmplv->type = LuaValstruct::TABLE_TYPE;
    lua_pushnil(L); /* first key */
    while (lua_next(L, index) != 0) {
      std::string key(lua_tostring(L, -2));
      std::shared_ptr<LuaValstruct> val = getLuaValue(L, -1, funcname);
      tmplv->value.table[key] = val;
      /* removes 'value'; keeps 'key' for next iteration */
      lua_pop(L, 1);
    }
  } else if (lua_isnil(L, index)) {
    std::string luaTypeName = std::string(lua_typename(L, index));
    std::ostringstream errStream;
    errStream << "Lua function " << funcname << " returned nil";
    std::string errorMsg = errStream.str();
    throw Error(errorMsg);
  } else {
    std::string luaTypeName = std::string(lua_typename(L, index));
    std::ostringstream errMsg;
    errMsg << "Lua function " << funcname << " returned a value of type " << luaTypeName << ", which is not supported";
    throw Error(errMsg.str());
  }
  return tmplv;
}

static void pushLuaValue(lua_State *L, const std::shared_ptr<LuaValstruct> &lv)
{
  switch (lv->type) {
  case LuaValstruct::INT_TYPE: {
    lua_pushinteger(L, lv->value.i);
    break;
  }
  case LuaValstruct::FLOAT_TYPE: {
    lua_pushnumber(L, lv->value.f);
    break;
  }
  case LuaValstruct::STRING_TYPE: {
    lua_pushstring(L, lv->value.s.c_str());
    break;
  }
  case LuaValstruct::BOOLEAN_TYPE: {
    lua_pushboolean(L, lv->value.b);
    break;
  }
  case LuaValstruct::TABLE_TYPE: {
    lua_createtable(L, 0, lv->value.table.size());
    for (const auto &keyval : lv->value.table) {
      lua_pushstring(L, keyval.first.c_str());
      pushLuaValue(L, keyval.second);
    }
    break;
  }
  }
}

std::vector<std::shared_ptr<LuaValstruct>> LuaServer::executeLuafunction(const std::string &funcname,
  const std::vector<std::shared_ptr<LuaValstruct>> &lvals)
{
  if (lua_getglobal(L, funcname.c_str()) != LUA_TFUNCTION) {
    lua_settop(L, 0);// clear stack
    std::ostringstream errMsg;
    errMsg << "LuaServer::executeLuafunction: function " << funcname << " not found";
    throw Error(errMsg.str());
  }

  for (const auto &lv : lvals) { pushLuaValue(L, lv); }

  if (lua_pcall(L, lvals.size(), LUA_MULTRET, 0) != LUA_OK) {
    std::string luaErrorMsg(lua_tostring(L, 1));
    lua_settop(L, 0);// clear stack
    std::ostringstream errMsg;
    errMsg << "LuaServer::executeLuafunction: function " << funcname << " failed: " << luaErrorMsg;
    throw Error(errMsg.str());
  }

  const int top = lua_gettop(L);
  std::vector<std::shared_ptr<LuaValstruct>> retval;

  for (int i = 1; i <= top; i++) { retval.push_back(getLuaValue(L, i, funcname)); }

  lua_pop(L, top);
  return retval;
}

//=========================================================================


bool LuaServer::luaFunctionExists(const std::string &funcname)
{
  int ltype = lua_getglobal(L, funcname.c_str());
  lua_pop(L, 1);
  return (ltype == LUA_TFUNCTION);
}
}// namespace shttps
