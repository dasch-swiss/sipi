# Simple Image Presentation Interface (SIPI) - Introduction
## What is SIPI?
### 1. A IIIF Image API V3 level 2 conformant image server
- SIPI is a full multithreaded, high performance, level2 compliant [IIIF Image API 3.0](https://iiif.io/api/image/3.0) written in C++. For the
  JPEG2000 implementation, it relies on the commercial [kakadu-library](https://kakadusoftware.com), but otherwise it is
  completely open source on [GitHub](https://github.com/dasch-swiss/sipi). It offers special support for multipage PDFs
  (through a SIPI-specific extensions to the IIIF Image API).
- SIPI has been designed for the long term preservation of images, intended for the needs of the cultural heritage field. Thus it offers
  some unique features for this purpose:
  - all file format conversions try to preserve all metadata (EXIF, XMP, IPTC etc.). These functionality is based
    on the open source [exiv2 library](https://www.exiv2.org).
  - SIPI can deal with and convert ICC color profiles based on the [littlecms library](http://www.littlecms.com).
  - SIPI can embed important preservation data such as the checksum of the pixel values, original filename etc. in the file
    headers.
- it supports SSL (https://…)
- SIPI embeds the scripting language [LUA](https://www.lua.org) that allows a very flexible, highly customizable 
  deployment that can be adapted to the enviroment SIPI is being used in. Before serving any request, a configurable
  LUA script ("pre flight script") is being executed that can check access rights, restrictions or other stuff. SIPI LUA
  has been extended with many SIPI-specific functions (including image conversion, HTTP-client etc.)
  
### 2. An ordinary HTTP webserver
- SIPI is also a normal webserver that is able to deliver arbitrary files. It also implements LUA embedded into HTML
  pages.
- Using SIPI LUA scripts and routing, RESTful interfaces may be implemented. E.g. image upload and conversions may
  by supported.
  
### 3. An image format conversion tool

#### Generic format conversions

- image format conversion are supported between TIFF, JPEG2000, JPG and PNG. SIPI can
  be used either as standalone command line tool or in server mode using [LUA](https://www.lua.org) scripting.
- SIPI preserves most embedded metadata (EXIF, IPTC, TIFF, XMP) and is preserving and/or converting ICC color profiles.

#### Preservation metadata (SIPI specific)

- SIPI is able to add SIPI specific metadata to most file formats. These metadata are relevant for long-term
  preservation and include the following information:
    - `original filename`: The original file name before conversion
    - `original mimetype`: The mimetype of the original image before conversion
    - `pixel checksum`: A checksum (e.g. SHA-256) of the original pixel values. This checksum can be used to verify that
      a format conversion didn't alter the image content.
      - `icc profile`: (optional) The raw ICC profile as binary string. This field is added if the destination file 
        format has no standard way to embed ICC color profiles (e.g. JPEG).
  
### 4. Integrated sqlite3 Database
SIPI has an integrated sqlite3 database that can be used with special LUA extensions. Thus, SIPI can be used as a
standalone media server with extended functionality. The sqlite3 database may be used to store metadata about
images, user data etc.

## Who is behind SIPI?
SIPI is developed and maintained by Lukas Rosenthaler, professor for Digital Humanities at the University of Basel, in
collaboration with the "Data and Service Center for the Humanities" ([DaSCH](https://dasch.swiss).

## How to get SIPI?
- The easiest way is to use the docker image provided on dockerhup [daschswiss/sipi](https://hub.docker.com/r/daschswiss/sipi).
  The dockerized version has the binary kakadu library compiled in.
- You can compile SIPI from the sources on [github](https://github.com/dasch-swiss/sipi). Since SIPI uses many
  third-party open source libraries, compiling Yourself is tedious and my be frustrating (but possible). *You have to
  provide the licensed source of kakadu by Yourself*. See [kakadu software](https://kakadusoftware.com) on how to get a
  licensed version of the kakadu code. SIPI should compile on Linux (Ubuntu) and Apple OS X.
    
## SIPI as IIIF-Server
### Extensions to the IIIF-Standard

#### Preflight script
- Before executing a IIIF request, a freely configurable LUA-script is being called. This script must return the
  permission to access the resource ("allow", "restrict" "deny") and the final path to the resource. This allows
  to handle access rights etc. Within the LUA-script, permission databases etc. may be accessed through RESTful
  services or using the internal SQLite database. In addition, the path to the resource may be redirected or other
  limitations imposed (size, watermark etc.).
- The preflight script has access to the full HTTP(s) header including cookies and Authorization information. There are
  also utility functions to decode JSON Web Tokens ([JWT](https://jwt.io)).

  
#### Access to non-image files
Sometimes it would be helpful to deliver non-image files such as XML, CSV etc. from the same directory tree as the
IIIF-conformant images:
- The url to download a file must have the form ```http(s)://{server}/{prefix}/{fileid}/file```. The clause
  _/file_ at the end indicates that the file should bypass any IIIF URl processing and just be served as file.
- Also in this case, a *preflight script* may be configured to control access to such file resources.
- if the url has the form ```http(s)://{server}/{prefix}/{fileid}/info.json```, SIPI returns a JSON containing
  information about the file. The JSON has the from:
  ```json
  {
    "@context": "http://sipi.io/api/file/3/context.json",
    "id": "https://localhost:1025/images/csv_test.csv",
    "internalMimeType": "text/csv",
    "fileSize": 36
  }
  ```
  Please note that SIPI determines the mimetype using the magic number. Due to the limitations thereof the mimetype
- may not be determined exactly.


