{
  description = "Sipi C++ build environment setup";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = { self, nixpkgs, flake-utils, ... }:
  flake-utils.lib.eachSystem ["x86_64-linux" "aarch64-linux" "x86_64-darwin" "aarch64-darwin"] (system:
    let
        pkgs = import nixpkgs {
            inherit system;
        };
    in
    rec {
      devShellPackages = with pkgs; [
          # TODO: extract only the dependencies provided through callPackage and not try to build the derivation
          # Include the package from packages.default defined on the pkgs.callPackage line
          # self'.packages.default

          # List other packages you want in your devShell
          # C++ Compiler is already part of stdenv
          # Build tool
          cmake
          gcovr # code coverage helper tool
          lcov # code coverage helper tool
          llvmPackages_19.llvm # llvm-cov, llvm-profdata (for gcovr coverage)

          # Build dependencies
          # Note: OpenSSL, libcurl, and libmagic libraries are built from source
          # by the build system (ext/). The packages below provide CLI tools only.
          asio # networking library needed for crow (microframework for the web)
          curl # curl CLI (library built from source)
          exiv2
          ffmpeg
          autoconf
          automake
          libtool # provides glibtoolize on macOS
          file # file CLI + autoreconf deps for libmagic build
          m4 # GNU M4, required by glibtoolize during libmagic autoreconf
          gettext
          glibcLocales # locales
          gperf
          iconv
          inih
          libidn
          libuuid # uuid und uuid-dev
          # numactl # libnuma-dev not available on mac
          libwebp
          openssl # openssl CLI (library built from source)
          readline70 # libreadline-dev

          # Other stuff
          pkg-config
          unzip
          # valgrind

          # Rust toolchain (for e2e test harness)
          rustc
          cargo
          hurl

          # additional test dependencies
          nginx
          libtiff
          graphicsmagick
          apacheHttpd
          imagemagick
          libxml2
          libxslt

        ];

        devShells = rec {
          # Use clang everywhere: simplifies sanitizer integration (ASan/UBSan/TSan),
          # aligns all build environments (Docker, Nix CI, Nix local, Zig all use Clang),
          # and eliminates "builds on GCC but warns on Clang" discrepancies.
          default = clang;

          # devShells.clang describes a shell with the clang compiler and libc++.
          # Uses libcxxStdenv so -stdlib=libc++ in CMakeLists.txt resolves correctly,
          # matching Docker (libc++-dev), Zig (bundled libc++), and macOS (system libc++).
          clang = pkgs.mkShell.override {stdenv = pkgs.llvmPackages_19.libcxxStdenv;} {
            name = "sipi";
            # Disable Nix hardening wrappers so --coverage, ASan, and UBSan
            # flags pass through to the compiler unmodified.
            hardeningDisable = ["all"];

            shellHook = ''
              export PS1="\\u@\\h | nix-develop> "
              export MKSHELL=clang
            '';

            packages = devShellPackages;
          };

          # Clang with libstdc++ (the default LLVM stdenv).  Nix's libFuzzer
          # (compiler-rt) is built against libstdc++, so fuzz targets must use
          # the same stdlib to avoid ABI mismatch.  The fuzz CI workflow uses
          # this shell instead of the libc++ 'clang' shell.
          fuzz = pkgs.mkShell.override {stdenv = pkgs.llvmPackages_19.stdenv;} {
            name = "sipi-fuzz";
            hardeningDisable = ["all"];

            shellHook = ''
              export PS1="\\u@\\h | nix-develop-fuzz> "
              export MKSHELL=fuzz
            '';

            packages = devShellPackages;
          };

          # devShells.default describes the default shell with C++, cmake,
          # and other dependencies
          gcc = pkgs.mkShell.override {stdenv = pkgs.gcc14Stdenv;} {
            name = "sipi";

            shellHook = ''
              export PS1="\\u@\\h | nix-develop> "
              export MKSHELL=default
            '';

            packages = devShellPackages;
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
