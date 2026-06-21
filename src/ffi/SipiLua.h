/*
 * Copyright © 2016 - 2024 Swiss National Data and Service Center for the Humanities and/or DaSCH Service Platform
 * contributors. SPDX-License-Identifier: AGPL-3.0-or-later
 */

#ifndef SIPI_SIPILUA_H
#define SIPI_SIPILUA_H

#include <iostream>
#include <unordered_map>
#include <vector>

#include "shttps/lua/LuaServer.h"

/*!
 * This module implements a new Lua datatype, "SipiImage"
 * This datatype is used to manipulate the Sipi::Image c++ class
 * The following Lua functions are implemented:
 *
 *    img = SipiImage.new("filename")
 *    img = SipiImage.new("filename",{region=<iiif-region-string>, size=<iiif-size-string> | reduce=<integer>})
 *    dims = SipiImage.dim(img) -- returns table { nx=<nx>, ny=<ny> }
 *    SipiImage.write(img, <filepath>)
 *    SipiImage.send(img, format)
 */
namespace Sipi {

extern void sipiGlobals(lua_State *L, shttps::RequestContext &ctx, void *user_data);

}// namespace Sipi


#endif// SIPI_SIPILUA_H
