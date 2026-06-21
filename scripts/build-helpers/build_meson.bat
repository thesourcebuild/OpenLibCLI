@echo off
:: =============================================================================
::  build_meson.bat - Thin wrapper around build_meson.ps1
:: =============================================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dpn0.ps1" %*
exit /b %ERRORLEVEL%