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
#include "SipiConf.h"

namespace Sipi {
    SipiConf::SipiConf() { }

    SipiConf::SipiConf(shttps::LuaServer &luacfg) {
            img_root = luacfg.configString("sipi", "imgroot", ".");
            port = luacfg.configInteger("sipi", "port", 3333);
            init_script = luacfg.configString("sipi", "initscript", ".");
            cache_size = luacfg.configString("sipi", "cachesize", "");
            cache_dir = luacfg.configString("sipi", "cachedir", "");
            cache_hysteresis = luacfg.configFloat("sipi", "cache_hysteresis", 0.1);
            prefix_as_path = luacfg.configBoolean("sipi", "prefix_as_path", true);
            keep_alive = luacfg.configInteger("sipi", "keep_alive", 20);
            thumb_size = luacfg.configString("sipi", "thumb_size", "!128,128");
            cache_n_files = luacfg.configInteger("sipi", "cache_nfiles", 0);
            n_threads = luacfg.configInteger("sipi", "nthreads", std::thread::hardware_concurrency());
            tmp_dir = luacfg.configString("sipi", "tmpdir", "/tmp");
            scriptdir = luacfg.configString("sipi", "scriptdir", "./scripts");
            routes = luacfg.configRoute("routes");
            knora_path = luacfg.configString("sipi", "knora_path", "localhost");
            knora_port = luacfg.configString("sipi", "knora_port", "3333");
            docroot = luacfg.configString("fileserver", "docroot", "");
            docroute = luacfg.configString("fileserver", "docroute", "");

    }

}
