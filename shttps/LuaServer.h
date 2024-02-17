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
 *//*!
 * \brief Lua handling...
 *
 */
#ifndef __shttp_lua_server_h
#define __shttp_lua_server_h

#include <iostream>
#include <vector>
#include <map>
#include <unordered_map>
#include <stdexcept>
#include <memory>

#include "Error.h"
#include "Connection.h"

#include "lua.hpp"


namespace shttps {

    typedef struct _LuaValstruct {
        enum {
            INT_TYPE, FLOAT_TYPE, STRING_TYPE, BOOLEAN_TYPE, TABLE_TYPE
        } type;
        struct {
            int i;
            float f;
            std::string s;
            bool b;
            std::unordered_map<std::string, std::shared_ptr<struct _LuaValstruct>> table;
        } value;
    } LuaValstruct;

    typedef struct _LuaRoute {
        Connection::HttpMethod method;
        std::string route;
        std::string script;
    } LuaRoute;

    typedef std::unordered_map<std::string, LuaValstruct> LuaKeyValStore;

    typedef void (*LuaSetGlobalsFunc)(lua_State *L, Connection &, void *);

    extern char luaconnection[];

    class LuaServer {
    private:
        lua_State *L{};

    public:
        /*!
         * Instantiates a lua interpreter
         */
        LuaServer();

        /*!
         * Instantiates a lua interpreter which has access to the HTTP connection
         *
         * \param[in] conn HTTP Connection object
         */
        explicit LuaServer(Connection &conn);


        /*!
         * Instantiates a lua interpreter an executes the given lua script
         *
         * \param[in] luafile A script containing lua commands
         * \param[in] iscode If true, the string contains lua-code to be executed directly
         */
        explicit LuaServer(const std::string &luafile, bool iscode = false);

        /*!
         * Instantiates a lua interpreter an executes the given lua script
         *
         * \param[in] conn HTTP Connection object
         * \param[in] luafile A script containing lua commands
         * \param[in] iscode If true, the string contains lua-code to be executed directly
         * \param[in] lua_scriptdir Pattern to be added to the Lua package.path (directory with Lua scripts)
         */
        LuaServer(Connection &conn, const std::string &luafile, bool iscode, const std::string &lua_scriptdir);

        /*!
         * Copy constructor throws error (not allowed!)
         */
        LuaServer(const LuaServer &other) {
            throw Error(__FILE__, __LINE__, "Copy constructor not allowed!");
        }

        /*!
         * Assignment operator throws error (not allowed!)
         */
        inline LuaServer &operator=(const LuaServer &other) {
            throw Error(__FILE__, __LINE__, "Assigment operator not allowed!");
        }

        /*!
         * Destroys the lua interpreter and all associated resources
         */
        ~LuaServer();

        /*!
         * Getter for the Lua state
         */
        lua_State *lua() const { return L; }

        /*!
         * Adds a value to the server tabe.
         *
         * The server table contains constants and functions which are related
         * to the shttps HTTP server. The server table a a globally accessible
         * table.
         *
         * \param[in] name Name of variable
         * \param[in] value Value (only String allowed)
         */
        void add_servertableentry(const std::string &name, const std::string &value);

        /*!
        * Add path to the lua package path for "require"
        *
        * \param[in] path The path
        */
        void setLuaPath(const std::string &path);


        /*!
         * Create the global values and functions
         *
         * \param[in] conn HTTP connection object
         */
        void createGlobals(Connection &conn);


        std::string configString(std::string table, std::string variable, std::string defval);

        bool configBoolean(std::string table, std::string variable, bool defval);

        int configInteger(std::string table, std::string variable, int defval);

        float configFloat(std::string table, std::string variable, float defval);

        const std::vector<std::string> configStringList(std::string table, std::string stringlist);

        const std::map<std::string,std::string> configStringTable(
                const std::string &table,
                const std::string &variable,
                const std::map<std::string,std::string> &defval);

        std::vector<LuaRoute> configRoute(const std::string &routetable) const;

        const std::map<std::string,LuaKeyValStore> configKeyValueStores(std::string table);
        /*!
         * Execute a chunk of Lua code
         *
         * \param[in] luastr String containing the Lua code
         * \param[in] scriptname String containing the Lua script name
         * \returns Either the value 1 or an integer result that the Lua code provides
         */
        int executeChunk(const std::string &luastr, const std::string &scriptname);

        /*!
         * Executes a Lua function that either is defined in C or in Lua
         *
         * \param[in] funcname the name of the function to be called
         * \param[in] lvals vector of parameters to be passed to the function
         * \returns vector of LuaValstruct containing the result of the execution of the lua function
         */
        std::vector<std::shared_ptr<LuaValstruct>> executeLuafunction(const std::string &funcname, const std::vector<std::shared_ptr<LuaValstruct>> &lvals);

        /*!
         * Executes a Lua function that either is defined in C or in Lua
         *
         * \param[in] funcname Name of the function
         * \param[in] n Number of arguments
         * \returns true if function with given name exists
         */
        bool luaFunctionExists(const std::string &funcname);
    };

}

#endif
