param([ValidateSet('Debug', 'Release')][string]$Configuration = 'Release', [switch]$SkipBuild)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $SkipBuild) { & (Join-Path $PSScriptRoot 'build.ps1') -Configuration $Configuration }
$violations = @()
Get-ChildItem -LiteralPath (Join-Path $root 'src/decision') -Include '*.h','*.cpp' -Recurse -File | ForEach-Object {
  $path = $_.FullName; $n = 0
  Get-Content -LiteralPath $path | ForEach-Object { ++$n; if ($_ -match '^\s*#\s*include\s*[<"]\s*(?:world|agents|producers|perception|behavior|sim|app)/') { $violations += "${path}:${n}: decision must not depend on higher layers" } }
}
Get-ChildItem -LiteralPath (Join-Path $root 'src/core') -Include '*.h','*.cpp' -Recurse -File | ForEach-Object {
  $path = $_.FullName; $n = 0
  Get-Content -LiteralPath $path | ForEach-Object { ++$n; if ($_ -match '^\s*#\s*include\s*[<"]\s*(?:world|agents|producers|perception|behavior|sim|app|decision)/') { $violations += "${path}:${n}: core must not depend on higher layers" } }
}
Get-ChildItem -LiteralPath (Join-Path $root 'src/world') -Include '*.h','*.cpp' -Recurse -File | ForEach-Object {
  $path = $_.FullName; $n = 0
  Get-Content -LiteralPath $path | ForEach-Object { ++$n; if ($_ -match '^\s*#\s*include\s*[<"]\s*(?:app|agents|producers|perception|behavior|sim|decision)/') { $violations += "${path}:${n}: world must not depend on higher layers" } }
}
Get-ChildItem -LiteralPath (Join-Path $root 'src') -Include '*.h','*.cpp' -Recurse -File | ForEach-Object {
  $path = $_.FullName; $n = 0
  Get-Content -LiteralPath $path | ForEach-Object { ++$n; if ($_ -match '^\s*#\s*include\s*[<"]\s*\.\.[\\/]') { $violations += "${path}:${n}: relative parent include is forbidden" } }
}
foreach ($layer in @('world', 'perception', 'behavior', 'environment', 'colony')) {
  Get-ChildItem -LiteralPath (Join-Path $root ('src/' + $layer)) -Include '*.h','*.cpp' -Recurse -File | ForEach-Object {
    $path = $_.FullName; $n = 0
    Get-Content -LiteralPath $path | ForEach-Object { ++$n; if ($_ -match '^\s*#\s*include\s*[<"]\s*(?:sim|app)/') { $violations += "${path}:${n}: lower layer must not depend on sim/app" } }
  }
}
Get-ChildItem -LiteralPath (Join-Path $root 'src/sim') -Include '*.h','*.cpp' -Recurse -File | ForEach-Object {
  $path = $_.FullName; $n = 0
  Get-Content -LiteralPath $path | ForEach-Object { ++$n; if ($_ -match '^\s*#\s*include\s*[<"]app/') { $violations += "${path}:${n}: sim must not depend on app" } }
}
if ($violations.Count -gt 0) { $violations | ForEach-Object { Write-Host $_ }; throw 'Architecture dependency check failed.' }
& (Join-Path $root 'build/unit_tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'Unit tests failed.' }
& (Join-Path $root 'tests/integration/smoke.ps1')
if ($LASTEXITCODE -ne 0) { throw 'Integration tests failed.' }
Write-Host 'Build, dependency, unit and integration checks passed.'
