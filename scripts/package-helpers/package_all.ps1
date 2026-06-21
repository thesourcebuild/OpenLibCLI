#!/usr/bin/env pwsh
# package_all.ps1 — Run all packaging scripts to generate all release artifacts.

$ErrorActionPreference = 'Stop'

try {
    $PSScriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Definition
    
    Write-Host "====================================================" -ForegroundColor Cyan
    Write-Host " Building all release packages..." -ForegroundColor Cyan
    Write-Host "====================================================" -ForegroundColor Cyan
    Write-Host ""

    # 1. Source Package
    Write-Host "--> [1/4] Building Source Package..." -ForegroundColor Yellow
    & "$PSScriptRoot/package_src.ps1"
    if ($LASTEXITCODE -ne 0) { throw "Source package build failed" }

    # 2. CMake Package
    Write-Host "--> [2/4] Building CMake Package..." -ForegroundColor Yellow
    & "$PSScriptRoot/package_cmake.ps1"
    if ($LASTEXITCODE -ne 0) { throw "CMake package build failed" }

    # 3. Meson Package
    Write-Host "--> [3/4] Building Meson Package..." -ForegroundColor Yellow
    & "$PSScriptRoot/package_meson.ps1"
    if ($LASTEXITCODE -ne 0) { throw "Meson package build failed" }

    # 4. Meson Wrap File
    Write-Host "--> [4/4] Generating Meson Wrap..." -ForegroundColor Yellow
    & "$PSScriptRoot/package_meson_wrap.ps1"
    if ($LASTEXITCODE -ne 0) { throw "Meson wrap generation failed" }

    Write-Host ""
    Write-Host "====================================================" -ForegroundColor Green
    Write-Host " SUCCESS: All packages generated in install/ directory." -ForegroundColor Green
    Write-Host "====================================================" -ForegroundColor Green
    exit 0
} catch {
    Write-Host ""
    Write-Host "====================================================" -ForegroundColor Red
    Write-Host " FAILURE: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host "====================================================" -ForegroundColor Red
    exit 1
}
