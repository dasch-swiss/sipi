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
with [Knora](https://dsp.dasch.swiss/). In addition, SIPI preserves most of
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

```bash
make docs-build
```

You will then find the manual under `site/index.html`.

## Building from Source

All commands should be run from inside the root of the repository.

### Setup Nix (One-time setup)

1. **Install Nix**:
  - On Linux and macOS, you can install Nix using the following command:
    ```bash
    sh <(curl -L https://nixos.org/nix/install) --daemon
    ```
  - Follow the instructions displayed on the screen to complete the installation.

2. **Enable Nix Flakes**:
- After installing Nix, enable the experimental flakes feature
  ```bash
  mkdir -p ~/.config/nix
  echo 'experimental-features = nix-command flakes' >> ~/.config/nix/nix.conf
  ```

3. **Enter the development environment**:
  - After installing Nix, you can enter the development environment configured specifically for this project by running:
    ```bash
    nix develop
    ```

### Build and Run Using Nix

Once inside the `nix develop` shell, you can use the following commands to build and run the application:

```bash
just build
just test
just run
```

This approach ensures that all necessary dependencies are automatically provided by Nix, offering a reproducible build
environment that works consistently across different systems.

**Note:** Building directly on macOS without Nix is not recommended and might lead to unpredictable build failures due
to differences in system configurations and library versions. We strongly advise using the Nix approach for a more
reliable and reproducible build process.

### Additional Information

- Using Nix not only abstracts away OS-specific details but also significantly simplifies dependency management.
- Should you encounter any issues with Nix, consider checking the official [Nix documentation](https://nixos.org/manual/nix/stable/) or the project's Nix scripts for any custom configurations that may affect the build process.

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
