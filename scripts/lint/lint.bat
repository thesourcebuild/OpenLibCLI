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

:: Only the tools this mode uses are required. Allow overriding by predefining
:: CLANG_FORMAT_EXE/CLANG_TIDY_EXE.
set "NEED_FORMAT=0"
set "NEED_TIDY=0"
if "!MODE!"=="all"    set "NEED_FORMAT=1"
if "!MODE!"=="format" set "NEED_FORMAT=1"
if not "!MODE!"=="format" set "NEED_TIDY=1"

if "!NEED_FORMAT!"=="1" (
    if not defined CLANG_FORMAT_EXE call :resolve_tool clang-format CLANG_FORMAT_EXE
    if not defined CLANG_FORMAT_EXE (
        echo [ERROR] clang-format not found in PATH or Visual Studio LLVM.
        set "FAIL=1"
    )
)
if "!NEED_TIDY!"=="1" (
    if not defined CLANG_TIDY_EXE call :resolve_tool clang-tidy CLANG_TIDY_EXE
    if not defined CLANG_TIDY_EXE (
        echo [ERROR] clang-tidy not found in PATH or Visual Studio LLVM.
        set "FAIL=1"
    )
)
if "!FAIL!"=="1" (
    popd >nul
    exit /b 1
)

set "STATUS=0"
if "!NEED_FORMAT!"=="1" call :do_format
if "!NEED_TIDY!"=="1" call :do_check
popd >nul
exit /b !STATUS!

:: The project's own sources only (never build output or generated packages).
:do_format
echo [lint.bat] Formatting code with clang-format...
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$fail = 0; Get-ChildItem -Path cli,examples,tests -Include *.c,*.h -Recurse | ForEach-Object { " ^
    "  & '%CLANG_FORMAT_EXE%' -i $_.FullName; if ($LASTEXITCODE -ne 0) { $fail = 1 } }; exit $fail"
if errorlevel 1 set "STATUS=1"
goto :eof

:do_check
if "!MODE!"=="fix" (
    echo [lint.bat] Auto-fixing code with clang-tidy...
    set "EXTRA_FLAGS=-fix-errors"
) else (
    echo [lint.bat] Analyzing code with clang-tidy...
    set "EXTRA_FLAGS="
)

:: Compiled as the Windows builds compile them (C99, the library headers on the include path,
:: the build defines). examples\linux is POSIX-only and is left out here.
set "TIDY_FLAGS=-std=c99 -Icli -DWIN32 -D_WIN32 -D_CRT_SECURE_NO_WARNINGS"

powershell -NoProfile -ExecutionPolicy Bypass -Command ^
    "$fail = 0; Get-ChildItem -Path cli,examples\shared,examples\win_linux,tests -Filter *.c -Recurse | ForEach-Object { " ^
    "  Write-Host \"Checking: $($_.FullName)\" -ForegroundColor Cyan; " ^
    "  & '%CLANG_TIDY_EXE%' $_.FullName %EXTRA_FLAGS% -- %TIDY_FLAGS%; if ($LASTEXITCODE -ne 0) { $fail = 1 } }; exit $fail"
if errorlevel 1 set "STATUS=1"
goto :eof

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
echo  Commands (on cli\, examples\ and tests\):
echo    (none)    Run both format and check
echo    format    Run clang-format on the .c and .h files
echo    check     Run clang-tidy on the .c files
echo    fix       Run clang-tidy with -fix-errors
echo    help      Show this message
echo.
exit /b 0
