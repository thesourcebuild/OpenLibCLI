@echo off
:: =============================================================================
::  build_msvc.bat - OpenLibCLI MSVC / NMake build
::
::  Output layout
::  -------------
::    build\msvc\windows\binaries\   executables
::    build\msvc\windows\libs\       static/import/shared libraries and PDBs
::    build\msvc\windows\obj\        object files
::
::  VS auto-initialisation
::    If cl.exe is not in PATH the script locates and sources vcvars64.bat
::    automatically for VS 2019/2022 (Community / Professional / Enterprise).
:: =============================================================================
setlocal enabledelayedexpansion
set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%..\.." >nul

:: --------------------------------------------------------------------------
::  Parse arguments
:: --------------------------------------------------------------------------
set "TARGET="
set "BUILD_VARS="

:parse_args
if "%~1"=="" goto :done_args
if /i "%~1"=="help"       goto :show_help
if /i "%~1"=="--help"     goto :show_help
if /i "%~1"=="-h"         goto :show_help

if /i "%~1"=="BUILD_EXAMPLE_TELNET" goto :set_cli_build_from_next
if /i "%~1"=="BUILD_EXAMPLE_TCP" goto :set_cli_build_from_next
if /i "%~1"=="BUILD_EXAMPLE_SERIAL" goto :set_cli_build_from_next
if /i "%~1"=="BUILD_EXAMPLE_PIPE" goto :set_cli_build_from_next
if /i "%~1"=="BUILD_EXAMPLE_UNIX_SOCKET" goto :set_cli_build_from_next
echo %~1 | findstr /i /b "BUILD_EXAMPLE_TELNET= BUILD_EXAMPLE_TCP= BUILD_EXAMPLE_SERIAL= BUILD_EXAMPLE_PIPE= BUILD_EXAMPLE_UNIX_SOCKET=" >nul
if !ERRORLEVEL! == 0 goto :append_cli_build_assignment

if "!TARGET!"=="" ( set "TARGET=%~1" ) else ( set "TARGET=!TARGET! %~1" )
shift
goto :parse_args
:done_args

if "!TARGET!"=="" set "TARGET=all"
goto :build

:append_cli_build_assignment
if "!BUILD_VARS!"=="" (
    set "BUILD_VARS=%~1"
) else (
    set "BUILD_VARS=!BUILD_VARS! %~1"
)
shift
goto :parse_args

:set_cli_build_from_next
if "%~2"=="" goto :append_target_literal
if "!BUILD_VARS!"=="" (
    set "BUILD_VARS=%~1=%~2"
) else (
    set "BUILD_VARS=!BUILD_VARS! %~1=%~2"
)
shift
shift
goto :parse_args

:append_target_literal
if "!TARGET!"=="" ( set "TARGET=%~1" ) else ( set "TARGET=!TARGET! %~1" )
shift
goto :parse_args

:: --------------------------------------------------------------------------
::  MSVC / NMake build
:: --------------------------------------------------------------------------
:build
call :try_vcvars
where cl.exe >nul 2>&1
if !ERRORLEVEL! NEQ 0 (
    echo.
    echo  ERROR: cl.exe not found. Run from a Visual Studio Developer Prompt
    echo         or install "Desktop development with C++".
    exit /b 1
)
where nmake.exe >nul 2>&1
if !ERRORLEVEL! NEQ 0 (
    echo.
    echo  ERROR: nmake.exe not found. Run from a Visual Studio Developer Prompt
    echo         or install "Desktop development with C++".
    exit /b 1
)
echo [build_msvc] Toolchain  : MSVC  (cl.exe)
echo [build_msvc] Build tool : nmake /f Makefile.nmake
echo [build_msvc] Output dir : build\msvc\windows\
if not "!BUILD_VARS!"=="" echo [build_msvc] Build vars  : !BUILD_VARS!
echo [build_msvc] Target     : !TARGET!
echo.
echo [build_msvc] Generating version header...
python "%SCRIPT_DIR%..\..\scripts\gen_version_header.py" >nul 2>&1
if !ERRORLEVEL! EQU 0 (
    echo [build_msvc] Regenerated cli\cli_version.h
) else (
    echo [build_msvc] Python not found, using existing cli\cli_version.h
)
nmake /nologo /f Makefile.nmake !BUILD_VARS! !TARGET!
exit /b !ERRORLEVEL!

:: --------------------------------------------------------------------------
::  Visual Studio vcvars auto-initialisation
:: --------------------------------------------------------------------------
:try_vcvars
where cl.exe >nul 2>&1
if !ERRORLEVEL! == 0 goto :eof
for %%E in (2022 2019) do (
    for %%D in (Enterprise Professional Community BuildTools) do (
        set "CANDIDATE=%ProgramFiles%\Microsoft Visual Studio\%%E\%%D\VC\Auxiliary\Build\vcvars64.bat"
        if exist "!CANDIDATE!" (
            echo [build_msvc] Initialising MSVC environment:
            echo              !CANDIDATE!
            call "!CANDIDATE!" >nul 2>&1
            goto :eof
        )
        set "CANDIDATE=%ProgramFiles(x86)%\Microsoft Visual Studio\%%E\%%D\VC\Auxiliary\Build\vcvars64.bat"
        if exist "!CANDIDATE!" (
            echo [build_msvc] Initialising MSVC environment:
            echo              !CANDIDATE!
            call "!CANDIDATE!" >nul 2>&1
            goto :eof
        )
    )
)
goto :eof

:: --------------------------------------------------------------------------
::  Help
:: --------------------------------------------------------------------------
:show_help
echo.
echo  OpenLibCLI - MSVC / NMake build
echo  ================================
echo.
echo  Output directories:
echo    build\msvc\windows\binaries
echo    build\msvc\windows\libs
echo    build\msvc\windows\obj
echo    build\msvc\windows\*.map
echo.
echo  Usage: scripts\build-helpers\build_msvc.bat [target] [BUILD_EXAMPLE_*=0^|1 ...]
echo.
echo  Targets:
echo    (none)        Build static + shared libraries and examples  [default]
echo    lib           Static library only
echo    shared        Shared library only
echo    telnet        Telnet server  (build\msvc\windows\binaries\cli_server_telnet.exe)
echo    tcp           TCP server   (build\msvc\windows\binaries\cli_server_tcp.exe)
echo    uart          Serial server demo  (build\msvc\windows\binaries\cli_server_serial.exe)
echo    run-telnet    Build + run telnet server on port 2323
echo    run-tcp       Build + run TCP server on port 2323
echo    run-serial      Build + run serial / stdin demo
echo    clean         Remove build\msvc\windows\ artefacts
echo    clean-all     Remove entire build\ tree
echo    help          Show this message
echo.
echo  Transport switches:
echo    BUILD_EXAMPLE_TELNET=0^|1      Enable Telnet transport  (default: 1)
echo    BUILD_EXAMPLE_TCP=0^|1         Enable TCP transport     (default: 0)
echo    BUILD_EXAMPLE_SERIAL=0^|1      Enable serial transport  (default: 1)
echo    BUILD_EXAMPLE_PIPE=0^|1        Enable pipe transport    (default: 0)
echo    BUILD_EXAMPLE_UNIX_SOCKET=0^|1 Enable UNIX socket       (default: 0)
echo.
echo  Examples:
echo    scripts\build-helpers\build_msvc.bat
echo    scripts\build-helpers\build_msvc.bat BUILD_EXAMPLE_TCP=1
echo    scripts\build-helpers\build_msvc.bat lib
echo.
exit /b 0
