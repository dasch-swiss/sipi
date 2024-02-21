{
  description = "Sipi C++ project setup";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/23.11";
    utils.url = "github:numtide/flake-utils";
  };

  outputs = inputs@{ self, nixpkgs, ...}: inputs.utils.lib.eachSystem [
    "aarch64-darwin" "aarch64-linux" "x86_64-linux"
  ] (system: let
    pkgs = import nixpkgs {
      inherit system;

      # Add overlays if you need to override the nixpkgs official packages.
      overlays = [];

      # Uncomment this if you need unfree softeare (e.g. cuda) for your project.
      # config.allowUnfree = true;
    };
  in {
    defShells.default = pkgs.mkShell rec {
      name = "sipi";

      packages = with pkgs; [
        
        # Build tool
        cmake

        llvmPackages_17.clang

        # Build dependencies
        ffmpeg
        file
        gettext
        gperf
        libacl1-dev
        libidn11-dev
        libnuma-dev
        libreadline-dev
        libmagic-dev
        libssl-dev
        locales
        openssl
        uuid
        uuid-dev
        
        # Other stuff
        at
        doxygen
        pkg-config
        valgrind

        # additional test dependencies
        nginx
        libtiff5-dev
        libopenjp2-7-dev
        graphicsmagick
        apache2-utils
        imagemagick
        libxml2
        libxml2-dev
        libxslt1.1
        libxslt1-dev
  
        # Python dependencies
        python311Full
        python311Packages.pip
        python311Packages.sphinx
        python311Packages.pytest
        python311Packages.requests
        python311Packages.psutil
      ];

    # Setting up the environment variables you need during development
    shelHook = let
      icon = "f121";
    in ''
      export PS1="$(echo -e '\u${icon}') {\[$(tput sgr0)\]\[\033[38;5;228m\]\w\[$(tput sgr0)\]\[\033[38;5;15m\]} (${name}) \\$ \[$(tput sgr0)\]"
    '';
    };

    packages.default = pkgs.callPackage ./default.nix {};
  });
    
}
