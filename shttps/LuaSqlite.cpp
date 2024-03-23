/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>

#include "LuaSqlite.h"
#include <sqlite3.h>
#include <stdio.h>


namespace shttps {


char shttpsserver[] = "__shttps";

static const char LUASQLITE[] = "ShttpsSqlite";

typedef struct
{
  sqlite3 *sqlite_handle;
  std::string *dbname;
} Sqlite;

static const char LUASQLSTMT[] = "ShttsSqliteStmt";

typedef struct
{
  sqlite3 *sqlite_handle;
  sqlite3_stmt *stmt_handle;
} Stmt;


static Sqlite *toSqlite(lua_State *L, int index)
{
  Sqlite *db = static_cast<Sqlite *>(lua_touserdata(L, index));

  if (db == nullptr) {
    lua_pushstring(L, "Type error");
    lua_error(L);
  }

  return db;
}
//=========================================================================

static Sqlite *checkSqlite(lua_State *L, int index)
{
  Sqlite *db;
  luaL_checktype(L, index, LUA_TUSERDATA);
  db = (Sqlite *)luaL_checkudata(L, index, LUASQLITE);

  if (db == nullptr) {
    lua_pushstring(L, "Type error");
    lua_error(L);
  }

  return db;
}
//=========================================================================

static Sqlite *pushSqlite(lua_State *L)
{
  Sqlite *db = static_cast<Sqlite *>(lua_newuserdata(L, sizeof(Sqlite)));
  luaL_getmetatable(L, LUASQLITE);
  lua_setmetatable(L, -2);
  return db;
}
//=========================================================================

//
// called by garbage collection
//
static int Sqlite_gc(lua_State *L)
{
  Sqlite *db = toSqlite(L, 1);

  if (db->sqlite_handle != nullptr) {
    sqlite3_close_v2(db->sqlite_handle);
    db->sqlite_handle = nullptr;
  }

  delete db->dbname;
  return 0;
}
//=========================================================================

static int Sqlite_tostring(lua_State *L)
{
  Sqlite *db = toSqlite(L, 1);
  std::stringstream ss;
  ss << "DB-File: " << *(db->dbname);
  lua_pushstring(L, ss.str().c_str());
  return 1;
}
//=========================================================================

static Stmt *toStmt(lua_State *L, int index)
{
  Stmt *stmt = (Stmt *)lua_touserdata(L, index);

  if (stmt == nullptr) {
    lua_pushstring(L, "Type error");
    lua_error(L);
  }

  return stmt;
}
//=========================================================================

static Stmt *checkStmt(lua_State *L, int index)
{
  luaL_checktype(L, index, LUA_TUSERDATA);

  Stmt *stmt = (Stmt *)luaL_checkudata(L, index, LUASQLSTMT);

  if (stmt == nullptr) {
    lua_pushstring(L, "Type error");
    lua_error(L);
  }

  return stmt;
}
//=========================================================================

static Stmt *pushStmt(lua_State *L)
{
  Stmt *stmt = (Stmt *)lua_newuserdata(L, sizeof(Stmt));
  luaL_getmetatable(L, LUASQLSTMT);
  lua_setmetatable(L, -2);
  return stmt;
}
//=========================================================================

//
// called by garbage collection
//
static int Stmt_gc(lua_State *L)
{
  Stmt *stmt = toStmt(L, 1);
  if (stmt->stmt_handle != nullptr) sqlite3_finalize(stmt->stmt_handle);
  return 0;
}
//=========================================================================

static int Stmt_tostring(lua_State *L)
{
  Stmt *stmt = toStmt(L, 1);
  std::stringstream ss;
  ss << "SQL: " << sqlite3_sql(stmt->stmt_handle);
  lua_pushstring(L, ss.str().c_str());
  return 1;
}
//=========================================================================


//
// usage
//    db = sqlite("/path/to/db/file", "RW")
//    qry = sqlite.query(db, "SELECT * FROM test")
//    -- or --
//    qry = db << "SELECT * FROM test"
//
static int Sqlite_query(lua_State *L)
{
  int top = lua_gettop(L);

  if (top != 2) {
    // throw an error!
    lua_pushstring(L, "Incorrect number of arguments!");
    return lua_error(L);
  }

  Sqlite *db = checkSqlite(L, 1);

  if (db == nullptr) {
    lua_pushstring(L, "Couldn't connect to database!");
    return lua_error(L);
  }

  const char *sql = nullptr;

  if (lua_isstring(L, 2)) {
    sql = lua_tostring(L, 2);
  } else {
    lua_pushstring(L, "No SQL given!");
    lua_error(L);
    return 0;
  }

  sqlite3_stmt *stmt_handle;
  int status = sqlite3_prepare_v2(db->sqlite_handle, sql, strlen(sql), &stmt_handle, nullptr);

  if (status != SQLITE_OK) {
    lua_pushstring(L, sqlite3_errmsg(db->sqlite_handle));
    return lua_error(L);
  }

  Stmt *stmt = pushStmt(L);
  stmt->sqlite_handle = db->sqlite_handle;// we save the handle of the database also here
  stmt->stmt_handle = stmt_handle;
  return 1;
}
//=========================================================================


static int Sqlite_destroy(lua_State *L)
{
  Sqlite *db = toSqlite(L, 1);
  if (db->sqlite_handle != nullptr) {
    sqlite3_close_v2(db->sqlite_handle);
    db->sqlite_handle = nullptr;
  }
  return 0;
}
//=========================================================================


static const luaL_Reg Sqlite_meta[] = { { "__gc", Sqlite_gc },
  { "__tostring", Sqlite_tostring },
  { "__shl", Sqlite_query },
  { "__bnot", Sqlite_destroy },
  { 0, 0 } };
//=========================================================================


//
// db = sqlite.new("/path/to/db/file", "RW")
// qry = db << "SELECT * FROM test"
// row = qry()
// while (row) do
//    ...
//    row = qry()
// end
// qry = ~qry -- delete query and free prepared statment
// ...
// db = ~db -- delete the database connection
//
int Stmt_next(lua_State *L)
{
  int top = lua_gettop(L);
  if (top < 1) {
    // throw an error!
    lua_pushstring(L, "Stmt_next: Incorrect number of arguments!");
    return lua_error(L);
  }

  Stmt *stmt = checkStmt(L, 1);
  if (stmt == nullptr) {
    lua_pushstring(L, "Stmt_next: Invalid prepared statment!");
    return lua_error(L);
  }

  if (top > 1) {
    if (sqlite3_reset(stmt->stmt_handle) != SQLITE_OK) {
      lua_pushstring(L, sqlite3_errmsg(stmt->sqlite_handle));
      return lua_error(L);
    }
    if (sqlite3_clear_bindings(stmt->stmt_handle) != SQLITE_OK) {
      lua_pushstring(L, sqlite3_errmsg(stmt->sqlite_handle));
      return lua_error(L);
    }
    for (int i = 2; i <= top; i++) {
      int status;
      if (lua_isinteger(L, i)) {
        int val = lua_tointeger(L, i);
        status = sqlite3_bind_int(stmt->stmt_handle, i - 1, val);
      } else if (lua_isnumber(L, i)) {
        double val = lua_tonumber(L, i);
        status = sqlite3_bind_double(stmt->stmt_handle, i - 1, val);
      } else if (lua_isstring(L, i)) {
        size_t len;
        const char *val = lua_tolstring(L, i, &len);
        if (strlen(val) == len) {// it's a real string
          status = sqlite3_bind_text(stmt->stmt_handle, i - 1, val, len, SQLITE_TRANSIENT);
        } else {
          status = sqlite3_bind_blob(stmt->stmt_handle, i - 1, val, len, SQLITE_TRANSIENT);
        }
      } else if (lua_isnil(L, i)) {
        status = sqlite3_bind_null(stmt->stmt_handle, i - 1);
      } else if (lua_isboolean(L, i)) {
        int val = lua_toboolean(L, i);
        status = sqlite3_bind_int(stmt->stmt_handle, i - 1, val);
      } else {
        lua_pushstring(L, "Stmt_next: Invalid datatype for binding to prepared statments!");
        return lua_error(L);
      }
      if (status != SQLITE_OK) {
        lua_pushstring(L, sqlite3_errmsg(stmt->sqlite_handle));
        return lua_error(L);
      }
    }
  }

  int status = sqlite3_step(stmt->stmt_handle);
  if (status == SQLITE_ROW) {
    int ncols = sqlite3_column_count(stmt->stmt_handle);

    lua_createtable(L, 0, ncols);// table

    for (int col = 0; col < ncols; col++) {
      lua_pushinteger(L, col);// table - col
      int ctype = sqlite3_column_type(stmt->stmt_handle, col);
      switch (ctype) {
      case SQLITE_INTEGER: {
        int val = sqlite3_column_int(stmt->stmt_handle, col);
        lua_pushinteger(L, val);// table - col - val
        break;
      }
      case SQLITE_FLOAT: {
        double val = sqlite3_column_double(stmt->stmt_handle, col);
        lua_pushnumber(L, val);// table - col - val
        break;
      }
      case SQLITE_BLOB: {
        size_t nval = sqlite3_column_bytes(stmt->stmt_handle, col);
        const char *val = (char *)sqlite3_column_blob(stmt->stmt_handle, col);
        lua_pushlstring(L, val, nval);
        break;
      }
      case SQLITE_NULL: {
        lua_pushnil(L);// table - col - nil
        break;
      }
      case SQLITE_TEXT: {
        const char *str = (char *)sqlite3_column_text(stmt->stmt_handle, col);
        lua_pushstring(L, str);// table - col - str
        break;
      }
      }
      lua_rawset(L, -3);// table
    }
  } else if (status == SQLITE_DONE) {
    lua_pushnil(L);
  } else {
    lua_pushstring(L, sqlite3_errmsg(stmt->sqlite_handle));
    return lua_error(L);
  }

  //
  // if we had bindings, we have to reset the prepared statment
  //
  /*
  if (top > 1) {
      if (sqlite3_reset(stmt->stmt_handle) != SQLITE_OK) {
          lua_pushstring(L, sqlite3_errmsg(stmt->sqlite_handle));
          return lua_error(L);
      }
      if (sqlite3_clear_bindings(stmt->stmt_handle) != SQLITE_OK) {
          lua_pushstring(L, sqlite3_errmsg(stmt->sqlite_handle));
          return lua_error(L);
      }
  }
   */
  return 1;
}
//=========================================================================


static int Stmt_destroy(lua_State *L)
{
  Stmt *stmt = toStmt(L, 1);
  if (stmt->stmt_handle != nullptr) {
    sqlite3_finalize(stmt->stmt_handle);
    stmt->stmt_handle = nullptr;
    stmt->sqlite_handle = nullptr;
  }
  return 0;
}
//=========================================================================


static const luaL_Reg Stmt_meta[] = { { "__gc", Stmt_gc },
  { "__tostring", Stmt_tostring },
  { "__call", Stmt_next },
  { "__bnot", Stmt_destroy },
  { 0, 0 } };
//=========================================================================


//
// usage:
//
//   db = sqlite(path [, "RO" | "RW" | "CRW"])
//
static int Sqlite_new(lua_State *L)
{
  int top = lua_gettop(L);
  if (top < 1) {
    lua_pushstring(L, "'sqlite(path, mode)': no enough parameters!");
    return lua_error(L);
  }

  if (!lua_isstring(L, 1)) {
    lua_pushstring(L, "'sqlite(path, mode)': no enough parameters!");
    return lua_error(L);
  }

  const char *dbpath = lua_tostring(L, 1);
  int flags = SQLITE_OPEN_READWRITE;

  if ((top >= 2) && (lua_isstring(L, 2))) {
    std::string flagstr = lua_tostring(L, 1);
    if (flagstr == "RO") {
      flags = SQLITE_OPEN_READONLY;
    } else if (flagstr == "RW") {
      flags = SQLITE_OPEN_READWRITE;
    } else if (flagstr == "CRW") {
      flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    }
  }

  flags |= SQLITE_OPEN_NOMUTEX;
  sqlite3 *handle;
  int status = sqlite3_open_v2(dbpath, &handle, flags, nullptr);

  if (status != SQLITE_OK) {
    lua_pushstring(L, sqlite3_errmsg(handle));
    return lua_error(L);
  }

  Sqlite *db = pushSqlite(L);
  db->sqlite_handle = handle;
  db->dbname = new std::string(dbpath);

  return 1;
}
//=========================================================================


void sqliteGlobals(lua_State *L, shttps::Connection &conn, void *user_data)
{

  //
  // let's prepare the metatable for the stmt-object
  //
  luaL_newmetatable(L, LUASQLSTMT);// create metatable, and add it to the Lua registry
  // stack: metatable


  luaL_setfuncs(L, Stmt_meta, 0);
  // stack: metatable  [with functions __gc and __tostring added]

  lua_pop(L, 1);
  // stack: -

  //
  // let's prepare the metatable for the sqlite object
  //
  luaL_newmetatable(L, LUASQLITE);// create metatable, and add it to the Lua registry
  // stack: metatable

  luaL_setfuncs(L, Sqlite_meta, 0);

  lua_pop(L, 1);// drop metatable
  // stack: table(LUASQLITE)

  lua_pushcfunction(L, Sqlite_new);
  lua_setglobal(L, "sqlite");
}
//=========================================================================


}// namespace shttps
