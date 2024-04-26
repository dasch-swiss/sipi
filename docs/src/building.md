# Building SIPI from Source Code


## Prerequisites


To build SIPI from source code, you must have
[Kakadu](http://kakadusoftware.com/), a JPEG 2000 development toolkit
that is not provided with Sipi and must be licensed separately. The
Kakadu source code archive `v8_0_5-01727L.zip` must be placed in the
`vendor` subdirectory of the source tree before building Sipi.

Sipi's build process requires [CMake](https://cmake.org/) (minimal
Version 3.0.0), a C++ compiler that supports the C++11 standard (such as
[GCC](https://gcc.gnu.org) or [clang](https://clang.llvm.org/)), and
several libraries that are readily available on supported platforms. The
test framework requires [Python 3](https://www.python.org/), (version
3.5 or later), [Apache ab](https://httpd.apache.org/docs/2.4/programs/ab.html) (which is
assumed to be installed by default on macOS Sierra),
[nginx](https://nginx.org/en/), and a recent version of
[ImageMagick](http://www.imagemagick.org/). Instructions for installing
these prerequisites are given below.

The build process downloads and builds SIPI's other prerequisites.

SIPI uses the Adobe ICC Color profiles, which are automatically
downloaded by the build process into the file `icc.zip`. The user is
responsible for reading and agreeing with Adobe's license conditions,
which are specified in the file `Color Profile EULA.pdf`.

## Building from Source

All commands should be run from inside the root of the repository.

### Setup Nix (One-time setup)

1. **Install Nix**:
- On Linux and macOS, you can install Nix using the following command:
  ```bash
  sh <(curl -L https://nixos.org/nix/install) --daemon
  ```
- Follow the instructions displayed on the screen to complete the installation.

2. **Enable Nix Flakes**:
- After installing Nix, enable the experimental flakes feature
  ```bash
  mkdir -p ~/.config/nix
  echo 'experimental-features = nix-command flakes' >> ~/.config/nix/nix.conf
  ```

3. **Enter the development environment**:
- After installing Nix, you can enter the development environment configured specifically for this project by running:
  ```bash
  nix develop
  ```

### Build and Run Using Nix

Once inside the `nix develop` shell, you can use the following commands to build and run the application:

```bash
just build
just test
just run
```

This approach ensures that all necessary dependencies are automatically provided by Nix, offering a reproducible build
environment that works consistently across different systems.

**Note:** Building directly on macOS without Nix is not recommended and might lead to unpredictable build failures due
to differences in system configurations and library versions. We strongly advise using the Nix approach for a more
reliable and reproducible build process.

## Generating Documentation

The documentation is online at https://sipi.io.

To build it locally, you will need [MkDocs](https://www.mkdocs.org/).
In the root the source tree, type:

```bash
make docs-build
```

You will then find the manual under `site/index.html`.

## Starting Over

To delete the previous build and start over from scratch, `cd` to the
top level of the source tree and type:

```bash
just clean
```

## Building Docker Image

To build the docker image, you need to have docker installed on your
system. The following command will build the docker image:

```bash
just docker-build
```

## Smoke Testing

To run a smoke test against the created docker image, you can run:
    
```bash
just test-smoke
```
