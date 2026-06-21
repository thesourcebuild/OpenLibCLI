cmake_minimum_required(VERSION 3.14)
project(OpenLibCLI C)

include(CheckIPOSupported)
check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
if(ipo_supported)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
    message(STATUS "LTO not supported: ${ipo_error}")
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
include(CTest)

# =========================================================================
#  Output directories — all artefacts go under build/<platform>/
#
#  CMake puts its own generator subdirs (Debug/Release) inside these when
#  using multi-config generators (VS, Xcode).  For single-config (Makefiles,
#  Ninja) the directory is used directly.
# =========================================================================
if(WIN32)
    set(CLI_PLATFORM "windows")
elseif(APPLE)
    set(CLI_PLATFORM "macos")
elseif(CMAKE_CROSSCOMPILING)
    set(CLI_PLATFORM "embedded-baremetal")
else()
    set(CLI_PLATFORM "linux")
endif()

set(CLI_OUT_DIR "${CMAKE_BINARY_DIR}/${CLI_PLATFORM}")
set(CLI_BIN_DIR "${CLI_OUT_DIR}/binaries")
set(CLI_LIB_DIR "${CLI_OUT_DIR}/libs")

# Runtime executables
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY         "${CLI_BIN_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CLI_BIN_DIR}")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CLI_BIN_DIR}")

# Static / import libraries (.a, .lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY         "${CLI_LIB_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_DEBUG   "${CLI_LIB_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${CLI_LIB_DIR}")

# Shared libraries (.so, .dll, .dylib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY         "${CLI_LIB_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_DEBUG   "${CLI_LIB_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CLI_LIB_DIR}")

# =========================================================================
#  Generate cli/cli_version.h from version file
# =========================================================================
find_package(Python3 QUIET)
if(Python3_FOUND)
    add_custom_command(
        OUTPUT ${CMAKE_SOURCE_DIR}/cli/cli_version.h
        COMMAND "${Python3_EXECUTABLE}" ${CMAKE_SOURCE_DIR}/scripts/gen_version_header.py
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        MAIN_DEPENDENCY ${CMAKE_SOURCE_DIR}/version
        COMMENT "Generating cli/cli_version.h"
    )
    add_custom_target(gen_version ALL DEPENDS ${CMAKE_SOURCE_DIR}/cli/cli_version.h)
else()
    message(STATUS "Python3 not found — using existing cli/cli_version.h")
endif()

message(STATUS "Platform     : ${CLI_PLATFORM}")
file(MAKE_DIRECTORY "${CLI_BIN_DIR}")
file(MAKE_DIRECTORY "${CLI_LIB_DIR}")

message(STATUS "Output dir   : ${CLI_OUT_DIR}")

# =========================================================================
#  Optional transports
# =========================================================================
option(BUILD_EXAMPLE_TELNET "Build the Telnet transport" OFF)
option(BUILD_EXAMPLE_TCP "Build the TCP transport" OFF)
option(BUILD_EXAMPLE_SERIAL "Build the generic serial transport" OFF)
option(BUILD_EXAMPLE_PIPE "Build the pipe transport" OFF)
option(BUILD_EXAMPLE_UNIX_SOCKET "Build the UNIX socket transport" OFF)
option(BUILD_TESTING "Build the test suite" OFF)

# =========================================================================
#  Library type selection
# =========================================================================
option(BUILD_LIB_STATIC "Build the static library (default: shared only)" OFF)

