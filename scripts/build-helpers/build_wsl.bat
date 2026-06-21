@echo off
:: =============================================================================
::  build_wsl.bat - Thin wrapper around build_wsl.ps1
:: =============================================================================
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dpn0.ps1" %*
exit /b %ERRORLEVEL%
