/*
 * Copyright © 2016 Lukas Rosenthaler, Andrea Bianco, Benjamin Geer,
 * Ivan Subotic, Tobias Schweizer, André Kilchenmann, and André Fatton.
 * This file is part of Sipi.
 * Sipi is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published
 * by the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * Sipi is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * Additional permission under GNU AGPL version 3 section 7:
 * If you modify this Program, or any covered work, by linking or combining
 * it with Kakadu (or a modified version of that library) or Adobe ICC Color
 * Profiles (or a modified version of that library) or both, containing parts
 * covered by the terms of the Kakadu Software Licence or Adobe Software Licence,
 * or both, the licensors of this Program grant you additional permission
 * to convey the resulting work.
 * See the GNU Affero General Public License for more details.
 * You should have received a copy of the GNU Affero General Public
 * License along with Sipi.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <algorithm>
#include <functional>
#include <cctype>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>      // Needed for memset
#include <chrono>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "SockStream.h"
#include "LuaServer.h"
#include "Connection.h"
#include "Server.h"
#include "ChunkReader.h"

#include "sole.hpp"

#ifdef SHTTPS_ENABLE_SSL
#include "jwt.h"
#endif

#include <jansson.h>

using namespace std;
using  ms = chrono::milliseconds;
using get_time = chrono::steady_clock ;

static const char __file__[] = __FILE__;

static const char servertablename[] = "server";

namespace shttps {


    char luaconnection[] = "__shttpsconnection";

    /*!
     * Error handler for Lua errors!
     */
    static int dont_panic(lua_State *L) {
        const char *luapanic = lua_tostring(L, -1);
        throw Error(__file__, __LINE__, string("Lua panic: ") + luapanic);
    }
    //=========================================================================

    // trim from start
    static inline std::string &ltrim(std::string &s) {
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), std::not1(std::ptr_fun<int, int>(std::isspace))));
        return s;
    }
    //=========================================================================

    // trim from end
    static inline std::string &rtrim(std::string &s) {
        s.erase(std::find_if(s.rbegin(), s.rend(), std::not1(std::ptr_fun<int, int>(std::isspace))).base(), s.end());
        return s;
    }
    //=========================================================================

    // trim from both ends
    static inline std::string &trim(std::string &s) {
        return ltrim(rtrim(s));
    }
    //=========================================================================

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


    static inline bool is_base64(unsigned char c) {
        return (isalnum(c) || (c == '+') || (c == '/'));
    }

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

    static std::string base64_decode(std::string const& encoded_string) {
        int in_len = encoded_string.size();
        int i = 0;
        int j = 0;
        int in_ = 0;
        unsigned char char_array_4[4], char_array_3[3];
        std::string ret;

        while (in_len-- && ( encoded_string[in_] != '=') && is_base64(encoded_string[in_])) {
            char_array_4[i++] = encoded_string[in_]; in_++;
            if (i ==4) {
                for (i = 0; i <4; i++)
                    char_array_4[i] = base64_chars.find(char_array_4[i]);

                char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
                char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
                char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

                for (i = 0; (i < 3); i++)
                    ret += char_array_3[i];
                i = 0;
            }
        }

        if (i) {
            for (j = i; j <4; j++)
                char_array_4[j] = 0;

            for (j = 0; j <4; j++)
                char_array_4[j] = base64_chars.find(char_array_4[j]);

            char_array_3[0] = (char_array_4[0] << 2) + ((char_array_4[1] & 0x30) >> 4);
            char_array_3[1] = ((char_array_4[1] & 0xf) << 4) + ((char_array_4[2] & 0x3c) >> 2);
            char_array_3[2] = ((char_array_4[2] & 0x3) << 6) + char_array_4[3];

            for (j = 0; (j < i - 1); j++) ret += char_array_3[j];
        }

        return ret;
    }

    /*!
     * Instantiates a Lua server
     */
    LuaServer::LuaServer() {
        if ((L = luaL_newstate()) == NULL) {
            throw new Error(__file__, __LINE__, "Couldn't start lua interpreter!");
        }
        lua_atpanic(L, dont_panic);
        luaL_openlibs(L);

    }
    //=========================================================================

    /*!
     * Instantiates a Lua server and directly executes the script file given
     *
     * \param[in] luafile A file containing a Lua script or a Lua code chunk
     */
    LuaServer::LuaServer(const string &luafile, bool iscode) {
        if ((L = luaL_newstate()) == NULL) {
            throw new Error(__file__, __LINE__, "Couldn't start lua interpreter!");
        }

        lua_atpanic(L, dont_panic);

        luaL_openlibs(L);


        if (!luafile.empty()) {
            if (iscode) {
                if (luaL_loadstring(L, luafile.c_str()) != 0) {
                    const char *luaerror = lua_tostring(L, -1);
                    throw Error(__FILE__, __LINE__, string("Lua error: ") + luaerror);
                }
            }
            else {
                if (luaL_loadfile(L, luafile.c_str()) != 0) {
                    const char *luaerror = lua_tostring(L, -1);
                    throw Error(__FILE__, __LINE__, string("Lua error: ") + luaerror);
                }
            }
            if (lua_pcall(L, 0, LUA_MULTRET, 0) != 0) {
                const char *luaerror = lua_tostring(L, -1);
                throw Error(__FILE__, __LINE__, string("Lua error: ") + luaerror);
            }
        }
    }
    //=========================================================================

    /*!
     * Destroys the Lua server and free's all resources (garbage collectors are called here)
     */
    LuaServer::~LuaServer() {
        lua_close(L);
    }
    //=========================================================================

    /*!
     * Activates the the connection buffer. Optionally the buffer size and increment size can be given
     * LUA: server.setBuffer([bufsize][,incsize])
     */
    static int lua_setbuffer(lua_State *L) {
        int bufsize = 0;
        int incsize = 0;

        int top = lua_gettop(L);
        if (top > 0) {
            if (lua_isinteger(L, 1)) {
                bufsize = lua_tointeger(L, 1);
            }
            else {
                lua_pushstring(L,
                               "'server.setbuffer([bufize][, incsize])': requires bufsize size as integer!");
                lua_error(L);
            }
        }
        if (top > 1) {
            if (lua_isinteger(L, 2)) {
                incsize = lua_tointeger(L, 2);
            }
            else {
                lua_pushstring(L,
                               "'server.setbuffer([bufize][, incsize])': requires incsize size as integer!");
                lua_error(L);
            }
        }
        lua_pop(L, 2);

        lua_getglobal(L, luaconnection); // push onto stack
        Connection *conn = (Connection *) lua_touserdata(L, -1); // does not change the stack
        lua_remove(L, -1); // remove from stack

        if ((bufsize > 0) && (incsize > 0)) {
            conn->setBuffer(bufsize, incsize);
        }
        else if (bufsize > 0) {
            conn->setBuffer(bufsize);
        }
        else {
            conn->setBuffer();
        }

        return 0;
    }
    //=========================================================================

    /*!
     * Checks the filetype of a given filepath
     * LUA: server.fs.ftype("path")
     * RETURNS: "FILE", "DIRECTORY", "CHARDEV", "BLOCKDEV", "LINK", "SOCKET" or "UNKNOWN"
     */
    static int lua_fs_ftype(lua_State *L) {
        struct stat s;

        int top = lua_gettop(L);
        if (top < 1) {
            lua_pushstring(L, "'server.fs.ftype(filename)': parameter missing!");
            lua_error(L);
            return 0;
        }
        if (!lua_isstring(L, 1)) {
            lua_pushstring(L, "'server.fs.ftype(filename)': filename is not a string!");
            lua_error(L);
            return 0;
        }
        const char *filename = lua_tostring(L, 1);
        lua_pop(L, 1); // clear stack

        if (stat(filename, &s) != 0) {
            lua_pushstring(L, strerror(errno));
            lua_error(L);
            return 0;
        }

        if (S_ISREG(s.st_mode)) {
            lua_pushstring(L, "FILE");
        }
        else if (S_ISDIR(s.st_mode)) {
            lua_pushstring(L, "DIRECTORY");
        }
        else if (S_ISCHR(s.st_mode)) {
            lua_pushstring(L, "CHARDEV");
        }
        else if (S_ISBLK(s.st_mode)) {
            lua_pushstring(L, "BLOCKDEV");
        }
        else if (S_ISLNK(s.st_mode)) {
            lua_pushstring(L, "LINK");
        }
        else if (S_ISFIFO(s.st_mode)) {
            lua_pushstring(L, "FIFO");
        }
        else if (S_ISSOCK(s.st_mode)) {
            lua_pushstring(L, "SOCKET");
        }
        else {
            lua_pushstring(L, "UNKNOWN");
        }
        return 1;
    }
    //=========================================================================

    /*!
     * check if a file is readable
     * LUA: server.fs.is_readable(filepath)
     */
    static int lua_fs_is_readable(lua_State *L) {
        int top = lua_gettop(L);
        if (top < 1) {
            lua_pushstring(L, "'server.fs.is_readable(filename)': parameter missing!");
            lua_error(L);
            return 0;
        }
        if (!lua_isstring(L, 1)) {
            lua_pushstring(L, "'server.fs.is_readable(filename)': filename is not a string!");
            lua_error(L);
            return 0;
        }
        const char *filename = lua_tostring(L, 1);
        lua_pop(L, 1); // clear stack

        if (access(filename, R_OK) == 0) {
            lua_pushboolean(L, true);
        }
        else {
            lua_pushboolean(L, false);
        }
        return 1;
    }
    //=========================================================================

    /*!
     * check if a file is writeable
     * LUA: server.fs.is_writeable(filepath)
     */
    static int lua_fs_is_writeable(lua_State *L) {
        int top = lua_gettop(L);
        if (top < 1) {
            lua_pushstring(L, "'server.fs.is_writeable(filename)': parameter missing!");
            lua_error(L);
            return 0;
        }
        if (!lua_isstring(L, 1)) {
            lua_pushstring(L, "'server.fs.is_writeable(filename)': filename is not a string!");
            lua_error(L);
            return 0;
        }
        const char *filename = lua_tostring(L, 1);
        lua_pop(L, 1); // clear stack

        if (access(filename, W_OK) == 0) {
            lua_pushboolean(L, true);
        }
        else {
            lua_pushboolean(L, false);
        }
        return 1;
    }
    //=========================================================================

    /*!
     * check if a file is executable
     * LUA: server.fs.is_executable(filepath)
     */
    static int lua_fs_is_executable(lua_State *L) {
        int top = lua_gettop(L);
        if (top < 1) {
            lua_pushstring(L, "'server.fs.is_executable(filename)': parameter missing!");
            lua_error(L);
            return 0;
        }
        if (!lua_isstring(L, 1)) {
            lua_pushstring(L, "'server.fs.is_executable(filename)': filename is not a string!");
            lua_error(L);
            return 0;
        }
        const char *filename = lua_tostring(L, 1);
        lua_pop(L, 1); // clear stack

        if (access(filename, X_OK) == 0) {
            lua_pushboolean(L, true);
        }
        else {
            lua_pushboolean(L, false);
        }
        return 1;
    }
    //=========================================================================

    /*!
     * check if a file exists
     * LUA: server.fs.exists(filepath)
     */
    static int lua_fs_exists(lua_State *L) {
        int top = lua_gettop(L);
        if (top < 1) {
            lua_pushstring(L, "'server.fs.exists(filename)': parameter missing!");
            lua_error(L);
            return 0;
        }
        if (!lua_isstring(L, 1)) {
            lua_pushstring(L, "'server.fs.exists(filename)': filename is not a string!");
            lua_error(L);
            return 0;
        }
        const char *filename = lua_tostring(L, 1);
        lua_pop(L, 1); // clear stack

        if (access(filename, F_OK) == 0) {
            lua_pushboolean(L, true);
        }
        else {
            lua_pushboolean(L, false);
        }
        return 1;
    }
    //=========================================================================


    /*!
     * deletes a file from the file system. The file must exist and the user must have write
     * access
     * LUA: server.fs.unlink(filename)
     */
    static int lua_fs_unlink(lua_State *L) {
        int top = lua_gettop(L);
        if (top < 1) {
            lua_pushstring(L, "'server.fs.unlink(filename)': parameter missing!");
            lua_error(L);
            return 0;
        }
        if (!lua_isstring(L, 1)) {
            lua_pushstring(L, "'server.fs.unlink(filename)': filename is not a string!");
            lua_error(L);
            return 0;
        }
        const char *filename = lua_tostring(L, 1);
        lua_pop(L, 1); // clear stack

        if (unlink(filename) != 0) {
            lua_pushstring(L, strerror(errno));
            lua_error(L);
            return 0;
        }
        return 0;
    }
    //=========================================================================

    /*!
     * Creates a new directory
     * LUA: server.fs.mkdir(dirname, tonumber('0755', 8))
     */
    static int lua_fs_mkdir(lua_State *L) {
        int top = lua_gettop(L);
        if (top < 2) {
            lua_pushstring(L, "'server.fs.mkdir(dirname, mask)': parameter missing!");
            lua_error(L);
            return 0;
        }
        if (!lua_isstring(L, 1)) {
            lua_pushstring(L, "'server.fs.mkdir(dirname, mask)': dirname is not a string!");
            lua_error(L);
            return 0;
        }
        if (!lua_isinteger(L, 2)) {
            lua_pushstring(L, "'server.fs.mkdir(dirname, mask)': mask is not an integer!");
            lua_error(L);
            return 0;
        }
        const char *dirname = lua_tostring(L, 1);
        int mode = lua_tointeger(L, 2);
        lua_pop(L, 2); // clear stack

        if (mkdir(dirname, mode) != 0) {
            lua_pushstring(L, strerror(errno));
            lua_error(L);
            return 0;
        }
        return 0;
    }
    //=========================================================================

    /*!
     * Creates a new directory
     * LUA: server.fs.mkdir(dirname)
     */
    static int lua_fs_rmdir(lua_State *L) {
        int top = lua_gettop(L);
        if (top < 1) {
            lua_pushstring(L, "'server.fs.rmdir(dirname)': parameter missing!");
            lua_error(L);
            return 0;
        }
        if (!lua_isstring(L, 1)) {
            lua_pushstring(L, "'server.fs.rmdir(dirname)': dirname is not a string!");
            lua_error(L);
            return 0;
        }
        const char *dirname = lua_tostring(L, 1);
        lua_pop(L, 1); // clear stack

        if (rmdir(dirname) != 0) {
            lua_pushstring(L, strerror(errno));
            lua_error(L);
            return 0;
        }
        return 0;
    }
    //=========================================================================

    /*!
     * gets the current working directory
     * LUA: curdir = server.fs.getcwd()
     */
    static int lua_fs_getcwd(lua_State *L) {
        char *dirname = getcwd(NULL, 0);
        lua_pushstring(L, dirname);
        free(dirname);
        return 1;
    }
    //=========================================================================

    /*!
     * Change working directory
     * LUA: oldir = server.fs.chdir(newdir)
     */
    static int lua_fs_chdir(lua_State *L) {
        int top = lua_gettop(L);
        if (top < 1) {
            lua_pushstring(L, "'server.fs.chdir(dirname)': parameter missing!");
            lua_error(L);
            return 0;
        }
        if (!lua_isstring(L, 1)) {
            lua_pushstring(L, "'server.fs.chdir(dirname)': dirname is not a string!");
            lua_error(L);
            return 0;
        }
        const char *dirname = lua_tostring(L, 1);
        lua_pop(L, 1); // clear stack

        char *olddirname = getcwd(NULL, 0);

        if (chdir(dirname) != 0) {
            lua_pushstring(L, strerror(errno));
            lua_error(L);
            return 0;
        }
        lua_pushstring(L, olddirname);
        free(olddirname);
        return 1;
    }
    //=========================================================================

    static const luaL_Reg fs_methods[] = {
            {"ftype", lua_fs_ftype},
            {"is_readable", lua_fs_is_readable},
            {"is_writeable", lua_fs_is_writeable},
            {"is_executable", lua_fs_is_executable},
            {"exists", lua_fs_exists},
            {"unlink", lua_fs_unlink},
            {"mkdir", lua_fs_mkdir},
            {"rmdir", lua_fs_rmdir},
            {"getcwd", lua_fs_getcwd},
            {"chdir", lua_fs_chdir},
            {0,     0}
    };
    //=========================================================================


    /*!
     * Generates a random version 4 uuid string
     * LUA: uuid = server.uuid()
     */
    static int lua_uuid(lua_State *L) {
        sole::uuid u4 = sole::uuid4();
        string uuidstr = u4.str();
        lua_pushstring(L, uuidstr.c_str());
        return 1;
    }
    //=========================================================================

    /*!
     * Generate a base62-uuid string
     * LUA: uuid62 = server.uuid62()
     */
    static int lua_uuid_base62(lua_State *L) {
        sole::uuid u4 = sole::uuid4();
        string uuidstr62 = u4.base62();
        lua_pushstring(L, uuidstr62.c_str());
        return 1;
    }
    //=========================================================================

    /*!
     * Converts a uuid-string to a base62 uuid
     * LUA: uuid62 = server.uuid_to_base62(uuid)
     */
    static int lua_uuid_to_base62(lua_State *L) {
        int top = lua_gettop(L);
        if (top != 1) {
            lua_pushstring(L, "'server.uuid_tobase62(uuid)': uuid parameter missing!");
            lua_error(L);
            return 0;
        }
        if (!lua_isstring(L, 1)) {
            lua_pushstring(L, "'server.uuid_tobase62(uuid)': uuid is not a string!");
            lua_error(L);
            return 0;
        }
        const char *uuidstr = lua_tostring(L, 1);
        lua_pop(L, 1); // clear stack

        sole::uuid u4 = sole::rebuild(uuidstr);
        string uuidb62str = u4.base62();
        lua_pushstring(L, uuidb62str.c_str());

        return 1;
    }
    //=========================================================================

    /*!
     * Converts a base62-uuid to a "normal" uuid
     * LUA: uuid = server.base62_to_uuid(uuid62)
     */
    static int lua_base62_to_uuid(lua_State *L) {
        int top = lua_gettop(L);
        if (top < 1) {
            lua_pushstring(L, "'server.base62_to_uuid(uuid62)': uuid62 parameter missing!");
            lua_error(L);
            return 0;
        }
        if (!lua_isstring(L, 1)) {
            lua_pushstring(L, "'server.base62_to_uuid(uuid62)': uuid62 is not a string!");
            lua_error(L);
            return 0;
        }
        const char *uuidb62 = lua_tostring(L, 1);
        lua_pop(L, 1); // clear stack


        sole::uuid u4 = sole::rebuild(uuidb62);
        string uuidstr = u4.str();
        lua_pushstring(L, uuidstr.c_str());

        return 1;
    }
    //=========================================================================

    /*!
     * Prints variables and/or strings to the HTTP connection
     * LUA: server.print("string"|var1 [,"string|var]...)
     */
    static int lua_print(lua_State *L) {
        lua_getglobal(L, luaconnection); // push onto stack
        Connection *conn = (Connection *) lua_touserdata(L, -1); // does not change the stack
        lua_remove(L, -1); // remove from stack

        int top = lua_gettop(L);

        for (int i = 1; i <= top; i++) {
            const char *str = lua_tostring(L, i);
            if (str != NULL) {
                conn->send(str, strlen(str));
            }
        }
        lua_pop(L, top);

        return 0;
    }
    //=========================================================================

    static int lua_require_auth(lua_State *L) {
        lua_getglobal(L, luaconnection); // push onto stack
        Connection *conn = (Connection *) lua_touserdata(L, -1); // does not change the stack
        lua_remove(L, -1); // remove from stack

        string auth = conn->header("authorization");
        lua_createtable(L, 0, 3); // table

        if (auth.empty()) {
            lua_pushstring(L, "status"); // table - "username"
            lua_pushstring(L, "NOAUTH"); // table - "username" - <username>
            lua_rawset(L, -3); // table
        }
        else {
            size_t npos;
            if ((npos = auth.find(" ")) != string::npos) {
                string auth_type = auth.substr(0, npos);
                asciitolower(auth_type);
                if (auth_type == "basic") {
                    string auth_secret = auth.substr(npos + 1);
                    string auth_string = base64_decode(auth_secret);
                    npos = auth_string.find(":");
                    if (npos != string::npos) {
                        string username = auth_string.substr(0, npos);
                        string password = auth_string.substr(npos + 1);

                        lua_pushstring(L, "status"); // table - "username"
                        lua_pushstring(L, "BASIC"); // table - "username" - <username>
                        lua_rawset(L, -3); // table

                        lua_pushstring(L, "username"); // table - "username"
                        lua_pushstring(L, username.c_str()); // table - "username" - <username>
                        lua_rawset(L, -3); // table

                        lua_pushstring(L, "password"); // table - "password"
                        lua_pushstring(L, password.c_str()); // table - "password" - <password>
                        lua_rawset(L, -3); // table
                    }
                    else {
                        lua_pushstring(L, "status"); // table - "username"
                        lua_pushstring(L, "ERROR"); // table - "username" - <username>
                        lua_rawset(L, -3); // table

                        lua_pushstring(L, "message"); // table - "username"
                        lua_pushstring(L, "Auth-string not valid!"); // table - "username" - <username>
                        lua_rawset(L, -3); // table
                    }
                }
                else if (auth_type == "bearer") {
                    string jwt_token = auth.substr(npos + 1);

                    lua_pushstring(L, "status"); // table - "username"
                    lua_pushstring(L, "BEARER"); // table - "username" - <username>
                    lua_rawset(L, -3); // table

                    lua_pushstring(L, "token"); // table - "username"
                    lua_pushstring(L, jwt_token.c_str()); // table - "username" - <username>
                    lua_rawset(L, -3); // table
                }
            }
            else {
                lua_pushstring(L, "status"); // table - "username"
                lua_pushstring(L, "ERROR"); // table - "username" - <username>
                lua_rawset(L, -3); // table

                lua_pushstring(L, "message"); // table - "username"
                lua_pushstring(L, "Auth-type not known!"); // table - "username" - <username>
                lua_rawset(L, -3); // table
            }
        }

        return 1;
    }
    //=========================================================================

    static map<string,string> process_http_header(istream *ins, int &content_length, int &status_code)
    {
        //
        // process header files
        //
        map<string,string> header;
        bool eoh = false; //end of header reached
        content_length = 0; // we don't know if we will get content...
        string line;
        while (!eoh && !ins->eof() && !ins->fail()) {
            (void) safeGetline(*ins, line);
            if (line.empty() || ins->fail() || ins->eof()) {
                eoh = true;
            } else {
                size_t pos;
                if ((line[0] == 'H') && (line[1] == 'T') && (line[2] == 'T') && (line[3] == 'P')) {
                    //
                    // we have the first line with the status code
                    //
                    pos = line.find(" ");
                    if (pos == string::npos) {
                        // TODO: process error!
                    }
                    string tmpstr = line.substr(pos + 1);
                    pos = tmpstr.find(" ");
                    if (pos == string::npos) {
                        // TODO: process error!
                    }
                    string codestr = tmpstr.substr(0, pos);
                    status_code = stoi(codestr);
                }
                else {
                    size_t pos = line.find(':');
                    string name = line.substr(0, pos);
                    name = trim(name);
                    asciitolower(name);
                    string value = line.substr(pos + 1);
                    value = header[name] = trim(value);
                    if (name == "content-length") {
                        content_length = stoi(value);
                    }
                    else if (name == "transfer-encoding") {
                        if (value == "chunked") {
                            content_length = -1;
                        }
                    }
                }
            }
        }
        return header;
    }
    //=========================================================================


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
    static int lua_http_client(lua_State *L) {

        //
        // local exception class
        //
        class _HttpError {
        private:
            int line;
            string errormsg;
        public:
            inline _HttpError(int line_p, string &errormsg_p) : line(line_p), errormsg(errormsg_p) {};
            inline _HttpError(int line_p, const char *errormsg_p) : line(line_p) { errormsg = errormsg_p; };
            inline string what(void) {
                stringstream ss;
                ss << "Error #" << line << ": " << errormsg;
                return ss.str();
            }
        };

        int top = lua_gettop(L);
        if (top < 2) {
            lua_pushstring(L, "'server.http(method, url [, header] [, timeout])' requires at least 2 parameters");
            lua_error(L);
            return 0;
        }

        string errormsg; // filled in case of errors...
        bool success = true;

        //
        // Get the first parameter: method (ATTENTION: only "GET" is supported at the moment
        //
        const char *_method = lua_tostring(L, 1);
        string method = _method;

        //
        // Get the second parameter: URL
        // It has the form: http[s]://domain.name[:port][/path/to/files]
        //
        const char *_url = lua_tostring(L, 2);
        string url = _url;

        //
        // the next parameters are either the header values and/or the timeout
        // header: table of key/value pairs of additional HTTP-headers to be sent
        // timeout: number of milliseconds any operation of the socket may take at maximum
        //
        map<string,string> outheader;
        int timeout = 500; // default is 500 ms
        for (int i = 3; i <= top; i++) {
            if (lua_istable(L, i)) { // process header table at position i
                lua_pushnil(L);
                while (lua_next(L, i) != 0) {
                    const char *key = lua_tostring(L, -2);
                    const char *value = lua_tostring(L, -1);
                    outheader[key] = value;
                    lua_pop(L, 1);
                }
            }
            else if (lua_isinteger(L, i)) { // process timeout at position i
                timeout = lua_tointeger(L, i);
            }
        }

        bool secure = false; // is set true, if the url starts with "https"
        int port = 80; // contains the port number, default is 80 (or 443, if https)
        string host; // contains the host name or IP-number
        string path; // path to document

        try {
            //
            // processing the URL given
            //
            if (url.find("http:") == 0) {
                url = url.substr(7);
            }
            else if (url.find("https:") == 0) {
                url = url.substr(8);
                secure = true;
                port = 443;
            }
            else {
                throw _HttpError(__LINE__,
                                 "server.http: unknown or missing protocol in URL! must be \"http:\" or \"https\"!");
            }

            size_t pos;
            pos = url.find_first_of(":/");
            if (pos == string::npos) {
                host = url;
                path = "/";
            }
            else {
                host = url.substr(0, pos);
                if (url[pos] == ':') { // we have a portnumber
                    try {
                        string rest_of_url = url.substr(pos + 1);
                        size_t endpos = rest_of_url.find("/");
                        string portstr;
                        if (endpos == string::npos) {
                            portstr = rest_of_url;
                        }
                        else {
                            portstr = rest_of_url.substr(0, endpos);
                        }
                        port = stoi(portstr);
                    }
                    catch (const std::invalid_argument &ia) {
                        string err = string("server.http: invalid portnumber! ") + string(ia.what());
                        throw _HttpError(__LINE__, err);
                    }
                    pos = url.find("/");
                    if (pos == string::npos) {
                        path = "/";
                    }
                    else {
                        path = url.substr(pos);
                    }
                }
                else {
                    path = url.substr(pos);
                }
            }

            if (method == "GET") { // the only method we support so far...
                struct addrinfo *ai;
                struct addrinfo hint;

                //
                // resolve hostname etc. to get the IP address of server
                //
                memset(&hint, 0, sizeof hint);
                hint.ai_family = PF_INET;
                hint.ai_socktype = SOCK_STREAM;
                hint.ai_protocol = IPPROTO_TCP;
                int res;
                if ((res = getaddrinfo(host.c_str(), NULL, &hint, &ai)) != 0) {
                    lua_pop(L, top);
                    string err = string("Couldn't resolve hostname! ") + string(gai_strerror(res));
                    throw _HttpError(__LINE__, err);
                }

                //
                // now we create the socket
                //
                int socketfd;
                if ((socketfd = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol)) == -1) {
                    lua_pop(L, top);
                    freeaddrinfo(ai);
                    string err = string("Could not create socket! ") + string(strerror(errno));
                    throw _HttpError(__LINE__, err);
                }

                //
                // now let's set the timeout of the socket
                //
                struct timeval tv;

                tv.tv_sec = timeout / 1000;  // 30 Secs Timeout
                tv.tv_usec = timeout % 1000;  // Not init'ing this can cause strange errors
                if (setsockopt(socketfd, SOL_SOCKET, SO_RCVTIMEO, (char *) &tv, sizeof(struct timeval)) != 0) {
                    lua_pop(L, top);
                    freeaddrinfo(ai);
                    string err = string("Could not create socket! ") + string(strerror(errno));
                    throw _HttpError(__LINE__, err);
                }

                //
                // set the port number (assuming it is an AF_INET we got...
                //
                struct sockaddr_in *serv_addr = (struct sockaddr_in *) ai->ai_addr;

                auto start = get_time::now(); // start elapsed time timer...

                //
                // connect the socket
                //
                serv_addr->sin_port = htons(port);
                if (connect(socketfd, ai->ai_addr, ai->ai_addrlen) == -1) {
                    close(socketfd);
                    lua_pop(L, top);
                    freeaddrinfo(ai);
                    string err = string("Could not connect socket! ") + string(strerror(errno));
                    throw _HttpError(__LINE__, err);
                }

#ifdef SHTTPS_ENABLE_SSL
                SSL_CTX *ctx = NULL;
                SSL *ssl = NULL;
                if (secure) {
                    ctx = SSL_CTX_new(SSLv23_client_method());
                    ssl = SSL_new(ctx);
                    SSL_set_fd(ssl, socketfd);
                    if (SSL_connect(ssl) <= 0) {
                        // ERROR.....
                        ssl = NULL;
                    }
                }

                X509 *cert;
                string certificate_subject;
                string certificate_issuer;
                cert = SSL_get_peer_certificate(ssl); /* get the server's certificate */
                if (cert != NULL) {
                    char *line;
                   line = X509_NAME_oneline(X509_get_subject_name(cert), 0, 0);
                    certificate_subject = line;
                    free(line);

                    line = X509_NAME_oneline(X509_get_issuer_name(cert), 0, 0);
                    certificate_issuer = line;
                    free(line);

                    X509_free(cert);
                }

#endif
                //
                // create the C++ streams
                //
                SockStream *sockstream = NULL;
#ifdef SHTTPS_ENABLE_SSL
                if (secure) {
                    sockstream = new SockStream(ssl);
                }
                else {
                    sockstream = new SockStream(socketfd);
                }
#else
                sockstream = new SockStream(socketfd);
#endif
                istream ins(sockstream);
                ostream os(sockstream);

                //
                // send the request header
                //
                os << "GET " << path << " HTTP/1.1\r\n";
                if (os.fail() || os.eof()) {
                    close(socketfd);
                    lua_pop(L, top);
                    freeaddrinfo(ai);
                    throw _HttpError(__LINE__, "Connection dropped by peer!");

                }
                if (!host.empty()) {
                    os << "Host: " << host << "\r\n";
                    if (os.fail() || os.eof()) {
                        close(socketfd);
                        lua_pop(L, top);
                        freeaddrinfo(ai);
                        throw _HttpError(__LINE__, "Connection dropped by peer!");

                    }
                }
                for (auto const &iterator : outheader) {
                    os << iterator.first << ": " << iterator.second << "\r\n";
                    if (os.fail() || os.eof()) {
                        close(socketfd);
                        lua_pop(L, top);
                        freeaddrinfo(ai);
                        throw _HttpError(__LINE__, "Connection dropped by peer!");

                    }
                }

                os << "\r\n";
                if (os.fail() || os.eof()) {
                    close(socketfd);
                    lua_pop(L, top);
                    freeaddrinfo(ai);
                    throw _HttpError(__LINE__, "Connection dropped by peer!");
                }
                os.flush();
                if (os.fail() || os.eof()) {
                    close(socketfd);
                    lua_pop(L, top);
                    freeaddrinfo(ai);
                    throw _HttpError(__LINE__, "Connection dropped by peer!");
                }

                //
                // let's process the header of the return HTTP-message
                //
                int content_length;
                int status_code;
                map <string, string> header = process_http_header(&ins, content_length, status_code);
                if (ins.fail() || ins.eof()) {
                    close(socketfd);
                    lua_pop(L, top);
                    freeaddrinfo(ai);
                    throw _HttpError(__LINE__, "Couldn't read HTTP header: Connection dropped by peer!");
                }

                //
                // now we read the data from the body of the HTTP-message
                //
                char *bodybuf = NULL;
                if (content_length == -1) { // we expect chunked data
                    try {
                        ChunkReader ckrd(&ins);
                        size_t n = ckrd.readAll(&bodybuf);
                    }
                    catch (int ierr) { // i/o error
                        close(socketfd);
                        lua_pop(L, top);
                        freeaddrinfo(ai);
                        throw _HttpError(__LINE__, "Couldn't read HTTP data: Connection dropped by peer!");
                    }
                    catch (Error err) {
                        close(socketfd);
                        lua_pop(L, top);
                        freeaddrinfo(ai);
                        string errstr = string("Couldn't read HTTP data: ") + err.to_string();
                        throw _HttpError(__LINE__, errstr);
                    }
                }
                else {
                    if ((bodybuf = (char *) malloc((content_length + 1) * sizeof(char))) == NULL) {
                        close(socketfd);
                        lua_pop(L, top);
                        freeaddrinfo(ai);
                        throw _HttpError(__LINE__, "Couldn't read HTTP data: malloc failed!");
                    }
                    ins.read(bodybuf, content_length);
                    if (ins.fail() || ins.eof()) {
                        free(bodybuf);
                        close(socketfd);
                        freeaddrinfo(ai);
                        throw _HttpError(__LINE__, "Couldn't read HTTP data: Connection dropped by peer!");
                    }
                    bodybuf[content_length] = '\0';
                }

#ifdef SHTTPS_ENABLE_SSL
                if (secure) {
                    SSL_free(ssl);
                }
#endif
                delete sockstream;
                close (socketfd);

#ifdef SHTTPS_ENABLE_SSL
                if (secure) {
                    SSL_CTX_free(ctx);
                }
#endif

                auto end = get_time::now();
                auto diff = end - start;
                int duration = chrono::duration_cast<ms>(diff).count();
                //
                // now let's build the Lua-table that's being returned
                //
                lua_createtable(L, 0, 0); // table

                lua_pushstring(L, "success"); // table - "success"
                lua_pushboolean(L, true); // table - "body" - true
                lua_rawset(L, -3); // table

                lua_pushstring(L, "status_code"); // table - "success"
                lua_pushinteger(L, status_code); // table - "body" - true
                lua_rawset(L, -3); // table

#ifdef SHTTPS_ENABLE_SSL
                if (!certificate_issuer.empty() || certificate_subject.empty()) {
                    lua_pushstring(L, "certificate");
                    lua_createtable(L, 0, header.size()); // table - "header" - table2

                    lua_pushstring(L, "subject"); // table - "header" - table2 - headername
                    lua_pushstring(L, certificate_subject.c_str()); // table - "header" - table2 - headername - headervalue
                    lua_rawset(L, -3); // table - "header" - table2

                    lua_pushstring(L, "issuer"); // table - "header" - table2 - headername
                    lua_pushstring(L, certificate_issuer.c_str()); // table - "header" - table2 - headername - headervalue
                    lua_rawset(L, -3); // table - "header" - table2

                    lua_rawset(L, -3); // table
                }
#endif

                lua_pushstring(L, "header"); // table1 - "header"
                lua_createtable(L, 0, header.size()); // table - "header" - table2
                for (auto const &iterator : header) {
                    lua_pushstring(L, iterator.first.c_str()); // table - "header" - table2 - headername
                    lua_pushstring(L, iterator.second.c_str()); // table - "header" - table2 - headername - headervalue
                    lua_rawset(L, -3); // table - "header" - table2
                }
                lua_rawset(L, -3); // table

                lua_pushstring(L, "body"); // table - "body"
                lua_pushstring(L, bodybuf); // table - "body" - bodybuf
                lua_rawset(L, -3); // table

                lua_pushstring(L, "duration"); // table - "duration"
                lua_pushinteger(L, duration); // table - "duration" - duration
                lua_rawset(L, -3); // table

                //
                // free resources used
                //
                free(bodybuf);
                close(socketfd);
                freeaddrinfo(ai);

                return 1; // we return one table...
            }
            else {
                lua_pushstring(L, "'server.http(method, url, [header])': unknown method");
                lua_error(L);
            }
        }
        catch (_HttpError &err) {
            lua_createtable(L, 0, 2); // table

            lua_pushstring(L, "success"); // table - "success"
            lua_pushboolean(L, false); // table - "success" - true
            lua_rawset(L, -3); // table

            lua_pushstring(L, "errmsg"); // table - "errmsg"
            lua_pushstring(L, err.what().c_str()); // table - "errmsg" - errormsg
            lua_rawset(L, -3); // table

            return 1;
        }
        return 0;
    }
    //=========================================================================


    static json_t *subtable(lua_State *L, int index) {
        const char table_error[] = "server.table_to_json(table): datatype inconsistency!";
        json_t *tableobj = NULL;
        json_t *arrayobj = NULL;
        json_t *tmp_luanumber = NULL;
        const char *skey;
        lua_pushnil(L);  /* first key */
        while (lua_next(L, index) != 0) {
            // key is at index -2  // index + 1
            // value is at index -1 // index + 2
            if (lua_type(L, index + 1) == LUA_TSTRING) {
                // we have a string as key
                skey = lua_tostring(L, index + 1);
                if (arrayobj != NULL) {
                    lua_pushstring(L, "'server.table_to_json(table)': Cannot mix int and strings as key");
                    lua_error(L);
                }
                if (tableobj == NULL) {
                    tableobj = json_object();
                }
            }
            else if (lua_type(L, index + 1) == LUA_TNUMBER) {
                int dummy = lua_tointeger(L, index + 1);
                if (tableobj != NULL) {
                    lua_pushstring(L, "'server.table_to_json(table)': Cannot mix int and strings as key");
                    lua_error(L);
                }
                if (arrayobj == NULL) {
                    arrayobj = json_array();
                }
            }
            else {
                // something else as key....
                lua_pushstring(L, "'server.table_to_json(table)': Cannot convert key to JSON object field");
                lua_error(L);
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
                    tmp_luanumber = json_integer(static_cast <long> (floor(val)));
                } else {
                    // the lua number is a double
                    tmp_luanumber = json_real(val);
                }

                if (tableobj != NULL) {
                    json_object_set_new(tableobj, skey, tmp_luanumber);
                }
                else if (arrayobj != NULL) {
                    json_array_append_new(arrayobj, tmp_luanumber);
                }
                else {
                    lua_pushstring(L, table_error);
                    lua_error(L);
                }
            }
            else if (lua_type(L, index + 2) == LUA_TSTRING) {
                // a string value
                const char *val = lua_tostring(L, index + 2);
                if (tableobj != NULL) {
                    json_object_set_new(tableobj, skey, json_string(val));
                }
                else if (arrayobj != NULL) {
                    json_array_append_new(arrayobj, json_string(val));
                }
                else {
                    lua_pushstring(L, table_error);
                    lua_error(L);
                }
            }
            else if (lua_type(L, index + 2) == LUA_TBOOLEAN) {
                // a boolean value
                bool val = lua_toboolean(L, index + 2);
                if (tableobj != NULL) {
                    json_object_set_new(tableobj, skey, json_boolean(val));
                }
                else if (arrayobj != NULL) {
                    json_array_append_new(arrayobj, json_boolean(val));
                }
                else {
                    lua_pushstring(L, table_error);
                    lua_error(L);
                }
            }
            else if (lua_type(L, index + 2) == LUA_TTABLE) {
                if (tableobj != NULL) {
                    json_object_set_new(tableobj, skey, subtable(L, index + 2));
                }
                else if (arrayobj != NULL) {
                    json_array_append_new(arrayobj, subtable(L, index + 2));
                }
                else {
                    lua_pushstring(L, table_error);
                    lua_error(L);
                }
            }
            else {
                lua_pushstring(L, table_error);
                lua_error(L);
            }
            lua_pop(L, 1);
        }
        return tableobj != NULL ? tableobj : (arrayobj != NULL ? arrayobj : NULL);
    }
    //=========================================================================

   /*!
    * Converts a Lua table into a JSON string
    * LUA: jsonstr = server.table_to_json(table)
    */
    static int lua_table_to_json(lua_State *L) {
       int top = lua_gettop(L);
       if (top < 1) {
           lua_pushstring(L, "'server.table_to_json(table)': table parameter missing!");
           lua_error(L);
           return 0;
       }
       if (!lua_istable(L, 1)) {
           lua_pushstring(L, "'server.table_to_json(table)': table is not a lua-table!");
           lua_error(L);
           return 0;
       }

       json_t *root = subtable(L, 1);
       char *jsonstr = json_dumps(root, JSON_INDENT(3));
       lua_pushstring(L, jsonstr);
       free(jsonstr);
       json_decref(root);
       return 1;
    }
    //=========================================================================

    //
    // forward declaration is needed here
    //
    static void lua_jsonarr(lua_State *L, json_t *obj);

    static void lua_jsonobj(lua_State *L, json_t *obj) {
        if (!json_is_object(obj)) {
            lua_pushstring(L, "'lua_jsonobj expects object!");
            lua_error(L);
            return;
        }

        lua_createtable(L, 0, 0);

        const char *key;
        json_t *value;
        json_object_foreach(obj, key, value) {
            lua_pushstring(L, key); // index
            switch(json_typeof(value)) {
                case JSON_NULL: {
                    lua_pushstring(L, "NIL"); // ToDo: we should create a custom Lua object named NULL!
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


    static void lua_jsonarr(lua_State *L, json_t *arr) {
        if (!json_is_array(arr)) {
            lua_pushstring(L, "'lua_jsonarr expects array!");
            lua_error(L);
            return;
        }

        lua_createtable(L, 0, 0);

        size_t index;
        json_t *value;
        json_array_foreach(arr, index, value) {
            lua_pushinteger(L, index);
            switch(json_typeof(value)) {
                case JSON_NULL: {
                    lua_pushstring(L, "NIL"); // ToDo: we should create a custom Lua object named NULL!
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
    static int lua_json_to_table (lua_State *L) {
        int top = lua_gettop(L);
        if (top < 1) {
            lua_pushstring(L, "'server.json_to_table(jsonstr)': jsonstr parameter missing!");
            lua_error(L);
            return 0;
        }
        if (!lua_isstring(L, 1)) {
            lua_pushstring(L, "'server.json_to_table(jsonstr)': jsonstr is not a string!");
            lua_error(L);
            return 0;
        }

        const char *jsonstr = lua_tostring(L, 1);
        lua_pop(L, top);

        json_error_t jsonerror;

        json_t *jsonobj = json_loads(jsonstr, JSON_REJECT_DUPLICATES, &jsonerror);

        if (jsonobj == NULL) {
            stringstream ss;
            ss << "'server.json_to_table(jsonstr)': Error parsing JSON: " << jsonerror.text << endl;
            ss << "JSON-source: " << jsonerror.source << endl;
            ss << "Line: " << jsonerror.line << " Column: " << jsonerror.column << " Pos: " << jsonerror.position << endl;
            lua_pushstring(L, ss.str().c_str());
            lua_error(L);
            return 0;
        }
        if (json_is_object(jsonobj)) {
            lua_jsonobj(L, jsonobj);
        }
        else if (json_is_array(jsonobj)) {
            lua_jsonarr(L, jsonobj);
        }
        else {
            lua_pushstring(L, "'server.json_to_table(jsonstr)': Not a valid json string!");
            lua_error(L);
            return 0;
        }
        json_decref(jsonobj);

        return 1;
    }
    //=========================================================================


    /*!
     * Stops the HTTP server and exits everything
     * TODO: There must be some authorization control here. This function should only be available for admins
     * TODO: Add the possibility to give a message which is printed on the console and logfiles
     * LUA: server.shutdown()
     */
    static int lua_exitserver(lua_State *L) {
        lua_getglobal(L, luaconnection); // push onto stack
        Connection *conn = (Connection *) lua_touserdata(L, -1); // does not change the stack
        lua_remove(L, -1); // remove from stack

        conn->server()->stop();

        return 0;
    }
    //=========================================================================

    /*!
     * Adds a new HTTP header field
     * LUA: server.sendHeader(key, value)
     */
    static int lua_send_header(lua_State *L) {
        lua_getglobal(L, luaconnection);
        Connection *conn = (Connection *) lua_touserdata(L, -1);
        lua_remove(L, -1); // remove from stack

        int top = lua_gettop(L);

        if (top != 2) {
            lua_pushstring(L, "'server.header(key,val)': Invalid number of parameters!");
            lua_error(L);
            return 0;
        }
        const char *hkey = lua_tostring(L, 1);
        const char *hval = lua_tostring(L, 2);
        lua_pop(L, 2);

        conn->header(hkey, hval);

        return 0;
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
    static int lua_send_cookie(lua_State *L) {
        lua_getglobal(L, luaconnection);
        Connection *conn = (Connection *) lua_touserdata(L, -1);
        lua_remove(L, -1); // remove from stack

        int top = lua_gettop(L);

        if ((top < 2) || (top > 3)) {
            lua_pushstring(L, "'server.sendCookie(name, value[, options])': Invalid number of parameters!");
            lua_error(L);
            return 0;
        }
        const char *ckey = lua_tostring(L, 1);
        const char *cval = lua_tostring(L, 2);

        Cookie cookie(ckey, cval);

        if (top == 3) {
            if (!lua_istable(L, 3)) {
                lua_pushstring(L, "'server.sendCookie(name, value[, options])': options is not a lua-table!");
                lua_error(L);
                return 0;
            }
            lua_pushnil(L);  /* first key */
            int index = 3;
            while (lua_next(L, index) != 0) {
                // key is at index -2
                // value is at index -1
                try {
                    string optname;
                    if (lua_isstring(L, -2)) {
                        // we have a string as key
                        optname = lua_tostring(L, -2);
                    }
                    else {
                        throw (1);
                    }
                    if (optname == "path") {
                        if (lua_isstring(L, -1)) {
                            string path = lua_tostring(L, -1);
                            cookie.path(path);
                        }
                        else {
                            throw (1);
                        }
                    }
                    else if (optname == "domain") {
                        if (lua_isstring(L, -1)) {
                            string domain = lua_tostring(L, -1);
                            cookie.domain(domain);
                        }
                        else {
                            throw (1);
                        }
                    }
                    else if (optname == "expires") {
                        if (lua_isinteger(L, -1)) {
                            int expires = lua_tointeger(L, -1);
                            cookie.expires(expires);
                        }
                        else {
                            throw (1);
                        }
                    }
                    else if (optname == "secure") {
                        if (lua_isboolean(L, -1)) {
                            bool secure = lua_toboolean(L, -1);
                            if (secure) cookie.secure(secure);
                        }
                        else {
                            throw (1);
                        }
                    }
                    else if (optname == "http_only") {
                        if (lua_isboolean(L, -1)) {
                            bool http_only = lua_toboolean(L, -1);
                            if (http_only) cookie.httpOnly(http_only);
                        }
                        else {
                            throw (1);
                        }
                    }
                    else {

                    }
                }
                catch (int err) {
                    lua_pushstring(L, "'server.sendCookie(name, value[, options])': Invalid options!");
                    lua_error(L);
                    return 0;
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, top);

        conn->cookies(cookie);

        return 0;
    }
    //=========================================================================

    static int lua_send_status(lua_State *L) {
        lua_getglobal(L, luaconnection);
        Connection *conn = (Connection *) lua_touserdata(L, -1);
        lua_remove(L, -1); // remove from stack

        int top = lua_gettop(L);
        int istatus = 200;
        if (top == 1) {
            istatus = lua_tointeger(L, 1);
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
     * LUA: server.copyTmpfile()
     *
     */
    static int lua_copytmpfile(lua_State *L) {
        lua_getglobal(L, luaconnection);
        Connection *conn = (Connection *) lua_touserdata(L, -1);
        lua_remove(L, -1); // remove from stack

        int top = lua_gettop(L);
        int tmpfile_id = lua_tointeger(L, 1);
        const char *outfile = lua_tostring(L, 2);

        vector <Connection::UploadedFile> uploads = conn->uploads();

        string infile = uploads[tmpfile_id - 1].tmpname;
        ifstream source(infile, ios::binary);
        ofstream dest(outfile, ios::binary);

        dest << source.rdbuf();

        source.close();
        dest.close();


        return 0;
    }
    //=========================================================================



#ifdef SHTTPS_ENABLE_SSL
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
    static int lua_generate_jwt(lua_State *L) {
        lua_getglobal(L, luaconnection);
        Connection *conn = (Connection *) lua_touserdata(L, -1);
        lua_remove(L, -1); // remove from stack

        jwt_t *jwt;
        int top = lua_gettop(L);
        if (top < 1) {
            lua_pushstring(L, "'server.table_to_json(table)': table parameter missing!");
            lua_error(L);
            return 0;
        }
        if (!lua_istable(L, 1)) {
            lua_pushstring(L, "'server.table_to_json(table)': table is not a lua-table!");
            lua_error(L);
            return 0;
        }

        json_t *root = subtable(L, 1);
        char *jsonstr = json_dumps(root, JSON_INDENT(3));

        if (jwt_new(&jwt) != 0) {
            //error!!
        }
        jwt_set_alg(jwt, JWT_ALG_HS256, (unsigned char *) conn->server()->jwt_secret().c_str(), conn->server()->jwt_secret().size());
        jwt_add_grants_json(jwt, jsonstr);

        char *token = jwt_encode_str(jwt);

        lua_pushstring(L, token);

        return 1;
    }
    //=========================================================================

    static int lua_decode_jwt(lua_State *L) {
        lua_getglobal(L, luaconnection);
        Connection *conn = (Connection *) lua_touserdata(L, -1);
        lua_remove(L, -1); // remove from stack

        int top = lua_gettop(L);
        if (top != 1) {
            lua_pushstring(L, "'server.decode_jwt(token)': error in parameter list!");
            lua_error(L);
            return 0;
        }

        string token = lua_tostring(L, 1);
        lua_pop(L, 1);

        jwt_t *jwt;
       int err;
        if ((err = jwt_decode(&jwt, token.c_str(), (unsigned char *) conn->server()->jwt_secret().c_str(), conn->server()->jwt_secret().size())) != 0) {
            lua_pushstring(L, "'server.decode_jwt(token)': Error in decoding token! (1)");
            lua_error(L);
            return 0;
        }
        char *tokendata;
        if ((tokendata = jwt_dump_str(jwt, false)) == NULL) {
            lua_pushstring(L, "'server.decode_jwt(token)': Error in decoding token! (2)");
            lua_error(L);
            return 0;
        }
        string tokenstr = tokendata;
        free(tokendata);
        size_t pos = tokenstr.find(".");
        string jsonstr = tokenstr.substr(pos + 1);

        json_error_t jsonerror;
        json_t *jsonobj = json_loads(jsonstr.c_str(), JSON_REJECT_DUPLICATES, &jsonerror);

        lua_jsonobj(L, jsonobj);
        json_decref(jsonobj);

        return 1;
    }

#endif

    /*!
     * This function registers all variables and functions in the server table
     */
    void LuaServer::createGlobals(Connection &conn) {
        lua_createtable(L, 0, 33); // table1
        //lua_newtable(L); // table1

        Connection::HttpMethod method = conn.method();
        lua_pushstring(L, "method"); // table1 - "index_L1"
        switch (method) {
            case Connection::OPTIONS:
                lua_pushstring(L, "OPTIONS");
                break; // table1 - "index_L1" - "value_L1"
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
        lua_rawset(L, -3); // table1

#ifdef SHTTPS_ENABLE_SSL

        lua_pushstring(L, "has_openssl"); // table1 - "index_L1"
        lua_pushboolean(L, true); // table1 - "index_L1" - "value_L1"
        lua_rawset(L, -3); // table1

#endif

        lua_pushstring(L, "client_ip"); // table1 - "index_L1"
        lua_pushstring(L, conn.peer_ip().c_str()); // table1 - "index_L1" - "value_L1"
        lua_rawset(L, -3); // table1

        lua_pushstring(L, "client_port"); // table1 - "index_L1"
        lua_pushinteger(L, conn.peer_port()); // table1 - "index_L1" - "value_L1"
        lua_rawset(L, -3); // table1

        lua_pushstring(L, "secure"); // table1 - "index_L1"
        lua_pushboolean(L, conn.secure()); // table1 - "index_L1" - "value_L1"
        lua_rawset(L, -3); // table1

        lua_pushstring(L, "header"); // table1 - "index_L1"
        std::vector<std::string> headers = conn.header();
        lua_createtable(L, 0, headers.size()); // table1 - "index_L1" - table2
        for (unsigned i = 0; i < headers.size(); i++) {
            lua_pushstring(L, headers[i].c_str()); // table1 - "index_L1" - table2 - "index_L2"
            lua_pushstring(L,
                           conn.header(headers[i]).c_str()); // table1 - "index_L1" - table2 - "index_L2" - "value_L2"
            lua_rawset(L, -3); // table1 - "index_L1" - table2
        }
        lua_rawset(L, -3); // table1

        std::map<std::string,std::string> cookies = conn.cookies();
        lua_pushstring(L, "cookies"); // table1 - "index_L1"
        lua_createtable(L, 0, cookies.size()); // table1 - "index_L1" - table2
        for (auto cookie : cookies) {
            lua_pushstring(L, cookie.first.c_str());
            lua_pushstring(L, cookie.second.c_str());
            lua_rawset(L, -3);
        }
        lua_rawset(L, -3); // table1

        string host = conn.host();
        lua_pushstring(L, "host"); // table1 - "index_L1"
        lua_pushstring(L, host.c_str()); // table1 - "index_L1" - "value_L1"
        lua_rawset(L, -3); // table1

        string uri = conn.uri();
        lua_pushstring(L, "uri"); // table1 - "index_L1"
        lua_pushstring(L, uri.c_str()); // table1 - "index_L1" - "value_L1"
        lua_rawset(L, -3); // table1

        std::vector<std::string> get_params = conn.getParams();
        if (get_params.size() > 0) {
            lua_pushstring(L, "get"); // table1 - "index_L1"
            lua_createtable(L, 0, get_params.size()); // table1 - "index_L1" - table2
            for (unsigned i = 0; i < get_params.size(); i++) {
                lua_pushstring(L, get_params[i].c_str()); // table1 - "index_L1" - table2 - "index_L2"
                lua_pushstring(L, conn.getParams(
                        get_params[i]).c_str()); // table1 - "index_L1" - table2 - "index_L2" - "value_L2"
                lua_rawset(L, -3); // table1 - "index_L1" - table2
            }
            lua_rawset(L, -3); // table1
        }

        std::vector<std::string> post_params = conn.postParams();
        if (post_params.size() > 0) {
            lua_pushstring(L, "post"); // table1 - "index_L1"
            lua_createtable(L, 0, post_params.size()); // table1 - "index_L1" - table2
            for (unsigned i = 0; i < post_params.size(); i++) {
                lua_pushstring(L, post_params[i].c_str()); // table1 - "index_L1" - table2 - "index_L2"
                lua_pushstring(L, conn.postParams(
                        post_params[i]).c_str()); // table1 - "index_L1" - table2 - "index_L2" - "value_L2"
                lua_rawset(L, -3); // table1 - "index_L1" - table2
            }
            lua_rawset(L, -3); // table1
        }

        vector <Connection::UploadedFile> uploads = conn.uploads();
        if (uploads.size() > 0) {
            lua_pushstring(L, "uploads"); // table1 - "index_L1"
            lua_createtable(L, 0, uploads.size());     // table1 - "index_L1" - table2
            for (unsigned i = 0; i < uploads.size(); i++) {
                lua_pushinteger(L, i + 1);             // table1 - "index_L1" - table2 - "index_L2"
                lua_createtable(L, 0, uploads.size()); // "table1" - "index_L1" - "table2" - "index_L2" - "table3"

                lua_pushstring(L,
                               "fieldname");          // "table1" - "index_L1" - "table2" - "index_L2" - "table3" - "index_L3"
                lua_pushstring(L,
                               uploads[i].fieldname.c_str()); // "table1" - "index_L1" - "table2" - "index_L2" - "table3" - "index_L3" - "value_L3"
                lua_rawset(L, -3);                       // "table1" - "index_L1" - "table2" - "index_L2" - "table3"

                lua_pushstring(L,
                               "origname");          // "table1" - "index_L1" - "table2" - "index_L2" - "table3" - "index_L3"
                lua_pushstring(L,
                               uploads[i].origname.c_str()); // "table1" - "index_L1" - "table2" - "index_L2" - "table3" - "index_L3" - "value_L3"
                lua_rawset(L, -3);                      // "table1" - "index_L1" - "table2" - "index_L2" - "table3"

                lua_pushstring(L,
                               "tmpname");          // "table1" - "index_L1" - "table2" - "index_L2" - "table3" - "index_L3"
                lua_pushstring(L,
                               uploads[i].tmpname.c_str()); // "table1" - "index_L1" - "table2" - "index_L2" - "table3" - "index_L3" - "value_L3"
                lua_rawset(L, -3);                     // "table1" - "index_L1" - "table2" - "index_L2" - "table3"

                lua_pushstring(L,
                               "mimetype");          // "table1" - "index_L1" - "table2" - "index_L2" - "table3" - "index_L3"
                lua_pushstring(L,
                               uploads[i].mimetype.c_str()); // "table1" - "index_L1" - "table2" - "index_L2" - "table3" - "index_L3" - "value_L3"
                lua_rawset(L, -3);                      // "table1" - "index_L1" - "table2" - "index_L2" - "table3"

                lua_pushstring(L,
                               "filesize");           // "table1" - "index_L1" - "table2" - "index_L2" - "table3" - "index_L3"
                lua_pushinteger(L,
                                uploads[i].filesize); // "table1" - "index_L1" - "table2" - "index_L2" - "table3" - "index_L3" - "value_L3"
                lua_rawset(L, -3);                       // "table1" - "index_L1" - "table2" - "index_L2" - "table3"

                lua_rawset(L, -3); // table1 - "index_L1" - table2
            }
            lua_rawset(L, -3); // table1
        }

        std::vector<std::string> request_params = conn.requestParams();
        if (request_params.size() > 0) {
            lua_pushstring(L, "request"); // table1 - "index_L1"
            lua_createtable(L, 0, request_params.size()); // table1 - "index_L1" - table2
            for (unsigned i = 0; i < request_params.size(); i++) {
                lua_pushstring(L, request_params[i].c_str()); // table1 - "index_L1" - table2 - "index_L2"
                lua_pushstring(L, conn.requestParams(
                        request_params[i]).c_str()); // table1 - "index_L1" - table2 - "index_L2" - "value_L2"
                lua_rawset(L, -3); // table1 - "index_L1" - table2
            }
            lua_rawset(L, -3); // table1
        }

        if (conn.contentLength() > 0) {
            lua_pushstring(L, "content");
            lua_pushlstring(L, conn.content(), conn.contentLength());
            lua_rawset(L, -3); // table1 - "index_L1" - table2

            lua_pushstring(L, "content_type");
            lua_pushstring(L, conn.contentType().c_str());
            lua_rawset(L, -3); // table1 - "index_L1" - table2
        }

        //
        // filesystem functions
        //
        lua_pushstring(L, "fs"); // table1 - "fs"
        lua_newtable(L); // table1 - "fs" - table2
        luaL_setfuncs(L, fs_methods, 0);
        lua_rawset(L, -3); // table1

        //
        // jsonstr = server.table_to_json(table)
        //
        lua_pushstring(L, "table_to_json"); // table1 - "index_L1"
        lua_pushcfunction(L, lua_table_to_json); // table1 - "index_L1" - function
        lua_rawset(L, -3); // table1

        lua_pushstring(L, "json_to_table"); // table1 - "index_L1"
        lua_pushcfunction(L, lua_json_to_table); // table1 - "index_L1" - function
        lua_rawset(L, -3); // table1

        //
        // server.print (var[, var...])
        //
        lua_pushstring(L, "print"); // table1 - "index_L1"
        lua_pushcfunction(L, lua_print); // table1 - "index_L1" - function
        lua_rawset(L, -3); // table1

        lua_pushstring(L, "uuid"); // table1 - "index_L1"
        lua_pushcfunction(L, lua_uuid); // table1 - "index_L1" - function
        lua_rawset(L, -3); // table1

        lua_pushstring(L, "uuid62"); // table1 - "index_L1"
        lua_pushcfunction(L, lua_uuid_base62); // table1 - "index_L1" - function
        lua_rawset(L, -3); // table1

        lua_pushstring(L, "uuid_to_base62"); // table1 - "index_L1"
        lua_pushcfunction(L, lua_uuid_to_base62); // table1 - "index_L1" - function
        lua_rawset(L, -3); // table1

        lua_pushstring(L, "base62_to_uuid"); // table1 - "index_L1"
        lua_pushcfunction(L, lua_base62_to_uuid); // table1 - "index_L1" - function
        lua_rawset(L, -3); // table1

        lua_pushstring(L, "setBuffer"); // table1 - "index_L1"
        lua_pushcfunction(L, lua_setbuffer); // table1 - "index_L1" - function
        lua_rawset(L, -3); // table1


        lua_pushstring(L, "sendHeader"); // table1 - "index_L1"
        lua_pushcfunction(L, lua_send_header); // table1 - "index_L1" - function
        lua_rawset(L, -3); // table1


        lua_pushstring(L, "sendCookie"); // table1 - "index_L1"
        lua_pushcfunction(L, lua_send_cookie); // table1 - "index_L1" - function
        lua_rawset(L, -3); // table1

        lua_pushstring(L, "copyTmpfile"); // table1 - "index_L1"
        lua_pushcfunction(L, lua_copytmpfile); // table1 - "index_L1" - function
        lua_rawset(L, -3); // table1

        lua_pushstring(L, "shutdown"); // table1 - "index_L1"
        lua_pushcfunction(L, lua_exitserver); // table1 - "index_L1" - function
        lua_rawset(L, -3); // table1

        lua_pushstring(L, "http"); // table1 - "index_L1"
        lua_pushcfunction(L, lua_http_client); // table1 - "index_L1" - function
        lua_rawset(L, -3); // table1

        lua_pushstring(L, "sendStatus"); // table1 - "index_L1"
        lua_pushcfunction(L, lua_send_status); // table1 - "index_L1" - function
        lua_rawset(L, -3); // table1

        lua_pushstring(L, "requireAuth"); // table1 - "index_L1"
        lua_pushcfunction(L, lua_require_auth); // table1 - "index_L1" - function
        lua_rawset(L, -3); // table1

#ifdef SHTTPS_ENABLE_SSL

        lua_pushstring(L, "generate_jwt"); // table1 - "index_L1"
        lua_pushcfunction(L, lua_generate_jwt); // table1 - "index_L1" - function
        lua_rawset(L, -3); // table1 lua_decode_jwt

        lua_pushstring(L, "decode_jwt"); // table1 - "index_L1"
        lua_pushcfunction(L, lua_decode_jwt); // table1 - "index_L1" - function
        lua_rawset(L, -3); // table1

#endif

        lua_setglobal(L, servertablename);

        lua_pushlightuserdata(L, &conn);
        lua_setglobal(L, luaconnection);
    }
    //=========================================================================


    void LuaServer::add_servertableentry(const string &name, const string &value) {
        lua_getglobal(L, servertablename); // "table1"

        lua_pushstring(L, name.c_str()); // table1 - "index_L1"
        lua_pushstring(L, value.c_str()); // table1 - "index_L1" - "value_L1"
        lua_rawset(L, -3); // table1
        lua_pop(L, 1);
    }
    //=========================================================================



    string LuaServer::configString(const string table, const string variable, const string defval) {
        if (lua_getglobal(L, table.c_str()) != LUA_TTABLE) {
            lua_pop(L, 1);
            return defval;
        }

        lua_getfield(L, -1, variable.c_str());
        if (lua_isnil(L, -1)) {
            lua_pop(L, 2);
            return defval;
        }
        if (!lua_isstring(L, -1)) {
            throw Error(__file__, __LINE__, "String expected for " + table + "." + variable);
        }
        string retval = lua_tostring(L, -1);
        lua_pop(L, 2);
        return retval;
    }
    //=========================================================================


    int LuaServer::configBoolean(const string table, const string variable, const bool defval) {
        if (lua_getglobal(L, table.c_str()) != LUA_TTABLE) {
            lua_pop(L, 1);
            return defval;
        }
        lua_getfield(L, -1, variable.c_str());
        if (lua_isnil(L, -1)) {
            lua_pop(L, 2);
            return defval;
        }
        if (!lua_isboolean(L, -1)) {
            throw Error(__file__, __LINE__, "Integer expected for " + table + "." + variable);
        }
        bool retval = lua_toboolean(L, -1);

        lua_pop(L, 2);
        return retval;
    }
    //=========================================================================

    int LuaServer::configInteger(const string table, const string variable, const int defval) {
        if (lua_getglobal(L, table.c_str()) != LUA_TTABLE) {
            lua_pop(L, 1);
            return defval;
        }
        lua_getfield(L, -1, variable.c_str());
        if (lua_isnil(L, -1)) {
            lua_pop(L, 2);
            return defval;
        }
        if (!lua_isinteger(L, -1)) {
            throw Error(__file__, __LINE__, "Integer expected for " + table + "." + variable);
        }
        int retval = lua_tointeger(L, -1);
        lua_pop(L, 2);
        return retval;
    }
    //=========================================================================

    float LuaServer::configFloat(const string table, const string variable, const float defval) {
        if (lua_getglobal(L, table.c_str()) != LUA_TTABLE) {
            lua_pop(L, 1);
            return defval;
        }
        lua_getfield(L, -1, variable.c_str());
        if (lua_isnil(L, -1)) {
            lua_pop(L, 2);
            return defval;
        }
        if (!lua_isnumber(L, -1)) {
            throw Error(__file__, __LINE__, "Number expected for " + table + "." + variable);
        }
        lua_Number num = lua_tonumber(L, -1);
        lua_pop(L, 2);
        return (float) num;
    }
    //=========================================================================


    const vector <LuaRoute> LuaServer::configRoute(const string routetable) {
        static struct {
            const char *name;
            int type;
        } fields[] = {
                {"method", LUA_TSTRING},
                {"route",  LUA_TSTRING},
                {"script", LUA_TSTRING},
                {NULL,     0}
        };

        vector <LuaRoute> routes;

        lua_getglobal(L, routetable.c_str());
        luaL_checktype(L, -1, LUA_TTABLE);
        int i;
        for (i = 1; ; i++) {
            lua_rawgeti(L, -1, i);
            if (lua_isnil(L, -1)) {
                lua_pop(L, 1);
                break;
            }
            // an element of the 'listen' table should now be at the top of the stack
            luaL_checktype(L, -1, LUA_TTABLE);
            int field_index;
            LuaRoute route;
            for (field_index = 0; (fields[field_index].name != NULL
                                   && fields[field_index].name != NULL); field_index++) {
                lua_getfield(L, -1, fields[field_index].name);
                luaL_checktype(L, -1, fields[field_index].type);

                string method;
                // you should probably use a function pointer in the fields table.
                // I am using a simple switch/case here
                switch (field_index) {
                    case 0:
                        method = lua_tostring(L, -1);
                        if (method == "GET") {
                            route.method = Connection::HttpMethod::GET;
                        }
                        else if (method == "PUT") {
                            route.method = Connection::HttpMethod::PUT;
                        }
                        else if (method == "POST") {
                            route.method = Connection::HttpMethod::POST;
                        }
                        else if (method == "DELETE") {
                            route.method = Connection::HttpMethod::DELETE;
                        }
                        else if (method == "OPTIONS") {
                            route.method = Connection::HttpMethod::OPTIONS;
                        }
                        else if (method == "CONNECT") {
                            route.method = Connection::HttpMethod::CONNECT;
                        }
                        else if (method == "HEAD") {
                            route.method = Connection::HttpMethod::HEAD;
                        }
                        else if (method == "OTHER") {
                            route.method = Connection::HttpMethod::OTHER;
                        }
                        else {
                            throw Error(__file__, __LINE__, "Unknown HTTP method!");
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


    int LuaServer::executeChunk(const string &luastr) {
        if (luaL_dostring(L, luastr.c_str()) != 0) {
            const char *luaerror = lua_tostring(L, -1);
            throw Error(__file__, __LINE__, string("Lua error: ") + luaerror);
        }
        int top = lua_gettop(L);
        if (top == 1) {
            int status = lua_tointeger(L, 1);
            lua_pop(L, 1);
            return status;
        }
        return 1;
    }
    //=========================================================================


    vector <LuaValstruct> LuaServer::executeLuafunction(const string *funcname, int n, LuaValstruct *lv) {
        if (lua_getglobal(L, funcname->c_str()) != LUA_TFUNCTION) {
            lua_pop(L, 1);
            throw Error(__file__, __LINE__, "Function not existing!");
        }
        for (int i = 0; i < n; i++) {
            switch (lv[i].type) {
                case LuaValstruct::INT_TYPE: {
                    lua_pushinteger(L, lv[i].value.i);
                    break;
                }
                case LuaValstruct::FLOAT_TYPE: {
                    lua_pushnumber(L, lv[i].value.f);
                    break;
                }
                case LuaValstruct::STRING_TYPE: {
                    lua_pushstring(L, lv[i].value.s.c_str());
                    break;
                }
            }

        }

        lua_call(L, n, LUA_MULTRET);

        int top = lua_gettop(L);
        vector <LuaValstruct> retval;

        LuaValstruct tmplv;
        for (int i = 1; i <= top; i++) {
            if (lua_isstring(L, i)) {
                tmplv.type = LuaValstruct::STRING_TYPE;
                tmplv.value.s = string(lua_tostring(L, i));
            }
            else if (lua_isinteger(L, i)) {
                tmplv.type = LuaValstruct::INT_TYPE;
                tmplv.value.i = lua_tointeger(L, i);
            }
            else if (lua_isnumber(L, i)) {
                tmplv.type = LuaValstruct::FLOAT_TYPE;
                tmplv.value.f = (float) lua_tonumber(L, i);
            }
            else {
                throw Error(__file__, __LINE__, "Datatype cannot be returned!");
            }
            retval.push_back(tmplv);
        }

        lua_pop(L, top);

        return retval;
    }
    //=========================================================================


    vector <LuaValstruct> LuaServer::executeLuafunction(const string *funcname, int n, ...) {
        va_list args;
        va_start(args, n);

        if (lua_getglobal(L, funcname->c_str()) != LUA_TFUNCTION) {
            lua_pop(L, 1);
            throw Error(__file__, __LINE__, "Function not existing!");
        }

        for (int i = 0; i < n; i++) {
            LuaValstruct *lv = va_arg(args, LuaValstruct*);
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
            }

        }
        va_end(args);

        lua_call(L, n, 1);

        int top = lua_gettop(L);
        vector <LuaValstruct> retval;

        LuaValstruct tmplv;
        for (int i = 1; i <= top; i++) {
            if (lua_isstring(L, i)) {
                tmplv.type = LuaValstruct::STRING_TYPE;
                tmplv.value.s = string(lua_tostring(L, i));
            }
            else if (lua_isinteger(L, i)) {
                tmplv.type = LuaValstruct::INT_TYPE;
                tmplv.value.i = lua_tointeger(L, i);
            }
            else if (lua_isnumber(L, i)) {
                tmplv.type = LuaValstruct::FLOAT_TYPE;
                tmplv.value.f = (float) lua_tonumber(L, i);
            }
            else {
                throw Error(__file__, __LINE__, "Datatype cannot be returned!");
            }
            retval.push_back(tmplv);
        }

        lua_pop(L, top);

        return retval;
    }
    //=========================================================================

    bool LuaServer::luaFunctionExists(const std::string *funcname) {
        int ltype = lua_getglobal(L, funcname->c_str());
        lua_pop(L, 1);
        return (ltype == LUA_TFUNCTION);
    }
}
