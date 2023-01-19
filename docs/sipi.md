# Basic Information and Reference

This section provides the basic information to use SIPI as a high performance, versatile media server implementing the
[IIIF](https://iiif.io) standards that can be used in many different settings, from a small standalone server providing
basic metadata to the deployment in a complex environment. For more information about the IIIF standard see
[https://iiif.io](https://iiif.io). The basic idea is that an image or rectangular region of an image can be downloaded
(e.g. to the browser) with a given width and height, rotation, image quality and format. All parameters are provided
with the IIIF conformant URL that has the following form:

```http(s)://{server}/{prefix}/{identifier}/{region}/{size}/{rotation}/{quality}.{format}```

The parts do have the following meaning:

- `{server}`: The DNS name of the server, eg. `iiif.dasch.swiss`. The server may include a portnumber,
  eg. `iiif2.dasch.swiss:8080`.
- `{prefix}`: A path (that may include `/`'s) to organize the assets. Usually the prefix reflect the internal
  directory or folder hierarchy. However this can be overridden using special features of SIPI (see pre-flight-script
  and sipi configuration file).
- `{identifier}`: The identifier of the requested image. By default, it is the filename and its extension.
- `{region}`: a region of interest that should be displayed. `full` indicates that the whole image is being requested.
  For more details see [IIIF regions](https://iiif.io/api/image/3.0/#41-region)
- `{size}`: The size of the displayed image (part). `max` indicates the the "natural" maximal resolution should be used.
  For more details see [IIIF size](https://iiif.io/api/image/3.0/#42-size)
- `{rotations}`: The image can be rotated and mirrored before being transmitted to the client. SIPI allows for
  arbitrary rotations. The Value `0` indicates no rotation. For more details see
  [IIIF rotation](https://iiif.io/api/image/3.0/#43-rotation)
- `{quality}`: The quality parameter determines whether the image is delivered in color, grayscale or black and white.
  Valid values are:
  - `default`: the "natural" quality of the original image
  - `color`: A color representation
  - `gray`: A gray value representation
  - `bitonal`: A bitonal representation
  
  All quality values are supported by SIPI
- `{format}`: The file format that should be delivered. SIPI supports the following formats, irrelevant on the format
  the image as in the repository of SIPI:
  - `jpg`: The image is delivered as JPEG image. Unfortunately the IIIF standard does not allow the dynamic selection
     of the compression ratio used in creating the JPEG. However, a server wide rate may be set in the configuration
     file.
  - `tif`: The image is delivered as TIFF image.
  - `png`: The image is delivered as PNG image.
  - `pdf`: The image is delivered as PDF document. **Note**: *If the file in the SIPI repository is a multi-page PDF,
     a SIPI-specific extensions allows to address single pages and deliver them as images in any format.
  - `jpx`: The image is delivered as JPEG2000 image.


## The SIPI Executable
The SIPI executable is a statically linked program that can be started as
- command line tool to perform image operations, mainly format conversions
- as server deamon that provides IIIF conforming media server

### Using SIPI as Command Line Tool

The SIPI command line mode can be used for the following tasks:

#### Format Conversions:

```bash
/path/to/sipi infile outfile [options]
```
#### Print Information about File and Metadata:

```bash
/path/to/sipi -x infile
/path/to/sipi --query infile
```

#### Compare two Images Pixelwise
The images may have different formats: if the have exactely the same pixels, they are considered identical). Metadata
is ignored for comparison:

```bash
/path/to/sipi -C file1 file2
/path/to/sipi --compare file1 file2
```

#### General Options for the Command Line Use 
In command line mode, SIPI supports the following options:

- `-h`, `--help`: Display a short help with all options available
- `-F <fmt>`, `--format <fmt>`: The format of the output file. Valid are `jpx`, `jp2`, `jpg`, `png` and `pdf`.
- `-I <profile>`, `--icc <profile>`: Convert the outfile to the given ICC color profile. Supported profiles are `sRGB`,
  `AdobeRGB` and `GRAY`.
- `-q <num>`, `--quality <num>`: Only used for the JPEG format. Ignored for all other formats. Its a number between 1 and
  100, where 1 is equivalent to the highest compression ratio and lowest quality, 100 to the lowest compression ration
  and highest quality of the output image.
- `-n <num>`, `--pagenum <num>`: Only for input files in multi-page PDF format: sets the page that should be converted.
  Ignored for all other input file formats.
- `-r <x> <y> <nx> <ny>`, `--region <x> <y> <nx> <ny>`: Selects a region of interest that should be converted. Needs
  4 integer values: `left_upper_corner_X`, `left_upper_corner_Y`, `width`, `height`.
- `-s <iiif-size>`, `--size <iif-size>`: The size of the resulting image. The option requires a string parameter
  formatted according to the size-syntax of IIIF [see IIIF-Size](https://iiif.io/api/image/3.0/#42-size). Not giving
  this parameters results in having the maximalsize (as the value `"max"`would give).
- `-s <num>`, `--scale <num>`: Scaling the image size by the given number (interpreted as percentage). Percentage must
  be given as integer value. It may be bigger than 100 to upscale an image.
- `-R <num>`, `--reduce <num>`: Reduce the size of the image by the given factor. Thus `-R 2`would resize the image to
  half of the original size. Using `--reduce` is usually much faster than using `--scale`, e.g. `--reduce 2` is faster
  than `--scale 50`.
- `-m <val>`, `--mirror <val>`: Takes either `horizontal` or `vertical`as parameter to mirror the image appropriately.
- `-o <angle>`, `--rotate <angle>`: Rotates the image by the given angle. The angle must be a floating point (or
  integer) value between `0.0`and `w60.0`.
- `-k`, `--skipmeta`: Strip all metadata from inputfile.
- `-w <filepath>`, `--watermark <filepath>`: Overlays a watermark to the output image. <filepath> must be a single
  channel, gray valued TIFF. That is, the TIFF file must have the following tag values: SAMPLESPERPIXEL = 1,
  BITSPERSAMPLE = 8, PHOTOMETRIC = PHOTOMETRIC_MINISBLACK.
  
#### JPEG2000 Specific Options
Usually, the SIPI command line tool is used to create JPEG2000 images suitable for a IIIF repository. SIPI supports
the following JPEG2000 specific options. For a in detail description of these options consult the kakadu documentation!

- `--Sprofile <profile>`: The following JPEG2000 profiles are supported: `PROFILE0`, `PROFILE1`, `PROFILE2`, `PART2`,
  `CINEMA2K`, `CINEMA4K`, `BROADCAST`, `CINEMA2S`, `CINEMA4S`, `CINEMASS`, `IMF`. Default: `PART2`.
- `--rates <string>`: One or more bit-rates (see kdu_compress help!). A value "-1" may be used in place of the first
  bit-rate in the list to indicate that the final quality layer should include all compressed bits.
- `--Clayers <num>`:Number of quality layers. Default: 8.
- `--Clevels <num>`: Number of wavelet decomposition levels, or stages. Default: 8.
- `--Corder <val>`: Progression order. The four character identifiers have the following interpretation:
  L=layer; R=resolution; C=component; P=position. The first character in the identifier refers to the index which
  progresses most slowly, while the last refers to the index which progresses most quickly.
  Thus must be one of `LRCP`, `RLCP`, `RPCL`, `PCRL`, `CPRL`, Default: `RPCL`.
- `--Stiles <string>`: Tiles dimensions `"{tx,ty}"`. Default: `"{256,256}"`.
- `--Cprecincts <string>`: Precinct dimensions `"{px,py}"` (must be powers of 2). Default: `"{256,256}"`.
- `--Cblk <string>`: Nominal code-block dimensions `"{dx,dy}"`(must be powers of 2, no less than 4 and no greater than 1024,
  whose product may not exceed 4096). Default: `"{64,64}"`.
- `--Cuse_sop <val>`: Include SOP markers (i.e., resync markers). Default: yes.

### Using SIPI as IIIF Media Server
In order to use SIPI as IIIF media server, some setup work has to be done. The *configuration* of SIPI can be done
using a configuration file (that is written in LUA) and/or using environment variables, and/or command line options.

The priority is as follows: *`configuration file parameters` are overwritten by `environment variables` are overwritten
by `command line options`*.

The SIPI server requires a few directories to be setup and listed in the configuration file. Then the SIPI server is
launched as follows:

```bash
/path/to/sipi --config /path/to/config-file.lua
```

#### SIPI specific extensions to IIIF
SIPI implements some backwards compatible, non-standard extensions to the IIIF Image API:

##### Page access to PDF's
SIPI allows to access the pages of a multipage pdf in any format using the well known IIIF image API. The prerequisite
is that the file is available in the repository in PDF format. Only the identifier section of the IIIF syntax has to be
extended with a non-standard extensions: adding a `@`-character followed by an integer between 1 the number of pages
retrieves this page of the PDF as if it would a a single standard image an can be rendered to any supported format:

    https://iiif.my.server/images/mydoc.pdf@5/10,10,800,500/!500,500/0/default.jpg

The above command would extract page #5 from the PDF, convert it into an image, select the given region and return
it a JPEG image. This, with this feature, a 3rd party viewer such a [mirador](https://projectmirador.org) or
[universalviewer](https://universalviewer.io) may be used for scrolling through the pages of a PDF file.

In order to better support full PDF files, SIPI also adds the total number of pages to the `info.json` response 
if being requested for a PDF file (e.g. with the URL `http//iiif.myserver.org/images/test.pdf/info.json`) in addition
 to the standard `info.json`-format:

```json
{
  ...
  "width": 2480,
  "height": 3508,
  "numpages": 27,
  ...
}
```

##### Access to a raw files
Sometimes it may be usefull to store non-image files such as XML-sidecars, manifests as JSON or complete PDF's, etc. in
the same environment as the images. For this reason SIPI supports an extension of the IIIF API:

    http(s)://{server}/{prefix}/{identifier}/file
    
The `/file`-path at the end of the URL makes SIPI to send the file as it is. Thus, for example a manifest file could
be accessed by

    https://iiif.my.server/images/myimage.json/file
    
This works also for PDF's. The URL

    https://iiif.my.server/images/mydocument.pdf/file
    
will download in toto to be opened by an external viewer or the webapplication.

It is possible to use the IIIF-`info.json` syntax also on non-image files. In this case the `info.json` has the
following format:

```json
{
   "@context": "http://sipi.io/api/file/3/context.json",
   "id": "http://localhost:1024/images/test.csv",
   "mimeType": "text/comma-separated-values",
   "fileSize": 327
}
```


#### Setup of SIPI Directories
SIPI needs the following directories and files setup and accessible (the real names of the directories must be indicated in the
configuration file). The following configuration parameters are in the `sipi`-table of the configuration script:

- <a name="imgroot"></a>`imgroot=path`: This is the top-directory of the media file repository. SIPI should at least have read access to it. If
  SIPI is used to upload and convert files, it must also have write access. The path may be given as absolute path or
  as relative path.  
  *Cmdline option: `--imgroot`*  
  *Environment variable: `SIPI_IMGROOT`*  
  *Default: `./images`*
  
- <a name="scriptinit"></a>`initscript=path/to/init.lua`: SIPI needs a minmal set of LUA functions that can be adapted to the local installation.
  These mandatory functions are definied in a init-script (usually it can be found in the config directory where also
  the configuration file is located).  
  *Cmdline option: `--initscript`*  
  *Environment variable: `SIPI_INITSCRIPT`*  
  *Default: `./config/sipi.init.lua`*
  
- <a name="tmpdir"></a>`tmpdir=path`: For the support of multipart POST SIPI requires read/write access to a directory to save temporary
  files.  
  *Cmdline option: `--tmpdir`*  
  *Environment variable: `SIPI_IMGROOT`*  
  *Default: `./tmp`*
  
- <a name="scriptdir"></a>`scriptdir=path`: Path to the directory where the LUA-scripts for the routes (e.g. RESTful services) can be found.  
  *Cmdline option: `--scriptdir`*  
  *Environment variable: `SIPI_SCRIPTDIR`*  
  *Default: `./scripts`*
  
- <a name="cachedir"></a>`cachedir=path`: SIPI may optionally use a cache directory to store converted image in order to avoid computationally
  intensive conversions if a specific variant is requested several times. Sipi starts with a warning if the cache
  directory is defined but not existing.  
  *Cmdline option: `--cachedir`*  
  *Environment variable: `SIPI_CACHEDIR`*  
  *Default: `./cache`*
  
In addition, SIPI can act as a webserver that offers image upload and conversion as web service. In order to use this
feature, a server directory has to be defined. This definition ist in the `fileserver`-table of the configuration file:

- <a name="docroot"></a>`docroot=path`: Path to the document root of the SIPI web server.  
  *Cmdline option: `--docroot`*  
  *Environment variable: `SIPI_DOCROOT`*  
  *Default: `./server`*
  
#### SIPI Configuration Parameters
The following configuration parameters are used by the SIPI server:

- <a name="hostname"></a>`hostname=dns-name`: The DNS name that SIPI shall show to the outside world. It should be
  the dns name the client uses to access the SIPI server (and not internal hostnames by proxies etc.). 
  *Cmdline option: `--hostname`*  
  *Environment variable: `SIPI_HOSTNAME`*  
  *Default: `localhost`*
  
- <a name="portnum"></a>`port=portnum`: Portnumber SIPI should listen on for incoming HTTP requests.  
  *Cmdline option: `--serverport`*  
  *Environment variable: `SIPI_SERVERPORT`*  
  *Default: `80`*
  
- <a name="sslport"></a>`ssl_port=portnum`: Portnumber SIPI should listen on for incoming SHTTP requests (using SSL).  
  *Cmdline option: `--sslport`*  
  *Environment variable: `SIPI_SSLPORT`*  
  *Default: `443`*
  
- <a name="nthreads"></a>`nthreads=num`: Number of worker threads that SIPI allocates. SIPI is a mutlithreaded server and pre-allocates a
  given number of working threads that can be configured.  
  *Cmdline option: `--nthreads`*  
  *Environment variable: `SIPI_NTHREADS`*  
  *Default: number of hardware cores as given by `std::thread::hardware_concurrency()`*
  
- <a name="prefixaspath"></a>`prefix_as_path=bool`: If `true`, the prefix is used as path within the image root directory. If false, the prefix
  is ignored and it is assumed that all images are directly located in the image root.  
  *Cmdline option: `--pathprefix`*  
  *Environment variable: `SIPI_PATHPREFIX`*  
  *Default: `false`*
  
- <a name="sslcertificate"></a>`ssl_certificate=path`: Path to the SSL certificate. Is mandatory if SSL is to be used.  
  *Cmdline option: `--sslcert`*  
  *Environment variable: `SIPI_SSLCERTIFICATE`*  
  *Default: `./certificate/certificate.pem`*

- <a name="sslkey">`ssl_key=path`: Path to the SSL key file. Is mandatory if SSL is to be used.  
  *Cmdline option: `--sslkey`*  
  *Environment variable: `SIPI_SSLKEY`*  
  *Default: `./certificate/key.pem`*

- <a name="jwt-secret"></a>`jwt_secret=string`: Shared secret to encode web tokens.  
  *Cmdline option: `--jwtkey`*  
  *Environment variable: `SIPI_JWTKEY`*  
  *Default: `UP 4888, nice 4-8-4 steam engine`*

- <a name="maxpostsize"></a>`max_post_size=amount`: Maximal size a file upload may have. The amount has the
  form "<number><type>" where `number` is an integer value and `type`an "M" for Megabytes, "G" for Gigabytes
  and "" (empty) for bytes.  
  *Cmdline option: `--maxpost`*  
  *Environment variable: `SIPI_MAXPOSTSIZE`*  
  *Default: `300M`*
  
- <a name="keepalive"></a>`keep_alive` : Number of seconds a connection (socket) remains open at maximum ("keep-alive"),
  if a client requests a "keep-alive" connection in the request header. For more information see
  [Keep-Alive](https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Keep-Alive).  
  *Cmdline option: `--keepalive`*  
  *Environment variable: `SIPI_KEEPALIVE`*  
  *Default: `5`*

- <a name="jpegquality">`jpeg_quality=num`: Compression parameter when producing JPEG output. Must be a number
  between 1 and 100. Unfortunately, the IIIF Image API does not allow to give a JPEG quality (=compression) on the IIIF URL. SIPI
  allows to configure the compression quality system wide with this parameter. Allowed values are in he range
  \[1..100\] where 1 the worst quality (and highest compression factor = smallest file size) and 100 the highest
  quality (with lowest compression factor = biggest file size). Please note that SIPI is not able to provide
  lossless compression for JPEG files.  
  *Cmdline option: `--quality`*  
  *Environment variable: `SIPI_JPEGQUALITY`*  
  *Default: `60`*
 
- <a name="thumbsize"></a>`thumb_size=string`: Default size for thumbnails. Parameter must be IIIF conformant size string. This configuration
  parameter can be used to define a default value for creating thumbnails. It has no direct implications but can be
  used in LUA scripts (e.g. the pre\_flight-function).  
  *Cmdline option: `--thumbsize`*  
  *Environment variable: `SIPI_THUMBSIZE`*  
  *Default: `!128,128`*

- <a name="logfile"></a>`logfile=path`: SIPI uses [syslog](https://en.wikipedia.org/wiki/Syslog) as logging facility. The logging name
  is `Sipi`. It supports the following levels:
  "EMERGENCY", "ALERT", "CRITICAL", "ERROR", "WARNING", "NOTICE", "INFORMATIONAL", "DEBUG".  
  *Cmdline option: `--logfile`*  
  *Environment variable: `SIPI_LOGFILE`*  
  *Default: `Sipi`*

- <a name="loglevel"></a>`loglevel=level`: SIPI uses syslog as logging facility. The logging name is `Sipi`. It supports the following levels:
  "EMERGENCY", "ALERT", "CRITICAL", "ERROR", "WARNING", "NOTICE", "INFORMATIONAL", "DEBUG".  
  *Cmdline option: `--loglevel`*  
  *Environment variable: `SIPI_LOGLEVEL`*  
  *Default: `DEBUG`*

- <a name="maxtmpfileage"></a>`max_temp_file_age=num`: The maximum allowed age of temporary files (in seconds) before they are deleted.  
  *Cmdline option: `--maxtmpage`*  
  *Environment variable: `SIPI_MAXTMPAGE`*  
  *Default: `86400`* (one day)
  
#### Cache Configuration
SIPI may optionally use a cache directory to store converted image in order to avoid computationally
intensive conversions if a specific variant is requested several times. The cache is based on timestamps and
the canonical IIIF URL. Before an image is being converted, the canonical URL is determined. If a file associated with
this canonical URL is in the cache directory, the timestamp of the original file in the repository is compated to the
cached file. If the cached file is newer, it will be served. If the file in the repository is newer, the cache file
(which is outdated) will be deleted and replaced be the newly converted repository file (that is being sent to the
client).

The following configuration parameters determine the behaviour of the cache:

- <a name="cachedir"></a>`cachedir=path`: SIPI may optionally use a cache directory to store converted image in order to avoid computationally
  intensive conversions if a specific variant is requested several times. Sipi starts with a warning if the cache
  directory is defined but not existing.  
  *Cmdline option: `--cachedir`*  
  *Environment variable: `SIPI_CACHEDIR`*  
  *Default: `./cache`*

- <a name="cachesize"></a>`cachesize=amount`: The maximal size of the cache. The cache will be purged if either the maximal size or maximal
  number of files is reached. The amount has the form "<number>M" with M indication Megabytes.  
  *Cmdline option: `--cachesize`*  
  *Environment variable: `SIPI_CACHESIZE`*  
  *Default: `200M`*

- <a name="cachenfiles"></a>`cache_nfiles=num`: The maximal number of files to be cached. The cache will be purged if either the maximal size
   or maximal number of files is reached.  
  *Cmdline option: `--cachenfiles`*  
  *Environment variable: `SIPI_CACHENFILES`*  
  *Default: `200`*

- <a name="hysteresis"></a>`cache_hysteresis=float`: If the cache becomes full, the given percentage of file space is marked for reuse and purged.  
  *Cmdline option: `--cachehysteresis`*  
  *Environment variable: `SIPI_CACHEHYSTERESIS`*  
  *Default: `0.15`*
  
#### Configuration of the HTTP File Server
SIPI offers  HTTP file server for HTML and other files. Files with the ending `.elua` are HTTP-files with embeded
LUA code. Everything between the <lua>...</lua> tags is interpreted as LUA code and the output embedded in the
data stream for the client.

All configurations for the HTTP server are in the `fileserver` table:

- `docroot=path`: Path to the document root of the file server.  
  *Cmdline option: `--docroot`*  
  *Environment variable: `SSIPI_DOCROOT`*  
  *Default: `./server`*
 
- `wwwroute=string`: Route for the file server should respond to requests.That is, a file with the name "dada.html"
  is accessed with `http://dnsname/server/dada.html`, if the `wwwroute`is set to `/server`.  
  *Cmdline option: `--wwwroute`*  
  *Environment variable: `SIPI_WWWROUTE`*  
  *Default: `/server`*
  
#### Configuration of Administrator Access
SIPI allows special administrator access for some tasks. In order to allow for this, an administrator has to be defined
as follows:
```lua
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
```
If You're using the administrator user, please make sure that the config file is not exposed!

#### Routing Table
SIPI allows to implement RESTful interfaces or other services based on LUA-scripts which are located in the scripts
directory. In order to use these LUA-scripts as endpoints, the appropriate routes have to be defined in the
`routes` table. An entry has the following form:
- `method`: the HTTP request. Supported are `GET`, `POST`, `PUT` and `DELETE`.
- `route`: A URL path that may contain `/`'s.
- `script`: Name of the LUA script in the script directory.

Thus, the routing section of a SIPI configuration file may look as follows:

```lua
routes = {
    {
        method = 'DELETE',
        route = '/api/cache',
        script = 'cache.lua'
    },
    {
        method = 'GET',
        route = '/api/cache',
        script = 'cache.lua'
    },
    {
        method = 'POST',
        route = '/api/upload',
        script = 'upload.lua'
    },
    {
        method = 'GET',
        route = '/sqlite',
        script = 'test_sqlite.lua'
    }
}
```
