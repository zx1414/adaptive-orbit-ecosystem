# agentsim - tools/package.ps1 : regenerate release/ and optional zip.
# Portable: all paths derived from $PSScriptRoot, no hardcoded machine paths.
param([string]$Version = (Get-Content (Join-Path $PSScriptRoot "..\VERSION") -Raw).Trim(),
      [switch]$Zip)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# 0) ensure build outputs exist (build.ps1 produces all three)
if (-not (Test-Path "build\agentsim.exe") -or -not (Test-Path "build\agentsimw.exe")) {
    Write-Error "run tools\build.ps1 first"; exit 1
}

# 1) refresh single-file bundle from sources
if (Test-Path "frontend\tools\bundle.mjs") {
    node frontend\tools\bundle.mjs
    if ($LASTEXITCODE -ne 0) { Write-Error "bundle.mjs failed"; exit 1 }
}

# 2) clean + assemble release/
$rel = Join-Path $root "release"
if (Test-Path $rel) { Remove-Item $rel -Recurse -Force }
New-Item -ItemType Directory -Force -Path "$rel\frontend" | Out-Null
Copy-Item "build\agentsim.exe" "$rel\agentsim.exe"
Copy-Item "build\agentsimw.exe" "$rel\agentsimw.exe"
Copy-Item "frontend\agentsim.html" "$rel\frontend\agentsim.html"
if (Test-Path "resourcepacks") { Copy-Item "resourcepacks" "$rel\resourcepacks" -Recurse }

$readme = @("
"agentsim $Version — 自适应环绕核生态模拟与生存游戏（发布包）",
"============================================================",
"双击 agentsimw.exe 启动：无控制台窗口，自动以 Edge --app 独立窗口打开画面。",
"- 同端口已有实例时，二次启动复用已有窗口并退出（单实例）。",
"- 参数页可调 113 个运行时参数；advanced 用 agentsim.exe --help。",
"- 绿色免安装：整个 release 目录可拷贝；需要 Win10/11 自带 Edge/WebView2。",
"- 源码/许可/CHANGELOG 见仓库根：https://github.com/zx1414/adaptive-orbit-ecosystem",
"") -join "`r`n"
Set-Content -Path "$rel\README.txt" -Value $readme -Encoding UTF8
Write-Host "release/ assembled ($Version)"

# 3) optional zip
if ($Zip) {
    $zip = Join-Path $root "agentsim-$Version-win64.zip"
    if (Test-Path $zip) { Remove-Item $zip -Force }
    Compress-Archive -Path "$rel\*" -DestinationPath $zip
    Write-Host "zip: $zip ($((Get-Item $zip).Length) bytes)"
}