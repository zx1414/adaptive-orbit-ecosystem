$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$exe = Join-Path $root 'build/simulator.exe'
if (-not (Test-Path -LiteralPath $exe)) { throw 'simulator.exe is missing; run tools/build.ps1 first.' }
$artifacts = Join-Path $root 'tests/artifacts'
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
function Invoke-SimProcess([string]$Name, [string[]]$Arguments, [int]$ExpectedExit) {
  $stdout = Join-Path $artifacts ($Name + '.out.txt')
  $stderr = Join-Path $artifacts ($Name + '.err.txt')
  $process = Start-Process -FilePath $exe -ArgumentList $Arguments -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
  if ($process.ExitCode -ne $ExpectedExit) { throw "$Name failed with exit code $($process.ExitCode), expected ${ExpectedExit}: $(Get-Content -Raw $stderr)" }
  return (Get-Content -Raw -LiteralPath $stdout)
}
function Invoke-Sim([string]$Name, [string[]]$Arguments) {
  return (Invoke-SimProcess $Name $Arguments 0)
}
$a = Invoke-Sim 'determinism-a' @('--no-render','--ticks','1000','--seed','42','--policy','rule','--audit')
$b = Invoke-Sim 'determinism-b' @('--no-render','--ticks','1000','--seed','42','--policy','rule','--audit')
if ($a -cne $b) { throw 'Same seed and policy produced different output.' }
$stub = Invoke-Sim 'stub' @('--no-render','--ticks','100','--policy','stub','--audit')
if ($stub -notmatch 'policy=stub') { throw 'Stub policy was not selected.' }
$empty = Invoke-Sim 'no-producers' @('--no-render','--ticks','5000','--producers','0','--agents','4','--audit')
if ($empty -notmatch 'agents=0') { throw 'Agents survived without producers.' }
$hashPath = Join-Path $artifacts 'state-hash.txt'
$summary = Invoke-Sim 'summary' @('--no-render','--ticks','200','--seed','7','--audit','--energy-summary','--state-hash',$hashPath)
if ($summary -notmatch 'energy summary:') { throw 'Energy summary was not printed.' }
if (-not (Test-Path -LiteralPath $hashPath)) { throw 'State hash file was not written.' }
if ((Get-Content -Raw -LiteralPath $hashPath).Trim() -notmatch '^[0-9a-f]+$') { throw 'State hash file is malformed.' }
$statsPath = Join-Path $artifacts 'stats.csv'
$null = Invoke-Sim 'stats' @('--no-render','--ticks','200','--seed','7','--audit','--stats',$statsPath,'--stats-interval','50')
if (-not (Test-Path -LiteralPath $statsPath)) { throw 'Stats CSV was not written.' }
$statsLines = @(Get-Content -LiteralPath $statsPath)
if ($statsLines.Count -lt 4) { throw 'Stats CSV has too few rows.' }
if ($statsLines[0] -notmatch '^tick,producers,agents,.*gene_5,audit_ok$') { throw 'Stats CSV header is malformed.' }
$null = Invoke-SimProcess 'bad-policy' @('--no-render','--ticks','10','--policy','bogus') 2
$null = Invoke-SimProcess 'bad-producers' @('--no-render','--ticks','10','--producers','-1') 2
$null = Invoke-SimProcess 'bad-hash' @('--no-render','--ticks','10','--state-hash',(Join-Path $artifacts 'missing/hash.txt')) 4
Write-Host 'Integration smoke checks passed.'
