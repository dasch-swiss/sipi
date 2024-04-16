include(FetchContent)

FetchContent_Declare(
        cli11
        URL https://github.com/CLIUtils/CLI11/releases/download/v1.9.1/CLI11.hpp
        URL_HASH SHA256=52a3bc829a000fb26645328c9093b014e08547dde50c66d4316aef624046dc4b
        DOWNLOAD_NO_EXTRACT TRUE
)

# Manually populate and check the properties
FetchContent_GetProperties(cli11)
if (NOT cli11_POPULATED)
    FetchContent_Populate(cli11)
endif ()

# message(STATUS "cli11 source dir: ${cli11_SOURCE_DIR}")
