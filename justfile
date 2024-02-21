# List all recipies
default:
    just --list --unsorted

doxygen-serve: # Serve the doxygen documentation. Needs to be first built through cmake.
    cd ./doxygen/generated/html && python3 -m http.server
