#!/usr/bin/env pwsh
# package_src.ps1 — Create the source-drop package (copy-paste install)
# Package name: OpenLibCLI-<version>-src
# Output: install/OpenLibCLI-<version>-src/{cli.c, cli.h, LICENSE, README.md, version}

pushd $PSScriptRoot/../..

try {
    $VERSION = (Get-Content ./version).Trim()
    $PKG_NAME = "OpenLibCLI-$VERSION-src"
    $INSTALL_DIR = "install/$PKG_NAME"
    if (Test-Path $INSTALL_DIR) { Remove-Item -Path $INSTALL_DIR -Recurse -Force }
    New-Item -ItemType Directory -Path $INSTALL_DIR -Force | Out-Null

    # Copy the entire cli directory recursively (including subfolders like 'config')
    Copy-Item -Path "cli" -Destination "$INSTALL_DIR" -Recurse -Force
    
    Copy-Item LICENSE           $INSTALL_DIR -ErrorAction SilentlyContinue
    Copy-Item README.md         $INSTALL_DIR
    Copy-Item version           $INSTALL_DIR

    Write-Host "Source package ready at $PWD/$INSTALL_DIR"
    popd
    exit 0
} catch {
    Write-Host "Error: $_"
    popd
    exit 1
}
