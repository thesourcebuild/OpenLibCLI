cmake_minimum_required(VERSION 3.14)
project(OpenLibCLI C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
# Before include(CTest), which would otherwise define BUILD_TESTING as ON first.
option(BUILD_TESTING "Build the test suite" OFF)
include(CTest)
# Before the targets: their install include directories use CMAKE_INSTALL_INCLUDEDIR.
include(GNUInstallDirs)

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

# Runtime executables; static / import libraries (.a, .lib); shared libraries (.so, .dll,
# .dylib). Set for every configuration: a multi-config generator (Visual Studio, Xcode,
# Ninja Multi-Config) would otherwise add a <Config>/ subdirectory for each one not set.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CLI_BIN_DIR}")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CLI_LIB_DIR}")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CLI_LIB_DIR}")
set(CLI_OUTPUT_CONFIGS Debug Release RelWithDebInfo MinSizeRel ${CMAKE_CONFIGURATION_TYPES})
list(REMOVE_DUPLICATES CLI_OUTPUT_CONFIGS)
foreach(cfg IN LISTS CLI_OUTPUT_CONFIGS)
    string(TOUPPER "${cfg}" cfg)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg} "${CLI_BIN_DIR}")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg} "${CLI_LIB_DIR}")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg} "${CLI_LIB_DIR}")
endforeach()

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
option(BUILD_ASAN "Build with AddressSanitizer" OFF)

# =========================================================================
#  Library type selection
# =========================================================================
option(BUILD_LIB_STATIC "Build the static library (default: shared only)" OFF)

# A shared library needs a dynamic loader: a bare-metal toolchain (CMAKE_SYSTEM_NAME Generic)
# has none, and CMake would build cli_shared as a second static archive of the same name.
# There the static library is the library.
get_property(CLI_HAS_SHARED GLOBAL PROPERTY TARGET_SUPPORTS_SHARED_LIBS)
if(BUILD_LIB_STATIC OR NOT CLI_HAS_SHARED)
    set(CLI_BUILD_STATIC ON)
else()
    set(CLI_BUILD_STATIC OFF)
endif()

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
#  AddressSanitizer  (GCC / Clang only; not MSVC)
# =========================================================================
if(BUILD_ASAN AND NOT MSVC)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fno-omit-frame-pointer -g")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -fsanitize=address")
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
#  Static library  (opt-in via BUILD_LIB_STATIC; the only library without shared support)
# =========================================================================
if(CLI_BUILD_STATIC)
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
    # Installed as OpenLibCLI::OpenLibCLI_static, or as OpenLibCLI::OpenLibCLI when it is the
    # only library.
    if(CLI_HAS_SHARED)
        set_target_properties(cli_static PROPERTIES EXPORT_NAME OpenLibCLI_static)
    else()
        set_target_properties(cli_static PROPERTIES EXPORT_NAME OpenLibCLI)
    endif()
    if(TARGET gen_version)
        add_dependencies(cli_static gen_version)
    endif()
endif()

# =========================================================================
#  Shared library  (where the platform supports one)
# =========================================================================
if(CLI_HAS_SHARED)
    add_library(cli_shared SHARED ${CLI_SOURCES})
    target_include_directories(cli_shared PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cli>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/OpenLibCLI>
    )
    target_link_libraries(cli_shared PUBLIC ${PLATFORM_LIBS})
    target_compile_definitions(cli_shared PRIVATE CLI_SHARED BUILD_LIB_SHARED PUBLIC CLI_SHARED)
    # Installed as OpenLibCLI::OpenLibCLI
    set_target_properties(cli_shared PROPERTIES OUTPUT_NAME OpenLibCLI EXPORT_NAME OpenLibCLI)
    # The shared library's path as built (its name differs per toolchain, its directory per
    # configuration), for build scripts that copy it: they never have to guess the file name.
    file(GENERATE OUTPUT "${CMAKE_BINARY_DIR}/openlibcli_shared_$<CONFIG>.txt"
         CONTENT "$<TARGET_FILE:cli_shared>")
    if(TARGET gen_version)
        add_dependencies(cli_shared gen_version)
    endif()
    set(CLI_LIB_TARGET cli_shared)
