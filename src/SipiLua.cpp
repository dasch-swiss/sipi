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
#include <string>
#include <iostream>
#include <sstream>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include<unordered_set>

#include <stdio.h>
#include <SipiCache.h>
#include <SipiFilenameHash.h>

#include "shttps/Connection.h"
#include "shttps/Parsing.h"
#include "SipiImage.h"
#include "SipiLua.h"
#include "SipiHttpServer.h"
#include "SipiCache.h"
#include "Error.h"

namespace Sipi {

    char sipiserver[] = "__sipiserver";

    static const char SIMAGE[] = "SipiImage";

    typedef struct {
        SipiImage *image;
        std::string *filename;
    } SImage;


    /*!
     * Get the size of the cache
     * LUA: cache_size = cache.size()
     */
    static int lua_cache_size(lua_State *L) {
        lua_getglobal(L, sipiserver);
        SipiHttpServer *server = (SipiHttpServer *) lua_touserdata(L, -1);
        lua_remove(L, -1); // remove from stack
        std::shared_ptr<SipiCache> cache = server->cache();

        if (cache == nullptr) {
            lua_pushnil(L);
            return 1;
        }

        unsigned long long size = cache->getCachesize();

        lua_pushinteger(L, size);
        return 1;
    }
    //=========================================================================

    /*!
     * Get the maximal size of the cache
     * LUA: cache.max_size = cache.max_size()
     */
    static int lua_cache_max_size(lua_State *L) {
        lua_getglobal(L, sipiserver);
        SipiHttpServer *server = (SipiHttpServer *) lua_touserdata(L, -1);
        lua_remove(L, -1); // remove from stack
        std::shared_ptr<SipiCache> cache = server->cache();

        if (cache == nullptr) {
            lua_pushnil(L);
            return 1;
        }

        unsigned long long maxsize = cache->getMaxCachesize();

        lua_pushinteger(L, maxsize);
        return 1;
    }
    //=========================================================================

    /*!
     * Get the size of the cache
     * LUA: cache_size = cache.size()
     */
    static int lua_cache_nfiles(lua_State *L) {
        lua_getglobal(L, sipiserver);
        SipiHttpServer *server = (SipiHttpServer *) lua_touserdata(L, -1);
        lua_remove(L, -1); // remove from stack
        std::shared_ptr<SipiCache> cache = server->cache();

        if (cache == nullptr) {
            lua_pushnil(L);
            return 1;
        }

        unsigned size = cache->getNfiles();

        lua_pushinteger(L, size);
        return 1;
    }
    //=========================================================================

    /*!
     * Get the size of the cache
     * LUA: cache_size = cache.size()
     */
    static int lua_cache_max_nfiles(lua_State *L) {
        lua_getglobal(L, sipiserver);
        SipiHttpServer *server = (SipiHttpServer *) lua_touserdata(L, -1);
        lua_remove(L, -1); // remove from stack
        std::shared_ptr<SipiCache> cache = server->cache();

        if (cache == nullptr) {
            lua_pushnil(L);
            return 1;
        }

        unsigned size = cache->getMaxNfiles();

        lua_pushinteger(L, size);

        return 1;
    }
    //=========================================================================

    /*!
     * Get path to cache dir
     * LUA: cache_path = cache.path()
     */
    static int lua_cache_path(lua_State *L) {
        lua_getglobal(L, sipiserver);
        SipiHttpServer *server = (SipiHttpServer *) lua_touserdata(L, -1);
        lua_remove(L, -1); // remove from stack
        std::shared_ptr<SipiCache> cache = server->cache();

        if (cache == nullptr) {
            lua_pushnil(L);
            return 1;
        }
        std::string cpath = cache->getCacheDir();

        lua_pushstring(L, cpath.c_str());
        return 1;
    }
    //=========================================================================

    static void
    add_one_cache_file(int index, const std::string &canonical, const SipiCache::CacheRecord &cr, void *userdata) {
        lua_State *L = (lua_State *) userdata;

        lua_pushinteger(L, index);
        lua_createtable(L, 0, 4); // table1

        lua_pushstring(L, "canonical");
        lua_pushstring(L, canonical.c_str());
        lua_rawset(L, -3);

        lua_pushstring(L, "origpath");
        lua_pushstring(L, cr.origpath.c_str());
        lua_rawset(L, -3);

        lua_pushstring(L, "cachepath");
        lua_pushstring(L, cr.cachepath.c_str());
        lua_rawset(L, -3);

        lua_pushstring(L, "size");
        lua_pushinteger(L, cr.fsize);
        lua_rawset(L, -3);

        struct tm *tminfo;
        tminfo = localtime(&cr.access_time);
        char timestr[100];
        strftime(timestr, sizeof(timestr), "%Y-%m-%d %H:%M:%S", tminfo);
        lua_pushstring(L, "last_access");
        lua_pushstring(L, timestr);
        lua_rawset(L, -3);

        lua_rawset(L, -3);

        return;
    }
    //=========================================================================

    static int lua_cache_filelist(lua_State *L) {
        lua_getglobal(L, sipiserver);
        SipiHttpServer *server = (SipiHttpServer *) lua_touserdata(L, -1);
        lua_remove(L, -1); // remove from stack

        int top = lua_gettop(L);

        std::string sortmethod;
        if (top == 1) {
            sortmethod = std::string(lua_tostring(L, 1));
        }
        lua_pop(L, top);
        std::shared_ptr<SipiCache> cache = server->cache();

        if (cache == nullptr) {
            lua_pushnil(L);
            return 1;
        }

        lua_createtable(L, 0, 0); // table1
        if (sortmethod == "AT_ASC") {
            cache->loop(add_one_cache_file, (void *) L, SipiCache::SortMethod::SORT_ATIME_ASC);
        } else if (sortmethod == "AT_DESC") {
            cache->loop(add_one_cache_file, (void *) L, SipiCache::SortMethod::SORT_ATIME_DESC);
        } else if (sortmethod == "FS_ASC") {
            cache->loop(add_one_cache_file, (void *) L, SipiCache::SortMethod::SORT_FSIZE_ASC);
        } else if (sortmethod == "FS_DESC") {
            cache->loop(add_one_cache_file, (void *) L, SipiCache::SortMethod::SORT_FSIZE_DESC);
        } else {
            cache->loop(add_one_cache_file, (void *) L);
        }

        return 1;
    }
    //=========================================================================


