{ lib
, stdenv
, fetchFromGitLab
, nix-update-script

, cmake

, jbigkit
, lerc
, libdeflate
, libjpeg_original
, libpng
, xz
, zlib
, zstd

  # for passthru.tests
, libgeotiff
, python3Packages
, imagemagick
, graphicsmagick
, gdal
, openimageio
, freeimage
, testers

, cxxStandard ? null
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "libtiff";
  version = "4.6.0";

  # if you update this, please consider adding patches and/or
  # setting `knownVulnerabilities` in libtiff `4.5.nix`

  src = fetchFromGitLab {
    owner = "libtiff";
    repo = "libtiff";
    rev = "v${finalAttrs.version}";
    hash = "sha256-qCg5qjsPPynCHIg0JsPJldwVdcYkI68zYmyNAKUCoyw=";
  };

  patches = [
    # FreeImage needs this patch
    ./headers.patch
    # libc++abi 11 has an `#include <version>`, this picks up files name
    # `version` in the project's include paths
    ./rename-version.patch
    # patch dirinfo as it was in sipi before. Don't know why it was changed.
    # ./dirinfo.patch
  ];

  postPatch = ''
    # mv VERSION VERSION.txt
  '';

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
    "-DCMAKE_FIND_FRAMEWORK=NEVER"
    "-DBUILD_SHARED_LIBS:BOOL=OFF"
  ] ++ lib.optionals (cxxStandard != null) [
    "-DCMAKE_CXX_STANDARD=${cxxStandard}"
  ];

  outputs = [ "bin" "dev" "out" ];

  postFixup = ''
    cp $src/libtiff/tif_dir.h $dev/include/
    cp $src/libtiff/tif_hash_set.h $dev/include/
    cp $src/libtiff/tiffiop.h $dev/include/
  '';

  nativeBuildInputs = [ cmake ];

  buildInputs = [ ];

  propagatedBuildInputs = [
    jbigkit
    lerc
    libdeflate
    libjpeg_original
    libpng
    xz
    zlib
    zstd
  ];

  enableParallelBuilding = true;

  doCheck = true;

  passthru = {
    tests = {
      inherit libgeotiff imagemagick graphicsmagick gdal openimageio freeimage;
      inherit (python3Packages) pillow imread;
      pkg-config = testers.hasPkgConfigModules {
        package = finalAttrs.finalPackage;
      };
    };
    updateScript = nix-update-script { };
  };

  meta = with lib; {
    description = "Library and utilities for working with the TIFF image file format";
    homepage = "https://libtiff.gitlab.io/libtiff";
    changelog = "https://libtiff.gitlab.io/libtiff/v${finalAttrs.version}.html";
    license = licenses.libtiff;
    platforms = platforms.unix ++ platforms.windows;
    pkgConfigModules = [ "libtiff-4" ];
    maintainers = teams.geospatial.members;
  };
})