# =========================================================================
#  Compiler warnings
# =========================================================================
if(MSVC)
    string(REGEX REPLACE "/W[0-4]" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    add_compile_options(/W4 /WX /D_CRT_SECURE_NO_WARNINGS)
else()
    add_compile_options(
        -Wall -Wextra -Wpedantic -Wshadow
        -Wstrict-prototypes -Wmissing-prototypes
        -Wno-unused-parameter
    )
endif()

# =========================================================================
#  Platform libraries
# =========================================================================
if(WIN32)
    set(PLATFORM_LIBS ws2_32)
else()
    set(PLATFORM_LIBS "")
endif()

# =========================================================================
#  Library sources
# =========================================================================
set(CLI_SOURCES
    cli/cli.c
    cli/utils/cli_args_parse.c
)

# =========================================================================
#  Static library  (opt-in via BUILD_LIB_STATIC)
# =========================================================================
if(BUILD_LIB_STATIC)
    add_library(cli_static STATIC ${CLI_SOURCES})
    target_include_directories(cli_static PUBLIC 
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cli>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/OpenLibCLI>
    )
    target_link_libraries(cli_static PUBLIC ${PLATFORM_LIBS})
    if(MSVC)
        set_target_properties(cli_static PROPERTIES OUTPUT_NAME openlibcli_static)
    else()
        set_target_properties(cli_static PROPERTIES OUTPUT_NAME OpenLibCLI)
    endif()
    if(TARGET gen_version)
        add_dependencies(cli_static gen_version)
    endif()
endif()

# =========================================================================
#  Shared library
# =========================================================================
add_library(cli_shared SHARED ${CLI_SOURCES})
target_include_directories(cli_shared PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cli>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/OpenLibCLI>
)
target_link_libraries(cli_shared PUBLIC ${PLATFORM_LIBS})
target_compile_definitions(cli_shared PRIVATE CLI_SHARED BUILD_LIB_SHARED PUBLIC CLI_SHARED)
set_target_properties(cli_shared PROPERTIES OUTPUT_NAME OpenLibCLI)
if(TARGET gen_version)
    add_dependencies(cli_shared gen_version)
endif()

# =========================================================================
#  Tests
# =========================================================================
if(BUILD_TESTING AND NOT CMAKE_CROSSCOMPILING)
    add_executable(test_cli_args_parse
        tests/test_cli_args_parse.c
        cli/utils/cli_args_parse.c)
    target_include_directories(test_cli_args_parse PRIVATE cli)
    add_test(NAME cli_args_parse COMMAND test_cli_args_parse)
endif()

# =========================================================================
#  Examples — only built for non-embedded (cross-compile) targets
# =========================================================================
if(NOT CMAKE_CROSSCOMPILING)

    # Shared demo sources compiled into every example
    set(DEMO_SHARED examples/shared/cli_demo_setup.c)
    if(NOT WIN32)
        find_package(Threads REQUIRED)
    endif()

    # Telnet server example
    if(BUILD_EXAMPLE_TELNET)
        add_executable(cli_server_telnet
            examples/win_linux/telnet/cli_server_telnet.c
            examples/win_linux/telnet/cli_transport_telnet.c
            ${DEMO_SHARED})
        target_link_libraries(cli_server_telnet PRIVATE cli_shared)
        if(TARGET cli_server_telnet)
            target_link_options(cli_server_telnet PRIVATE
                "$<$<C_COMPILER_ID:MSVC>:/MAP:${CLI_OUT_DIR}/cli_server_telnet.map>"
                "$<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wl,-Map,${CLI_OUT_DIR}/cli_server_telnet.map>"
            )
        endif()
    endif()

    # TCP server example
    if(BUILD_EXAMPLE_TCP)
        add_executable(cli_server_tcp
            examples/win_linux/tcp/cli_server_tcp.c
            examples/win_linux/tcp/cli_transport_tcp.c
            ${DEMO_SHARED})
        target_link_libraries(cli_server_tcp PRIVATE cli_shared)
        if(TARGET cli_server_tcp)
            target_link_options(cli_server_tcp PRIVATE
                "$<$<C_COMPILER_ID:MSVC>:/MAP:${CLI_OUT_DIR}/cli_server_tcp.map>"
                "$<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wl,-Map,${CLI_OUT_DIR}/cli_server_tcp.map>"
            )
        endif()
    endif()

    # Serial server example (stdin/stdout simulation on host)
    if(BUILD_EXAMPLE_SERIAL)
        add_executable(cli_server_serial
            examples/win_linux/serial/cli_server_serial.c
            examples/win_linux/serial/cli_transport_serial.c
            ${DEMO_SHARED})
        target_link_libraries(cli_server_serial PRIVATE cli_shared)
        if(TARGET cli_server_serial)
            target_link_options(cli_server_serial PRIVATE
                "$<$<C_COMPILER_ID:MSVC>:/MAP:${CLI_OUT_DIR}/cli_server_serial.map>"
                "$<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wl,-Map,${CLI_OUT_DIR}/cli_server_serial.map>"
            )
        endif()
    endif()

    if(BUILD_EXAMPLE_PIPE)
        add_executable(cli_server_pipe
            examples/win_linux/pipe/cli_server_pipe.c
            examples/win_linux/pipe/cli_transport_pipe.c
            ${DEMO_SHARED})
        target_link_libraries(cli_server_pipe PRIVATE cli_shared)
        if(NOT WIN32)
            target_link_libraries(cli_server_pipe PRIVATE Threads::Threads)
        endif()
        if(TARGET cli_server_pipe)
            target_link_options(cli_server_pipe PRIVATE
                "$<$<C_COMPILER_ID:MSVC>:/MAP:${CLI_OUT_DIR}/cli_server_pipe.map>"
                "$<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wl,-Map,${CLI_OUT_DIR}/cli_server_pipe.map>"
            )
        endif()
    endif()

    if((NOT WIN32) AND BUILD_EXAMPLE_UNIX_SOCKET)
        add_executable(cli_server_unix_socket
            examples/linux/unix_socket/cli_server_unix_socket.c
            examples/linux/unix_socket/cli_transport_unix_socket.c
            ${DEMO_SHARED})
        target_link_libraries(cli_server_unix_socket PRIVATE cli_shared Threads::Threads)
        if(TARGET cli_server_unix_socket)
            target_link_options(cli_server_unix_socket PRIVATE
                "$<$<C_COMPILER_ID:MSVC>:/MAP:${CLI_OUT_DIR}/cli_server_unix_socket.map>"
                "$<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wl,-Map,${CLI_OUT_DIR}/cli_server_unix_socket.map>"
            )
        endif()
    endif()

endif()

# =========================================================================
#  Install rules + CMake package support (find_package)
# =========================================================================
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

# Read the version from the plain-text 'version' file
file(READ "${CMAKE_SOURCE_DIR}/version" _RAW_VERSION)
string(STRIP "${_RAW_VERSION}" PROJECT_VERSION)

# --- Targets ---
set(_INSTALL_TARGETS cli_shared)
if(BUILD_LIB_STATIC)
    list(APPEND _INSTALL_TARGETS cli_static)
endif()
install(TARGETS ${_INSTALL_TARGETS}
        EXPORT  openlibcliTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

install(DIRECTORY cli/
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/OpenLibCLI
        FILES_MATCHING PATTERN "*.h")

# --- Export file (openlibcliTargets.cmake) ---
install(EXPORT openlibcliTargets
        FILE      openlibcliTargets.cmake
        NAMESPACE OpenLibCLI::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OpenLibCLI)

# --- Config version file ---
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/openlibcliConfigVersion.cmake"
    VERSION       "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

# --- Config file (from template) ---
configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/openlibcliConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/openlibcliConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OpenLibCLI)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/openlibcliConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/openlibcliConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OpenLibCLI)

