# cmake/dependencies.cmake — External dependency manifest
#
# Single source of truth for all external dependency metadata.
# Update versions here, then run `make vendor-download` to fetch archives.
#
# Usage in ext/*/CMakeLists.txt:
#   Variables are available after root CMakeLists.txt does:
#     include(cmake/dependencies.cmake)
#   Then use: DEP_<NAME>_VERSION, DEP_<NAME>_URL, DEP_<NAME>_SHA256, DEP_<NAME>_FILENAME

# --- xz (liblzma) --- public domain / LGPL-2.1-or-later
set(DEP_XZ_VERSION "5.2.5")
set(DEP_XZ_URL "https://tukaani.org/xz/xz-5.2.5.tar.gz")
set(DEP_XZ_FILENAME "xz-5.2.5.tar.gz")
set(DEP_XZ_SHA256 "f6f4910fd033078738bd82bfba4f49219d03b17eb0794eb91efbae419f4aba10")

# --- zlib --- zlib license
set(DEP_ZLIB_VERSION "1.3.2")
set(DEP_ZLIB_URL "https://zlib.net/zlib-1.3.2.tar.gz")
set(DEP_ZLIB_FILENAME "zlib-1.3.2.tar.gz")
set(DEP_ZLIB_SHA256 "bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16")

# --- zstd (pinned release, was tracking mutable dev branch) --- BSD-3-Clause / GPL-2.0
set(DEP_ZSTD_VERSION "1.5.7")
set(DEP_ZSTD_URL "https://github.com/facebook/zstd/archive/refs/tags/v1.5.7.tar.gz")
set(DEP_ZSTD_FILENAME "zstd-1.5.7.tar.gz")
set(DEP_ZSTD_SHA256 "37d7284556b20954e56e1ca85b80226768902e2edabd3b649e9e72c0c9012ee3")

# --- bzip2 --- bzip2 license (BSD-like)
set(DEP_BZIP2_VERSION "1.0.8")
set(DEP_BZIP2_URL "https://gitlab.com/bzip2/bzip2/-/archive/bzip2-1.0.8/bzip2-bzip2-1.0.8.tar.bz2")
set(DEP_BZIP2_FILENAME "bzip2-bzip2-1.0.8.tar.bz2")
set(DEP_BZIP2_SHA256 "1afcfe4d3eb95bac3b7f43588c9457ccf772360f7e657ba7279fac776f2469d4")

# --- openssl --- Apache-2.0
set(DEP_OPENSSL_VERSION "3.4.1")
set(DEP_OPENSSL_URL "https://github.com/openssl/openssl/releases/download/openssl-3.4.1/openssl-3.4.1.tar.gz")
set(DEP_OPENSSL_FILENAME "openssl-3.4.1.tar.gz")
set(DEP_OPENSSL_SHA256 "002a2d6b30b58bf4bea46c43bdd96365aaf8daa6c428782aa4feee06da197df3")

# --- curl --- MIT/X (curl license)
set(DEP_CURL_VERSION "8.12.1")
set(DEP_CURL_URL "https://curl.se/download/curl-8.12.1.tar.gz")
set(DEP_CURL_FILENAME "curl-8.12.1.tar.gz")
set(DEP_CURL_SHA256 "7b40ea64947e0b440716a4d7f0b7aa56230a5341c8377d7b609649d4aea8dbcf")

# --- libmagic (file) --- BSD-2-Clause
set(DEP_LIBMAGIC_VERSION "FILE5_46")
set(DEP_LIBMAGIC_URL "https://github.com/file/file/archive/refs/tags/FILE5_46.tar.gz")
set(DEP_LIBMAGIC_FILENAME "file-FILE5_46.tar.gz")
set(DEP_LIBMAGIC_SHA256 "73c5f11a8edf0fded2fe3471b23a7fccb3f3369a13ea612529b869c8dc96aa2b")

# --- jpeg (IJG libjpeg) --- IJG license (BSD-like)
set(DEP_JPEG_VERSION "9f")
set(DEP_JPEG_URL "https://ijg.org/files/jpegsrc.v9f.tar.gz")
set(DEP_JPEG_FILENAME "jpegsrc.v9f.tar.gz")
set(DEP_JPEG_SHA256 "04705c110cb2469caa79fb71fba3d7bf834914706e9641a4589485c1f832565b")

# --- jbigkit --- GPL-2.0-or-later
set(DEP_JBIGKIT_VERSION "2.1")
set(DEP_JBIGKIT_URL "https://www.cl.cam.ac.uk/~mgk25/jbigkit/download/jbigkit-2.1.tar.gz")
set(DEP_JBIGKIT_FILENAME "jbigkit-2.1.tar.gz")
set(DEP_JBIGKIT_SHA256 "de7106b6bfaf495d6865c7dd7ac6ca1381bd12e0d81405ea81e7f2167263d932")

# --- webp --- BSD-3-Clause
set(DEP_WEBP_VERSION "1.2.0")
set(DEP_WEBP_URL "https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.2.0.tar.gz")
set(DEP_WEBP_FILENAME "libwebp-1.2.0.tar.gz")
set(DEP_WEBP_SHA256 "2fc8bbde9f97f2ab403c0224fb9ca62b2e6852cbc519e91ceaa7c153ffd88a0c")

