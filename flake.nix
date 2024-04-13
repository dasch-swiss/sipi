{
  description = "Sipi C++ build environment setup";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils, ... }:
  flake-utils.lib.eachSystem ["x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin"] (system:
    let
        overlays = [
            (final: prev: {
                abseil-cpp = final.callPackage ./package-abseil-cpp.nix {
                    cxxStandard = "23";
                    # stdenv = prev.gcc13Stdenv;
                 };
                protobuf = final.callPackage ./package-protobuf.nix {
                    cxxStandard = "23";
                    # stdenv = prev.gcc13Stdenv;
                };
                # The iiif-validator and opentelemetry-cpp are not yet part of nixpkgs, so we need to add it as an overlay
                iiif-validator = final.callPackage ./package-iiif-validator.nix { };
                opentelemetry-cpp = final.callPackage ./package-opentelemetry-cpp.nix {
                    cxxStandard = "23";
                    stdenv = prev.gcc13Stdenv;
                };
            })
        ];
        pkgs = import nixpkgs {
            inherit system overlays;
        };
    in
    {
        devShells = {

          # devShells.clang describes a shell with the clang compiler
          clang = pkgs.mkShell.override {stdenv = pkgs.clang16Stdenv;} {
            name = "sipi";

            shellHook = ''
              export PS1="\\u@\\h | nix-develop> "
            '';

            packages = with pkgs; [
              # List other packages you want in your devShell
              # C++ Compiler is already part of stdenv
              # Build tool
              cmake
              gcovr # code coverage helper tool
              lcov # code coverage helper tool

              # Build dependencies
              abseil-cpp
              asio # networking library needed for crow (microframework for the web)
              curl
              exiv2
              ffmpeg
              file # libmagic-dev
              gettext
              glibcLocales # locales
              gperf
              iconv
              inih
              libidn
              libuuid # uuid und uuid-dev
              # numactl # libnuma-dev not available on mac
              libwebp
              nlohmann_json
              openssl # libssl-dev
              opentelemetry-cpp # our own overlay
              protobuf
              readline70 # libreadline-dev
              pkg-config
              unzip
            ];
          };

          # devShells.default describes the default shell with C++, cmake,
          # and other dependencies
          default = pkgs.mkShell.override {stdenv = pkgs.gcc13Stdenv;} {
            name = "sipi";

            shellHook = ''
              export PS1="\\u@\\h | nix-develop> "
            '';

            packages = with pkgs; [
              # TODO: extract only the dependencies provided through callPackage and not try to build the derivation
              # Include the package from packages.default defined on the pkgs.callPackage line
              # self'.packages.default

              # List other packages you want in your devShell
              # C++ Compiler is already part of stdenv
              # Build tool
              cmake
              gcovr # code coverage helper tool
              lcov # code coverage helper tool

              # Build dependencies
              abseil-cpp
              asio # networking library needed for crow (microframework for the web)
              curl
              exiv2
              ffmpeg
              file # libmagic-dev
              gettext
              glibcLocales # locales
              gperf
              iconv
              inih
              libidn
              libuuid # uuid und uuid-dev
              # numactl # libnuma-dev not available on mac
              libwebp
              nlohmann_json
              openssl # libssl-dev
              opentelemetry-cpp # our own overlay
              protobuf
              readline70 # libreadline-dev

              # Other stuff
              # at
              doxygen
              pkg-config
              unzip
              # valgrind

              # additional test dependencies
              nginx
              libtiff
              graphicsmagick
              apacheHttpd
              imagemagick
              libxml2
              libxslt

              # Python dependencies
              python311Full
              python311Packages.deprecation
              python311Packages.docker
              python311Packages.pip
              python311Packages.psutil
              python311Packages.pytest
              python311Packages.requests
              python311Packages.sphinx
              python311Packages.testcontainers
              python311Packages.wrapt

              # added through overlay to nixpkgs
              iiif-validator
            ];
          };
        };

        # The `callPackage` automatically fills the parameters of the function
        # in package.nix with what's inside the `pkgs` attribute.
        packages.default = pkgs.callPackage ./package.nix {};

        # The `config` variable contains our own outputs, so we can reference
        # neighbor attributes like the package we just defined one line earlier.
        # devShells.default = config.packages.default;
    });
}
