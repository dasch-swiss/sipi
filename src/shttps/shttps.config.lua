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
-- it with Kakadu (or a modified version of that library), containing parts
-- covered by the terms of the Kakadu Software Licence, the licensors of this
-- Program grant you additional permission to convey the resulting work.
-- See the GNU Affero General Public License for more details.
-- You should have received a copy of the GNU Affero General Public
-- License along with Sipi.  If not, see <http://www.gnu.org/licenses/>.

--
-- Configuration parameters
--
shttps = {
    --
    -- user under which the sipi server should run. Don't set (comment out) this configuration variable
    -- if SIPI should use the user which launches SIPI.
    userid = 'rosenth',

    --
    -- The path to the root of the documents (.elua, .lua, image files etc.)
    docroot = './docroot',

    --
    -- Path to temporary directory used for uplaods etc.
    --
    tmpdir = '/tmp',

    --
    -- The maximum number of threads allowed
    --
    nthreads = 8,

    --
    -- Port number where server is listening to
    --
    port = 4711,

    --
    -- If compiled with SSL support, the port the server is listening for secure connections
    --
    ssl_port = 4712,

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
    -- The secret for generating JWT's (JSON Web Tokens)
    --
    jwt_secret = 'UP 4888, nice 4-8-4 steam engine',
    --            12345678901234567890123456789012


    --
    -- Path to the scripts directory, where Lua-scripts are used to process requests using
    -- given routes
    --
    scriptdir = './scripts'
}

--
-- Routes processed by Lua scripts in the "scriptdir"
--
routes = {
    {
        method = 'GET',
        route = '/luaexe/test1',
        script = 'test1.lua'
    },
    {
        method = 'GET',
        route = '/luaexe/test2',
        script = 'test2.elua'
    }

}
