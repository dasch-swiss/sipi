Overview
========

Sipi is a high-performance, IIIF compatible media server developed by
the [Digital Humanities Lab](http://www.dhlab.unibas.ch) at the
[University of Basel](https://www.unibas.ch/en.html). It is designed to
be used by archives, libraries, and other institutions that need to
preserve high-quality images while making them available online.

Sipi implements the International Image Interoperability Framework
([IIIF](http://iiif.io/)), and efficiently converts between image
formats, preserving metadata contained in image files. In particular, if
images are stored in [JPEG 2000](https://jpeg.org/jpeg2000/) format,
Sipi can convert them on the fly to formats that are commonly used on
the Internet. Sipi offers a flexible framework for specifying
authentication and authorization logic in [Lua](https://www.lua.org/)
scripts, and supports restricted access to images, either by reducing
image dimensions or by adding watermarks. It can easily be integrated
with [Knora](http://www.knora.org/). In addition SIPI preserves most of
the [EXIF](http://www.exif.org),
[IPTC](https://iptc.org/standards/photo-metadata/iptc-standard/) and
[XMP](http://www.adobe.com/products/xmp.html) metadata and can preserve
or transform [ICC](https://en.wikipedia.org/wiki/ICC_profile) colour
profiles.

In addition, a simple webserver is integrated. The server is able to
serve most common file types. In addition Lua scripts and embedded Lua
(i.e., Lua embedded into HTML pages using the tags
&lt;lua&gt;…&lt;/lua&gt; are supported.

Sipi can also be used from the command line to convert images to/from
TIFF\_, [JPEG 2000](https://jpeg.org/jpeg2000/), JPEG\_ and PNG\_
formats. For all these conversion, Sipi tries to preserve all embedded
metadata such as
[IPTC](https://iptc.org/standards/photo-metadata/iptc-standard/),
[EXIF](http://www.exif.org),
[XMP](http://www.adobe.com/products/xmp.html) and
[ICC](https://en.wikipedia.org/wiki/ICC_profile) color profiles.
However, due to the limitations of some file formats, it cannot be
guaranteed that all metadata and ICC profiles are preserved. [JPEG
2000](https://jpeg.org/jpeg2000/) does not allow all types of
[ICC](https://en.wikipedia.org/wiki/ICC_profile) profiles. Unsupported
profile types will be added to the [JPEG
2000](https://jpeg.org/jpeg2000/) header as comment and will be
reinstated if the [JPEG 2000](https://jpeg.org/jpeg2000/) file is
converted back to the TIFF\_ format.

Sipi is [free software](http://www.gnu.org/philosophy/free-sw.en.html),
released under the [GNU Affero General Public
License](http://www.gnu.org/licenses/agpl-3.0.en.html). It is written in
C++ and runs on Linux (including [Debian](https://www.debian.org/),
[Ubuntu](https://www.ubuntu.com/), and
[CentOS](https://www.centos.org/)) and Mac OS X.

Freely distributable binary releases are available.
