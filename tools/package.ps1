# Assemble the Windows distribution from the current build.
param([string]$Version = (Get-Content (Join-Path $PSScriptRoot "..\VERSION") -Raw).Trim(),
      [string]$BuildDirectory = "build", [switch]$Zip)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "Invalid version: $Version" }
$simExe = Join-Path $BuildDirectory 'agentsim.exe'
$launcherExe = Join-Path $BuildDirectory 'agentsimw.exe'
if (-not (Test-Path -LiteralPath $simExe) -or -not (Test-Path -LiteralPath $launcherExe)) {
    throw 'Run tools\build.ps1 first.'
}
$reportedVersion = & (Resolve-Path -LiteralPath $simExe).ProviderPath --version
if ($LASTEXITCODE -ne 0 -or $reportedVersion -ne "agentsim $Version") {
    throw "Expected agentsim $Version, got '$reportedVersion'. Rebuild before packaging."
}
node frontend\tools\bundle.mjs
if ($LASTEXITCODE -ne 0) { throw 'Frontend bundle failed.' }

# Archive only release files so data from previous local launches stays out of the ZIP.
$buildRoot = (Resolve-Path -LiteralPath 'build').ProviderPath
$stageName = 'package-' + [Guid]::NewGuid().ToString('N')
$stagePath = Join-Path $buildRoot $stageName
try {
    New-Item -ItemType Directory -Path (Join-Path $stagePath 'frontend') | Out-Null
    Copy-Item -LiteralPath $simExe,$launcherExe,'LICENSE' -Destination $stagePath
    Copy-Item -LiteralPath 'frontend\agentsim.html' -Destination (Join-Path $stagePath 'frontend')
    Copy-Item -LiteralPath 'resourcepacks' -Destination $stagePath -Recurse
    $instructions = @(
        "agentsim $Version"
        ''
        '解压整个压缩包，然后打开 agentsimw.exe。'
        '启动器会打开开局菜单。可选择生态 Mod 和材质包，再点「开始模拟」。'
        '横屏默认铺满地图，点「全图」可查看完整地图。'
        'Windows 10/11 x64。可将 agentsimw.exe 固定到任务栏，之后点击一次即可启动。'
        '请将 agentsimw.exe、agentsim.exe、frontend 和 resourcepacks 保留在同一目录。'
        'https://github.com/zx1414/adaptive-orbit-ecosystem'
    )
    Set-Content -LiteralPath (Join-Path $stagePath 'README.txt') -Value $instructions -Encoding UTF8
    $releasePath = Join-Path $root 'release'
    New-Item -ItemType Directory -Path $releasePath -Force | Out-Null
    Get-ChildItem -LiteralPath $stagePath | Copy-Item -Destination $releasePath -Recurse -Force
    Write-Host "release/ assembled ($Version)"

    if ($Zip) {
        $zipPath = Join-Path $root "agentsim-$Version-win64.zip"
        Compress-Archive -Path (Join-Path $stagePath '*') -DestinationPath $zipPath -Force
        Write-Host "zip: $zipPath ($((Get-Item -LiteralPath $zipPath).Length) bytes)"
    }
} finally {
    if (Test-Path -LiteralPath $stagePath) {
        $resolvedStage = (Resolve-Path -LiteralPath $stagePath).ProviderPath
        if ((Split-Path -Parent $resolvedStage) -eq $buildRoot -and
            (Split-Path -Leaf $resolvedStage) -eq $stageName) {
            Remove-Item -LiteralPath $resolvedStage -Recurse -Force
        }
    }
}