    static int lua_delete_cache_file(lua_State *L) {
        lua_getglobal(L, sipiserver);
        SipiHttpServer *server = (SipiHttpServer *) lua_touserdata(L, -1);
        lua_remove(L, -1); // remove from stack
        std::shared_ptr<SipiCache> cache = server->cache();

        int top = lua_gettop(L);

        if (cache == nullptr) {
            lua_pop(L, top);
            lua_pushnil(L);
            return 1;
        }

        std::string canonical;

        if (top == 1) {
            canonical = std::string(lua_tostring(L, 1));
            lua_pop(L, 1);
            lua_pushboolean(L, cache->remove(canonical));
        } else {
            lua_pop(L, top);
            lua_pushboolean(L, false);
        }

        return 1;
    }
    //=========================================================================

    static int lua_purge_cache(lua_State *L) {
        lua_getglobal(L, sipiserver);
        SipiHttpServer *server = (SipiHttpServer *) lua_touserdata(L, -1);
        lua_remove(L, -1); // remove from stack
        std::shared_ptr<SipiCache> cache = server->cache();

        if (cache == nullptr) {
            lua_pushnil(L);
            return 1;
        }

        int n = cache->purge(true);
        lua_pushinteger(L, n);

        return 1;
    }
    //=========================================================================

    static const luaL_Reg cache_methods[] = {{"size",       lua_cache_size},
                                             {"max_size",   lua_cache_max_size},
                                             {"nfiles",     lua_cache_nfiles},
                                             {"max_nfiles", lua_cache_max_nfiles},
                                             {"path",       lua_cache_path},
                                             {"filelist",   lua_cache_filelist},
                                             {"delete",     lua_delete_cache_file},
                                             {"purge",      lua_purge_cache},
                                             {0,            0}};
    //=========================================================================

    static int lua_filenamehash_helper(lua_State *L) {
        int top = lua_gettop(L);

        if (top < 1) {
            lua_settop(L, 0); // clear stack
            lua_pushboolean(L, false);
            lua_pushstring(L, "'helper.hash(filename)': parameter missing");
            return 2;
        }

        if (!lua_isstring(L, 1)) {
            lua_settop(L, 0); // clear stack
            lua_pushboolean(L, false);
            lua_pushstring(L, "'helper.hash(filename)': filename is not a string");
            return 2;
        }

        const char *filename = lua_tostring(L, 1);
        lua_settop(L, 0); // clear stack

        std::string filepath;
        try {
            SipiFilenameHash hash(filename);
            filepath = hash.filepath();
        } catch(shttps::Error &err) {
            lua_settop(L, 0); // clear stack
            lua_pushboolean(L, false);
            std::stringstream ss;
            ss << "'helper.hash(filename)': ";
            ss << err;
            lua_pushstring(L, ss.str().c_str());
            return 2;
        }

        lua_pushboolean(L, true);
        lua_pushstring(L, filepath.c_str());

        return 2;
    }
    //=========================================================================

    static const luaL_Reg helper_methods[] = {{"filename_hash", lua_filenamehash_helper},
                                             {0,            0}};
    //=========================================================================



    static SImage *toSImage(lua_State *L, int index) {
        SImage *img = (SImage *) lua_touserdata(L, index);
        if (img == nullptr) {
            lua_pushstring(L, "Type error: Not userdata object");
            lua_error(L);
        }
        return img;
    }
    //=========================================================================

    static SImage *checkSImage(lua_State *L, int index) {
        SImage *img;
        luaL_checktype(L, index, LUA_TUSERDATA);
        img = (SImage *) luaL_checkudata(L, index, SIMAGE);
        if (img == nullptr) {
            lua_pushstring(L, "Type error: Expected an SipiImage");
            lua_error(L);
        }
        return img;
    }
    //=========================================================================

    static SImage *pushSImage(lua_State *L, const SImage &simage) {
        SImage *img = (SImage *) lua_newuserdata(L, sizeof(SImage));
        *img = simage;
        luaL_getmetatable(L, SIMAGE);
        lua_setmetatable(L, -2);
        return img;
    }
    //=========================================================================

