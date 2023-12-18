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
/*!
 * \ Handler of various
 *
 */
#ifndef __sipi_conf_h
#define __sipi_conf_h

#include "shttps/LuaServer.h"
#include "shttps/Connection.h"

namespace Sipi {

    /*!
     * This class is used to read the sipi server configuration from
     * a Lua configuration file.
     */
    class SipiConf {
    private:
        std::string userid_str;
        std::string hostname;
        int port; //<! port number for server
        int ssl_port = -1;
        std::string ssl_certificate;
        std::string ssl_key;

        std::string img_root; //<! path to root of image repository
        int max_temp_file_age;
        int subdir_levels = -1;
        std::vector<std::string> subdir_excludes;
        bool prefix_as_path; //<! Use IIIF-prefix as part of path or ignore it...
        int jpeg_quality;
        std::map<std::string,std::string> scaling_quality;
        std::string init_script;
        std::string cache_dir;
        size_t cache_size;
        float cache_hysteresis;
        int keep_alive;
        std::string thumb_size;
        int cache_n_files;
        unsigned int n_threads;
        size_t max_post_size;
        std::string tmp_dir;
        std::string scriptdir;
        std::vector<shttps::LuaRoute> routes;
        std::string knora_path;
        std::string knora_port;
        std::string logfile;
        std::string loglevel;
        std::string docroot;
        std::string wwwroute;
        std::string jwt_secret;
        std::string adminuser;
        std::string password;

    public:
        SipiConf();

        SipiConf(shttps::LuaServer &luacfg);

        inline std::string getUseridStr(void) { return userid_str; }
        inline void setUseridStr(const std::string &str) {userid_str = str; };

        inline std::string getHostname(void) { return hostname; }
        inline void setHostname(const std::string &str) { hostname = str; }

        inline int getPort(void) { return port; }
        inline void setPort(int i) { port = i; }


        inline int getSSLPort(void) { return ssl_port; }
        inline void setSSLPort(int i) { ssl_port = i; }

        inline std::string getSSLCertificate(void) { return ssl_certificate; }
        inline void setSSLCertificate(const std::string &str) { ssl_certificate = str; }

        inline std::string getSSLKey(void) { return ssl_key; }
        inline void setSSLKey(const std::string &str) { ssl_key = str; }

        inline std::string getImgRoot(void) { return img_root; }
        inline void setImgRoot(const std::string &str) { img_root = str; }

        inline int getMaxTempFileAge(void) { return max_temp_file_age; }
        inline void setMaxTempFileAge(int i) { max_temp_file_age = i; }

        inline bool getPrefixAsPath(void) { return prefix_as_path; }
        inline void setPrefixAsPath(bool b) { prefix_as_path = b; }

        inline int getJpegQuality(void) { return jpeg_quality; }
        inline void setJpegQuality(int i) { jpeg_quality = i; }

        inline std::map<std::string,std::string> getScalingQuality(void) { return scaling_quality; }
        void inline setScalingQuality(const std::map<std::string,std::string> &v) { scaling_quality = v; }

        inline int getSubdirLevels(void) { return subdir_levels; }
        inline void setSubdirLevels(int i) { subdir_levels = i; }

        inline std::vector<std::string> getSubdirExcludes(void) { return subdir_excludes; }
        inline void setSubdirExcludes(const std::vector<std::string> &v) { subdir_excludes = v; }

        inline std::string getInitScript(void) { return init_script; }
        inline void setInitScript(const std::string &str) { init_script = str; }

        inline size_t getCacheSize(void) { return cache_size; }
        inline void setCacheSize(size_t i) { cache_size = i; }

        inline std::string getCacheDir(void) { return cache_dir; }
        inline void setCacheDir(const std::string &str) { cache_dir = str; }

        inline float getCacheHysteresis(void) { return cache_hysteresis; }
        inline void setCacheHysteresis(float f) { cache_hysteresis = f; }

        inline int getKeepAlive(void) { return keep_alive; }
        inline void setKeepAlive(int i) { keep_alive = i; }

        inline std::string getThumbSize(void) { return thumb_size; }
        inline void setThumbSize(const std::string &str) { thumb_size = str; }

        inline int getCacheNFiles(void) { return cache_n_files; }
        inline void setCacheNFiles(int i) { cache_n_files = i; }

        inline unsigned int getNThreads() const { return n_threads; }
        inline void setNThreads(const unsigned int i) { n_threads = i; }

        inline size_t getMaxPostSize(void) { return max_post_size; }
        inline void setMaxPostSize(size_t i) { max_post_size = i; }

        inline std::string getTmpDir(void) { return tmp_dir; }
        inline void setTmpDir(const std::string &str) { tmp_dir = str; }

        inline std::string getScriptDir(void) { return scriptdir; }
        inline void setScriptDir(const std::string &str) { scriptdir = str; }

        inline std::vector<shttps::LuaRoute> getRoutes(void) { return routes; }
        inline void seRoutes(const std::vector<shttps::LuaRoute> &r) { routes = r; }

        inline std::string getKnoraPath(void) { return knora_path; }
        inline void setKnoraPath(const std::string &str) { knora_path = str; }

        inline std::string getKnoraPort(void) { return knora_port; }
        inline void setKnoraPort(const std::string &str) { knora_port = str; }

        inline std::string getLoglevel(void) { return loglevel; }
        inline void setLogLevel(const std::string &str) { loglevel = str; }

        inline std::string getLogfile(void) { return logfile; }
        inline void setLogfile(const std::string &str) { logfile = str; }

        inline std::string getDocRoot(void) { return docroot; }
        inline void setDocRoot(const std::string &str) { docroot = str; }

        inline std::string getWWWRoute(void) { return wwwroute; }
        inline void setWWWRoute(const std::string &str) { wwwroute = str; }

        inline std::string getJwtSecret(void) { return jwt_secret; }
        inline void setJwtSecret(const std::string &str) { jwt_secret = str; }

        inline std::string getAdminUser(void) { return adminuser; }
        inline void setAdminUser(const std::string &str) { adminuser = str; }

        inline std::string getPassword(void) { return password; }
        inline void setPasswort(const std::string &str) { password = str; }
    };

}


#endif
