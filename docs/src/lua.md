# SIPI Lua Interface
SIPI has an embedded [LUA](http://www.lua.org) interpreter. LUA is a simple script language that was developped
specifically to be embedded into applications. For example the games [minecraft](https://www.minecraft.net) and
[World of Warcraft](https://worldofwarcraft.com/de-de/) make extensive use of LUA scripting for customization and programming extensions.

Each HTTP request to SIPI invokes a recent, independent
lua-instance (Version 5.3.5). Therefore, LUA may be used in the following contexts:

- Preflight function
- Embedded in HTML pages
- RESTful services using the SIPI routing

Each lua-instance in SIPI includes additional SIPI-specific information:

- global variables about the SIPI configuration
- information about the current HTTP request
- SIPI specific functions for
  - processing the request and send back information
  - getting image information and transforming images
  - querying and changing the SIPI runtime configuration (e.g. the cache)

In general, the SIPI LUA function make use that a Lua function's return value may consist of
more than one element (see [Multiple Results](http://www.lua.org/pil/5.3.html)):

Sipi provides the [LuaRocks](https://luarocks.org/)
package manager which must be used in the context of SIPI.

*The Lua interpreter in Sipi runs in a multithreaded environment: each
request runs in its own thread and has its own Lua interpreter.
Therefore, only Lua packages that are known to be thread-safe may be
used!*

## Preflight function
It is possible to define a LUA pre-flight function for *IIIF*-requests and independently one for *file*-requests
(indicated by a _/file_ postfix in the URL). Both are optional and are best located in the init-script (see
[configuarion options](../sipi/#setup-of-directories-needed) of SIPI). It is executed after the incoming
HTTP request data has been processed but before an action to respond to the request has been taken. It should
be noted that the pre-flight script is only executed for IIIF-specific requests (either using the IIIF URL-syntax or the
_/file_ postfix). All other HTTP requests are being directed to "normal" HTTP-server part of SIPI.
These can utilize the lua functionality by embedding LUA commands within the HTML.

### IIIF preflight function
The IIIF preflight function must have the name **pre_flight** with the following signature:

```lua
function pre_flight(prefix,identifier,cookie)

    return "allow", filepath
end
```
The preflight function takes 3 parameter:

- `prefix`: This is the prefix that is given on the IIIF url [mandatory]  
  *http(s)://{server}/__{prefix}__/{id}/{region}/{size}/{rotation}/{quality}.{format}*  
  Please note that the prefix may contain several "/" that can be used as path to the repository file
- `identifier`: The image identifier (which must not correspond to an actual filename in the media files repositoy
  of the SIPI IIIF server)
  [mandatory]
- `cookie`: A cookie containing authorization information. Usually the cookie contains a Json Web Token [optional]

The pre-flight function must return at least 2 parameters:

- `permission`: A string or a table indication the permission to read the image. In a simple case it's either the
  string `"allow"` or `"deny"`.  
  To allow more flexibility, the following permission tables are supported:  
    - Restricted access with watermark. The watermark must be a TIFF file with a single 8-bit channel (gray value
    image). For example:  
    `{ type = 'restrict', watermark = './wm/mywatermark.tif' }`
    - Restricted access with size limitation. The size must be a
      [IIIF size expression](https://iiif.io/api/image/3.0/#42-size). For example:  
     `{ type = 'restrict', size='!256,256' }`
    - SIPI also supports the [IIIF Authentification API](https://iiif.io/api/auth/1.0/). See section [IIIF
      Authentification]() on how to implement this feature in the pre-flight function.
- `filepath`: The path to the master image file in the media files repository. This path can be assembled using the
  `prefix` and `identifier` using any additional information (e.g. accessing a database or using the LUA restful client)
  
The most simple working pre-flight looks as follows assuming that the `identifier`is the name of the master image
file in the repository and the `prefix` is the path:
```lua
function pre_flight(prefix, identifier, cookie)
    if config.prefix_as_path then
        filepath = config.imgroot .. '/' .. prefix .. '/' .. identifier
    else
        filepath = config.imgroot .. '/' .. identifier
    end
    return 'allow', filepath
end
```
Above example preflight function allows all files to be served without restriction.

#### More complex example of preflight function 

The following example uses some SIPI lua funtions
to access an authorization server to check if the user (identified by a cookie) is allowed to see the specific image. We are
using [Json Web Tokens](https://jwt.io) (JWT) which are supported by SIPI specific LUA functions. Please note that the
SIPI JTW-functions support an arbitrary payload that has not to follow the JWT recommendations. In order to encode, the
JWT_ALG_HS256 is beeing used together with the key that is defined in the SIPI configuration as
[jwt_secret](../sipi/#jwt-secret).
```lua
function pre_flight(prefix, identifier, cookie) 
    --
    -- make up the file path
    --
    if config.prefix_as_path then
        filepath = config.imgroot .. '/' .. prefix .. '/' .. identifier
    else
        filepath = config.imgroot .. '/' .. identifier
    end
    --
    -- we need a cookie containing the user information that will be
    -- sent to the authorization server. In this
    -- example, the content does not follow the JWT rules
    -- (which is possible to pack any table into a JWT encoded token)
    --
    if cookie then
        --
        -- we decode the cookie in order to get a table of key/value pairs
        --
        success, userinfo = server.decode_jwt(cookie)
        if not success then
            return 'deny', filepath
        end
        --
        -- prepare the RESTful call to the authorization server
        --

        -- add the image identifier to the info table:
        userinfo["imgid"] = identifier
        -- encode the userinfo to a JWT-like token:
        local new_cookie = server.generate_jwt(userinfo) 
        local url = 'http://auth.institution.org/api/getauth/' .. identifier
        local auth_information = { Cookie = new_cookie }
        --
        -- make the HTTP request with a timeout of 500 ms
        --
        success, result = server.http('GET', url, auth_information, 500) 
        if success then
            --
            -- we got a response from the server
            --
            success, response_json = server.json_to_table(result.body)
            if success then  -- everything OK
                return {
                            type = response_json.type,
                            restriction = response_json.restriction
                        }, filepath
            else
                return 'deny', filepath
            end
        else
            return 'deny', filepath
        end
    else
        return 'deny', filepath
    end
end
```
Above example assumes that the cookie data is a string that contains encrypted user data from a table (key/value pair).
Jason Web Token. This token is decoded and the information about the image to be displayed is added. Then the information
is encoded as a new token that ist transmitted to the RESTful interface of the authentification server. The answer is
assumed to be json containing information about the type ('allow', 'deny', 'restrict') and the restriction settings.
The pre-flight function uses the following SIPI-specific LUA global variables and function:

- [config.imgroot](#configimgroot): (Global variable) Root directory of the image repository.
- [server.http()](#serverhttp): (Function) Used to create a RESTful GET request.
- [server.generate_jwt()](#servergenerate_jwt): (Function) Create a new JWT token from a key/value table.
- [server.json_to_table()](#serverjson_to_table): (function) Convert a JSON into a LUA table.

### File preflight function
An URL in the form ```http(s)://{server}/{prefix}/{identifier}/file``` serves the given file as binary object (including
propere mimetype in the header etc.). The file has to reside in the directory tree defined for IIIF requests. In these
cases, a preflight script name `file_pre_flight` is being called if defined. Its signature is as follows:
```lua
function file_pre_flight(filepath, cookie)

end
```
A simple example allowing access only to the file _"unit/test.csv"_ would be:
```lua
function file_pre_flight(filepath, cookie)
    if filepath == "./images/unit/test.csv" then
        return "allow", filepath
    else
        return "deny", ""
    end
end
```
This script would deny all other file access and the SIPI IIIF server responds with a `401 Unauthorized` error.

## LUA embedded in HTML
The HTTP server that is included in SIPI can serve any type of file which are just transfered as is to the client.
However, if a file has an extension of `.elua`, it is assumed to be a HTML file with embedded LUA code. ALL SIPI-specific
LUA functions and global variables are available.

Embedding works with the special tag `<lua>` and `</lua>`. All text between the opening and closing tag is interpreted
as LUA code. SIPI provides an extra LUA function to output data to the client ([server.print](#serverprint)). Thus, dynamic,
server-generated HTML may be created. A sample page that displays some information about the server configuration and
client info could like follows:
```html
<html>
    <head>
        <title>SIPI Configuration Info</title>
    </head>
    <body>
    <h1>SIPI Configuration Info</h1>
    <h2>Configuration variables</h2>
    <table>
        <tr>
            <td>imgroot</td>
            <td>:</td>
            <td><lua>server.print(config.imgroot)</lua></td>
        </tr>
        <tr>
            <td>docroot</td>
            <td>:</td>
            <td><lua>server.print(server.docroot)</lua></td>
        </tr>
        <tr>
            <td>hostname</td>
            <td>:</td>
            <td><lua>server.print(config.hostname)</lua></td>
        </tr>
        <tr>
            <td>scriptdir</td>
            <td>:</td>
            <td><lua>server.print(config.scriptdir)</lua></td>
        </tr>
        <tr>
            <td>cachedir</td>
            <td>:</td>
            <td><lua>server.print(config.cache_dir)</lua></td>
        </tr>
        <tr>
            <td>tmpdir</td>
            <td>:</td>
            <td><lua>server.print(config.tmpdir)</lua></td>
        </tr>
        <tr>
            <td>port</td>
            <td>:</td>
            <td><lua>server.print(config.port)</lua></td>
        </tr>
        <lua>
            if server.has_openssl then
                server.print('<tr><td>SSL port</td><td>:</td><td>' ..
                             config.sslport .. '</td></tr>')
            end
        </lua>
        <tr>
            <td>number of threads:</td>
            <td>:</td>
            <td><lua>server.print(config.n_threads)</lua></td>
        </tr>
        <tr>
            <td>maximal post size:</td>
            <td>:</td>
            <td><lua>server.print(config.max_post_size)</lua></td>
        </tr>
    </table>
    <h2>Client information</h2>
    <table>
        <tr>
            <td>Host in request</td>
            <td>:</td>
            <td><lua>server.print(server.host)</lua></td>
        </tr>
        <tr>
            <td>IP of client</td>
            <td>:</td>
            <td><lua>server.print(server.client_ip)</lua></td>
        </tr>
        <tr>
            <td>URL path</td>
            <td>:</td>
            <td><lua>server.print(server.uri)</lua></td>
            </tr>
    </table>

    <p>Important Note: "IP of client" and "Host in request" may
       indicate the information of a proxy and notof the actual
       client!</p>
    <h2>Request Header Information</h2>
    <table>
        <lua>
            for key, val in pairs(server.header) do
                server.print('<tr><td>' .. key ..
                             '</td><td>:</td><td>' .. val .
                             '</td></tr>')
            end
        </lua>
    </table>
    </body>
</html>
```

### Embedded LUA and enforcing SSL
The supplied example initialization file offers a LUA function that enforces the use of a SSL encryption page
proteced by a user name and password. It is used as follows by adding the following code
*before the `<html>` opening tag*:

```lua
<lua>
    if server.secure then
        protocol = 'https://'
    else
        protocol = 'http://'
    end

    success,token = authorize_page('admin.sipi.org',
                                   'administrator',
                                    extecteduser, expectedPassword)
    if not success then
        return
    end
</lua>
```
where `expectedUser` and `extectedPassword` are the user/password combination the user is expected to enter.

### File uploads to SIPI

The SIPI specific LUA function allow the upload of files using POST requests with `multipart/form-data` content.
The global variable `server.uploads` contains`the information about the uploads. The following variables and
function help to deal with uploads:

- [server.uploads](#serveruploads) : information about the files in the upload request.
- [server.copyTmpfile](#servercopytmpfile) : copies a fie from the upload location to the destination directory.

In addition the file system functions that SIPI provides may be used.

See the scripts `upload.elua` and `do-upload.elua` in the server directory, and `upload.lua` in the scripts directory
for a working example.

## RESTful API and custom routes

Custom routes to implement a RESTful API can be defined in Sipi's configuration file using the
`routes` configuration variable. For example:

    routes = {
        {
            method = 'GET',
            route = '/status',
            script = 'get_repository_status.lua'
        },
        {
            method = 'POST',
            route = '/make_thumbnail',
            script = 'make_image_thumbnail.lua'
        }
    }

Sipi looks for these scripts in the directory specified by `scriptdir`
in its configuration file. The first route that matches the beginning of
the requested URL path will be used.

  
## IIIF Authentication API 1.0 in SIPI
The `pre_flight` function is also responsible for activating the IIIF Auth API. In order to do so, the pre_flight script
returns a table that contains all necessary information. For details about the IIIF Authentication API 1.0 see the
[IIIF documentation](https://iiif.io/api/auth/1.0/). The following fields have to be returned by the
`pre_flight`-function as LUA-table:

- `type`: String giving the type. Valid are:  
  `"login"`, `"clickthrough"`, `""kiosk"` or `"external"`.
- `cookieUrl`: URL where to get a valid IIIF Auth cookie for this service.
- `tokenUrl`: URL where to get a valid IIIF Auth token for this service.
- `confirmLabel`: Label to display in confirmation box.
- `description`: Description for login window.
- `failureDescription`: Information, if login fails.
- `failureHeader`: Header for failure window.
- `header`: Header of login window
- `label`: Label of the login window

In addition, the filepath has to be returns. A full response may look as follows:

```lua
return {
   type = 'login',
    cookieUrl = 'https://localhost/iiif-cookie.html',
    tokenUrl = 'https://localhost/iiif-token.php',
    confirmLabel =  'Login to SIPI',
    description = 'This Example requires a demo login!',
    failureDescription = '<a href="http://example.org/policy">Access Policy</a>',
    failureHeader = 'Authentication Failed',
    header = 'Please Log In',
    label = 'Login to SIPI',
}, filepath
```

SIPI will use this information returned by the `pre_flight` function to return the appropriate responses to the
client requests based on the IIIF Authentication API 1.0. Check for support of the IIIF Authentication API 1.0
for [mirador](https://projectmirador.org) and [universalviewer](https://universalviewer.io), both applications which
suppport the IIIF standards.


## SIPI variables available to Lua scripts
There are many globally accessible LUA variables made available which reflext the configuration of SIPI and the
state of the server and request. This variables a read only and created for every request.

### SIPI configuration variables
This variables are defined ither in the configuration file if SIPI, in environemt variables at startup or
as command line option when starting the server.

#### config.hostname

    config.hostname
    
The hostname  SIPI is configures to run on
(see [hostname](../sipi/#hostname) in configuration description).

#### config.port

    config.port
    
Portnumber where the SIPI server listens
(see [serverport](../sipi/#port) in configuration description).

#### config.sslport

    config.sslport
    
Portnumber for SSL connections of SIPI
(see [sslport](../sipi/#sslport) in configuration description).

####config.imgroot

    config.imgroot
    
Root directory for IIIF-served images
(see [imgroot](../sipi/#imgroot) in configuration description).

#### config.docroot

    config.docroot

Root directory for WEB-Server
(see [docroot](../sipi/#docroot) in configuration description).

#### config.max\_temp\_file\_age

    config.max_temp_file_age
    
maximum age of temporary files
(see [max_temp_file_age](../sipi/#maxtmpfileage) in configuration description).

#### config.prefix\_as\_path

    config.prefix_as_path`
    
`true` if the prefix should be used as path info
(see [prefix_as_path](../sipi/#prefixaspath) in configuration description).

#### config.init\_script

    config.init_script
    
Path to initialization script
(see [initscript](../sipi/#initscript) in configuration description).

#### config.scriptdir

    config.scriptdir

Path to script directory.
(see [scriptdir](../sipi/#scriptdir) in configuration description).

#### config.cache\_dir

    config.cache_dir
    
Path to cache directory for iIIF served images.
(see [cachedir](../sipi/#cachedir) in configuration description).

#### config.cache\_size

    config.cache_size
    
Maximal size of cache
(see [cachesize](../sipi/#cachesize) in configuration description).

#### config.cache\_n\_files

    config.cache_n_files
    
Maximal number of files in cache.
(see [cache_nfiles](../sipi/#cachenfiles) in configuration description).

#### config.cache\_hysteresis

    config.cache_hysteresis
    
Amount of data to be purged if cache reaches maximum size.
(see [cache_hysteresis](../sipi/#hysteresis) in configuration description).

#### config.jpeg\_quality

    config.jpeg_quality
    
Unfortunately, the IIIF Image API does not allow to give a JPEG quality (=compression) on the IIIF URL. SIPI
allows to configure the compression quality system wide with this parameter. Allowed values are in he range
\[1..100\] where 1 the worst quality (and highest compression factor = smallest file size) and 100 the highest
quality (with lowest compression factor = biggest file size). Please note that SIPI is not able to provide
lossless compression for JPEG files.
(see [jpeg_quality](../sipi/#jpegquality) in configuration description).

#### config.keep\_alive

    config.keep_alive
    
Maximal keep-alive time for HTTP requests that ask for a keep-alive connection.
(see [keep_alive](../sipi/#keepalive) in configuration description).

#### config.thumb\_size

    config.thumb_size
    
Default thumbnail image size.
(see [thumb_size](../sipi/#thumbsize) in configuration description).

#### config.n\_threads

    config.n_threads
    
Number of worker threads SIPI uses.
(see [nthreads](../sipi/#nthreads) in configuration description).

#### config.max\_post\_size

    config.max_post_size
    
Maximal size of POST data allowed
(see [max_post_size](../sipi/#maxpostsize) in configuration description).

#### config.tmpdir

    config.tmpdir
    
Temporary directory to store uploads.
(see [tmpdir](../sipi/#tmpdir) in configuration description).

#### config.ssl/_certificate

    config.ssl_certificate
    
Path to the SSL certificate that SIPI uses.
(see [ssl_certificate](../sipi/#sslcertificate) in configuration description).

#### config.ssl/_key

    config.ssl_key
    
Path to the SSL key that SIPI uses.
(see [ssl_key](../sipi/#sslkey) in configuration description).

#### config.logfile

    config.logfile
    
Name of the logfile. SIPI is currently using the [syslog](https://en.wikipedia.org/wiki/Syslog) facility and the
logfile name is ignored. 
(see [logfile](../sipi/#logfile) in configuration description).

#### config.loglevel

    config.loglevel
    
Indicates what should be logged. The variable contains a integer that corresponds to the syslog level.
(see [loglevel](../sipi/#loglevel) in configuration description).

#### config.adminuser

    config.adminuser
    
Name of admin user.
(see [user](../sipi/#configuration-of-administrator-access) in configuration description).

#### config.password

    config.password
    
Password (plain text, not encrypted) of admin user (*use with caution*)!
(see [password](../sipi/#configuration-of-administrator-access) in configuration description).

### SIPI Server Variables
Sipi server variables are dependent on the incoming request and are created by SIPI automatically for each
request.

#### server.method

    server.method
    
The HTTP request method. Is one of `OPTIONS`, `GET`, `HEAD`, `POST`, `PUT`, `DELETE`, `TRACE`, `CONNECT` or `OTHER`.

#### server.has\_openssl

    server.has_openssl
    
`true` if OpenSSL is available. This variable is determined compilation time. Usually SSL should be included, but SIPI
can be compiled without SSL support. There is no option in the configuration file for this.

#### server.secure

    server.secure
    
`true` if the connection was made over HTTPS using SSL.

#### server.host

    server.host
    
The hostname of the Sipi server that was used in the request.

#### server.client\_ip

    server.client_ip
    
The IPv4 or IPv6 address of the client connecting to Sipi.

#### server.client\_port

    server.client_port
    
The port number of the client socket.

#### server.uri

    server.uri
    
The URL path used to access Sipi (does not include the hostname).

#### server.header

    server.header
    
A table containing all the HTTP request headers(in lowercase).

#### server.cookies

    server.cookies
     
A table of the cookies that were sent with the request.

#### server.get

    server.get

A table of GET request parameters.

#### server.post

    server.post
    
A table of POST or PUT request parameters.

#### server.request

    server.request

All request parameters.

#### server.content

    server.content
    
If the request had a body, the variable contains the body data. Otherwise it's `nil`.

#### server.content\_type

    server.content_type

Returns the content type of the request. If there is no type or no content, this variable is `nil`.

#### server.uploads

    server.uploads
    
This is an array of upload parameters, one per file. Each one is a table containing:

- `fieldname`: the name of the form field.
- `origname`: the original filename.
- `tmpname`: a temporary path to the uploaded file.
- `mimetype`: the MIME type of the uploaded file as provided by the browser.
- `filesize`: the size of uploaded file in bytes.

The upload can be accessed as follows:
```lua
for index, value in pairs(server.uploads) do
    --
    -- copy the uploaded file to the image repository using the original name
    --
    server.copyTmpfile(index, config.imgdir .. '/' .. value["origname"])
end
```

### Knora-specific variables
The development of SIPI came out of the need to have a flexible, high performance IIIF server for the Swiss National
research infrastructure [Data and Service Center for the Humanities](https://dasch.swiss) (DaSCH). The aim of the DaSCH
is to guarantee long-term accessibility of research data from the Humanities. The operates a specialized platform
[Knora](https://knora.org). The following variables are for internal use only.

#### config.knora\_path

    config.knora_path
    
Path to knora REST API (only for SIPI used with Knora)

#### config.knora\_port

    config.knora_port
    
Port that the Knora API uses


## SIPI functions available to Lua scripts

Sipi provides the following functions that can be called from Lua
scripts. Each function returns two values. The first value is `true` if
the operation succeeded, `false` otherwise. If the operation succeeded,
the second value is the result of the operation, otherwise it is an
error message.

### SIPI Connection Functions
These LUA function alter the way the HTTP connection is handled.

#### server.setBuffer

    success, errmsg = server.setBuffer([bufsize][,incsize])

Activates the the connection buffer. Optionally the buffer size and
increment size can be given. Returns `true, nil` on success or
`false, errormsg` on failure.

#### server.sendHeader

    success, errormsg = server.sendHeader(key, value)

Sets an HTTP response header. Returns `true, nil` on success or
`false, errormsg` on failure.

#### server.sendCookie

    success, errormsg = server.sendCookie(key, value [, options-table])

Sets a cookie in the HTTP response. Returns `true, nil` on success or
`false, errormsg` on failure. The optional `options-table` is a Lua
table containing the following keys:

-   `path`
-   `domain`
-   `expires` (value in seconds)
-   `secure` (boolean)
-   `http_only` (boolean)

#### server.sendStatus

    server.sendStatus(code)

Sends an HTTP status code. This function is always successful and
returns nothing.

#### server.print

    success, errormsg = server.print(values)

Prints variables and/or strings over the HTTP connection to the client that originated the request. Returns
`true, nil` on success or `false, errormsg` on failure.

#### server.requireAuth

    success, table = server.requireAuth()

This function retrieves HTTP authentication data that was supplied after sending a `'WWW-Authenticate'`-header (e.g.
by issuing a the following commands to enter the HTTP login dialog:

        server.setBuffer()
        server.sendStatus(401);
        server.sendHeader('WWW-Authenticate', 'Basic realm="Sipi"')

It returns `true, table` on success or `false, errormsg` on failure. The result of the authorization
is returned as table with the following elements:

- `status`: Either `BASIC`, `BEARER`, `NOAUTH` (no authorization header) or `ERROR`
- `username`: A string containing the supplied username (only existing if stats is `BASIC`)
- `password`: A string containing the supplied password (only existing if stats is `BASIC`)
- `token`: A string containing the raw token information (only if status `BEARER`)
- `message`: A string containing the error message (only if status `ERROR`)

Example:

    success, auth = server.requireAuth()
    if not success then
        server.sendStatus(501)
        server.print("Error in getting authentication scheme!")
        return -1
    end

    if auth.status == 'BASIC' then
        --
        -- everything OK, let's create the token for further
        -- calls and ad it to a cookie
        --
        if auth.username == config.adminuser and
           auth.password == config.password then
            tokendata = {
                iss = "sipi.unibas.ch",
                aud = "knora.org",
                user = auth.username
            }
            success, token = server.generate_jwt(tokendata)
            if not success then
                server.sendStatus(501)
                server.print("Could not generate JWT!")
                return -1
            end
            success, errormsg = server.sendCookie('sipi',
                                                  token,
                                                  {path = '/', expires = 3600})
            if not success then
                server.sendStatus(501)
                server.print("Couldn't send cookie with JWT!")
                return -1
            end
        else
            server.sendStatus(401)
            server.sendHeader('WWW-Authenticate', 'Basic realm="Sipi"')
            server.print("Wrong credentials!")
            return -1
        end
    elseif auth.status == 'BEARER' then
        success, jwt = server.decode_jwt(auth.token)
        if not success then
            server.sendStatus(501)
            server.print("Couldn't deocde JWT!")
            return -1
        end
        if (jwt.iss ~= 'sipi.unibas.ch') or
           (jwt.aud ~= 'knora.org') or
           (jwt.user ~= config.adminuser) then
            server.sendStatus(401)
            server.sendHeader('WWW-Authenticate', 'Basic realm="Sipi"')
            return -1
        end
    elseif auth.status == 'NOAUTH' then
        server.setBuffer()
        server.sendStatus(401);
        server.sendHeader('WWW-Authenticate', 'Basic realm="Sipi"')
        return -1
    else
        server.status(401)
        server.sendHeader('WWW-Authenticate', 'Basic realm="Sipi"')
        return -1
    end


### SIPI File System Function
These functions offer tools to manipuale files and directories, and to gather file information.

#### server.fs.ftype

    success, filetype = server.fs.ftype(filepath)

Checks the filetype of a given filepath. Returns either `true, filetype`
(with filetype one of `"FILE"`, `"DIRECTORY"`, `"CHARDEV"`, `"BLOCKDEV"`, `"LINK"`,
`"SOCKET"` or `"UNKNOWN"`) or `false, errormsg`.

#### server.fs.modtime

    success, modtime = server.fs.modtime(filepath)

Retrieves the last modification date of a file in seconds since epoch
UTC. Returns either `true`, `modtime` or `false`, `errormsg`.

#### server.fs.is\_readable

    success, readable = server.fs.is_readable(filepath)

Checks if a file is readable. Returns `true, readable` (boolean) on
success or `false, errormsg` on failure.

#### server.fs.is\_writeable

    success, writeable = server.fs.is_writeable(filepath)

Checks if a file is writeable. Returns `true, writeable` (boolean) on
success or `false, errormsg` on failure.

#### server.fs.is\_executable

    success, errormsg = server.fs.is_executable(filepath)

Checks if a file is executable. Returns `true, executable` (boolean) on
success or `false, errormsg` on failure.

#### server.fs.exists

    success, exists = server.fs.exists(filepath)

Checks if a file exists. Checks if a file exists. Returns `true, exists`
(boolean) on success or `false, errormsg` on failure.

#### server.fs.unlink

    success, errormsg = server.fs.unlink(filename)

Deletes a file from the file system. The file must exist and the user
must have write access. Returns `true, nil` on success or
`false, errormsg` on failure.

#### server.fs.mkdir

    success, errormsg = server.fs.mkdir(dirname, [tonumber('0755', 8)])

Creates a new directory, optionally with the specified permissions.
Returns `true, nil` on success or `false, errormsg` on failure.

#### server.fs.rmdir

    success, errormsg = server.fs.rmdir(dirname)

Deletes a directory. Returns `true, nil` on success or `false, errormsg`
on failure.

#### server.fs.getcwd

    success, curdir = server.fs.getcwd()

Gets the current working directory. Returns `true, current_dir` on
success or `false, errormsg` on failure.

#### server.fs.readdir

    success, filenames = server.fs.readdir(dirname)

Gets the names of the files in a directory, not including `.` and `..`.
Returns `true, table` on success or `false, errormsg` on failure.

#### server.fs.chdir

    success, oldir = server.fs.chdir(newdir)

Change working directory. Returns `true, olddir` on success or
`false, errormsg` on failure.

#### server.fs.copyFile

    success, errormsg = server.fs.copyFile(source, destination)

Copies a file from source to destination. Returns `true, nil`on success
or `false, errormsg` on failure.

#### server.fs.moveFile

    success, errormsg = server.fs.moveFile(from, to)

Moves a file. The move connot cross filesystem boundaries! `true, nil`on
success or `false, errormsg` on failure.

### Other Helper Function

#### server.http

    success, result = server.http(method, "http://server.domain[:port]/path/file" [, header] [, timeout])

Performs an HTTP request using curl. Currently implements only GET requests. Parameters:

-   `method`: The HTTP request method. Currently must be `"GET"`.
-   `url`: The HTTP URL.
-   `header`: An optional table of key-value pairs representing HTTP
    request headers.
-   `timeout`: An optional number of milliseconds until the connection
    times out.

Authentication is not yet supported.

The result is a table:

    result = {
        status_code = value -- HTTP status code returned
        erromsg = "error description" -- only if success is false
        header = {
            name = value [, name = value, ...]
        },
        certificate = { -- only if HTTPS connection
            subject = value,
            issuer = value
        },
        body = data,
        duration = milliseconds
    }

Example:

    success, result = server.http("GET", "http://www.salsah.org/api/resources/1", 100)

    if (result.success) then
       server.print("<table>")
       server.print("<tr><th>Field</th><th>Value</th></tr>")
       for k,v in pairs(server.header) do
           server.print("<tr><td>", k, "</td><td>", v, "</td></tr>")
       end
       server.print("</table><hr/>")

       server.print("Duration: ", result.duration, " ms<br/><hr/>")
       server.print("Body:<br/>", result.body)
    else
       server.print("ERROR: ", result.errmsg)
    end

#### server.table\_to\_json

    success, jsonstr = server.table\_to\_json(table)

Converts a (nested) Lua table to a JSON string. Returns `true, jsonstr`
on success or `false, errormsg` on failure.

#### server.json\_to\_table

    success, table = server.json_to_table(jsonstr)

Converts a JSON string to a (nested) Lua table. Returns `true, table` on
success or `false, errormsg` on failure.

#### server.generate\_jwt

    success, token = server.generate_jwt(table)

Generates a [JSON Web Token](https://jwt.io/) (JWT) with the supplied table as
payload. Returns `true, token` on success or `false, errormsg` on
failure. The internal may contain arbitrary keys and/or may contains the JWT
claims as follows. (The type `IntDate` is a number of seconds since
1970-01-01T0:0:0Z):

-   `iss` (string =&gt; StringOrURI) OPT: principal that issued the JWT.
-   `exp` (number =&gt; IntDate) OPT: expiration time on or after which
    the token MUST NOT be accepted for processing.
-   `nbf` (number =&gt; IntDate) OPT: identifies the time before which
    the token MUST NOT be accepted for processing.
-   `iat` (number =&gt; IntDate) OPT: identifies the time at which the
    JWT was issued.
-   `aud` (string =&gt; StringOrURI) OPT: identifies the audience that
    the JWT is intended for. The audience value is a string, typically
    the base address of the resource being accessed, such as
    `https://contoso.com`.
-   `prn` (string =&gt; StringOrURI) OPT: identifies the subject of
    the JWT.
-   `jti` (string =&gt; String) OPT: provides a unique identifier for
    the JWT.

#### server.decode\_jwt

    success, table = server.decode_jwt(token)

Decodes a [JSON Web Token](https://jwt.io/) (JWT) and returns its
content as table. Returns `true, table` on success or `false, errormsg`
on failure.

#### server.parse\_mimetype

    success, mimetype = server.parse_mimetype(str)
    
Parses a mimtype HTTP header string and returns a pair containing the actual
mimetype and the charset used (if available). It returns `true, pair` with pair as mimetype
and charset on success, `false, errormsg` on failure.

#### server.file\_mimetype

    success, table = server.file_mimetype(path)
    success, table = server.file_mimetype(index)

Determines the mimetype of a file. The *first* form is used if the file
path is known. The *second* form can be used for uploads by passing the
upload file index. It returns `true, table` on success or `false, errormsg` on
failure. The table has 2 members: - `mimetype` - `charset`

#### server.file\_mimeconsistency

    success, is_consistent = server.file_mimeconsistency(path)
    success, is_consistent = server.file_mimeconsistency(index)
    
Checks if the file extension and the mimetype determined by the magic
of the file is consistent. The *first* form requires a path (including the
filename with extension), the *second* can be used for checking uploads by 
passing the file index. It returns `true, is_consistent` on success or
`false, errormsg` in case of an error. `is_consistent` is true if the
mimetype corresponds to the file extension.

#### server.copyTmpfile

    success, errormsg = server.copyTmpfile(from, to)

Sipi saves each uploaded file in a temporary location (given by the
config variable `tmpdir`) and deletes it after the request has been
served. This function is used to copy the file to another location where
it can be retrieved later. Returns `true, nil` on success or
`false, errormsg` on failure.

Parameters:

- `from`:    an index (integer value) of array server.uploads.
- `target`:  destination path

#### server.systime

    systime = server.systime()

Returns the current system time on the server in seconds since epoch.

#### server.log

    server.log(message, loglevel)

Writes a message to
[syslog](http://man7.org/linux/man-pages/man3/syslog.3.html). Severity
levels are:

-   `server.loglevel.LOG_EMERG`
-   `server.loglevel.LOG_ALERT`
-   `server.loglevel.LOG_CRIT`
-   `server.loglevel.LOG_ERR`
-   `server.loglevel.LOG_WARNING`
-   `server.loglevel.LOG_NOTICE`
-   `server.loglevel.LOG_INFO`
-   `server.loglevel.LOG_DEBUG`

#### server.uuid

    success, uuid = server.uuid()

Generates a random UUID version 4 identifier in canonical form, as
described in [RFC 4122](https://tools.ietf.org/html/rfc4122). Returns
`true, uuid` on success or `false, errormsg` on failure.

#### server.uuid62

    success, uuid62 = server.uuid62()

Generates a Base62-encoded UUID. Returns `true, uuid62` on success or
`false, errormsg` on failure.

#### server.uuid\_to\_base62

    success, uuid62 = server.uuid_to_base62(uuid)

Converts a canonical UUID string to a Base62-encoded UUID. Returns
`true, uuid62` on success or `false, errormsg` on failure.

#### server.base62\_to\_uuid

    success, uuid = server.base62_to_uuid(uuid62)

Converts a Base62-encoded UUID to canonical form. Returns `true, uuid`
on success or `false, errormsg` on failure.


## Installing Lua modules

To install Lua modules that can be used in Lua scripts, use
`local/bin/luarocks`. Make sure that the location where the modules are
stored is in the Lua package path, which is printed by
local/bin/lurocks path. The Lua paths will be used by the Lua
interpreter when loading modules in a script with `require` (see [Using
LuaRocks to install packages in the current
directory](http://leafo.net/guides/customizing-the-luarocks-tree.html)).

For example, using `local/bin/luarocks install --local package`, the
package will be installed in `~/.luarocks/`. To include this path in the
Lua's interpreter package search path, you can use an environment
variable. Running `local/bin/luarocks path` outputs the code you can use
to do so. Alternatively, you can build the package path at the beginning
of a Lua file by setting `package.path` and `package.cpath` (see
[Running scripts with
packages](http://leafo.net/guides/customizing-the-luarocks-tree.html#the-install-locations/using-a-custom-directory/quick-guide/running-scripts-with-packages)).

