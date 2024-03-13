{
  description = "Sipi C++ build environment setup";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
  };

  outputs = inputs @ {flake-parts, ...}:
    flake-parts.lib.mkFlake {inherit inputs;} {
      # This is the list of architectures that work with this project
      systems = [
        "x86_64-linux"
        "aarch64-linux"
        "x86_64-darwin"
        "aarch64-darwin"
      ];
      perSystem = {
        config,
        self',
        inputs',
        pkgs,
        system,
        ...
      }: {
        # devShells.default describes the default shell with C++, cmake,
        # and other dependencies
        devShells = {
          default = pkgs.mkShell.override {stdenv = pkgs.clang17Stdenv;} {
            name = "sipi";

            shellHook = ''
              export PS1="\\u@\\h | nix-develop> "
              unset SDKROOT
              unset CMAKE_FRAMEWORK_PATH
              unset CMAKE_INCLUDE_PATH
              unset CMAKE_LIBRARY_PATH
              # Any other environment tweaks

              # Explicitly tell CMake not to search the system paths
              export CMAKE_IGNORE_PATH="/System/Library/Frameworks"
              echo "Ignoring macOS System Frameworks for CMake"
              # Additional environment tweaks
            '';

            packages = with pkgs; [
              # TODO: extract only the dependencies provided through callPackage ant not try to build the derivation
              # Include the package from packages.default defined on the pkgs.callPackage line
              # self'.packages.default
              
              # List other packages you want in your devShell
              # C++ Compiler is already part of stdenv
              # Build tool
              cmake

              # Build dependencies
              ffmpeg
              file # libmagic-dev
              gettext
              glibcLocales # locales
              gperf
              iconv
              libidn
              libuuid # uuid und uuid-dev
              # numactl # libnuma-dev not available on mac
              libwebp
              openssl # libssl-dev
              readline70 # libreadline-dev

              # Other stuff
              # at
              doxygen
              pkg-config
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
              python311Packages.pip
              python311Packages.sphinx
              python311Packages.pytest
              python311Packages.requests
              python311Packages.psutil
            ];
          };
        };

        # The `callPackage` automatically fills the parameters of the function
        # in package.nix with what's inside the `pkgs` attribute.
        packages.default = pkgs.callPackage ./package.nix {};

        # The `config` variable contains our own outputs, so we can reference
        # neighbor attributes like the package we just defined one line earlier.
        # devShells.default = config.packages.default;
      };
    };
}
