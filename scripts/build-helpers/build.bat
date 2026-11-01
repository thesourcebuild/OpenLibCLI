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
::  Parse: an optional toolchain name as the first argument. Everything after it
::  is passed on exactly as typed (REST), never rebuilt from %~1 ... %~9, which
::  would drop quotes and split "-DVAR=VAL" at the '='.
:: --------------------------------------------------------------------------
set "TOOLCHAIN="
set "ARGS=%*"
set "REST=!ARGS!"
set "FIRST=%~1"

if /i "!FIRST!"=="help"   goto :show_help
if /i "!FIRST!"=="--help" goto :show_help
if /i "!FIRST!"=="-h"     goto :show_help

for %%T in (msvc gcc mingw cmake meson all) do (
    if /i "!FIRST!"=="%%T" set "TOOLCHAIN=%%T"
)
if defined TOOLCHAIN (
    rem Drop the first word; the rest keeps its original text (quotes, '=').
    set "REST="
    for /f "tokens=1,*" %%A in ("!ARGS!") do set "REST=%%B"
)
if /i "!TOOLCHAIN!"=="mingw" set "TOOLCHAIN=gcc"
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