else()
    set(CLI_LIB_TARGET cli_static)
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

    # Engine behaviour tests link the sources directly (no shared-library export needed).
    add_executable(test_cli_engine
        tests/test_cli_engine.c
        ${CLI_SOURCES})
    target_include_directories(test_cli_engine PRIVATE cli)
    target_link_libraries(test_cli_engine PRIVATE ${PLATFORM_LIBS})
    add_test(NAME cli_engine COMMAND test_cli_engine)

    # Seeded random stress test (default seeds; longer runs: fuzz_cli_engine <seed> <n> <iters>).
    add_executable(fuzz_cli_engine
        tests/fuzz_cli_engine.c
        ${CLI_SOURCES})
    target_include_directories(fuzz_cli_engine PRIVATE cli)
    target_link_libraries(fuzz_cli_engine PRIVATE ${PLATFORM_LIBS})
    add_test(NAME cli_engine_fuzz COMMAND fuzz_cli_engine)
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
        target_link_libraries(cli_server_telnet PRIVATE ${CLI_LIB_TARGET})
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
        target_link_libraries(cli_server_tcp PRIVATE ${CLI_LIB_TARGET})
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
        target_link_libraries(cli_server_serial PRIVATE ${CLI_LIB_TARGET})
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
        target_link_libraries(cli_server_pipe PRIVATE ${CLI_LIB_TARGET})
        if(WIN32)
            target_link_libraries(cli_server_pipe PRIVATE advapi32)
        else()
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
        target_link_libraries(cli_server_unix_socket PRIVATE ${CLI_LIB_TARGET} Threads::Threads)
        if(TARGET cli_server_unix_socket)
            target_link_options(cli_server_unix_socket PRIVATE
                "$<$<C_COMPILER_ID:MSVC>:/MAP:${CLI_OUT_DIR}/cli_server_unix_socket.map>"
                "$<$<NOT:$<C_COMPILER_ID:MSVC>>:-Wl,-Map,${CLI_OUT_DIR}/cli_server_unix_socket.map>"
            )
        endif()
    endif()

endif()

# =========================================================================
#  Link-time optimisation for the shared library and the programs built here. Not for the
#  static library: it is installed for other builds, and its objects would hold
#  compiler-specific LTO bytecode that another compiler, compiler version or linker cannot
#  link. A CMAKE_INTERPROCEDURAL_OPTIMIZATION given by the user applies to every target as
#  usual. The check links a test program, so it runs only when there is a target to use it
#  (none in a bare-metal build, which has only the static library).
# =========================================================================
if(NOT DEFINED CMAKE_INTERPROCEDURAL_OPTIMIZATION)
    set(CLI_LTO_TARGETS)
    foreach(_t cli_shared test_cli_args_parse test_cli_engine fuzz_cli_engine cli_server_telnet
               cli_server_tcp cli_server_serial cli_server_pipe cli_server_unix_socket)
        if(TARGET ${_t})
            list(APPEND CLI_LTO_TARGETS ${_t})
        endif()
    endforeach()
    if(CLI_LTO_TARGETS)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error)
        if(ipo_supported)
            set_property(TARGET ${CLI_LTO_TARGETS} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
        else()
            message(STATUS "LTO not supported: ${ipo_error}")
        endif()
    endif()
endif()

# =========================================================================
#  Install rules + CMake package support (find_package)
# =========================================================================
include(CMakePackageConfigHelpers)

# Read the version from the plain-text 'version' file
file(READ "${CMAKE_SOURCE_DIR}/version" _RAW_VERSION)
string(STRIP "${_RAW_VERSION}" PROJECT_VERSION)

# --- Targets ---
set(_INSTALL_TARGETS)
if(CLI_HAS_SHARED)
    list(APPEND _INSTALL_TARGETS cli_shared)
endif()
if(CLI_BUILD_STATIC)
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
    "${CMAKE_CURRENT_BINARY_DIR}/openlibcli-config-version.cmake"
    VERSION       "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

# --- Config file (from template) ---
# Lower case with hyphens: the one spelling find_package() looks for under any case of the
# package name, so find_package(OpenLibCLI) and find_package(openlibcli) both work on
# case-sensitive file systems.
configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/openlibcliConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/openlibcli-config.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/OpenLibCLI)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/openlibcli-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/openlibcli-config-version.cmake"
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
if(NOT CLI_HAS_SHARED)
    message(STATUS "Library type : static only (no shared libraries on this platform)")
