[![Build Status](https://github.com/dasch-swiss/sipi/workflows/CI/badge.svg?branch=main)](https://github.com/dasch-swiss/sipi/actions)

# Sipi - Simple Image Presentation Interface

## Overview

Simple Image Presentation Interface (SIPI) is a multithreaded, high-performance, IIIF compatible media server developed by
the [Swiss National Data and Service Center for the Humanities](https://dasch.swiss) at the
[University of Basel](https://www.unibas.ch). It is designed to
be used by archives, libraries, and other institutions that need to
preserve high-quality images while making them available online.

SIPI implements the Image API 3.0 of the International Image Interoperability Framework
([IIIF](https://iiif.io/)), and efficiently converts between image
formats, preserving metadata contained in image files. In particular, if
images are stored in [JPEG 2000](https://jpeg.org/jpeg2000/) format,
Sipi can convert them on the fly to formats that are commonly used on
the Internet. SIPI offers a flexible framework for specifying
authentication and authorization logic in [Lua](https://www.lua.org/)
scripts, and supports restricted access to images, either by reducing
image dimensions or by adding watermarks. It can easily be integrated
with [Knora](https://dsp.dasch.swiss/). In addition SIPI preserves most of
the [EXIF](http://www.exif.org),
[IPTC](https://iptc.org/standards/photo-metadata/iptc-standard/) and
[XMP](http://www.adobe.com/products/xmp.html) metadata and can preserve
or transform [ICC](https://en.wikipedia.org/wiki/ICC_profile) color
profiles.

In addition, a simple webserver is integrated. The server is able to
serve most common file types. In addition Lua scripts and embedded Lua
(i.e., Lua embedded into HTML pages using the tags
&lt;lua&gt;…&lt;/lua&gt; are supported.

SIPI can also be used from the command line to convert images to/from
TIFF-, [JPEG 2000](https://jpeg.org/jpeg2000/), JPEG- and PNG-
formats. For all these conversion, SIPI tries to preserve all embedded
metadata such as
- [IPTC](https://iptc.org/standards/photo-metadata/iptc-standard/)
- [EXIF](https://www.exif.org/)
- [XMP](https://www.adobe.com/products/xmp.html)
- [ICC](https://en.wikipedia.org/wiki/ICC_profile) color profiles.
However, due to the limitations of some file formats, it cannot be
guaranteed that all metadata and ICC profiles are preserved.
- [JPEG2000](https://jpeg.org/jpeg2000/) (J2k) does not allow all types of ICC profiles
  profiles. Unsupported profile types will be added to the J2k header as comment and will be

SIPI is a [free software](http://www.gnu.org/philosophy/free-sw.en.html),
released under the [GNU Affero General Public
License](http://www.gnu.org/licenses/agpl-3.0.en.html). It is written in
C++ and runs on Linux and macOS. Note: In order to compile SIPI, the user has
to provide a licensed source of the [kakadu software](https://kakadusoftware.com).

It is written in C++ and runs on Linux (including Debian, Ubuntu, and CentOS) and
macOS.

Freely distributable binary releases are available
[daschswiss/sipi](https://hub.docker.com/r/daschswiss/sipi) as docker image.

## Documentation

The documentation is online at https://sipi.io.

To build it locally, you will need [MkDocs](https://www.mkdocs.org/).
In the root the source tree, type:

```
make docs-build
```

You will then find the manual under `site/index.html`.

## Building from source

All should be run from inside the root of the repository.

### Build and run inside Docker - recommended
```bash
$ make compile
$ make test
$ make run
```

### Build under macOS - not recommended. You are on your own. We warned you ;-)

```bash
$ (mkdir -p ./build-mac && cd build-mac && cmake .. && make && ctest --verbose)
```

## Sentry.io
If you would like to use Sentry.io for error reporting, you can set the environment variable
`SIPI_SENTRY_DSN` to the DSN of your Sentry project. If you do not set this variable, Sentry.io will not be used.

Further variables can be set to configure the Sentry client:
- `SIPI_SENTRY_ENVIRONMENT`: The environment in which the application is running. Defaults to `development`.
- `SIPI_SENTRY_RELEASE`: The release version number of the application. 

## Releases

Releases are published on Dockerhub: https://hub.docker.com/repository/docker/daschswiss/sipi

## How should I write my commits?

We are using [Conventional Commit messages](https://www.conventionalcommits.org/).

The most important prefixes you should have in mind are:

* `fix:` which represents bug fixes, and correlates to a [SemVer](https://semver.org/)
  patch.
* `feat:` which represents a new feature, and correlates to a SemVer minor.
* `feat!:`,  or `fix!:`, `refactor!:`, etc., which represent a breaking change
  (indicated by the `!`) and will result in a SemVer major.

This is the complete list of what is defined and if it is visible in the
changelog:
- 'feat' -> section: 'Features'
- 'feature' -> section: 'Features'
- 'fix' -> section: 'Bug Fixes'
- 'perf' -> section: 'Performance Improvements'
- 'revert' -> section: 'Reverts'
- 'docs' -> section: 'Documentation', hidden: true
- 'style' -> section: 'Styles', hidden: true
- 'chore' -> section: 'Miscellaneous Chores', hidden: true
- 'refactor' -> section: 'Code Refactoring', hidden: true
- 'test' -> section: 'Tests', hidden: true
- 'build' -> section: 'Build System', hidden: true
- 'ci' -> section: 'Continuous Integration', hidden: true
```

# Contact Information

DaSCH Support `<support@dasch.swiss>`
