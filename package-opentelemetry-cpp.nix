{ lib
, cacert
, stdenv
, fetchFromGitHub
, cmake
, abseil-cpp
, curl
, protobuf
, git
, nlohmann_json
, cxxStandard
}:

stdenv.mkDerivation (finalAttrs: rec {
  pname = "opentelemetry-cpp";
  version = "v1.14.2";

  src = fetchFromGitHub {
    owner = "open-telemetry";
    repo = "opentelemetry-cpp";
    rev = "refs/tags/${finalAttrs.version}";
    hash = "sha256-jLRUpB9aDvxsc7B42b08vN2rygN/ycgOyt78i2Hms0Q=";
  };

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
    "-DBUILD_SHARED_LIBS=OFF"
    "-DWITH_ABSEIL=ON"
    "-DWITH_OTLP_HTTP=ON"
    "-DWITH_OTLP_GRPC=OFF"
    "-DBUILD_TESTING=OFF"
    "-DWITH_FUNC_TESTS=OFF"
    "-DWITH_EXAMPLES=OFF"
    "-DCMAKE_CXX_STANDARD=${cxxStandard}"
  ];

  strictDeps = true;

  nativeBuildInputs = [ cmake git cacert ];

  buildInputs = [ abseil-cpp curl protobuf nlohmann_json ];

  meta = with lib; {
    description = "The C++ OpenTelemetry client.";
    homepage = "https://opentelemetry.io/";
    license = licenses.asl20;
    platforms = platforms.all;
    maintainers = [ maintainers.subotic ];
  };
})
