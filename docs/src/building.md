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

### docker

We provide a docker image based on Ubuntu LTS releases, containing all
dependencies: <https://hub.docker.com/r/dhlabbasel/sipi-base/>
If you must build sipi, the preferred way is to build it inside a docker image. Docker must be installed on the system.
Then, in the top directory of the source tree, do

- ```make compile``` will download and compile SIPI within a docker container
- ```make test``` to run the full test suite
- ```make run``` to run SIPI in docker image

### macOS

You will need [Homebrew](http://brew.sh/) and at least OSX 10.11.5.

Prerequisites for building Sipi without its automated test framework:

    xcode-select --install
    brew install cmake
    brew install doxygen
    brew install openssl
    brew install libmagic
    brew install gettext
    brew install libidn

If you also want to run Sipi's tests:

    brew install nginx
    sudo chown -R $USER /usr/local/var/log/nginx/
    brew install imagemagick --with-openjpeg
    brew install python3
    pip3 install Sphinx
    pip3 install pytest
    pip3 install requests
    pip3 install psutil
    pip3 install iiif_validator

### Ubuntu 22.04

Prerequisites for building Sipi without its automated test framework:

    sudo apt-get install g++-12
    sudo apt-get install cmake
    sudo apt-get install libssl-dev
    sudo apt-get install doxygen
    sudo apt-get install libreadline-dev
    sudo apt-get install gettext
    sudo apt-get install libmagic-dev
    sudo apt-get install unzip
    sudo apt-get install libidn11-dev

If you also want to run Sipi's tests, you will need
[ImageMagick](http://www.imagemagick.org/), version 7.0.6 or higher. We
suggest compiling it from source:

    sudo apt-get install libtiff5-dev libjpeg-turbo8-dev libopenjp2-7-dev
    wget https://github.com/ImageMagick/ImageMagick/archive/7.0.6-0.tar.gz
    tar -xzf 7.0.6-0.tar.gz
    cd ImageMagick-7.0.6-0/
    ./configure
    make
    sudo make install
    sudo ldconfig /usr/local/lib

Then:

    sudo apt-get install ab
    sudo apt-get install nginx
    sudo chown -R $USER /var/log/nginx
    sudo apt-get install python3
    sudo apt-get install python3-pip
    sudo -H pip3 install --upgrade pip
    sudo -H pip3 install Sphinx
    sudo -H pip3 install pytest
    sudo -H pip3 install requests
    sudo -H pip3 install psutil
    sudo -H pip3 install iiif_validator

### Debian 8

First, follow the instructions for ubuntu-build.

Then, CMake has to be patched. Unfortunately the version of CMake
provided by the Debian packages contains a bug and cannot find the
OpenSSL libraries and includes. To apply the patch, go to the Sipi
directory and run:

    sudo ./debian-cmake-patch.sh

### Docker

We provide a docker image based on Ubuntu LTS releases, containing all
dependencies: <https://hub.docker.com/r/dhlabbasel/sipi-base/>

## Compiling the Source Code

Start in the `build` subdirectory of the source tree:

    cd build

Then compile Sipi:

    cmake ..
    make

By default, Sipi is built without optimization and with debug
information output. To compile Sipi with optimization level 3, run:

    cmake .. -DMAKE_DEBUG:BOOL=OFF
    make

## Running Tests

You can run the automated tests in the `build` directory like this:

    make test // will run all tests (minimum output)
    ctest --verbose // will run all tests (detailed output)
    make check // will run only e2e tests (detailed output)

## Making a Directory Tree for Installing Sipi

In `build`, type this to install Sipi in the `local` subdirectory of the
source tree:

    make install

You can then copy the contents of `local` to the desired location.

Generating Documentation
------------------------

To generate this manual in HTML format, `cd` to the `manual`
subdirectory of the source tree and type:

    make html

You will then find the manual under `manual/_build/html`.

To generate developer documentation about Sipi's C++ internals, `cd` to
the `build` directory and type:

    make doc

You will find the developer documentation in HTML format under
`doc/html`. To generate developer documentation in PDF format, first
ensure that you have [LaTeX](https://www.latex-project.org/) installed.
Then `cd` to `doc/html/latex` and type `make`.

Starting Over
-------------

To delete the previous build and start over from scratch, `cd` to the
top level of the source tree and type:

    rm -rf build/* lib local extsrcs include/*_icc.h

Building inside Docker
----------------------

All that was described before, can also be done by using docker. All
commands need to be executed from inside the source directory (and not
`build` the build directory). Also, Docker needs to be installed on the
system.

    // deletes cached image and needs only to be used when newer image is available on dockerhub
    docker image rm --force dhlabbasel/sipi-base:18.04
    // building
    docker run --rm -v $PWD:/sipi dhlabbasel/sipi-base:18.04 /bin/sh -c "cd /sipi/build; cmake .. && make"
    // building and running all tests
    docker run --rm -v $PWD:/sipi dhlabbasel/sipi-base:18.04 /bin/sh -c "cd /sipi/build; cmake .. && make && ctest --verbose"
    // make html documentation
    docker run --rm -v $PWD:/sipi dhlabbasel/sipi-base:18.04 /bin/sh -c "cd /sipi/manual; make html"

Since we mount the current source directory into the docker container,
all build artifacts can be accessed as if the build would have been
performed without docker.
