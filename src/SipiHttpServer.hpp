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
 * This file implements a Webserver using mongoose (See \url https://github.com/cesanta/mongoose)
 *
 * {scheme}://{server}{/prefix}/{identifier}/{region}/{size}/{rotation}/{quality}.{format}
 *
 * We support cross domain scripting (CORS according to \url http://www.html5rocks.com/en/tutorials/cors/)
 */
#ifndef __defined_sipihttp_server_h
#define __defined_sipihttp_server_h

#include <string>
#include <sys/types.h>
#include <unistd.h>
#include <memory>

#include "shttps/Server.h"
#include "iiifparser/SipiRegion.h"
#include "iiifparser/SipiSize.h"
#include "iiifparser/SipiRotation.h"
#include "iiifparser/SipiQualityFormat.h"
#include "SipiCache.h"

#include "lua.hpp"
#include "SipiIO.h"


namespace Sipi
{
    /*!
     * The class SipiHttpServer implements a webserver that can be used to serve images using the IIIF
     * API. For details on the API look for  \url http://iiif.io . I implemented support for
     * cross domain scripting (CORS according to \url http://www.html5rocks.com/en/tutorials/cors/). As a
     * special feature we support acces to the old PHP-based salsah version (this is a bad hack!)
     */
    class SipiHttpServer : public shttps::Server
    {
    private:
    protected:
        pid_t _pid{};
        std::string _imgroot;
        std::string _salsah_prefix;
        bool _prefix_as_path{};
        std::vector<std::string> _dirs_to_exclude;
        //!< Directories which should have no subdirs even if subdirs are enabled
        std::string _logfile;
        std::shared_ptr<SipiCache> _cache;
        int _jpeg_quality{};
        std::unordered_map<std::string, SipiCompressionParams> _j2k_compression_profiles;
        ScalingQuality _scaling_quality{};

    public:
        /*!
         * Constructor which automatically starts the server
         *
         * \param port_p Portnumber on which the server should listen
         * \param nthreads_p Number of threads to be used for the server
         * \param userid_str Userid under which the server should run
         * \param logfile_p Name of the logfile
         * \param loglevel_p Loglevel (DEBUG, INFO, WARNING, ERROR, CRITICAL)
         */
        explicit SipiHttpServer(int port_p, size_t nthreads_p = 4, std::string userid_str = "",
                                const std::string& logfile_p = "sipi.log", const std::string& loglevel_p = "DEBUG");

        void run() override;

        static std::pair<std::string, std::string>
        get_canonical_url(size_t img_w, size_t img_h, const std::string& host, const std::string& prefix,
                          const std::string& identifier, std::shared_ptr<SipiRegion> region,
                          std::shared_ptr<SipiSize> size, SipiRotation& rotation,
                          SipiQualityFormat& quality_format, int pagenum = 0);


        pid_t pid() const { return _pid; }

        void imgroot(const std::string& imgroot_p) { _imgroot = imgroot_p; }

        std::string imgroot() { return _imgroot; }

        std::string salsah_prefix() { return _salsah_prefix; }

        void salsah_prefix(const std::string& salsah_prefix) { _salsah_prefix = salsah_prefix; }

        bool prefix_as_path() const { return _prefix_as_path; }

        void prefix_as_path(bool prefix_as_path_p) { _prefix_as_path = prefix_as_path_p; }

        std::vector<std::string> dirs_to_exclude() { return _dirs_to_exclude; }

        void dirs_to_exclude(const std::vector<std::string>& dirs_to_exclude) { _dirs_to_exclude = dirs_to_exclude; }

        void jpeg_quality(int jpeg_quality_p) { _jpeg_quality = jpeg_quality_p; }

        void j2k_compression_profiles(
            const std::unordered_map<std::string, SipiCompressionParams>& j2k_compression_profiles)
        {
            _j2k_compression_profiles = j2k_compression_profiles;
        }

        int jpeg_quality() const { return _jpeg_quality; }


        void scaling_quality(std::map<std::string, std::string> jpeg_quality_p)
        {
            if (jpeg_quality_p["jpk"] == "high")
            {
                _scaling_quality.jk2 = HIGH;
            }
            else if (jpeg_quality_p["jpk"] == "medium")
            {
                _scaling_quality.jk2 = MEDIUM;
            }
            else if (jpeg_quality_p["jpk"] == "low")
            {
                _scaling_quality.jk2 = LOW;
            }
            else
            {
                _scaling_quality.jk2 = HIGH;
            }

            if (jpeg_quality_p["jpeg"] == "high")
            {
                _scaling_quality.jpeg = HIGH;
            }
            else if (jpeg_quality_p["jpeg"] == "medium")
            {
                _scaling_quality.jpeg = MEDIUM;
            }
            else if (jpeg_quality_p["jpeg"] == "low")
            {
                _scaling_quality.jpeg = LOW;
            }
            else
            {
                _scaling_quality.jpeg = HIGH;
            }

            if (jpeg_quality_p["tiff"] == "high")
            {
                _scaling_quality.tiff = HIGH;
            }
            else if (jpeg_quality_p["tiff"] == "medium")
            {
                _scaling_quality.tiff = MEDIUM;
            }
            else if (jpeg_quality_p["tiff"] == "low")
            {
                _scaling_quality.tiff = LOW;
            }
            else
            {
                _scaling_quality.tiff = HIGH;
            }

            if (jpeg_quality_p["png"] == "high")
            {
                _scaling_quality.png = HIGH;
            }
            else if (jpeg_quality_p["png"] == "medium")
            {
                _scaling_quality.png = MEDIUM;
            }
            else if (jpeg_quality_p["png"] == "low")
            {
                _scaling_quality.png = LOW;
            }
            else
            {
                _scaling_quality.png = HIGH;
            }
        }

        ScalingQuality scaling_quality() const { return _scaling_quality; }

        void cache(const std::string& cachedir_p, size_t max_cachesize_p = 0, size_t max_nfiles_p = 0,
                   float cache_hysteresis_p = 0.1);

        std::shared_ptr<SipiCache> cache() { return _cache; }
    };
}

#endif
