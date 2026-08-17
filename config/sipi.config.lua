--
-- Copyright © 2016 Lukas Rosenthaler, Andrea Bianco, Benjamin Geer,
-- Ivan Subotic, Tobias Schweizer, André Kilchenmann, and André Fatton.
-- This file is part of Sipi.
-- Sipi is free software: you can redistribute it and/or modify
-- it under the terms of the GNU Affero General Public License as published
-- by the Free Software Foundation, either version 3 of the License, or
-- (at your option) any later version.
-- Sipi is distributed in the hope that it will be useful,
-- but WITHOUT ANY WARRANTY; without even the implied warranty of
-- MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
-- Additional permission under GNU AGPL version 3 section 7:
-- If you modify this Program, or any covered work, by linking or combining
-- it with Kakadu (or a modified version of that library) or Adobe ICC Color
-- Profiles (or a modified version of that library) or both, containing parts
-- covered by the terms of the Kakadu Software Licence or Adobe Software Licence,
-- or both, the licensors of this Program grant you additional permission
-- to convey the resulting work.
-- You should have received a copy of the GNU Affero General Public
-- License along with Sipi.  If not, see <http://www.gnu.org/licenses/>.
--
sipi = {
    --
    -- port number the server is listening to. If SIPI is running on a dedicated system, this should
    -- be set to 80
    --
    port = 1024,

    --
    -- `hostname` and `ssl_port` are not used by the server itself; they are only
    -- exposed to Lua route scripts via the `config` table (e.g. to build absolute
    -- preview URLs). Defaults: hostname = "localhost", ssl_port = -1.
    --
    hostname = 'localhost',
    ssl_port = 1025,

    --
    -- Worker threads and the wait-queue in front of the pool are CLI/env knobs,
    -- not config-file keys: --nthreads/SIPI_NTHREADS (0/unset = auto-detect from
    -- CPU cores), --max-waiting/SIPI_MAX_WAITING, --queue-timeout/SIPI_QUEUE_TIMEOUT.
    --

    --
    -- SIPI is using libjpeg to generate the JPEG images. libjpeg requires a quality value which
    -- corresponds to the compression rate. 100 is (almost) no compression and best quality, 0
    -- would be full compression and no quality. Reasonable values are between 30 and 95...
    --
    jpeg_quality = 60,

    --
    -- For scaling images, SIPI offers two methods. The value "high" offers best quality using expensive
    -- algorithms (bilinear interpolation, if downscaling the image is first scaled up to an integer
    -- multiple of the requires size, and then downscaled using averaging. This results in the best
    -- image quality. "medium" uses bilinear interpolation but does not do upscaling before
    -- downscaling. Scaling quality is set to "low", then just a lookup table and nearest integer
    -- interpolation is being used to scale the images.
    -- Recognized values are: "high", "medium", "low".
    --
    scaling_quality = {
        jpeg = "medium",
        tiff = "high",
        png = "high",
        j2k = "high"
    },

    --
    -- Maximal size of a post request.
    --
    max_post_size = '300M',

    --
    -- indicates the path to the root of the image directory. Depending on the settings of the variable
    -- "prefix_as_path" the images are searched at <imgroot>/<prefix>/<imageid> (prefix_as_path = TRUE)
    -- or <imgroot>/<imageid> (prefix_as_path = true). Please note that "prefix" and "imageid" are
    -- expected to be urlencoded. Both will be decoded. That is, "/" will be recognized and expanded
    -- in the final path of the image file.
    --
    -- To use Sipi's test data, use the following imgroot, and set prefix_as_path to true below:
    -- imgroot = './test/_test_data/images',
    --
    imgroot = './images',

    --
    -- If true, the IIIF prefix is used to build the path to the image files.
    --
    prefix_as_path = true,

    --
    -- Lua script which is executed on initialization of the Lua interpreter
    --
    initscript = './config/sipi.init.lua',

    --
    -- path to the caching directory (auto-created if missing)
    --
    cache_dir = './cache',

    --
    -- maximum cache size: '-1' = unlimited, '0' = disabled, or e.g. '200M', '1G'
    -- Eviction triggers at 100% and purges down to 80% (low-water mark)
    --
    cache_size = '20M',

    --
    -- maximum number of files to cache (0 = no file count limit)
    -- Eviction triggers when either size or file count limit is reached
    --
    cache_nfiles = 8,

    --
    -- Path to the directory where the scripts for the routes defined below are to be found
    --
    scriptdir = './scripts',

    ---
    --- Size of the thumbnails (to be used within Lua)
    ---
    thumb_size = '!128,128',

    --
    -- Path to the temporary directory
    --
    tmpdir = '/tmp',

    --
    -- The maximum allowed age of temporary files (in seconds) before they are deleted. Defaults to one day.
    --
    max_temp_file_age = 86400,

    --
    -- The secret for generating JWT's (JSON Web Tokens) (exactly 42 characters)
    --
    jwt_secret = 'UP 4888, nice 4-8-4 steam engine',
    --            123456789012345678901234567890123456789012

    --
    -- The engine log level is a CLI/env (or Rust TOML config) setting, not a Lua
    -- config key: --loglevel / SIPI_LOGLEVEL, one of "DEBUG", "INFO", "NOTICE",
    -- "WARNING", "ERR", "CRIT", "ALERT", "EMERG" (default "INFO").
    --

    --
    -- The two-lane admission knobs are CLI/env (or Rust TOML config) settings,
    -- not Lua config keys:
    --   memory_limit       --memory-limit / SIPI_MEMORY_LIMIT (0 = auto-detect RAM)
    --   tiles_memory_ratio --tiles-memory-ratio / SIPI_TILES_MEMORY_RATIO (0.25)
    --   admission_mode     --admission-mode / SIPI_ADMISSION_MODE (basic | advanced)
    --
}

admin = {
    --
    -- username of admin user
    --
    user = 'admin',

    --
    -- Administration password
    --
    password = 'Sipi-Admin'
}

fileserver = {
    --
    -- directory on disk where the documents for the normal webserver are located
    --
    docroot = './server',

    --
    -- URL route under which the normal webserver should respond to requests
    --
    wwwroute = '/server'
}

--
-- here we define routes that are handled by lua scripts. A route is a defined url:
-- http://<server-DNS>/<route>
-- executes the given script defined below
--
routes = {
    {
        method = 'POST',
        route = '/api/upload',
        script = 'upload.lua'
    },
    {
        method = 'GET',
        route = '/api/token',
        script = 'token.lua'
    },
    {
        method = 'GET',
        route = '/test/orientation',
        script = 'orientation.lua'
    }
}
