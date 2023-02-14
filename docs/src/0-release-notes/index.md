Main releases
====================

v3.1.0 Release Notes
--------------------

See the
[release 3.1.0](https://github.com/dasch-swiss/sipi/releases/tag/3.1.0) on
GitHub.

### New features:

- Added sidecar support (Kakadu)
- Published on Dockerhub https://hub.docker.com/r/daschswiss/sipi under the V3.1.0 tag.

v3.0.1 Release Notes
--------------------

See the
[release 3.0.1](https://github.com/dasch-swiss/sipi/releases/tag/v3.0.1) on
GitHub.

### New features:

- Compliant with the [IIIF Image API 3.0](https://iiif.io/api/image/3.0/)
- Published on Dockerhub https://hub.docker.com/r/daschswiss/sipi (new URL) under the V3.0.1 tag.

### Bug fixes: 

- Fix parse URL crash

v2.0.0 Release Notes
--------------------
See the
[release 2.0.0](https://github.com/dasch-swiss/sipi/releases/tag/v2.0.0) on
GitHub.

### New features:

- Published on Dockerhub https://hub.docker.com/r/dhlabbasel/sipi under the v2.0.0 tag.

v1.4.0 Release Notes
--------------------

See the
[release 1.4.0](https://github.com/dasch-swiss/Sipi/releases/tag/v1.4.0) on
GitHub.

### New features:

-   Added latest kakadu version v7\_A\_4-01727L.zip
-   support for CIELab for both 8- and 16-bit images
-   try/catch for ICC profiles that are not supported by kakadu. These
    profiles are added to the "essential metadata" in order to be
    reinstated if the JPX is converted back to a TIFF or JPEG.
-   added more unit tests

### Bug fixes:

-   16 Bit PNG images are now teated correctly by byteswapping of the
    data (htons), since PNG uses network byte order which is usually
    noit zthe host byte order on intel processors
-   alpha channels are treated correctly with JPEG2000
-   The parameter names in a multidata/form-data POST request now have
    the double quotes removed

v1.3.0 Release Notes
--------------------

See the
[release 1.3.0](https://github.com/dasch-swiss/sipi/releases/tag/v1.3.0) on
GitHub.

### New features:

-   Added latest kakadu version v7\_A\_4-01727L.zip
-   support for CIELab for both 8- and 16-bit images
-   try/catch for ICC profiles that are not supported by kakadu. These
    profiles are added to the "essential metadata" in order to be
    reinstated if the JPX is converted back to a TIFF or JPEG.
-   added more unit tests

### Bug fixes:

-   16 Bit PNG images are now teated correctly by byteswapping of the
    data (htons), since PNG uses network byte order which is usually
    noit zthe host byte order on intel processors
-   alpha channels are treated correctly with JPEG2000
-   The parameter names in a multidata/form-data POST request now have
    the double quotes removed

v1.2.0 Release Notes
--------------------

See the
[release 1.2.0](https://github.com/dasch-swiss/Sipi/releases/tag/v1.2.0) on
GitHub.

v1.1.0 Release Notes
--------------------

See the
[release 1.1.0](https://github.com/dasch-swiss/sipi/releases/tag/v1.1.0) on
GitHub.