elseif(CLI_BUILD_STATIC)
    message(STATUS "Library type : static + shared")
else()
    message(STATUS "Library type : shared only")
endif()
message(STATUS "Transports   :")
message(STATUS "  Telnet      : ${BUILD_EXAMPLE_TELNET}")
message(STATUS "  TCP         : ${BUILD_EXAMPLE_TCP}")
message(STATUS "  Serial      : ${BUILD_EXAMPLE_SERIAL}")
message(STATUS "  Pipe        : ${BUILD_EXAMPLE_PIPE}")
message(STATUS "  Unix socket : ${BUILD_EXAMPLE_UNIX_SOCKET}")
message(STATUS "")
message(STATUS "Targets:")
# File names as the targets above produce them (OUTPUT_NAME and the platform's affixes).
if(CLI_BUILD_STATIC)
    get_target_property(_static_name cli_static OUTPUT_NAME)
    message(STATUS "  cli_static         -> ${CLI_LIB_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}${_static_name}${CMAKE_STATIC_LIBRARY_SUFFIX}")
endif()
if(CLI_HAS_SHARED)
    set(_shared_file "${CMAKE_SHARED_LIBRARY_PREFIX}OpenLibCLI${CMAKE_SHARED_LIBRARY_SUFFIX}")
    if(WIN32)
        # The DLL is a runtime file (next to the examples); the import library is an archive.
        message(STATUS "  cli_shared         -> ${CLI_BIN_DIR}/${_shared_file} (+ ${CLI_LIB_DIR}/${CMAKE_IMPORT_LIBRARY_PREFIX}OpenLibCLI${CMAKE_IMPORT_LIBRARY_SUFFIX})")
    else()
        message(STATUS "  cli_shared         -> ${CLI_LIB_DIR}/${_shared_file}")
    endif()
endif()
if(NOT CMAKE_CROSSCOMPILING)
if(BUILD_EXAMPLE_TELNET)
message(STATUS "  cli_server_telnet   -> ${CLI_BIN_DIR}/cli_server_telnet${CMAKE_EXECUTABLE_SUFFIX}")
endif()
if(BUILD_EXAMPLE_TCP)
message(STATUS "  cli_server_tcp      -> ${CLI_BIN_DIR}/cli_server_tcp${CMAKE_EXECUTABLE_SUFFIX}")
endif()
if(BUILD_EXAMPLE_SERIAL)
message(STATUS "  cli_server_serial   -> ${CLI_BIN_DIR}/cli_server_serial${CMAKE_EXECUTABLE_SUFFIX}")
endif()
if(BUILD_EXAMPLE_PIPE)
message(STATUS "  cli_server_pipe     -> ${CLI_BIN_DIR}/cli_server_pipe${CMAKE_EXECUTABLE_SUFFIX}")
endif()
if((NOT WIN32) AND BUILD_EXAMPLE_UNIX_SOCKET)
message(STATUS "  cli_server_unix_socket -> ${CLI_BIN_DIR}/cli_server_unix_socket${CMAKE_EXECUTABLE_SUFFIX}")
endif()
endif()
message(STATUS "")
message(STATUS "Cross-compile (ARM bare-metal), with a toolchain file of your own that sets")
message(STATUS "CMAKE_SYSTEM_NAME Generic, the cross-compiler and the MCU's CPU flags:")
message(STATUS "  cmake -B build/cmake -DCMAKE_TOOLCHAIN_FILE=<your-toolchain-file>.cmake")
message(STATUS "  cmake --build build/cmake")
message(STATUS "  -> output: build/cmake/embedded-baremetal/libs/libOpenLibCLI.a")
