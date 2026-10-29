# M3 snapshot acceptance: split run must equal uninterrupted run.
param(
  [int]$Ticks = 20000,
  [int]$SplitTick = 10000,
  [int[]]$Seeds = @(42, 7),
  [string]$Exe = 'build\simulator.exe'
)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$exePath = $Exe
if (-not [System.IO.Path]::IsPathRooted($exePath)) { $exePath = Join-Path $root $exePath }
if (-not (Test-Path -LiteralPath $exePath)) { throw ('simulator not found: ' + $exePath) }
$outDir = Join-Path $root 'build\m3'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$script:failures = 0

function Check([string]$Name, [bool]$Passed, [string]$Detail) {
  if ($Passed) { Write-Host ('PASS  ' + $Name) }
  else { Write-Host ('FAIL  ' + $Name + '  ' + $Detail); $script:failures = $script:failures + 1 }
}
function Run-Sim([string]$Name, [string[]]$Arguments) {
  $stdout = Join-Path $outDir ($Name + '.out.txt')
  $stderr = Join-Path $outDir ($Name + '.err.txt')
  $process = Start-Process -FilePath $exePath -ArgumentList $Arguments -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
  return [pscustomobject]@{
    Code = $process.ExitCode
    Out = [string](Get-Content -Raw -LiteralPath $stdout)
    Err = [string](Get-Content -Raw -LiteralPath $stderr)
  }
}
function Field([string]$Text, [string]$Key) {
  $match = [regex]::Match($Text, [regex]::Escape($Key) + '=([^\s]+)')
  if (-not $match.Success) { return '<missing>' }
  return $match.Groups[1].Value
}
function Hash-File([string]$Path) {
  return ([string](Get-Content -Raw -LiteralPath $Path)).Trim()
}
function Expect-Code([string]$Name, [string[]]$Arguments, [int]$Expected) {
  $result = Run-Sim $Name $Arguments
  Check ($Name + ' exit code ' + $Expected) ($result.Code -eq $Expected) ('actual=' + $result.Code + ' stderr=' + $result.Err)
}

foreach ($seed in $Seeds) {
  $directHash = Join-Path $outDir ('direct_' + $seed + '.hash')
  $midHash = Join-Path $outDir ('mid_' + $seed + '.hash')
  $resumedHash = Join-Path $outDir ('resumed_' + $seed + '.hash')
  $midSnap = Join-Path $outDir ('mid_' + $seed + '.snap')

  $direct = Run-Sim ('direct_' + $seed) @('--no-render','--ticks',[string]$Ticks,'--seed',[string]$seed,'--audit','--state-hash',$directHash)
  Check ('seed ' + $seed + ' direct exit') ($direct.Code -eq 0) ('code=' + $direct.Code)
  $mid = Run-Sim ('mid_' + $seed) @('--no-render','--ticks',[string]$SplitTick,'--seed',[string]$seed,'--audit','--save',$midSnap,'--state-hash',$midHash)
  Check ('seed ' + $seed + ' split exit') ($mid.Code -eq 0) ('code=' + $mid.Code)
  $resumed = Run-Sim ('resumed_' + $seed) @('--no-render','--load',$midSnap,'--ticks',[string]$Ticks,'--audit','--state-hash',$resumedHash)
  Check ('seed ' + $seed + ' resume exit') ($resumed.Code -eq 0) ('code=' + $resumed.Code)

  $a = Hash-File $directHash
  $b = Hash-File $resumedHash
  Check ('seed ' + $seed + ' state hash') ($a -ne '' -and $a -ceq $b) ('direct=' + $a + ' resumed=' + $b)
  foreach ($key in @('producers','agents','total_energy','injected','dissipated','transferred')) {
    $va = Field $direct.Out $key
    $vb = Field $resumed.Out $key
    Check ('seed ' + $seed + ' ' + $key) ($va -ceq $vb) ('direct=' + $va + ' resumed=' + $vb)
  }
  Check ('seed ' + $seed + ' audit ok') ((Field $direct.Out 'audit') -eq 'ok' -and (Field $resumed.Out 'audit') -eq 'ok') ('direct=' + (Field $direct.Out 'audit') + ' resumed=' + (Field $resumed.Out 'audit'))

  $badSnap = Join-Path $outDir ('bad_' + $seed + '.snap')
  $bytes = [System.IO.File]::ReadAllBytes($midSnap)
  $bytes[$bytes.Length - 1] = $bytes[$bytes.Length - 1] -bxor 0xFF
  [System.IO.File]::WriteAllBytes($badSnap, $bytes)
  Expect-Code ('seed ' + $seed + ' corrupt') @('--no-render','--load',$badSnap,'--ticks','100') 5

  $emptySnap = Join-Path $outDir ('empty_' + $seed + '.snap')
  New-Item -ItemType File -Force -Path $emptySnap | Out-Null
  Expect-Code ('seed ' + $seed + ' empty') @('--no-render','--load',$emptySnap,'--ticks','100') 5

  $versionSnap = Join-Path $outDir ('version_' + $seed + '.snap')
  $versionBytes = [System.IO.File]::ReadAllBytes($midSnap)
  $versionBytes[8] = 2
  [System.IO.File]::WriteAllBytes($versionSnap, $versionBytes)
  Expect-Code ('seed ' + $seed + ' version') @('--no-render','--load',$versionSnap,'--ticks','100') 5

  Expect-Code ('seed ' + $seed + ' load+seed override') @('--no-render','--load',$midSnap,'--ticks','100','--seed','7') 2
}

if ($script:failures -gt 0) { Write-Host ($script:failures.ToString() + ' M3 acceptance check(s) FAILED.'); exit 1 }
Write-Host 'All M3 acceptance checks passed.'
exit 0
