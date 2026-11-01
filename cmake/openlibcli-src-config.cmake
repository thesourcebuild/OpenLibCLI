# OpenLibCLI source package (scripts/package-helpers/package_cmake.*): the library is
# compiled as part of the project that finds it.
#
#   find_package(OpenLibCLI REQUIRED)
#   target_link_libraries(my_app PRIVATE OpenLibCLI::OpenLibCLI)
#
# Layout of the package:
#   include/OpenLibCLI/     public headers (config/, utils/)
#   share/OpenLibCLI/src/   the library's source tree (sources and their headers)
#   lib/cmake/OpenLibCLI/   this file and the version file

get_filename_component(_OPENLIBCLI_PREFIX "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)

# Several find_package() calls in one project define the target once.
if(NOT TARGET OpenLibCLI::OpenLibCLI)
    add_library(OpenLibCLI STATIC
        "${_OPENLIBCLI_PREFIX}/share/OpenLibCLI/src/cli.c"
        "${_OPENLIBCLI_PREFIX}/share/OpenLibCLI/src/utils/cli_args_parse.c")
    target_include_directories(OpenLibCLI PUBLIC "${_OPENLIBCLI_PREFIX}/include/OpenLibCLI")
    if(WIN32)
        target_link_libraries(OpenLibCLI PUBLIC ws2_32)
    endif()
    add_library(OpenLibCLI::OpenLibCLI ALIAS OpenLibCLI)
endif()

# The name find_package() was called with (OpenLibCLI or openlibcli).
set(${CMAKE_FIND_PACKAGE_NAME}_FOUND TRUE)