    /*
     * Lua usage:
     *    img = SipiImage.new("filename")
     *    img = SipiImage.new("filename",
     *    {
     *      region=<iiif-region-string>,
     *      size=<iiif-size-string>,
     *      reduce=<integer>,
     *      original=origfilename},
     *      hash="md5"|"sha1"|"sha256"|"sha384"|"sha512"
     *    })
     */
    static int SImage_new(lua_State *L) {
        lua_getglobal(L, shttps::luaconnection);
        shttps::Connection *conn = (shttps::Connection *) lua_touserdata(L, -1);
        lua_remove(L, -1); // remove from stacks
        int top = lua_gettop(L);

        if (top < 1) {
            lua_pop(L, top);
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.new(): No filename given");
            return 2;
        }

        int pagenum = 0;
        std::shared_ptr<SipiRegion> region;
        std::shared_ptr<SipiSize> size;
        std::string original;
        shttps::HashType htype = shttps::HashType::sha256;
        std::string imgpath;

        if (lua_isinteger(L, 1)) {
            std::vector<shttps::Connection::UploadedFile> uploads = conn->uploads();
            int tmpfile_id = static_cast<int>(lua_tointeger(L, 1));
            try {
                imgpath = uploads.at(tmpfile_id - 1).tmpname; // In Lua, indexes are 1-based.
                original = uploads.at(tmpfile_id - 1).origname;
            } catch (const std::out_of_range &oor) {
                lua_settop(L, 0); // clear stack
                lua_pushboolean(L, false);
                lua_pushstring(L, "'SipiImage.new()': Could not read data of uploaded file. Invalid index?");
                return 2;
            }
        }
        else if (lua_isstring(L, 1)) {
            imgpath = lua_tostring(L, 1);
        }
        else {
            lua_pop(L, top);
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.new(): filename must be string or index");
            return 2;
        }

        if (top == 2) {
            if (lua_istable(L, 2)) {
                //lua_pop(L,1); // remove filename from stack
            } else {
                lua_pop(L, top);
                lua_pushboolean(L, false);
                lua_pushstring(L, "SipiImage.new(): Second parameter must be table");
                return 2;
            }

            lua_pushnil(L);

            while (lua_next(L, 2) != 0) {
                if (lua_isstring(L, -2)) {
                    const char *param = lua_tostring(L, -2);

                    if (strcmp(param, "pagenum") == 0) {
                        if (lua_isnumber(L, -1)) {
                            pagenum = lua_tointeger(L, -1);
                        } else {
                            lua_pop(L, lua_gettop(L));
                            lua_pushboolean(L, false);
                            lua_pushstring(L, "SipiImage.new(): Error in pagenum parameter");
                            return 2;
                        }
                    } else if (strcmp(param, "region") == 0) {
                        if (lua_isstring(L, -1)) {
                            region = std::make_shared<SipiRegion>(lua_tostring(L, -1));
                        } else {
                            lua_pop(L, lua_gettop(L));
                            lua_pushboolean(L, false);
                            lua_pushstring(L, "SipiImage.new(): Error in region parameter");
                            return 2;
                        }
                    } else if (strcmp(param, "size") == 0) {
                        if (lua_isstring(L, -1)) {
                            size = std::make_shared<SipiSize>(lua_tostring(L, -1));
                        } else {
                            lua_pop(L, lua_gettop(L));
                            lua_pushboolean(L, false);
                            lua_pushstring(L, "SipiImage.new(): Error in size parameter");
                            return 2;
                        }
                    } else if (strcmp(param, "reduce") == 0) {
                        if (lua_isnumber(L, -1)) {
                            size = std::make_shared<SipiSize>(static_cast<int>(lua_tointeger(L, -1)));
                        } else {
                            lua_pop(L, lua_gettop(L));
                            lua_pushboolean(L, false);
                            lua_pushstring(L, "SipiImage.new(): Error in reduce parameter");
                            return 2;
                        }
                    } else if (strcmp(param, "original") == 0) {
                        if (lua_isstring(L, -1)) {
                            const char *tmpstr = lua_tostring(L, -1);
                            original = tmpstr;
                        } else {
                            lua_pop(L, lua_gettop(L));
                            lua_pushboolean(L, false);
                            lua_pushstring(L, "SipiImage.new(): Error in original parameter");
                            return 2;
                        }
                    } else if (strcmp(param, "hash") == 0) {
                        if (lua_isstring(L, -1)) {
                            const char *tmpstr = lua_tostring(L, -1);
                            std::string hashstr = tmpstr;
                            if (hashstr == "md5") {
                                htype = shttps::HashType::md5;
                            } else if (hashstr == "sha1") {
                                htype = shttps::HashType::sha1;
                            } else if (hashstr == "sha256") {
                                htype = shttps::HashType::sha256;
                            } else if (hashstr == "sha384") {
                                htype = shttps::HashType::sha384;
                            } else if (hashstr == "sha512") {
                                htype = shttps::HashType::sha512;
                            } else {
                                lua_pop(L, lua_gettop(L));
                                lua_pushboolean(L, false);
                                lua_pushstring(L, "SipiImage.new(): Error in hash type");
                                return 2;
                            }
                        } else {
                            lua_pop(L, lua_gettop(L));
                            lua_pushboolean(L, false);
                            lua_pushstring(L, "SipiImage.new(): Error in hash parameter");
                            return 2;
                        }
                    } else {
                        lua_pop(L, lua_gettop(L));
                        lua_pushboolean(L, false);
                        lua_pushstring(L, "SipiImage.new(): Error in parameter table (unknown parameter)");
                        return 2;
                    }
                }

                /* removes value; keeps key for next iteration */
                lua_pop(L, 1);
            }
        }

        lua_pushboolean(L, true); // result code
        SImage simg;
        simg.image = new SipiImage();
        simg.filename = new std::string(imgpath);
        SImage *img = pushSImage(L, simg);

        try {
            if (!original.empty()) {
                img->image->readOriginal(imgpath, pagenum, region, size, original, htype);
            } else {
                img->image->read(imgpath, pagenum, region, size);
            }
        } catch (SipiImageError &err) {
            delete img->image;
            img->image = nullptr;
            delete img->filename;
            img->filename = nullptr;
            lua_pop(L, lua_gettop(L));
            lua_pushboolean(L, false);
            std::stringstream ss;
            ss << "SipiImage.new(): ";
            ss << err;
            lua_pushstring(L, ss.str().c_str());
            return 2;
        }

        return 2;
    }
    //=========================================================================

    static int SImage_dims(lua_State *L) {
        size_t nx, ny;
        Orientation orientation;

        if (lua_gettop(L) != 1) {
            lua_pop(L, lua_gettop(L));
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.dims(): Incorrect number of arguments");
            return 2;
        }

        if (lua_isstring(L, 1)) {
            const char *imgpath = lua_tostring(L, 1);
            SipiImage img;
            SipiImgInfo info;
            try {
                info = img.getDim(imgpath);
            }
            catch (InfoError &e) {
                lua_pop(L, lua_gettop(L));
                lua_pushboolean(L, false);
                std::stringstream ss;
                ss << "SipiImage.dims(): Couldn't get dimensions";
                lua_pushstring(L, ss.str().c_str());
                return 2;
            }
            catch (SipiImageError &err) {
                lua_pop(L, lua_gettop(L));
                lua_pushboolean(L, false);
                std::stringstream ss;
                ss << "SipiImage.dims(): " << err;
                lua_pushstring(L, ss.str().c_str());
                return 2;
            }
            nx = info.width;
            ny = info.height;
            orientation = info.orientation;
        } else {
            SImage *img = checkSImage(L, 1);
            if (img == nullptr) {
                lua_pop(L, lua_gettop(L));
                lua_pushboolean(L, false);
                lua_pushstring(L, "SipiImage.dims(): not a valid image");
                return 2;
            }
            nx = img->image->getNx();
            ny = img->image->getNy();
            orientation = img->image->getOrientation();
        }

        lua_pop(L, lua_gettop(L));

        lua_pushboolean(L, true);
        lua_createtable(L, 0, 3); // table

        lua_pushstring(L, "nx"); // table - "nx"
        lua_pushinteger(L, nx); // table - "nx" - <nx>
        lua_rawset(L, -3); // table

        lua_pushstring(L, "ny"); // table - "ny"
        lua_pushinteger(L, ny); // table - "ny" - <ny>
        lua_rawset(L, -3); // table

        lua_pushstring(L, "orientation"); // table - "orientation"
        lua_pushinteger(L, static_cast<int>(orientation)); // table - "orientation" - <orientation>
        lua_rawset(L, -3); // table

        return 2;
    }
    //=========================================================================

    static void get_exif_string(lua_State *L, std::shared_ptr<SipiExif> exif, const std::string &tagname) {
        std::string tagvalue;
        if (!exif->getValByKey(tagname, tagvalue)) {
            lua_pop(L, lua_gettop(L));
            lua_pushboolean(L, false);
            lua_pushstring(L, "Sipi.Image.exif(): requested exif tag not available");
        }
        std::cerr << "=====> get_exif_string: " << tagname << " -> " << tagvalue << std::endl;
        lua_pop(L, lua_gettop(L));
        lua_pushboolean(L, true);
        lua_pushstring(L, tagvalue.c_str());
    }

    static void get_exif_ushort(lua_State *L, std::shared_ptr<SipiExif> exif, const std::string &tagname) {
        unsigned short uval;
        if (!exif->getValByKey(tagname, uval)) {
            lua_pop(L, lua_gettop(L));
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.exif(): requested exif tag not available");
        }
        std::cerr << "=====> get_exif_ushort: " << tagname << " -> " << uval << std::endl;
        lua_pop(L, lua_gettop(L));
        lua_pushboolean(L, true);
        lua_pushinteger(L, uval);
    }

