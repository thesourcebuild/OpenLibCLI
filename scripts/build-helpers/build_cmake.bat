@echo off
:: =============================================================================
::  build_cmake.bat - Thin wrapper around build_cmake.ps1
:: =============================================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dpn0.ps1" %*
exit /b %ERRORLEVEL%