# The cmake version of this build is meant to enable both cmake and .pc being exported
# this is important because grpc exports a .cmake file which also expects for protobuf
# to have been exported through cmake as well.
{ lib
, stdenv
, abseil-cpp
, buildPackages
, cmake
, fetchFromGitHub
, fetchpatch
, gtest
, zlib
, cxxStandard

  # downstream dependencies
, python3
, grpc

, ...
}:

stdenv.mkDerivation (finalAttrs: {
  pname = "protobuf";
  version = "26.1";

  src = fetchFromGitHub {
    owner = "protocolbuffers";
    repo = "protobuf";
    rev = "v${finalAttrs.version}";
    hash = "sha256-9sA+MYeDqRZl1v6HV4mpy60vqTbVTtinp9er6zkg/Ng=";
  };

  nativeBuildInputs = [
    cmake
  ] ++ lib.optionals (stdenv.hostPlatform != stdenv.buildPlatform) [
    # protoc of the same version must be available for build. For non-cross builds, it's able to
    # re-use the executable generated as part of the build
    buildPackages."protobuf_${lib.versions.major finalAttrs.version}"
  ];

  buildInputs = [
    gtest
    zlib
  ];

  propagatedBuildInputs = [
    abseil-cpp
  ];

  strictDeps = true;

  cmakeFlags = [
    "-Dprotobuf_BUILD_TESTS=OFF"
    "-Dprotobuf_ABSL_PROVIDER=package"
    "-Dprotobuf_BUILD_SHARED_LIBS=OFF"
  ] ++ lib.optionals (cxxStandard != null) [
    "-DCMAKE_CXX_STANDARD=${cxxStandard}"
  ];

  doCheck = false;

  passthru = {
    tests = {
      pythonProtobuf = python3.pkgs.protobuf.override (_: {
        protobuf = finalAttrs.finalPackage;
      });
      inherit grpc;
    };

    inherit abseil-cpp;
  };

  meta = {
    description = "Google's data interchange format";
    longDescription = ''
      Protocol Buffers are a way of encoding structured data in an efficient
      yet extensible format. Google uses Protocol Buffers for almost all of
      its internal RPC protocols and file formats.
    '';
    license = lib.licenses.bsd3;
    platforms = lib.platforms.all;
    homepage = "https://protobuf.dev/";
    maintainers = with lib.maintainers; [ jonringer ];
    mainProgram = "protoc";
  };
})
