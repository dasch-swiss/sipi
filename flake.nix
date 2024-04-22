{
  description = "Sipi C++ build environment setup";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixpkgs-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils, ... }:
  flake-utils.lib.eachSystem ["x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin"] (system:
    let
        overlays = [
            (final: prev: {
                abseil-cpp = prev.callPackage ./nix-overlays/abseil-cpp {
                    cxxStandard = "23";
                    stdenv = prev.clang17Stdenv;
                };
                protobuf = prev.callPackage ./nix-overlays/protobuf {
                    cxxStandard = "23";
                    stdenv = prev.clang17Stdenv;
                };
                # The iiif-validator and opentelemetry-cpp are not yet part of nixpkgs, so we need to add it as an overlay
                iiif-validator = prev.callPackage ./nix-overlays/iiif-validator { };
                opentelemetry-cpp = final.callPackage ./nix-overlays/opentelemetry-cpp {
                    cxxStandard = "23";
                    stdenv = prev.clang17Stdenv;
                };
                libtiff-static = prev.callPackage ./nix-overlays/libtiff {
                    cxxStandard = "23";
                    stdenv = prev.clang17Stdenv;
                    jbigkit = prev.pkgsStatic.jbigkit;
                    libdeflate = prev.pkgsStatic.libdeflate;
                    libjpeg_original = prev.pkgsStatic.libjpeg_original;
                    libpng = prev.pkgsStatic.libpng;
                    # libwebp = prev.pkgsStatic.libwebp;
                    # libyuv = prev.pkgsStatic.libyuv;
                    xz = prev.pkgsStatic.xz;
                    zlib = prev.pkgsStatic.zlib;
                    zstd = prev.pkgsStatic.zstd;
                };
            })
        ];
        pkgs = import nixpkgs {
            inherit system overlays;
            config = {
                 packageOverrides = pkgs: rec {};
            };
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

            buildInputs = with pkgs; [
              # List other packages you want in your devShell
              # C++ Compiler is already part of stdenv
              # Build tool
              autoconf
              cmake
              doxygen
              gcovr # code coverage helper tool
              lcov # code coverage helper tool
              pkg-config
              unzip

              # Build dependencies
              asio # networking library needed for crow (microframework for the web)
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
              nlohmann_json
              readline70 # libreadline-dev

              # Build dependencies
              abseil-cpp # our own overlay
              libtiff-static # our own overlay
              openjpeg # our own overlay
              protobuf # our own overlay
              opentelemetry-cpp # our own overlay

              # additional test dependencies
              nginx
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

              iiif-validator # our own overlay
            ] ++ (with pkgs.pkgsStatic; [
                # static variants of the libraries
                bzip2
                curl
                # jbigkit
                # libjpeg_original
                # libpng
                libwebp
                openssl
                # xz
                # zlib
                # zstd
            ]);
          };
        };

        # The `callPackage` automatically fills the parameters of the function
        # in package.nix with what's inside the `pkgs` attribute.
        packages.default = pkgs.callPackage ./package.nix  {
            inherit (pkgs) abseil-cpp libtiff-static protobuf opentelemetry-cpp zlib;
            cxxStandard = "23";
            stdenv = pkgs.clang17Stdenv;
        };

        # The `config` variable contains our own outputs, so we can reference
        # neighbor attributes like the package we just defined one line earlier.
        # devShells.default = config.packages.default;
    });
}
