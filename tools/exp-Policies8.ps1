# exp-Policies8.ps1 - E4: 8-policy x 6-seed full comparison (read-only vs src)
# Usage:
#   powershell -File exp-Policies8.ps1 -Fresh                 # full run, truncate CSV
#   powershell -File exp-Policies8.ps1 -PoliciesCsv rule,stub -SeedsCsv 42,7   # partial run, append CSV
#   powershell -File exp-Policies8.ps1 -Ticks 5000 -OutDir <dir>          # quick smoke (use -OutDir to avoid polluting E4 data)
# Exit 5 = extinction (expected outcome, parsed as data, not an error).
param(
    [string]$ExePath = '<repo>\build\agentsim.exe',
    [int]$Ticks = 20000,
    [string]$OutDir = '<repo>-wt\B\build\e4',
    [string]$PoliciesCsv = 'rule,stub,perceptron,thermal,hysteresis,foragebank,bandit,riskaverse',
    [string]$SeedsCsv = '42,7,123,5,11,999',
    [int]$StatsInterval = 10,
    [switch]$Fresh
)
$Policies = $PoliciesCsv -split ',' | ForEach-Object { $_.Trim() } | Where-Object { $_ }
$Seeds = $SeedsCsv -split ',' | ForEach-Object { [int]$_.Trim() }
$ErrorActionPreference = 'Continue'
$env:PATHEXT = '.COM;.EXE;.BAT;.CMD'

if (-not (Test-Path $ExePath)) { Write-Output 'FATAL: exe not found'; exit 9 }
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$logDir  = Join-Path $OutDir 'logs'
$statDir = Join-Path $OutDir 'stats'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
New-Item -ItemType Directory -Force -Path $statDir | Out-Null
$csvPath = Join-Path $OutDir 'E4-results.csv'

if ($Fresh -and (Test-Path $csvPath)) { Remove-Item $csvPath -Force }
if (-not (Test-Path $csvPath)) {
    'policy,seed,exit_code,tick,nuclei,food,total_energy,audit,extinct,stderr_extinct_tick,stats_first_zero_tick,peak_nuclei,elapsed_sec,lastline' | Set-Content $csvPath -Encoding UTF8
}

$total = $Policies.Count * $Seeds.Count
$done = 0
foreach ($p in $Policies) {
  foreach ($s in $Seeds) {
    $statsPath = Join-Path $statDir ($p + '-s' + $s + '.csv')
    $lines = New-Object System.Collections.Generic.List[string]
    $ms = Measure-Command {
      try { $raw = & $ExePath --no-render --ticks $Ticks --seed $s --policy $p --audit --stats $statsPath --stats-interval $StatsInterval 2>&1 } catch { $raw = @(('SCRIPTCAPTURE-ERROR: ' + $_.Exception.Message)) }
      if ($null -ne $raw) { foreach ($l in $raw) { [void]$lines.Add([string]$l) } }
    }
    $exit = $LASTEXITCODE
    $elapsed = [math]::Round($ms.TotalSeconds, 3)

    $tick = ''; $nuc = ''; $food = ''; $te = ''; $audit = ''; $extTick = ''
    foreach ($l in $lines) {
      if ($l -match 'done tick=(\d+) nuclei=(\d+) food=(\d+) total_energy=([\d.eE+-]+) audit=(\w+) policy=(\S+)') {
        $tick = $Matches[1]; $nuc = $Matches[2]; $food = $Matches[3]; $te = $Matches[4]; $audit = $Matches[5]
      }
      if ($l -match 'population extinct at tick (\d+)') { $extTick = $Matches[1] }
    }
    # real extinction moment: first sampled tick with nuclei==0 in stats csv
    $firstZero = ''; $peak = ''
    if (Test-Path $statsPath) {
      $peak = 0
      foreach ($row in (Get-Content $statsPath)) {
        if ($row -match '^(\d+),(\d+),') {
          $t = [int]$Matches[1]; $n = [int]$Matches[2]
          if ($n -gt $peak) { $peak = $n }
          if ($n -eq 0 -and $firstZero -eq '') { $firstZero = $t }
        }
      }
      if ($firstZero -ne '') { $peak = [string]$peak } else { $peak = [string]$peak }
    }
    $extinct = 'no'; if ($extTick -ne '' -or $firstZero -ne '') { $extinct = 'yes' }
    $lastline = ''
    for ($i = $lines.Count - 1; $i -ge 0; $i--) { if ([string]$lines[$i].Trim() -ne '') { $lastline = [string]$lines[$i].Trim(); break } }
    $lastline = $lastline.Replace(',', ';')
    if ($lastline.Length -gt 160) { $lastline = $lastline.Substring(0, 160) }

    ("{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10},{11},{12},{13}" -f $p,$s,$exit,$tick,$nuc,$food,$te,$audit,$extinct,$extTick,$firstZero,$peak,$elapsed,$lastline) | Add-Content $csvPath -Encoding UTF8
    $logPath = Join-Path $logDir ($p + '-s' + $s + '.log')
    ('EXIT=' + $exit) | Set-Content $logPath -Encoding UTF8
    if ($lines.Count -gt 0) { $lines | Add-Content $logPath -Encoding UTF8 }
    $done++
    Write-Output ('[' + $done + '/' + $total + '] ' + $p + ' seed=' + $s + ' exit=' + $exit + ' tick=' + $tick + ' nuclei=' + $nuc + ' te=' + $te + ' extinct=' + $extinct + ' first0=' + $firstZero + ' peak=' + $peak + ' ' + $elapsed + 's')
  }
}
Write-Output ('CSV: ' + $csvPath)
