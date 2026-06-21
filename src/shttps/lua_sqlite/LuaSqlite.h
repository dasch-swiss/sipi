/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_LUASQLITE_H
#define SIPI_LUASQLITE_H

#include <iostream>
#include <vector>

#include "shttps/lua/LuaServer.h"

namespace shttps {
extern void sqliteGlobals(lua_State *L, shttps::RequestContext &ctx, void *user_data);
};

#endif// SIPI_LUASQLITE_H