    static void get_exif_uint(lua_State *L, std::shared_ptr<SipiExif> exif, const std::string &tagname) {
        unsigned int uval;
        if (!exif->getValByKey(tagname, uval)) {
            lua_pop(L, lua_gettop(L));
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.exif(): requested exif tag not available");
        }
        std::cerr << "=====> get_exif_uint: " << tagname << " -> " << uval << std::endl;
        lua_pop(L, lua_gettop(L));
        lua_pushboolean(L, true);
        lua_pushinteger(L, uval);
    }

    static void get_exif_rational(lua_State *L, std::shared_ptr<SipiExif> exif, const std::string &tagname) {
        Exiv2::Rational ratval;
        if (!exif->getValByKey(tagname, ratval)) {
            lua_pop(L, lua_gettop(L));
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.exif(): requested exif tag not available");
        }
        std::cerr << "=====> get_exif_rational: " << tagname << " -> " << ratval.first << "/" << ratval.second << std::endl;
        lua_pop(L, lua_gettop(L));
        lua_pushboolean(L, true);
        lua_pushnumber(L, (double) ratval.first / (double) ratval.second);
    }


    /*!
     *
     * @param L Lua interpreter
     * @return Number of parameters on stack
     *
     * Lua usage:
     *    SipiImage.exif(img, "<EXIF-TAGNAME>"
     *
     */
    static int SImage_get_exif(lua_State *L) {
        if (lua_gettop(L) != 2) {
            lua_pop(L, lua_gettop(L));
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.exif(): Incorrect number of arguments");
            return 2;
        }

        SImage *img = checkSImage(L, 1);
        if (img == nullptr) {
            lua_pop(L, lua_gettop(L));
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.exif(): not a valid image");
            return 2;
        }
        const char *tagname = lua_tostring(L, 2);
        std::shared_ptr<SipiExif> exif = img->image->getExif();
        if (exif == nullptr) {
            lua_pop(L, lua_gettop(L));
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.exif(): no exif data available");
            return 2;
        }

        std::unordered_set<std::string> ushort_taglist{
                "Orientation",
                "Compression",
                "PhotometricInterpretation",
                "SamplesPerPixel",
                "ResolutionUnit",
                "PlanarConfiguration"
        };
        std::unordered_set<std::string> uint_taglist{
                "ImageWidth",
                "ImageLength",
                "ImageNumber",
        };
        std::unordered_set<std::string> string_taglist{
                "ProcessingSoftware",
                "DocumentName",
                "Make",
                "Model",
                "Software",
                "Artist",
                "DateTime",
                "ImageDescription",
                "HostComputer",
                "Copyright",
                "ImageID",
                "DateTimeOriginal",
                "SecurityClassification",
                "ImageHistory",
                "UniqueCameraModel",
                "CameraSerialNumber",
                "ReelName",
                "CameraLabel"
        };
        std::unordered_set<std::string> rational_taglist{
            "XResolution",
            "YResolution",
            "ExposureTime",
            "FNumber",
            "ApertureValue",
            "FocalLength",
            "FlashEnergy",
            "NoiseReductionApplied"
        };

        std::string tag{tagname};
        std::cerr << "TAGNAME=" << tag << std::endl;
        if (ushort_taglist.find(tag) != ushort_taglist.end()) {
            std::cerr << "FOUND IN ushort_taglist" << std::endl;
            get_exif_ushort(L, exif, "Exif.Image." + tag);
            return 2;
        }
        else if (uint_taglist.find(tag) != uint_taglist.end()) {
            std::cerr << "FOUND IN uint_taglist" << std::endl;
            get_exif_uint(L, exif, "Exif.Image." + tag);
            return 2;
        }
        else if (rational_taglist.find(tag) != rational_taglist.end()) {
            std::cerr << "FOUND IN rational_taglist" << std::endl;
            get_exif_rational(L, exif, "Exif.Image." + tag);
            return 2;
        }
        else if (string_taglist.find(tag) != string_taglist.end()) {
            std::cerr << "FOUND IN string_taglist" << std::endl;
            get_exif_string(L, exif, "Exif.Image." + tag);
            return 2;
        }
        else {
            lua_pop(L, lua_gettop(L));
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.exif(): Unrecognized EXIF-Tag");
            return 2;
        }
    }

