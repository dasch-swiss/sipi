{ lib
, stdenv
, cmake
, cxxStandard ? null
}:

let
  src = ./.;
  version = "v8_4_1-01382N";
in
stdenv.mkDerivation {
  inherit src version;
  pname = "kakadu";

  cmakeFlags = [
    "-DCMAKE_BUILD_TYPE=Release"
    "-DCMAKE_FIND_FRAMEWORK=NEVER"
    "-DBUILD_SHARED_LIBS:BOOL=OFF"
  ] ++ lib.optionals (cxxStandard != null) [
    "-DCMAKE_CXX_STANDARD=${cxxStandard}"
  ];

  outputs = [ "bin" "dev" "out" ];

  # postFixup = '' '';

  # nativeBuildInputs = [ cmake ];

  # buildInputs = [ ];

  # propagatedBuildInputs = [ ];

  enableParallelBuilding = false;

  doCheck = true;

  meta = with lib; {
    description = "Kakadu is a closed-source library to encode and decode JPEG 2000 images. It implements the ISO/IEC 15444-1 standard fully in part 1, and partly in parts 2–3.";
    homepage = "https://kakadusoftware.com/";
    license = "proprietary";
    platforms = platforms.unix ++ platforms.windows;
    maintainers = "Ivan Subotic";
  };
}
