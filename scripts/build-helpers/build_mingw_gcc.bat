@echo off
:: =============================================================================
::  build_mingw_gcc.bat - Thin wrapper around build_mingw_gcc.ps1
:: =============================================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dpn0.ps1" %*
exit /b %ERRORLEVEL%