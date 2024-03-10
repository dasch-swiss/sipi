# List all recipies
default:
    just --list --unsorted

doxygen-serve: # Serve the doxygen documentation. Needs to be first built through cmake.
    cd ./doxygen/generated/html && python3 -m http.server

# Start Docker container for running the binary
docker-run:
    docker run -it --rm -v /Users/subotic/_github.com/dasch-swiss/sipi:/app -v /Volumes/PhotoArchive:/scratch -w /app daschswiss/remote-sipi-env:1.0 /bin/bash
