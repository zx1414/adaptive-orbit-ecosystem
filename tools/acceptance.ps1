# M2.5 acceptance script for the ecosystem simulator.
# PowerShell 5.1 compatible, no third-party modules.
# Usage (from repo root): powershell -ExecutionPolicy Bypass -File tools\acceptance.ps1
# Runs per seed: two deterministic runs to -Ticks (state hashes _a/_b compared),
# CSV checks (final population, audit_ok, below-cap sample share),
# then one endurance run of seed 42 to -EnduranceTicks.
param(
  [int]$Ticks = 20000,
  [int]$EnduranceTicks = 50000,
  [int[]]$Seeds = @(42, 7, 123),
  [int]$StatsInterval = 100,
  [string]$Exe = 'build\simulator.exe'
)

# ---------------------------------------------------------------------------
# Recommended M2.5 balance configuration, kept in one place.
# The caps are safety valves; the equilibrium is set by local competition,
# density-dependent reproduction and saturating harvest.
# ---------------------------------------------------------------------------
$BalanceArgs = @(
  '--producer-max', '1000',
  '--agent-max', '1000',
  '--producer-competition', '0.2',
  '--producer-radius', '400',
  '--producer-density-repro', '0.02',
  '--agent-move-k', '0.00002',
  '--agent-harvest', '0.5',
  '--agent-reserve', '10',
  '--agent-half-saturation', '20',
  '--agent-density-repro', '0.01',
  '--agent-density-radius', '200',
  '--agent-lifespan', '3000'
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$exePath = $Exe
if (-not [System.IO.Path]::IsPathRooted($exePath)) { $exePath = Join-Path $root $exePath }
if (-not (Test-Path -LiteralPath $exePath)) { throw ('simulator executable not found: {0} (run tools/build.ps1 first)' -f $exePath) }
$outDir = Join-Path $root 'build\acceptance'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null

$CapWindowStartTick = 5000
$CapShareThreshold = 0.8
$EnduranceSeed = 42
$script:failures = 0

function Write-Check([string]$Name, [bool]$Passed, [string]$Detail) {
  if ($Passed) {
    Write-Host ('PASS  ' + $Name)
  } else {
    Write-Host ('FAIL  ' + $Name + '  ' + $Detail)
    $script:failures = $script:failures + 1
  }
}

function Get-BalanceValue([string]$Flag) {
  $index = [Array]::IndexOf($BalanceArgs, '--' + $Flag)
  if ($index -lt 0 -or $index + 1 -ge $BalanceArgs.Count) { throw ('BalanceArgs is missing a value for --' + $Flag) }
  return [double]$BalanceArgs[$index + 1]
}

function Invoke-Sim([string]$Name, [string[]]$Arguments) {
  $stdoutPath = Join-Path $outDir ($Name + '.out.txt')
  $stderrPath = Join-Path $outDir ($Name + '.err.txt')
  $process = Start-Process -FilePath $exePath -ArgumentList $Arguments -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
  if ($process.ExitCode -ne 0) {
    $stderrText = ''
    if (Test-Path -LiteralPath $stderrPath) { $stderrText = [string](Get-Content -Raw -LiteralPath $stderrPath) }
    throw ('{0} failed with exit code {1}: {2}' -f $Name, $process.ExitCode, $stderrText)
  }
  return (Get-Content -Raw -LiteralPath $stdoutPath)
}

function Read-StatsRows([string]$CsvPath) {
  if (-not (Test-Path -LiteralPath $CsvPath)) { throw ('stats CSV was not written: ' + $CsvPath) }
  $rows = @(Import-Csv -LiteralPath $CsvPath)
  if ($rows.Count -eq 0) { throw ('stats CSV has no data rows: ' + $CsvPath) }
  return ,$rows
}

$producerMax = Get-BalanceValue 'producer-max'
$agentMax = Get-BalanceValue 'agent-max'
Write-Host ('Acceptance run: exe={0} ticks={1} enduranceTicks={2} seeds={3} statsInterval={4}' -f $exePath, $Ticks, $EnduranceTicks, ($Seeds -join ','), $StatsInterval)

foreach ($seed in $Seeds) {
  $tag = 'seed_{0}' -f $seed
  $csvPath = Join-Path $outDir ($tag + '.csv')
  $hashAPath = Join-Path $outDir ($tag + '_a.hash')
  $hashBPath = Join-Path $outDir ($tag + '_b.hash')
  $simArgs = @('--no-render', '--ticks', [string]$Ticks, '--seed', [string]$seed, '--audit') + $BalanceArgs
  try {
    $null = Invoke-Sim ($tag + '_a') ($simArgs + @('--stats', $csvPath, '--stats-interval', [string]$StatsInterval, '--state-hash', $hashAPath))
    $null = Invoke-Sim ($tag + '_b') ($simArgs + @('--state-hash', $hashBPath))

    $rows = Read-StatsRows $csvPath
    $last = $rows[$rows.Count - 1]
    Write-Check ('seed {0} final population positive' -f $seed) (([double]$last.producers -gt 0) -and ([double]$last.agents -gt 0)) ('producers={0} agents={1}' -f $last.producers, $last.agents)

    $badAudit = @($rows | Where-Object { $_.audit_ok -ne '1' })
    Write-Check ('seed {0} audit_ok all 1' -f $seed) ($badAudit.Count -eq 0) ('{0} row(s) with audit_ok != 1' -f $badAudit.Count)

    $window = @($rows | Where-Object { [double]$_.tick -ge $CapWindowStartTick })
    $shareProducers = 0.0
    $shareAgents = 0.0
    if ($window.Count -gt 0) {
      $belowProducers = @($window | Where-Object { [double]$_.producers -lt (0.8 * $producerMax) })
      $belowAgents = @($window | Where-Object { [double]$_.agents -lt (0.8 * $agentMax) })
      $shareProducers = $belowProducers.Count / $window.Count
      $shareAgents = $belowAgents.Count / $window.Count
    }
    $capPassed = ($window.Count -gt 0) -and (($shareProducers -ge $CapShareThreshold) -or ($shareAgents -ge $CapShareThreshold))
    Write-Check ('seed {0} below-cap sample share >= {1} (tick >= {2})' -f $seed, $CapShareThreshold, $CapWindowStartTick) $capPassed ('window={0} producersShare={1:P1} agentsShare={2:P1}' -f $window.Count, $shareProducers, $shareAgents)

    $hashA = (Get-Content -Raw -LiteralPath $hashAPath).Trim()
    $hashB = (Get-Content -Raw -LiteralPath $hashBPath).Trim()
    $hashOk = ($hashA -ne '') -and ($hashA -ceq $hashB)
    Write-Check ('seed {0} deterministic state hash (a == b)' -f $seed) $hashOk ('a={0} b={1}' -f $hashA, $hashB)
  } catch {
    Write-Check ('seed {0} checks' -f $seed) $false $_.Exception.Message
  }
}

$enduranceTag = 'endurance_seed_{0}' -f $EnduranceSeed
$enduranceCsv = Join-Path $outDir ($enduranceTag + '.csv')
$enduranceArgs = @('--no-render', '--ticks', [string]$EnduranceTicks, '--seed', [string]$EnduranceSeed, '--audit', '--stats', $enduranceCsv, '--stats-interval', [string]$StatsInterval) + $BalanceArgs
try {
  $null = Invoke-Sim $enduranceTag $enduranceArgs
  $rows = Read-StatsRows $enduranceCsv
  $last = $rows[$rows.Count - 1]
  $badAudit = @($rows | Where-Object { $_.audit_ok -ne '1' })
  Write-Check ('endurance seed {0} audit_ok all 1' -f $EnduranceSeed) ($badAudit.Count -eq 0) ('{0} row(s) with audit_ok != 1' -f $badAudit.Count)
  Write-Check ('endurance seed {0} final population positive' -f $EnduranceSeed) (([double]$last.producers -gt 0) -and ([double]$last.agents -gt 0)) ('producers={0} agents={1}' -f $last.producers, $last.agents)
} catch {
  Write-Check ('endurance seed {0} checks' -f $EnduranceSeed) $false $_.Exception.Message
}

if ($script:failures -gt 0) {
  Write-Host ('{0} acceptance check(s) FAILED.' -f $script:failures)
  exit 1
}
Write-Host 'All acceptance checks passed.'
exit 0
