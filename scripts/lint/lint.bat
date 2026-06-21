@echo off
setlocal enabledelayedexpansion
set "SCRIPT_DIR=%~dp0"
pushd "%SCRIPT_DIR%..\.." >nul

:: =============================================================================
::  lint.bat — Code formatting and static analysis for OpenLibCLI
:: =============================================================================
:: How to run
@REM .\scripts\lint\lint.bat
@REM .\scripts\lint\lint.bat check
@REM .\scripts\lint\lint.bat format
@REM
@REM # Using specific clang binaries
@REM You can force the scripts to use explicit clang binaries by setting
@REM the `CLANG_FORMAT_EXE` and `CLANG_TIDY_EXE` environment variables.
@REM PowerShell (current session):
@REM   $env:CLANG_FORMAT_EXE = 'C:\Path\to\clang-format.exe'
@REM   $env:CLANG_TIDY_EXE  = 'C:\Path\to\clang-tidy.exe'
@REM   .\scripts\lint\lint.bat check
@REM CMD (current session):
@REM   set CLANG_FORMAT_EXE=C:\Path\to\clang-format.exe
@REM   set CLANG_TIDY_EXE=C:\Path\to\clang-tidy.exe
@REM   scripts\lint\lint.bat check


set "MODE=all"
if /i "%~1"=="format" set "MODE=format"
if /i "%~1"=="check"  set "MODE=check"
if /i "%~1"=="fix"    set "MODE=fix"
if /i "%~1"=="help"   goto :show_help

:: Check for tools. Allow overriding by predefining CLANG_FORMAT_EXE/CLANG_TIDY_EXE.
if not defined CLANG_FORMAT_EXE call :resolve_tool clang-format CLANG_FORMAT_EXE
if not defined CLANG_FORMAT_EXE (
    echo [ERROR] clang-format not found in PATH or Visual Studio LLVM.
    set "FAIL=1"
)
if not defined CLANG_TIDY_EXE call :resolve_tool clang-tidy CLANG_TIDY_EXE
if not defined CLANG_TIDY_EXE (
    echo [ERROR] clang-tidy not found in PATH or Visual Studio LLVM.
    set "FAIL=1"
)
if "!FAIL!"=="1" exit /b 1

if "!MODE!"=="all"    goto :do_format
if "!MODE!"=="format" goto :do_format
goto :do_check

:do_format
echo [lint.bat] Formatting code with clang-format...
powershell -ExecutionPolicy Bypass -Command ^
    "Get-ChildItem -Include *.c,*.h -Recurse | ForEach-Object { & '%CLANG_FORMAT_EXE%' -i $_.FullName }"
if "!MODE!"=="format" exit /b 0

:do_check
if "!MODE!"=="fix" (
    echo [lint.bat] Auto-fixing code with clang-tidy...
    set "EXTRA_FLAGS=-fix-errors"
) else (
    echo [lint.bat] Analyzing code with clang-tidy...
    set "EXTRA_FLAGS="
)

:: We define the include paths and some basic defines to match build.bat
set "TIDY_FLAGS=-Icli -DWIN32 -D_WIN32"

powershell -ExecutionPolicy Bypass -Command ^
    "Get-ChildItem -Path src,transport,examples -Filter *.c -Recurse | ForEach-Object { " ^
    "  Write-Host \"Checking: $($_.Name)\" -ForegroundColor Cyan; " ^
    "  & '%CLANG_TIDY_EXE%' $_.FullName %EXTRA_FLAGS% -- %TIDY_FLAGS% " ^
    "}"
exit /b 0

:resolve_tool
set "%~2="
for /f "delims=" %%P in ('where %~1 2^>nul') do (
    set "%~2=%%P"
    goto :eof
)

for %%D in (
    "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\x64\bin"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin"
    "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin"
    "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Enterprise\VC\Tools\Llvm\x64\bin"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Professional\VC\Tools\Llvm\x64\bin"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin"
    "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin"
) do (
    if exist "%%~D\%~1.exe" (
        set "%~2=%%~D\%~1.exe"
        goto :eof
    )
)

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "delims=" %%P in ('"%VSWHERE%" -latest -products * -find **\x64\bin\%~1.exe 2^>nul') do (
        set "%~2=%%P"
        goto :eof
    )
)

goto :eof

:show_help
echo.
echo  Usage: scripts\lint\lint.bat [format^|check^|fix^|help]
echo.
echo  Commands:
echo    (none)    Run both format and check
echo    format    Run clang-format on all .c and .h files
echo    check     Run clang-tidy on all .c files
echo    fix       Run clang-tidy with -fix-errors
echo    help      Show this message
echo.
exit /b 0
