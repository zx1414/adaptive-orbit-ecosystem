# evolve.ps1 - E1 perceptron hyperparameter sweep (agentsim)
# Usage: powershell -ExecutionPolicy Bypass -File tools\evolve.ps1
# Scans --perceptron-seed x --perceptron-scale at ticks=2000 seed=42 --audit,
# plus rule/stub baselines; writes CSV to build\exp\perceptron-sweep.csv;
# prints a Top-10 table by total_energy and lists any failed runs.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\build\agentsim.exe"),
    [string]$OutCsv = (Join-Path $PSScriptRoot "..\build\exp\perceptron-sweep.csv"),
    [int]$Ticks = 2000,
    [int]$SimSeed = 42
)
$ErrorActionPreference = "Stop"
$expDir = Split-Path $OutCsv -Parent
if (-not (Test-Path $expDir)) { New-Item -ItemType Directory -Path $expDir -Force | Out-Null }

$Seeds  = @(1, 7, 42, 99, 123, 999, 2024, 31337)
$Scales = @(0.01, 0.05, 0.1, 0.3, 0.5)

function Invoke-Run {
    param([string]$Policy, [object]$PSeed, [object]$PScale)
    $simArgs = @("--no-render", "--ticks", "$Ticks", "--seed", "$SimSeed", "--audit", "--policy", $Policy)
    if ($null -ne $PSeed)  { $simArgs += @('--perceptron-seed', "$PSeed") }
    if ($null -ne $PScale) { $simArgs += @('--perceptron-scale', "$PScale") }
    $outF = Join-Path $env:TEMP "evolve_last.out"
    $errF = Join-Path $env:TEMP "evolve_last.err"
    $p = Start-Process -FilePath $Exe -ArgumentList $simArgs -NoNewWindow -Wait -PassThru -RedirectStandardOutput $outF -RedirectStandardError $errF
    $txt = ""
    if (Test-Path $outF) { $txt = Get-Content $outF -Raw -ErrorAction SilentlyContinue }
    if ($null -eq $txt) { $txt = "" }
    $row = [ordered]@{
        policy = $Policy; perceptronSeed = "$PSeed"; perceptronScale = "$PScale"
        nuclei = ""; food = ""; totalEnergy = ""; audit = ""; exitCode = $p.ExitCode
    }
    $last = ($txt -split "\r?\n" | Where-Object { $_ -match "\S" } | Select-Object -Last 1)
    if ($last -match "nuclei=(\d+)")      { $row.nuclei = $Matches[1] }
    if ($last -match "food=(\d+)")        { $row.food = $Matches[1] }
    if ($last -match "total_energy=([0-9.eE+-]+)") { $row.totalEnergy = $Matches[1] }
    if ($last -match "audit=(\w+)")       { $row.audit = $Matches[1] }
    if (-not $last) { $row.audit = "NO-OUTPUT" }
    return [pscustomobject]$row
}

$rows = @()
# baselines: rule & stub (no perceptron params)
$rows += Invoke-Run -Policy "rule"
$rows += Invoke-Run -Policy "stub"
# sweep: 8 seeds x 5 scales = 40 groups
foreach ($s in $Seeds) {
  foreach ($sc in $Scales) {
    $rows += Invoke-Run -Policy "perceptron" -PSeed $s -PScale $sc
  }
}

# --- export CSV ---
$rows | Export-Csv -Path $OutCsv -NoTypeInformation -Encoding UTF8
Write-Host ("CSV written: {0}  rows={1}" -f $OutCsv, $rows.Count)

# --- failures (exit code != 0) ---
$fails = @($rows | Where-Object { $_.exitCode -ne 0 })
if ($fails.Count -gt 0) {
    Write-Host ""
    Write-Host ("FAILED RUNS ({0}):" -f $fails.Count) -ForegroundColor Red
    $fails | ForEach-Object { Write-Host ("  policy={0} pseed={1} pscale={2} exit={3} audit={4}" -f $_.policy, $_.perceptronSeed, $_.perceptronScale, $_.exitCode, $_.audit) }
} else {
    Write-Host "All runs exited 0."
}

# --- Top 10 by total_energy (desc) ---
$ranked = $rows | Sort-Object { [double]$_.totalEnergy } -Descending
Write-Host ""
Write-Host "Top 10 by total_energy:" -ForegroundColor Cyan
$hdr = "{0,-12} {1,6} {2,8} {3,7} {4,6} {5,14} {6,6} {7,5}" -f "policy","pseed","pscale","nuclei","food","totalEnergy","audit","exit"
Write-Host $hdr
Write-Host ("-" * $hdr.Length)
foreach ($r in ($ranked | Select-Object -First 10)) {
    Write-Host ("{0,-12} {1,6} {2,8} {3,7} {4,6} {5,14} {6,6} {7,5}" -f $r.policy, $r.perceptronSeed, $r.perceptronScale, $r.nuclei, $r.food, $r.totalEnergy, $r.audit, $r.exitCode)
}

# --- worst 5 (perceptron only) for context ---
$worst = @($rows | Where-Object { $_.policy -eq "perceptron" } | Sort-Object { [double]$_.totalEnergy }) | Select-Object -First 5
Write-Host ""
Write-Host "Worst 5 (perceptron):" -ForegroundColor Yellow
foreach ($r in $worst) {
    Write-Host ("{0,-12} {1,6} {2,8} {3,7} {4,6} {5,14} {6,6} {7,5}" -f $r.policy, $r.perceptronSeed, $r.perceptronScale, $r.nuclei, $r.food, $r.totalEnergy, $r.audit, $r.exitCode)
}
