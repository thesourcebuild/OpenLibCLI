@echo off
:: =============================================================================
::  build.bat - Unified build dispatcher for OpenLibCLI
::
::  Routes to the appropriate build script based on the first argument:
::
::    build.bat                     Auto-detect toolchain (MSVC -> GCC)
::    build.bat msvc [args...]      MSVC + NMake          (build_msvc.bat)
::    build.bat gcc [args...]       GCC + GNU Make         (build_mingw_gcc.bat)
::    build.bat mingw [args...]     Alias for gcc
::    build.bat cmake [args...]     CMake                 (build_cmake.bat)
::    build.bat meson [args...]     Meson                 (build_meson.bat)
::    build.bat all [args...]       All available toolchains
::
::  All remaining arguments are passed through to the sub-script.
:: =============================================================================
setlocal enabledelayedexpansion
set "SCRIPT_DIR=%~dp0"

:: --------------------------------------------------------------------------
::  Parse: toolchain name (if first arg matches) + build REST from remaining
:: --------------------------------------------------------------------------
set "TOOLCHAIN="
set "REST="

:parse_args
if "%~1"=="" goto :done_parse

if /i "%~1"=="help"     if "!TOOLCHAIN!"=="" goto :show_help
if /i "%~1"=="--help"   if "!TOOLCHAIN!"=="" goto :show_help
if /i "%~1"=="-h"       if "!TOOLCHAIN!"=="" goto :show_help

:: First arg matching a known toolchain — store it, skip it
if /i "%~1"=="msvc" (
    if "!TOOLCHAIN!"=="" ( set "TOOLCHAIN=msvc" & shift & goto :parse_args )
)
if /i "%~1"=="gcc" (
    if "!TOOLCHAIN!"=="" ( set "TOOLCHAIN=gcc" & shift & goto :parse_args )
)
if /i "%~1"=="mingw" (
    if "!TOOLCHAIN!"=="" ( set "TOOLCHAIN=gcc" & shift & goto :parse_args )
)
if /i "%~1"=="cmake" (
    if "!TOOLCHAIN!"=="" ( set "TOOLCHAIN=cmake" & shift & goto :parse_args )
)
if /i "%~1"=="meson" (
    if "!TOOLCHAIN!"=="" ( set "TOOLCHAIN=meson" & shift & goto :parse_args )
)
if /i "%~1"=="all" (
    if "!TOOLCHAIN!"=="" ( set "TOOLCHAIN=all" & shift & goto :parse_args )
)

:: Not a toolchain name (or already consumed one) — append to REST
if "!REST!"=="" ( set "REST=%~1" ) else ( set "REST=!REST! %~1" )
shift
goto :parse_args

:done_parse

if "!TOOLCHAIN!"=="" set "TOOLCHAIN=auto"

:: --------------------------------------------------------------------------
::  Dispatch
:: --------------------------------------------------------------------------
if /i "!TOOLCHAIN!"=="msvc"  echo [build] Toolchain: MSVC   & call "%SCRIPT_DIR%build_msvc.bat" !REST!   & exit /b !ERRORLEVEL!
if /i "!TOOLCHAIN!"=="gcc"   echo [build] Toolchain: GCC    & call "%SCRIPT_DIR%build_mingw_gcc.bat" !REST!    & exit /b !ERRORLEVEL!
if /i "!TOOLCHAIN!"=="cmake" echo [build] Toolchain: CMake  & call "%SCRIPT_DIR%build_cmake.bat" !REST!  & exit /b !ERRORLEVEL!
if /i "!TOOLCHAIN!"=="meson" echo [build] Toolchain: Meson  & call "%SCRIPT_DIR%build_meson.bat" !REST!  & exit /b !ERRORLEVEL!
if /i "!TOOLCHAIN!"=="all"   goto :dispatch_all

:: --------------------------------------------------------------------------
::  Auto-detect: MSVC -> MinGW
:: --------------------------------------------------------------------------
call :try_vcvars
where cl.exe >nul 2>&1
if !ERRORLEVEL! == 0 (
    echo [build] Auto-detected MSVC
    call "%SCRIPT_DIR%build_msvc.bat" !REST!
    exit /b !ERRORLEVEL!
)
where gcc.exe >nul 2>&1
if !ERRORLEVEL! == 0 (
    echo [build] Auto-detected GCC
    call "%SCRIPT_DIR%build_mingw_gcc.bat" !REST!
    exit /b !ERRORLEVEL!
)
echo.
echo  ERROR: No C compiler found. Install one of:
echo    a) Visual Studio 2019/2022 with "Desktop development with C++"
echo    b) MinGW-w64 (https://www.mingw-w64.org/) and add to PATH
echo.
exit /b 1

:dispatch_all
echo [build] Running all available toolchains...

call :try_vcvars
where cl.exe >nul 2>&1
if !ERRORLEVEL! == 0 (
    echo [build] --- MSVC ---
    call "%SCRIPT_DIR%build_msvc.bat" !REST!
    if !ERRORLEVEL! NEQ 0 exit /b !ERRORLEVEL!
) else ( echo [build] MSVC not available, skipping )

where gcc.exe >nul 2>&1
if !ERRORLEVEL! == 0 (
    echo [build] --- GCC ---
    call "%SCRIPT_DIR%build_mingw_gcc.bat" !REST!
    if !ERRORLEVEL! NEQ 0 exit /b !ERRORLEVEL!
) else ( echo [build] GCC not available, skipping )

where cmake >nul 2>&1
if !ERRORLEVEL! == 0 (
    echo [build] --- CMake ---
    call "%SCRIPT_DIR%build_cmake.bat" !REST!
    if !ERRORLEVEL! NEQ 0 exit /b !ERRORLEVEL!
) else ( echo [build] CMake not available, skipping )

where meson >nul 2>&1
if !ERRORLEVEL! == 0 (
    echo [build] --- Meson ---
    call "%SCRIPT_DIR%build_meson.bat" !REST!
    if !ERRORLEVEL! NEQ 0 exit /b !ERRORLEVEL!
) else ( echo [build] Meson not available, skipping )

echo [build] All toolchains done.
exit /b 0

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
            call "!CANDIDATE!" >nul 2>&1
            goto :eof
        )
        set "CANDIDATE=%ProgramFiles(x86)%\Microsoft Visual Studio\%%E\%%D\VC\Auxiliary\Build\vcvars64.bat"
        if exist "!CANDIDATE!" (
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
echo  OpenLibCLI - Unified build dispatcher
echo  =====================================
echo.
echo  Routes to the appropriate build script based on toolchain.
echo.
echo  Usage:
echo    scripts\build-helpers\build.bat                    Auto-detect
echo    scripts\build-helpers\build.bat msvc [args...]     MSVC + NMake
echo    scripts\build-helpers\build.bat gcc [args...]      GCC + GNU Make
echo    scripts\build-helpers\build.bat mingw [args...]    Alias for gcc
echo    scripts\build-helpers\build.bat cmake [args...]    CMake
echo    scripts\build-helpers\build.bat meson [args...]    Meson
echo    scripts\build-helpers\build.bat all [args...]      All available
echo.
echo  Arguments after the toolchain name are passed through.
echo  Run the sub-script directly for per-toolchain help:
echo    build_msvc.bat help
echo    build_mingw_gcc.bat help
echo    build_cmake.bat help
echo    build_meson.bat help
echo.
echo  Output directories:
echo    build\msvc\windows\       MSVC  (NMake)
echo    build\gcc\windows\        GCC   (GNU Make)
echo    build\cmake\windows\      CMake
echo    build\meson\windows\      Meson
echo.
exit /b 0
