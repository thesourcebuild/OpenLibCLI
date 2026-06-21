#!/usr/bin/env pwsh
# package_meson_wrap.ps1 — Generate the OpenLibCLI.wrap file for consumers
# Output: install/wrap/OpenLibCLI.wrap
# Usage: place OpenLibCLI.wrap in MyProject/subprojects/ then call
#        dependency('OpenLibCLI') in your meson.build.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

try {
    $repoRoot    = (Resolve-Path (Join-Path $PSScriptRoot '../../')).Path
    $versionFile = Join-Path $repoRoot 'version'
    $template    = Join-Path $repoRoot 'meson/meson.wrap.in'
    $wrapDir     = Join-Path $repoRoot 'install/wrap'
    $wrapFile    = Join-Path $wrapDir  'OpenLibCLI.wrap'

    foreach ($f in @($versionFile, $template)) {
        if (-not (Test-Path $f)) { throw "Missing required file: $f" }
    }

    $version = (Get-Content $versionFile -TotalCount 1).Trim()
    if (-not $version) { throw "version file is empty: $versionFile" }

    New-Item -ItemType Directory -Path $wrapDir -Force | Out-Null

    # Replace @CLI_VERSION@ placeholder in the template
    $content = Get-Content $template -Raw
    $content = $content -replace '@CLI_VERSION@', $version
    Set-Content -Path $wrapFile -Value $content -Encoding UTF8

    Write-Host "Generated: $wrapFile"
    Write-Host ""
    Get-Content $wrapFile | Write-Host
    exit 0
} catch {
    Write-Host "Error: $($_.Exception.Message)"
    exit 1
}
