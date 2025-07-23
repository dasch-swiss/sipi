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
--
-- Configuration file for use with nginx to simulate Knora.
--
sipi = {
    --
    -- The user under which the Sipi server should run. Use this only if Sipi should setuid to a particular user after
    -- starting. Otherwise, leave this commented out. If this setting is used, Sipi must be started as root.
    --
    -- userid = '_www',

    --
    -- port number the server is listening to
    --
    port = 1024,

    --
    -- Number of threads to use
    --
    nthreads = 4,

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
    -- Number of seconds a connection (socket) remains open
    --
    keep_alive = 5,

    --
    -- Maximal size of a post request.
    --
    max_post_size = '300M',

    --
    -- indicates the path to the root of the image directory. Depending on the settings of the variable
    -- "prefix_as_path" the images are search at <imgroot>/<prefix>/<imageid> (prefix_as_path = TRUE)
    -- or <imgroot>/<imageid> (prefix_as_path = FALSE). Please note that "prefix" and "imageid" are
    -- expected to be urlencoded. Both will be decoded. That is, "/" will be recoignized and expanded
    -- in the final path the image file!
    --
    imgroot = './images', -- directory for Knora Sipi integration testing

    --
    -- If TRUE, the IIIF prefix is used to build the path to the image files.
    --
    prefix_as_path = true,

    --
    -- In order not to accumulate too many files into one directory (which slows down file
    -- access considerabely), the images are stored in recursive subdirectories 'A'-'Z'.
    -- If subdir_levels is equal 0, no subdirectories are used. The maximum is 6.
    -- The recommendation is that on average there should not be more than a few
    -- thousand files in a unix directory (your mileage may vary depending on the
    -- file system used).
    --
    subdir_levels = 0,

    --
    -- if subdir_levels is > 0 and if prefix_as_path is true, all prefixes will be
    -- regarded as directories under imgroot. Thus, the subdirs 'A'-'Z' will be
    -- created in these directories for the prefixes. However, it may make sense
    -- for certain prefixes *not* to use subdirs. A list of these prefix-directories
    -- can be given with this configuration parameter.
    --
    subdir_excludes = { "tmp", "thumb"},

    --
    -- Lua script which is executed on initialization of the Lua interpreter
    --
    initscript = './config/sipi.init-knora.lua',

    --
    -- path to the caching directory
    --
    cachedir = './cache',

    --
    -- maxcimal size of the cache
    --
    cachesize = '20M',

    --
    -- maximal number of files to be cached
    -- The cache will be purged if either the maximal size or maximal number
    -- of files is reached
    --
    cache_nfiles = 8,

    --
    -- if the cache becomes full, the given percentage of file space is marked for reuse
    --
    cache_hysteresis = 0.15,

    --
    -- Path to the directory where the scripts for the routes defined below are to be found
    --
    scriptdir = './scripts',

    ---
    --- Size of the thumbnails
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
    -- Path to Knora Application
    --
    knora_path = 'localhost',

    --
    -- Port of Knora Application
    --
    knora_port = '3434',

    --
    -- If compiled with SSL support, the port the server is listening for secure connections
    --
    ssl_port = 1025,

    --
    -- If compiled with SSL support, the path to the certificate (must be .pem file)
    -- The follow commands can be used to generate a self-signed certificate
    -- # openssl genrsa -out key.pem 2048
    -- # openssl req -new -key key.pem -out csr.pem
    -- #openssl req -x509 -days 365 -key key.pem -in csr.pem -out certificate.pem
    --
    ssl_certificate = './certificate/certificate.pem',

    --
    -- If compiled with SSL support, the path to the key file (see above to create)
    --
    ssl_key = './certificate/key.pem',


    --
    -- The secret for generating JWT's (JSON Web Tokens) (42 characters)
    --
    jwt_secret = 'UP 4888, nice 4-8-4 steam engine',
    --            12345678901234567890123456789012


    --
    -- Name of the logfile (a ".txt" is added...) !!! Currently not used, since logging
    -- is based on syslog !!!!
    --
    logfile = "sipi.log",

    --
    -- loglevel, one of "DEBUG", "INFO", "NOTICE", "WARNING", "ERR",
    -- "CRIT", "ALERT", "EMERG"
    --
    loglevel = "DEBUG"
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
        method = 'GET',
        route = '/read_write_lua',
        script = 'read_write_lua.lua'
    },
    {
        method = 'POST',
        route = '/make_thumbnail',
        script = 'make_thumbnail.lua'
    },
    {
        method = 'POST',
        route = '/convert_from_binaries',
        script = 'convert_from_binaries.lua'
    },
    {
        method = 'POST',
        route = '/convert_from_file',
        script = 'convert_from_file.lua'
    },
    {
        method = 'POST',
        route = '/Knora_login',
        script = 'Knora_login.lua'
    },
    {
        method = 'POST',
        route = '/Knora_logout',
        script = 'Knora_logout.lua'
    },
    {
        method = 'GET',
        route = '/test_functions',
        script = 'test_functions.lua'
    },
    {
        method = 'GET',
        route = '/test_clean_temp_dir',
        script = 'test_clean_temp_dir.lua'
    },
    {
        method = 'GET',
        route = '/test_mediatype',
        script = 'test_mediatype.lua'
    },
    {
        method = 'GET',
        route = '/test_knora_session_cookie',
        script = 'test_knora_session_cookie.lua'
    },
    {
        method = 'POST',
        route = '/api/upload',
        script = 'upload.lua'
    },
    {
        method = 'GET',
        route = '/test_mimetype_func',
        script = 'test_mimetype_func.lua'
    },
    {
        method = 'POST',
        route = '/api/mimetest',
        script = 'mimetest.lua'
    },
    {
        method = 'GET',
        route = '/sqlite',
        script = 'test_sqlite.lua'
    },
    {
        method = 'GET',
        route = '/test_orientation',
        script = 'test_orientation.lua'
    },
    {
        method = 'GET',
        route = '/test_exif_gps',
        script = 'test_exif_gps.lua'
    }
}
