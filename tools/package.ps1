# Build a portable, release-only archive from the compiled Windows executables.
param([string]$BuildDirectory = 'build')
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

$version = (Get-Content -LiteralPath 'VERSION' -Raw).Trim()
if ($version -notmatch '^\d+\.\d+\.\d+$') { throw "Invalid version: $version" }
$simExe = Join-Path $BuildDirectory 'agentsim.exe'
$launcherExe = Join-Path $BuildDirectory 'agentsimw.exe'
if (-not (Test-Path -LiteralPath $simExe) -or -not (Test-Path -LiteralPath $launcherExe)) {
    throw 'Run tools\build.ps1 first.'
}
$reportedVersion = & (Resolve-Path -LiteralPath $simExe).ProviderPath --version
if ($LASTEXITCODE -ne 0 -or $reportedVersion -ne "agentsim $version") {
    throw "Expected agentsim $version, got '$reportedVersion'. Rebuild before packaging."
}

node frontend\tools\bundle.mjs
if ($LASTEXITCODE -ne 0) { throw 'Frontend bundle failed.' }

$buildRoot = (New-Item -ItemType Directory -Force -Path 'build').FullName
$stageName = 'package-' + [Guid]::NewGuid().ToString('N')
$stageRoot = Join-Path $buildRoot $stageName
$stage = Join-Path $stageRoot 'agentsim'
$zipName = "agentsim-$version-win64.zip"
$singleName = "agentsim-$version-win64.exe"
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class AgentSimResources {
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr BeginUpdateResource(string fileName, bool deleteExisting);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool UpdateResource(IntPtr update, IntPtr type, IntPtr name,
                                             ushort language, byte[] bytes, uint size);

    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern bool EndUpdateResource(IntPtr update, bool discard);
}
'@
try {
    foreach ($dir in 'frontend', 'mods', 'resourcepacks\classic_flat', 'tools', 'docs') {
        New-Item -ItemType Directory -Path (Join-Path $stage $dir) | Out-Null
    }
    Copy-Item -LiteralPath $simExe, $launcherExe, 'LICENSE', 'VERSION' -Destination $stage
    Copy-Item -LiteralPath 'frontend\agentsim.html' -Destination (Join-Path $stage 'frontend')
    $modFiles = @('mods\constant_light.txt', 'mods\rich_nutrients.txt', 'mods\nutrient_diffusion.txt')
    Copy-Item -LiteralPath $modFiles -Destination (Join-Path $stage 'mods')
    $packFiles = @('resourcepacks\classic_flat\pack.txt', 'resourcepacks\classic_flat\colors.txt')
    Copy-Item -LiteralPath $packFiles -Destination (Join-Path $stage 'resourcepacks\classic_flat')
    Copy-Item -LiteralPath 'tools\agentsim_env.py', 'tools\train_navigation.py' -Destination (Join-Path $stage 'tools')
    $guides = @('docs\MODS.md', 'docs\RESOURCE-PACKS.md', 'docs\RESEARCH.md', 'docs\ARCHITECTURE.md')
    Copy-Item -LiteralPath $guides -Destination (Join-Path $stage 'docs')

    @(
        "agentsim $version for Windows x64"
        ''
        'Extract every file into one writable folder, then open agentsimw.exe.'
        'The launcher opens the game in an independent Edge app profile.'
        'To open the game in a browser yourself, run agentsim.exe --serve 8080 --no-open'
        'and visit the loopback address printed by the server.'
        ''
        'See docs/ for Mod, resource pack and Python research instructions.'
        'https://github.com/zx1414/adaptive-orbit-ecosystem'
    ) | Set-Content -LiteralPath (Join-Path $stage 'README.txt') -Encoding UTF8

    $stagedZip = Join-Path $stageRoot $zipName
    Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $stagedZip

    $singleExe = Join-Path $stageRoot $singleName
    Copy-Item -LiteralPath $launcherExe -Destination $singleExe
    $handle = [AgentSimResources]::BeginUpdateResource($singleExe, $false)
    if ($handle -eq [IntPtr]::Zero) { throw 'Cannot prepare the standalone launcher.' }
    $complete = $false
    try {
        foreach ($line in Get-Content -LiteralPath 'src\app\StandaloneAssets.inc') {
            if ($line -notmatch '^ASSET\((\d+), "([^"]+)", (true|false)\)$') {
                throw "Invalid standalone asset declaration: $line"
            }
            $id = [IntPtr][int]$Matches[1]
            $payload = [System.IO.File]::ReadAllBytes((Join-Path $stage $Matches[2]))
            if (-not [AgentSimResources]::UpdateResource($handle, [IntPtr]10, $id, 0, $payload, $payload.Length)) {
                throw "Cannot embed standalone asset #$id."
            }
        }
        $complete = $true
    } finally {
        if (-not [AgentSimResources]::EndUpdateResource($handle, (-not $complete))) {
            throw 'Cannot finalize the standalone launcher.'
        }
    }

    $zipPath = Join-Path $buildRoot $zipName
    Move-Item -LiteralPath $stagedZip -Destination $zipPath -Force
    $singlePath = Join-Path $root $singleName
    Move-Item -LiteralPath $singleExe -Destination $singlePath -Force
    Write-Host "zip: $zipPath ($((Get-Item -LiteralPath $zipPath).Length) bytes)"
    Write-Host "standalone: $singlePath ($((Get-Item -LiteralPath $singlePath).Length) bytes)"
} finally {
    if (Test-Path -LiteralPath $stageRoot) {
        $resolved = (Resolve-Path -LiteralPath $stageRoot).ProviderPath
        if ((Split-Path -Parent $resolved) -eq $buildRoot -and
            (Split-Path -Leaf $resolved) -eq $stageName) {
            Remove-Item -LiteralPath $resolved -Recurse -Force
        }
    }
}