# =========================================================================
#  Summary
# =========================================================================
message(STATUS "")
message(STATUS "Build type   : ${CMAKE_BUILD_TYPE}")
message(STATUS "C compiler   : ${CMAKE_C_COMPILER}")
message(STATUS "C standard   : C${CMAKE_C_STANDARD}")
message(STATUS "Platform libs: ${PLATFORM_LIBS}")
message(STATUS "Cross-compile: ${CMAKE_CROSSCOMPILING}")
message(STATUS "Library type : $<IF:$<BOOL:${BUILD_LIB_STATIC}>,static + shared,shared only>")
message(STATUS "Transports   :")
message(STATUS "  Telnet      : ${BUILD_EXAMPLE_TELNET}")
message(STATUS "  TCP         : ${BUILD_EXAMPLE_TCP}")
message(STATUS "  Serial      : ${BUILD_EXAMPLE_SERIAL}")
message(STATUS "  Pipe        : ${BUILD_EXAMPLE_PIPE}")
message(STATUS "  Unix socket : ${BUILD_EXAMPLE_UNIX_SOCKET}")
message(STATUS "")
message(STATUS "Targets:")
if(BUILD_LIB_STATIC)
    if(MSVC)
    message(STATUS "  cli_static         -> ${CLI_LIB_DIR}/openlibcli_static.lib")
    else()
    message(STATUS "  cli_static         -> ${CLI_LIB_DIR}/libopenlibcli${CMAKE_STATIC_LIBRARY_SUFFIX}")
    endif()
endif()
if(MSVC)
message(STATUS "  cli_shared         -> ${CLI_LIB_DIR}/OpenLibCLI.dll (+ OpenLibCLI.lib)")
elseif(WIN32)
message(STATUS "  cli_shared         -> ${CLI_LIB_DIR}/libopenlibcli.dll (+ libopenlibcli.dll.a)")
elseif(APPLE)
message(STATUS "  cli_shared         -> ${CLI_LIB_DIR}/libopenlibcli.dylib")
else()
message(STATUS "  cli_shared         -> ${CLI_LIB_DIR}/libopenlibcli.so")
endif()
if(NOT CMAKE_CROSSCOMPILING)
if(BUILD_EXAMPLE_TELNET)
message(STATUS "  cli_server_telnet   -> ${CLI_BIN_DIR}/cli_server_telnet")
endif()
if(BUILD_EXAMPLE_TCP AND BUILD_EXAMPLE_TELNET)
message(STATUS "  cli_server_tcp      -> ${CLI_BIN_DIR}/cli_server_tcp")
endif()
if(BUILD_EXAMPLE_SERIAL)
message(STATUS "  cli_server_serial   -> ${CLI_BIN_DIR}/cli_server_serial")
endif()
if(BUILD_EXAMPLE_PIPE)
message(STATUS "  cli_server_pipe     -> ${CLI_BIN_DIR}/cli_server_pipe")
endif()
if((NOT WIN32) AND BUILD_EXAMPLE_UNIX_SOCKET)
message(STATUS "  cli_server_unix_socket -> ${CLI_BIN_DIR}/cli_server_unix_socket")
endif()
endif()
message(STATUS "")
message(STATUS "Cross-compile example (ARM bare-metal):")
message(STATUS "  cmake -B build/cmake -DCMAKE_TOOLCHAIN_FILE=arm-none-eabi.cmake")
message(STATUS "  cmake --build build/cmake")
message(STATUS "  -> output: build/cmake/embedded-baremetal/libs/libopenlibcli.a")
