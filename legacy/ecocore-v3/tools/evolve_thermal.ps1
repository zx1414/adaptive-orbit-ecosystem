# evolve_thermal.ps1 - E2 ThermalPolicy parameter sweep (agentsim)
# Usage: powershell -ExecutionPolicy Bypass -File tools\evolve_thermal.ps1
# Scans --thermal-pref x --thermal-w-thermal x --thermal-w-caution
# at ticks=2000 seed=42 --audit --policy thermal; 5x4x4 = 80 groups.
# Writes CSV to build\exp2\thermal-sweep.csv; prints Top-10 by total_energy,
# worst-5, failed runs, and a per-parameter effectiveness summary.
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\build\agentsim.exe"),
    [string]$OutCsv = (Join-Path $PSScriptRoot "..\build\exp2\thermal-sweep.csv"),
    [int]$Ticks = 2000,
    [int]$SimSeed = 42
)
$ErrorActionPreference = "Stop"
$expDir = Split-Path $OutCsv -Parent
if (-not (Test-Path $expDir)) { New-Item -ItemType Directory -Path $expDir -Force | Out-Null }

$Prefs  = @(0.2, 0.35, 0.5, 0.65, 0.8)
$WTherm = @(0.0, 0.6, 1.2, 2.4)
$WCaut  = @(0.0, 0.4, 0.8, 1.6)

function Invoke-Run {
    param([object]$Pref, [object]$WThermal, [object]$WCaution)
    $simArgs = @("--no-render", "--ticks", "$Ticks", "--seed", "$SimSeed", "--audit",
                 "--policy", "thermal",
                 "--thermal-pref", "$Pref",
                 "--thermal-w-thermal", "$WThermal",
                 "--thermal-w-caution", "$WCaution")
    $outF = Join-Path $env:TEMP "evolve_thermal_last.out"
    $errF = Join-Path $env:TEMP "evolve_thermal_last.err"
    $p = Start-Process -FilePath $Exe -ArgumentList $simArgs -NoNewWindow -Wait -PassThru -RedirectStandardOutput $outF -RedirectStandardError $errF
    $txt = ""
    if (Test-Path $outF) { $txt = Get-Content $outF -Raw -ErrorAction SilentlyContinue }
    if ($null -eq $txt) { $txt = "" }
    $row = [ordered]@{
        pref = "$Pref"; wThermal = "$WThermal"; wCaution = "$WCaution"
        nuclei = ""; food = ""; totalEnergy = ""; audit = ""; exitCode = $p.ExitCode
    }
    $last = ($txt -split "\r?\n" | Where-Object { $_ -match "\S" } | Select-Object -Last 1)
    if ($last -match "nuclei=(\d+)")               { $row.nuclei = $Matches[1] }
    if ($last -match "food=(\d+)")                 { $row.food = $Matches[1] }
    if ($last -match "total_energy=([0-9.eE+-]+)") { $row.totalEnergy = $Matches[1] }
    if ($last -match "audit=(\w+)")                { $row.audit = $Matches[1] }
    if (-not $last) { $row.audit = "NO-OUTPUT" }
    return [pscustomobject]$row
}

# --- sweep: 5 x 4 x 4 = 80 groups ---
$rows = @()
foreach ($pf in $Prefs) {
  foreach ($wt in $WTherm) {
    foreach ($wc in $WCaut) {
      $rows += Invoke-Run -Pref $pf -WThermal $wt -WCaution $wc
    }
  }
}

# --- export CSV ---
$rows | Export-Csv -Path $OutCsv -NoTypeInformation -Encoding UTF8
Write-Host ("CSV written: {0}  rows={1}" -f $OutCsv, $rows.Count)

# --- failed runs ---
$fails = @($rows | Where-Object { $_.exitCode -ne 0 })
Write-Host ""
if ($fails.Count -gt 0) {
    Write-Host ("FAILED RUNS ({0}):" -f $fails.Count) -ForegroundColor Red
    $fails | ForEach-Object { Write-Host ("  pref={0} wT={1} wC={2} exit={3} audit={4}" -f $_.pref, $_.wThermal, $_.wCaution, $_.exitCode, $_.audit) }
} else {
    Write-Host "All runs exited 0."
}

# --- per-parameter effectiveness summary ---
# Group rows by (pref,wThermal) and count distinct energy values over the wCaution axis.
# If a wCaution value changes nothing for every cell, wCaution is provably inert here.
$eff = @()
foreach ($pf in $Prefs) {
  foreach ($wt in $WTherm) {
    $cell = @($rows | Where-Object { [double]$_.pref -eq $pf -and [double]$_.wThermal -eq $wt })
    $energies = @($cell | ForEach-Object { $_.totalEnergy } | Sort-Object -Unique)
    $eff += [pscustomobject]@{
        pref = $pf; wThermal = $wt
        distinctEnergies = $energies.Count
        distinctNuclei   = @($cell | ForEach-Object { $_.nuclei } | Sort-Object -Unique).Count
        distinctFood     = @($cell | ForEach-Object { $_.food } | Sort-Object -Unique).Count
    }
  }
}
$inertCells = @($eff | Where-Object { $_.distinctEnergies -eq 1 })
Write-Host ""
Write-Host ("wCaution effectiveness: {0}/20 cells show >1 distinct outcome across wCaution axis" -f (20 - $inertCells.Count)) -ForegroundColor Cyan
$eff | Where-Object { $_.distinctEnergies -gt 1 } | ForEach-Object {
    Write-Host ("  LIVE cell: pref={0} wT={1} -> {2} distinct energies" -f $_.pref, $_.wThermal, $_.distinctEnergies)
}

# --- Top 10 by total_energy (desc) ---
$ranked = $rows | Sort-Object { [double]$_.totalEnergy } -Descending
Write-Host ""
Write-Host "Top 10 by total_energy:" -ForegroundColor Cyan
$hdr = "{0,-6} {1,9} {2,9} {3,7} {4,6} {5,14} {6,6} {7,5}" -f "pref","wThermal","wCaution","nuclei","food","totalEnergy","audit","exit"
Write-Host $hdr
Write-Host ("-" * $hdr.Length)
foreach ($r in ($ranked | Select-Object -First 10)) {
    Write-Host ("{0,-6} {1,9} {2,9} {3,7} {4,6} {5,14} {6,6} {7,5}" -f $r.pref, $r.wThermal, $r.wCaution, $r.nuclei, $r.food, $r.totalEnergy, $r.audit, $r.exitCode)
}

# --- worst 5 by total_energy (asc) ---
Write-Host ""
Write-Host "Worst 5 by total_energy:" -ForegroundColor Yellow
foreach ($r in ($ranked | Select-Object -Last 5)) {
    Write-Host ("{0,-6} {1,9} {2,9} {3,7} {4,6} {5,14} {6,6} {7,5}" -f $r.pref, $r.wThermal, $r.wCaution, $r.nuclei, $r.food, $r.totalEnergy, $r.audit, $r.exitCode)
}
