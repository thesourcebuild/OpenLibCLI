#!/usr/bin/env pwsh
# package_meson.ps1 — Create the Meson subproject package (microlog-style).
# Package name: OpenLibCLI-<version>-meson
# Output layout:
#   install/OpenLibCLI-<version>-meson/
#     meson.build, meson_options.txt <- build files (from meson/)
#     include/OpenLibCLI/            <- public headers (config/, utils/)
#     src/                           <- library source tree
#     examples/                      <- serial + telnet examples
#     LICENSE, README.md, version

pushd $PSScriptRoot/../..

try {
    $VERSION = (Get-Content ./version).Trim()
    $PKG_NAME = "OpenLibCLI-$VERSION-meson"
    $INSTALL_DIR = "install/$PKG_NAME"
    if (Test-Path $INSTALL_DIR) { Remove-Item -Path $INSTALL_DIR -Recurse -Force }

    New-Item -ItemType Directory -Path $INSTALL_DIR -Force | Out-Null

    # Headers: the public tree under include/OpenLibCLI/ (config/, utils/ kept); sources: the
    # whole library tree, so their relative includes resolve
    $cliRoot = (Resolve-Path "cli").Path
    Get-ChildItem -Path cli -Recurse -File -Include *.h, *.c | ForEach-Object {
        $rel = $_.FullName.Substring($cliRoot.Length + 1)
        $targets = @("$INSTALL_DIR/src/$rel")
        if ($_.Extension -eq ".h") { $targets += "$INSTALL_DIR/include/OpenLibCLI/$rel" }
        foreach ($t in $targets) {
            New-Item -ItemType Directory -Path (Split-Path $t -Parent) -Force | Out-Null
            Copy-Item -Path $_.FullName -Destination $t
        }
    }

    # Build files of the package (maintained in meson/)
    Copy-Item -Path "meson/meson.build", "meson/meson_options.txt" -Destination $INSTALL_DIR

    # Examples built by -Dbuild_examples=true
    New-Item -ItemType Directory -Path "$INSTALL_DIR/examples/shared" -Force | Out-Null
    New-Item -ItemType Directory -Path "$INSTALL_DIR/examples/win_linux/serial" -Force | Out-Null
    New-Item -ItemType Directory -Path "$INSTALL_DIR/examples/win_linux/telnet" -Force | Out-Null
    Copy-Item -Path "examples/shared/*" -Destination "$INSTALL_DIR/examples/shared/" -Force
    Copy-Item -Path "examples/win_linux/serial/*" -Destination "$INSTALL_DIR/examples/win_linux/serial/" -Force
    Copy-Item -Path "examples/win_linux/telnet/*" -Destination "$INSTALL_DIR/examples/win_linux/telnet/" -Force

    # Copy version, LICENSE, README
    Copy-Item version   $INSTALL_DIR
    Copy-Item LICENSE   $INSTALL_DIR
    Copy-Item README.md $INSTALL_DIR

    Write-Host "Meson subproject package ready at $PWD/$INSTALL_DIR"
    Write-Host ""
    Write-Host "Consumer usage (subproject copy):"
    Write-Host "  Copy install/$PKG_NAME/ to MyProject/subprojects/OpenLibCLI/"
    Write-Host "  dep = dependency('OpenLibCLI')"
    Write-Host ""
    Write-Host "Or use the wrap file:"
    Write-Host "  Copy install/wrap/OpenLibCLI.wrap to MyProject/subprojects/"

    popd
    exit 0
} catch {
    Write-Host "Error: $_"
    popd
    exit 1
}