    static int SImage_get_exifgps(lua_State *L) {
        if (lua_gettop(L) != 1) {
            lua_pop(L, lua_gettop(L));
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.gps(): Incorrect number of arguments");
            return 2;
        }
        SImage *img = checkSImage(L, 1);
        if (img == nullptr) {
            lua_pop(L, lua_gettop(L));
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.gps(): not a valid image");
            return 2;
        }
        std::shared_ptr<SipiExif> exif = img->image->getExif();
        if (exif == nullptr) {
            lua_pop(L, lua_gettop(L));
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.gps(): no exif data available");
            return 2;
        }

        char latref{'\0'};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSLatitudeRef"), latref)) {
            latref = '\0';
        }
        std::vector<Exiv2::Rational> latitude{{0,1}, {0,1}, {0,1}};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSLatitude"), latitude)) {
            latitude = {{0,1}, {0,1}, {0,1}};
        }

        char longituderef{'\0'};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSLongitudeRef"), longituderef)) {
            longituderef = '\0';
        }
        std::vector<Exiv2::Rational> longitude{{0,1}, {0,1}, {0,1}};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSLongitude"), latitude)) {
            longitude = {{0,1}, {0,1}, {0,1}};
        }

        char altituderef{'\0'};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSAltitudeRef"), altituderef)) {
            altituderef = '\0';
        }
        std::vector<Exiv2::Rational> altitude{{0,1}, {0,1}, {0,1}};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.Exif.GPSInfo.GPSAltitude"), altitude)) {
            altitude = {{0,1}, {0,1}, {0,1}};
        }

        std::vector<Exiv2::Rational> timestamp{{0,1}, {0,1}, {0,1}};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSTimeStamp"), timestamp)) {
            timestamp = {{0,1}, {0,1}, {0,1}};
        }

        char speedref{'\0'};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSSpeedRef"), speedref)) {
            speedref = '\0';
        }
        Exiv2::Rational speed{0,1};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSSpeed"), speed)) {
            speed = {0,1};
        }

        char trackref{'\0'};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSTrackRef"), trackref)) {
            trackref = '\0';
        }
        Exiv2::Rational track{0,1};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSTrack"), track)) {
            track = {0,1};
        }

        char directionref{'\0'};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSImgDirectionRef"), directionref)) {
            directionref = '\0';
        }
        Exiv2::Rational direction{0,1};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSImgDirection"), direction)) {
            direction = {0,1};
        }

        char destlatref{'\0'};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSDestLatitudeRef"), destlatref)) {
            destlatref = '\0';
        }
        std::vector<Exiv2::Rational> destlatitude{{0,1}, {0,1}, {0,1}};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSDestLatitude"), destlatitude)) {
            destlatitude = {{0,1}, {0,1}, {0,1}};
        }

        char destlongituderef{'\0'};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSDestLongitudeRef"), destlongituderef)) {
            destlongituderef = '\0';
        }
        std::vector<Exiv2::Rational> destlongitude{{0,1}, {0,1}, {0,1}};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSDestLongitude"), destlongitude)) {
            destlongitude = {{0,0}, {0,0}, {0,0}};
        }

        char destbearingref{'\0'};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSDestBearingRef"), destbearingref)) {
            destbearingref = '\0';
        }
        Exiv2::Rational destbearing{0,1};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSDestBearing"), destbearing)) {
            destbearing = {0,1};
        }

        char destdistanceref{'\0'};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSDestDistanceRef"), destdistanceref)) {
            destdistanceref = '\0';
        }
        Exiv2::Rational destdistance{0,1};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSDestDistance"), destdistance)) {
            destdistance = {0,1};
        }

        Exiv2::Rational positioningerror{0,1};
        if (!exif->getValByKey(std::string("Exif.GPSInfo.GPSHPositioningError"), positioningerror)) {
            positioningerror = {0,1};
        }


        lua_pop(L, lua_gettop(L));
        lua_pushboolean(L, true); // sucess

        lua_createtable(L, 0, 4); // success - table1

        lua_pushstring(L, "GPSLatitudeRef"); // success - table1 - name
        lua_pushfstring(L, "%c", latref); // success - table1 - name - value
        lua_rawset(L, -3); // success - table1

        lua_pushstring(L, "GPSLatitude"); // success - table1 - name
        lua_createtable(L, 0, 3); // success - table1 - name - table2
        for (int i = 0; i < 3; ++i) {
            lua_pushinteger(L, i); // success - table1 - name - table2 - i
            lua_pushnumber(L, (double) latitude[i].first / (double) latitude[i].second); // success - table1 - name - table2 - i - latitude
            lua_rawset(L, -3); // success - table1 - name - table2
        }
        lua_rawset(L, -3); // success - table1

        lua_pushstring(L, "GPSLongitudeRef"); // success - table1 - name
        lua_pushfstring(L, "%c", longituderef); // success - table1 - name - value
        lua_rawset(L, -3); // success - table1

        lua_pushstring(L, "GPSLongitude"); // success - table1 - name
        lua_createtable(L, 0, 3); // success - table1 - name - table2
        for (int i = 0; i < 3; ++i) {
            lua_pushinteger(L, i); // success - table1 - name - table2 - i
            lua_pushnumber(L, (double) longitude[i].first / (double) longitude[i].second); // success - table1 - name - table2 - i - latitude
            lua_rawset(L, -3); // success - table1 - name - table2
        }
        lua_rawset(L, -3); // success - table1

        lua_pushstring(L, "GPSAltitudeRef"); // success - table1 - name
        lua_pushfstring(L, "%c", altituderef); // success - table1 - name - value
        lua_rawset(L, -3); // success - table1

        lua_pushstring(L, "GPSAltitude"); // success - table1 - name
        lua_createtable(L, 0, 3); // success - table1 - name - table2
        for (int i = 0; i < 3; ++i) {
            lua_pushinteger(L, i); // success - table1 - name - table2 - i
            lua_pushnumber(L, (double) altitude[i].first / (double) altitude[i].second); // success - table1 - name - table2 - i - latitude
            lua_rawset(L, -3); // success - table1 - name - table2
        }
        lua_rawset(L, -3); // success - table1

        lua_pushstring(L, "GPSTimeStamp"); // success - table1 - name
        lua_createtable(L, 0, 3); // success - table1 - name - table2
        for (int i = 0; i < 3; ++i) {
            lua_pushinteger(L, i); // success - table1 - name - table2 - i
            lua_pushnumber(L, (double) timestamp[i].first / (double) timestamp[i].second); // success - table1 - name - table2 - i - latitude
            lua_rawset(L, -3); // success - table1 - name - table2
        }

        lua_pushstring(L, "GPSSpeedRef"); // success - table1 - name
        lua_pushfstring(L, "%c", speedref); // success - table1 - name - value
        lua_rawset(L, -3); // success - table1

        lua_pushstring(L, "GPSSpeed"); // success - table1 - name
        lua_pushnumber(L, (double) speed.first / (double) speed.second); // success - table1 - name - value
        lua_rawset(L, -3); // success - table1

        lua_pushstring(L, "GPSTrackRef"); // success - table1 - name
        lua_pushfstring(L, "%c", trackref); // success - table1 - name - value
        lua_rawset(L, -3); // success - table1

        lua_pushstring(L, "GPSTrack"); // success - table1 - name
        lua_pushnumber(L, (double) track.first / (double) track.second); // success - table1 - name - value
        lua_rawset(L, -3); // success - table1

        lua_pushstring(L, "GPSImgDirectionRef"); // success - table1 - name
        lua_pushfstring(L, "%c", directionref); // success - table1 - name - value
        lua_rawset(L, -3); // success - table1

        lua_pushstring(L, "GPSImgDirection"); // success - table1 - name
        lua_pushnumber(L, (double) direction.first / (double) direction.second); // success - table1 - name - value
        lua_rawset(L, -3); // success - table1

        lua_rawset(L, -3); // success - table1

        lua_pushstring(L, "GPSDestLatitudeRef"); // success - table1 - name
        lua_pushfstring(L, "%c", destlatref); // success - table1 - name - value
        lua_rawset(L, -3); // success - table1

        lua_pushstring(L, "GPSDestLatitude"); // success - table1 - name
        lua_createtable(L, 0, 3); // success - table1 - name - table2
        for (int i = 0; i < 3; ++i) {
            lua_pushinteger(L, i); // success - table1 - name - table2 - i
            lua_pushnumber(L, (double) destlatitude[i].first / (double) destlatitude[i].second); // success - table1 - name - table2 - i - latitude
            lua_rawset(L, -3); // success - table1 - name - table2
        }
        lua_rawset(L, -3); // success - table1

        lua_pushstring(L, "GPSDestLongitudeRef"); // success - table1 - name
        lua_pushfstring(L, "%c", destlongituderef); // success - table1 - name - value
        lua_rawset(L, -3); // success - table1

        lua_pushstring(L, "GPSDestLongitude"); // success - table1 - name
        lua_createtable(L, 0, 3); // success - table1 - name - table2
        for (int i = 0; i < 3; ++i) {
            lua_pushinteger(L, i); // success - table1 - name - table2 - i
            lua_pushnumber(L, (double) destlongitude[i].first / (double) destlongitude[i].second); // success - table1 - name - table2 - i - latitude
            lua_rawset(L, -3); // success - table1 - name - table2
        }
        lua_rawset(L, -3); // success - table1

        lua_pushstring(L, "GPSDestBearingRef"); // success - table1 - name
        lua_pushfstring(L, "%c", destbearingref); // success - table1 - name - value
        lua_rawset(L, -3); // success - table1

        lua_pushstring(L, "GPSDestBearing"); // success - table1 - name
        lua_pushnumber(L, (double) destbearing.first / (double) destbearing.second); // success - table1 - name - value
        lua_rawset(L, -3); // success - table1

        lua_pushstring(L, "GPSDestDistanceRef"); // success - table1 - name
        lua_pushfstring(L, "%c", destdistanceref); // success - table1 - name - value
        lua_rawset(L, -3); // success - table1

        lua_pushstring(L, "GPSDestDistance"); // success - table1 - name
        lua_pushnumber(L, (double) destdistance.first / (double) destdistance.second); // success - table1 - name - value
        lua_rawset(L, -3); // success - table1

        lua_pushstring(L, "GPSHPositioningError"); // success - table1 - name
        lua_pushnumber(L, (double) positioningerror.first / (double) positioningerror.second); // success - table1 - name - value
        lua_rawset(L, -3); // success - table1

        return 2;
    }

        /*!
         * Lua usage:
         *    SipiImage.mimetype_consistency(img, "image/jpeg", "myfile.jpg")
         */
    static int SImage_mimetype_consistency(lua_State *L) {
        int top = lua_gettop(L);

        // three arguments are expected
        if (top != 3) {
            lua_pop(L, top); // clear stack
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.mimetype_consistency(): Incorrect number of arguments");
            return 2;
        }

        // get pointer to SImage
        SImage *img = checkSImage(L, 1);

        // get name of uploaded file
        std::string *path = img->filename;

        // get the indicated mimetype and the original filename
        const char *given_mimetype = lua_tostring(L, 2);
        const char *given_filename = lua_tostring(L, 3);

        lua_pop(L, top); // clear stack

        // do the consistency check

        bool check;

        try {
            check = shttps::Parsing::checkMimeTypeConsistency(*path, given_filename, given_mimetype);
        } catch (SipiImageError &err) {
            lua_pushboolean(L, false);
            std::stringstream ss;
            ss << "SipiImage.mimetype_consistency(): " << err;
            lua_pushstring(L, ss.str().c_str());
            return 2;
        }

        lua_pushboolean(L, true); // status
        lua_pushboolean(L, check); // result

        return 2;

    }
    //=========================================================================

    /*!
     * SipiImage.crop(img, <iiif-region>)
     */
    static int SImage_crop(lua_State *L) {
        SImage *img = checkSImage(L, 1);
        int top = lua_gettop(L);

        if (!lua_isstring(L, 2)) {
            lua_pop(L, top);
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.crop(): Incorrect number of arguments");
            return 2;
        }

        const char *regionstr = lua_tostring(L, 2);
        lua_pop(L, top);
        std::shared_ptr<SipiRegion> reg;

        try {
            reg = std::make_shared<SipiRegion>(regionstr);
        } catch (SipiError &err) {
            lua_pushboolean(L, false);
            std::stringstream ss;
            ss << "SipiImage.crop(): " << err;
            lua_pushstring(L, ss.str().c_str());
            return 2;
        }

        img->image->crop(reg); // can not throw exception!

        lua_pushboolean(L, true);
        lua_pushnil(L);

        return 2;
    }
    //=========================================================================

    /*!
    * SipiImage.scale(img, sizestring)
    */
    static int SImage_scale(lua_State *L) {
        SImage *img = checkSImage(L, 1);
        int top = lua_gettop(L);

        if (!lua_isstring(L, 2)) {
            lua_pop(L, top);
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.scale(): Incorrect number of arguments");
            return 2;
        }

        const char *sizestr = lua_tostring(L, 2);
        lua_pop(L, top);
        size_t nx, ny;

        try {
            SipiSize size(sizestr);
            int r;
            bool ro;
            size.get_size(img->image->getNx(), img->image->getNy(), nx, ny, r, ro);
        } catch (SipiError &err) {
            lua_pushboolean(L, false);
            std::stringstream ss;
            ss << "SipiImage.scale(): " << err;
            lua_pushstring(L, ss.str().c_str());
            return 2;
        }

        img->image->scale(nx, ny);

        lua_pushboolean(L, true);
        lua_pushnil(L);

        return 2;
    }
    //=========================================================================

    /*!
    * SipiImage.rotate(img, number, boolean)
    * <img>:rotate(number, boolean)
    */
    static int SImage_rotate(lua_State *L) {
        int top = lua_gettop(L);

        if ((top < 2) || (top > 3)) {
            lua_pop(L, top);
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.rotate(): Incorrect number of arguments");
            return 2;
        }

        SImage *img = checkSImage(L, 1);

        if (!lua_isnumber(L, 2)) {
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.rotate(): Incorrect  arguments");
            return 2;
        }

        float angle = lua_tonumber(L, 2);
        bool mirror{false};
        if (top == 3) {
            if (!lua_isboolean (L, 3)) {
                lua_pushboolean(L, false);
                lua_pushstring(L, "IIIFImage.rotate(): Incorrect  argument for mirror");
                return 2;
            }
            mirror = lua_toboolean(L, 3);
        }
        lua_pop(L, top);

        img->image->rotate(angle, mirror); // does not throw an exception!

        lua_pushboolean(L, true);
        lua_pushnil(L);

        return 2;
    }
    //=========================================================================

    /*!
     * success = SipiImage.topleft(img)
     * success = <img>:topleft()
     *
     * @param L Lua interpreter
     * @return Always 2 (one param, success, on stack)
     */
    static int SImage_set_topleft(lua_State *L) {
        int top = lua_gettop(L);

        if (top != 1) {
            lua_pop(L, top);
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.topleft(): Incorrect number of arguments");
            return 2;
        }

        SImage *img = checkSImage(L, 1);

        Orientation orientation = img->image->getOrientation();
        std::shared_ptr<SipiExif> exif = img->image->getExif();
        if (exif != nullptr) {
            unsigned short ori;
            if (exif->getValByKey("Exif.Image.Orientation", ori)) {
                orientation = static_cast<Orientation>(ori);
            }
        }

        img->image->set_topleft();
        img->image->setOrientation(TOPLEFT);

        lua_pop(L, lua_gettop(L));
        lua_pushboolean(L, true);
        return 1;
    }

    /*!
     * SipiImage.watermark(img, <wm-file>)
     */
    static int SImage_watermark(lua_State *L) {
        int top = lua_gettop(L);

        SImage *img = checkSImage(L, 1);

        if (!lua_isstring(L, 2)) {
            lua_pop(L, top);
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.watermark(): Incorrect arguments");
            return 2;
        }

        const char *watermark = lua_tostring(L, 2);
        lua_pop(L, top);

        try {
            img->image->add_watermark(watermark);
        } catch (SipiImageError &err) {
            lua_pushboolean(L, false);
            std::stringstream ss;
            ss << "SipiImage.watermark(): " << err;
            lua_pushstring(L, ss.str().c_str());
            return 2;
        }

        lua_pushboolean(L, true);
        lua_pushnil(L);

        return 2;
    }
    //=========================================================================


    /*!
     * SipiImage.write(img, <filepath> [, compression_parameter])
     */
    static int SImage_write(lua_State *L) {

        SImage *img = checkSImage(L, 1);

        if (!lua_isstring(L, 2)) {
            lua_pop(L, lua_gettop(L));
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.write(): Incorrect arguments");
            return 2;
        }

        const char *imgpath = lua_tostring(L, 2);

        Sipi::SipiCompressionParams comp_params;
        if (lua_gettop(L) > 2) { // we do have compressing parameters
            if (lua_istable(L, 3)) {
                lua_pushnil(L);
                while (lua_next(L, 3) != 0) {
                    if (!lua_isstring(L, -2)) {
                        lua_pop(L, lua_gettop(L));
                        lua_pushboolean(L, false);
                        lua_pushstring(L, "SipiImage.write(): Incorrect compression value name: Must be string!");
                        return 2;
                    }
                    const char *key = lua_tostring(L, -2);
                    if (!lua_isstring(L, -1)) {
                        lua_pop(L, lua_gettop(L));
                        lua_pushboolean(L, false);
                        lua_pushstring(L, "SipiImage.write(): Incorrect compression value: Must be string!");
                        return 2;
                    }
                    const char *tmpvalue = lua_tostring(L, -1);
                    std::string value(tmpvalue);
                    if (key == std::string("Sprofile")) {
                        std::set<std::string> validvalues {"PROFILE0", "PROFILE1", "PROFILE2", "PART2",
                                                           "CINEMA2K", "CINEMA4K", "BROADCAST", "CINEMA2S", "CINEMA4S",
                                                           "CINEMASS", "IMF"};
                        if (validvalues.find(value) != validvalues.end()) {
                            comp_params[Sipi::J2K_Sprofile] = value;
                        } else {
                            lua_pop(L, lua_gettop(L));
                            lua_pushstring(L, "SipiImage.write(): invalid Sprofile!");
                            return lua_error(L);
                        }
                    } else if (key == std::string("Creversible")) {
                        if (value == "yes" || value == "no") {
                            comp_params[Sipi::J2K_Creversible] = value;
                        } else {
                            lua_pop(L, lua_gettop(L));
                            lua_pushstring(L, "SipiImage.write(): invalid Creversible!");
                            return lua_error(L);
                        }
                    } else if (key == std::string("Clayers")) {
                        try {
                            int i = std::stoi(value);
                            std::stringstream ss;
                            ss << i;
                            value = ss.str();
                        } catch (std::invalid_argument) {
                            lua_pop(L, lua_gettop(L));
                            lua_pushstring(L, "SipiImage.write(): invalid Clayers!");
                            return lua_error(L);
                        } catch(std::out_of_range) {
                            lua_pop(L, lua_gettop(L));
                            lua_pushstring(L, "SipiImage.write(): invalid Clayers!");
                            return lua_error(L);
                        }
                        comp_params[Sipi::J2K_Clayers] = value;
                    } else if (key == std::string("Clevels")) {
                        try {
                            int i = std::stoi(value);
                            std::stringstream ss;
                            ss << i;
                            value = ss.str();
                        } catch (std::invalid_argument) {
                            lua_pop(L, lua_gettop(L));
                            lua_pushstring(L, "SipiImage.write(): invalid Clevels!");
                            return lua_error(L);
                        } catch(std::out_of_range) {
                            lua_pop(L, lua_gettop(L));
                            lua_pushstring(L, "SipiImage.write(): invalid Clevels!");
                            return lua_error(L);
                        }
                        comp_params[Sipi::J2K_Clevels] = value;
                    } else if (key == std::string("Corder")) {
                        std::set<std::string> validvalues = {"LRCP", "RLCP", "RPCL", "PCRL", "CPRL"};
                        if (validvalues.find(value) != validvalues.end()) {
                            comp_params[Sipi::J2K_Corder] = value;
                        } else {
                            lua_pop(L, lua_gettop(L));
                            lua_pushstring(L, "SipiImage.write(): invalid Corder!");
                            return lua_error(L);
                        }
                    } else if (key == std::string("Cprecincts")) {
                        comp_params[Sipi::J2K_Cprecincts] = value;
                    } else if (key == std::string("Cblk")) {
                        comp_params[Sipi::J2K_Cblk] = value;
                    } else if (key == std::string("Cuse_sop")) {
                        if (value == "yes" || value == "no") {
                            comp_params[Sipi::J2K_Cuse_sop] = value;
                        } else {
                            lua_pop(L, lua_gettop(L));
                            lua_pushstring(L, "SipiImage.write(): invalid Cuse_sop!");
                            return lua_error(L);
                          }
                    } else if (key == std::string("rates")) {
                        comp_params[Sipi::J2K_rates] = value;
                    } else if (key == std::string("quality")) {
                        comp_params[Sipi::JPEG_QUALITY] = value;
                    } else {
                        lua_pop(L, lua_gettop(L));
                        lua_pushstring(L, "SipiImage.write(): invalid compression parameter!");
                        return lua_error(L);
                    }
                    lua_pop(L, 1);
                }
            }
        }

        std::string filename = imgpath;
        size_t pos_ext = filename.find_last_of(".");
        size_t pos_start = filename.find_last_of("/");
        std::string dirpath;
        std::string basename;
        std::string extension;

        if (pos_start == std::string::npos) {
            pos_start = 0;
        } else {
            dirpath = filename.substr(0, pos_start);
        }

        if (pos_ext != std::string::npos) {
            basename = filename.substr(pos_start, pos_ext - pos_start);
            extension = filename.substr(pos_ext + 1);
        } else {
            basename = filename.substr(pos_start);
        }

        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        std::string ftype;

        if ((extension == "tif") || (extension == "tiff")) {
            ftype = "tif";
        } else if ((extension == "jpg") || (extension == "jpeg")) {
            ftype = "jpg";
        } else if (extension == "png") {
            ftype = "png";
        } else if ((extension == "j2k") || (extension == "jpx") || (extension == "jp2")) {
            ftype = "jpx";
        } else {
            lua_pop(L, lua_gettop(L));
            lua_pushstring(L, "SipiImage.write(): unsupported file format");
            return lua_error(L);
        }

        if ((basename == "http") || (basename == "HTTP")) {
            lua_getglobal(L, shttps::luaconnection); // push onto stack
            shttps::Connection *conn = (shttps::Connection *) lua_touserdata(L, -1); // does not change the stack
            lua_remove(L, -1); // remove from stack
            img->image->connection(conn);
            try {
                img->image->write(ftype, "HTTP", comp_params.size() > 0 ? &comp_params : nullptr);
            } catch (SipiImageError &err) {
                lua_pop(L, lua_gettop(L));
                lua_pushboolean(L, false);
                lua_pushstring(L, err.to_string().c_str());
                return 2;
            }
        } else {
            try {
                img->image->write(ftype, imgpath, comp_params.size() > 0 ? &comp_params : nullptr);
            } catch (SipiImageError &err) {
                lua_pop(L, lua_gettop(L));
                lua_pushboolean(L, false);
                lua_pushstring(L, err.to_string().c_str());
                return 2;
            }
        }

        lua_pop(L, lua_gettop(L));

        lua_pushboolean(L, true);
        lua_pushstring(L, imgpath);
        //lua_pushnil(L);

        return 2;
    }
    //=========================================================================


    /*!
    * SipiImage.send(img, <format>)
    */
    static int SImage_send(lua_State *L) {
        int top = lua_gettop(L);
        SImage *img = checkSImage(L, 1);

        if (!lua_isstring(L, 2)) {
            lua_pop(L, top);
            lua_pushboolean(L, false);
            lua_pushstring(L, "SipiImage.send(): Incorrect arguments");
            return 2;
        }

        const char *ext = lua_tostring(L, 2);
        lua_pop(L, top);

        std::string extension = ext;
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        std::string ftype;

        if ((extension == "tif") || (extension == "tiff")) {
            ftype = "tif";
        } else if ((extension == "jpg") || (extension == "jpeg")) {
            ftype = "jpg";
        } else if (extension == "png") {
            ftype = "png";
        } else if ((extension == "j2k") || (extension == "jpx")) {
            ftype = "jpx";
        } else {
            lua_pushstring(L, "SipiImage.send(): unsupported file format");
            return lua_error(L);
        }

        lua_getglobal(L, shttps::luaconnection); // push onto stack
        shttps::Connection *conn = (shttps::Connection *) lua_touserdata(L, -1); // does not change the stack
        lua_remove(L, -1); // remove from stack

        img->image->connection(conn);

        try {
            img->image->write(ftype, "HTTP");
        } catch (SipiImageError &err) {
            lua_pushboolean(L, false);
            lua_pushstring(L, err.to_string().c_str());
            return 2;
        }

        lua_pushboolean(L, true);
        lua_pushnil(L);

        return 2;
    }
    //=========================================================================

    // map the method names exposed to Lua to the names defined here
    static const luaL_Reg SImage_methods[] = {{"new",                  SImage_new},
                                              {"dims",                 SImage_dims}, // #myimg
                                              {"exif",                 SImage_get_exif}, // myimg
                                              {"gps",                  SImage_get_exifgps},
                                              {"write",                SImage_write}, // myimg >> filename
                                              {"send",                 SImage_send}, // myimg
                                              {"crop",                 SImage_crop}, // myimg - "100,100,500,500"
                                              {"scale",                SImage_scale}, // myimg % "500,"
                                              {"rotate",               SImage_rotate}, // myimg * 45.0
                                              {"topleft",              SImage_set_topleft},
                                              {"watermark",            SImage_watermark}, // myimg + "wm-path"
                                              {"mimetype_consistency", SImage_mimetype_consistency},
                                              {0,                      0}};
    //=========================================================================

    static int SImage_gc(lua_State *L) {
        SImage *img = toSImage(L, 1);
        delete img->image;
        delete img->filename;
        return 0;
    }
    //=========================================================================

    static int SImage_tostring(lua_State *L) {
        SImage *img = toSImage(L, 1);
        std::stringstream ss;
        ss << "File: " << *(img->filename);
        ss << *(img->image);
        lua_pushstring(L, ss.str().c_str());
        return 1;
    }
    //=========================================================================

    static const luaL_Reg SImage_meta[] = {{"__gc",       SImage_gc},
                                           {"__tostring", SImage_tostring},
                                           {0,            0}};
    //=========================================================================


    void sipiGlobals(lua_State *L, shttps::Connection &conn, void *user_data) {
        SipiHttpServer *server = (SipiHttpServer *) user_data;

        //
        // filesystem functions
        //
        lua_newtable(L); // table
        luaL_setfuncs(L, cache_methods, 0);
        lua_setglobal(L, "cache");

        lua_newtable(L); // table
        luaL_setfuncs(L, helper_methods, 0);
        lua_setglobal(L, "helper");

        lua_getglobal(L, SIMAGE);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            lua_newtable(L);
        }
        // stack:  table(SIMAGE)

        luaL_setfuncs(L, SImage_methods, 0);
        // stack:  table(SIMAGE)

        luaL_newmetatable(L, SIMAGE); // create metatable, and add it to the Lua registry
        // stack: table(SIMAGE) - metatable

        luaL_setfuncs(L, SImage_meta, 0);
        lua_pushliteral(L, "__index");
        // stack: table(SIMAGE) - metatable - "__index"

        lua_pushvalue(L, -3); // dup methods table
        // stack: table(SIMAGE) - metatable - "__index" - table(SIMAGE)

        lua_rawset(L, -3); // metatable.__index = methods
        // stack: table(SIMAGE) - metatable

        lua_pushliteral(L, "__metatable");
        // stack: table(SIMAGE) - metatable - "__metatable"

        lua_pushvalue(L, -3); // dup methods table
        // stack: table(SIMAGE) - metatable - "__metatable" - table(SIMAGE)

        lua_rawset(L, -3);
        // stack: table(SIMAGE) - metatable

        lua_pop(L, 1); // drop metatable
        // stack: table(SIMAGE)

        lua_setglobal(L, SIMAGE);
        // stack: empty

        lua_pushlightuserdata(L, server);
        lua_setglobal(L, sipiserver);

    }
    //=========================================================================

} // namespace Sipi