# --- expat --- MIT
set(DEP_EXPAT_VERSION "2.5.0")
set(DEP_EXPAT_URL "https://github.com/libexpat/libexpat/releases/download/R_2_5_0/expat-2.5.0.tar.bz2")
set(DEP_EXPAT_FILENAME "expat-2.5.0.tar.bz2")
set(DEP_EXPAT_SHA256 "6f0e6e01f7b30025fa05c85fdad1e5d0ec7fd35d9f61b22f34998de11969ff67")

# --- png (libpng) --- libpng license (BSD-like)
set(DEP_PNG_VERSION "1.6.41")
set(DEP_PNG_URL "https://sourceforge.net/projects/libpng/files/libpng16/1.6.41/libpng-1.6.41.tar.gz/download")
set(DEP_PNG_FILENAME "libpng-1.6.41.tar.gz")
set(DEP_PNG_SHA256 "f00a11840f60616bdced9056d0f4cf2e4897697db039f15ce911704f957d3c5d")

# --- tiff (libtiff) --- libtiff license (BSD-like)
set(DEP_TIFF_VERSION "4.7.0")
set(DEP_TIFF_URL "https://download.osgeo.org/libtiff/tiff-4.7.0.tar.gz")
set(DEP_TIFF_FILENAME "tiff-4.7.0.tar.gz")
set(DEP_TIFF_SHA256 "67160e3457365ab96c5b3286a0903aa6e78bdc44c4bc737d2e486bcecb6ba976")

# --- lcms2 (Little CMS) --- MIT
set(DEP_LCMS2_VERSION "2.16")
set(DEP_LCMS2_URL "https://github.com/mm2/Little-CMS/releases/download/lcms2.16/lcms2-2.16.tar.gz")
set(DEP_LCMS2_FILENAME "lcms2-2.16.tar.gz")
set(DEP_LCMS2_SHA256 "d873d34ad8b9b4cea010631f1a6228d2087475e4dc5e763eb81acc23d9d45a51")

# --- exiv2 --- GPL-2.0-or-later
set(DEP_EXIV2_VERSION "0.28.5")
set(DEP_EXIV2_URL "https://github.com/Exiv2/exiv2/archive/refs/tags/v0.28.5.tar.gz")
set(DEP_EXIV2_FILENAME "exiv2-0.28.5.tar.gz")
set(DEP_EXIV2_SHA256 "e1671f744e379a87ba0c984617406fdf8c0ad0c594e5122f525b2fb7c28d394d")

# --- jansson --- MIT
set(DEP_JANSSON_VERSION "2.13.1")
set(DEP_JANSSON_URL "https://www.digip.org/jansson/releases/jansson-2.13.1.tar.gz")
set(DEP_JANSSON_FILENAME "jansson-2.13.1.tar.gz")
set(DEP_JANSSON_SHA256 "f4f377da17b10201a60c1108613e78ee15df6b12016b116b6de42209f47a474f")

# --- lua --- MIT
set(DEP_LUA_VERSION "5.3.5")
set(DEP_LUA_URL "https://www.lua.org/ftp/lua-5.3.5.tar.gz")
set(DEP_LUA_FILENAME "lua-5.3.5.tar.gz")
set(DEP_LUA_SHA256 "0c2eed3f960446e1a3e4b9a1ca2f3ff893b6ce41942cf54d5dd59ab4b3b058ac")

# --- luarocks --- MIT
set(DEP_LUAROCKS_VERSION "3.3.1")
set(DEP_LUAROCKS_URL "https://luarocks.github.io/luarocks/releases/luarocks-3.3.1.tar.gz")
set(DEP_LUAROCKS_FILENAME "luarocks-3.3.1.tar.gz")
set(DEP_LUAROCKS_SHA256 "eb20cd9814df05535d9aae98da532217c590fc07d48d90ca237e2a7cdcf284fe")

# --- sqlite3 --- public domain
set(DEP_SQLITE3_VERSION "3450200")
set(DEP_SQLITE3_URL "https://www.sqlite.org/2024/sqlite-autoconf-3450200.tar.gz")
set(DEP_SQLITE3_FILENAME "sqlite-autoconf-3450200.tar.gz")
set(DEP_SQLITE3_SHA256 "bc9067442eedf3dd39989b5c5cfbfff37ae66cc9c99274e0c3052dc4d4a8f6ae")

# --- sentry-native --- MIT
set(DEP_SENTRY_VERSION "0.9.0")
set(DEP_SENTRY_URL "https://github.com/getsentry/sentry-native/archive/refs/tags/0.9.0.tar.gz")
set(DEP_SENTRY_FILENAME "sentry-native-0.9.0.tar.gz")
set(DEP_SENTRY_SHA256 "657391465eb6236d6e3f3eec1d25434178783328f1f0a744c99c9b049c6225e1")

# --- prometheus-cpp --- MIT
set(DEP_PROMETHEUS_VERSION "1.3.0")
set(DEP_PROMETHEUS_URL "https://github.com/jupp0r/prometheus-cpp/archive/refs/tags/v1.3.0.tar.gz")
set(DEP_PROMETHEUS_FILENAME "prometheus-cpp-1.3.0.tar.gz")
set(DEP_PROMETHEUS_SHA256 "ac6e958405a29fbbea9db70b00fa3c420e16ad32e1baf941ab233ba031dd72ee")
