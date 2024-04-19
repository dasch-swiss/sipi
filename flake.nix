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
                    stdenv = prev.clang17Stdenv;
                };
                protobuf = final.callPackage ./package-protobuf.nix {
                    cxxStandard = "23";
                    stdenv = prev.clang17Stdenv;
                };
                # The iiif-validator and opentelemetry-cpp are not yet part of nixpkgs, so we need to add it as an overlay
                iiif-validator = final.callPackage ./package-iiif-validator.nix { };
                opentelemetry-cpp = final.callPackage ./package-opentelemetry-cpp.nix {
                    cxxStandard = "23";
                    stdenv = prev.clang17Stdenv;
                };
            })
        ];
        pkgs = import nixpkgs {
            inherit system overlays;
        };
    in
    {
        devShells = {
          default = pkgs.mkShell.override {stdenv = pkgs.clang17Stdenv;} {
            name = "sipi";

            nativeBuildInputs = [ pkgs.git pkgs.cmake pkgs.openssl pkgs.cacert pkgs.autoconf ];

            shellHook = ''
                export PS1="\\u@\\h | nix-develop> "
            '';

            packages = with pkgs; [
              # List other packages you want in your devShell
              # C++ Compiler is already part of stdenv
              # Build tool
              autoconf
              cmake
              gcovr # code coverage helper tool
              lcov # code coverage helper tool

              # Build dependencies
              abseil-cpp # our own overlay
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
              protobuf # our own overlay
              readline70 # libreadline-dev
              doxygen
              pkg-config
              unzip

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
        packages.default = pkgs.callPackage ./package.nix  {
            inherit (pkgs) abseil-cpp protobuf opentelemetry-cpp;
            cxxStandard = "23";
            stdenv = pkgs.clang17Stdenv;
        };

        # The `config` variable contains our own outputs, so we can reference
        # neighbor attributes like the package we just defined one line earlier.
        # devShells.default = config.packages.default;
    });
}
