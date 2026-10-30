param([string]$Exe = 'build\simulator.exe')
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$exePath = $Exe
if (-not [System.IO.Path]::IsPathRooted($exePath)) { $exePath = Join-Path $root $exePath }
$outDir = Join-Path $root 'build\m4'
New-Item -ItemType Directory -Force -Path $outDir | Out-Null
$script:failures = 0
function Check([string]$Name, [bool]$Passed, [string]$Detail) {
  if ($Passed) { Write-Host ('PASS  ' + $Name) }
  else { Write-Host ('FAIL  ' + $Name + '  ' + $Detail); $script:failures += 1 }
}
function Run-Sim([string]$Name, [string[]]$Arguments) {
  $stdout = Join-Path $outDir ($Name + '.out.txt')
  $stderr = Join-Path $outDir ($Name + '.err.txt')
  $process = Start-Process -FilePath $exePath -ArgumentList $Arguments -NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
  return $process.ExitCode
}
$tracePath = Join-Path $outDir 'trace.html'
$code = Run-Sim 'trace' @('--no-render','--ticks','2000','--seed','42','--audit','--html',$tracePath,'--html-interval','100')
Check 'html run exit' ($code -eq 0) ('code=' + $code)
Check 'html file exists' (Test-Path -LiteralPath $tracePath) $tracePath
if (Test-Path -LiteralPath $tracePath) {
  $text = Get-Content -Raw -LiteralPath $tracePath
  Check 'html size' ($text.Length -gt 10000) ('length=' + $text.Length)
  Check 'embedded trace' ($text.Contains('const TRACE =')) 'missing const TRACE'
  Check 'canvas present' ($text.Contains('canvas')) 'missing canvas'
  Check 'no external urls' (-not ($text -match 'https?://')) 'external URL found'
  $match = [regex]::Match($text, '(?s)const TRACE = (\{.*\});')
  Check 'trace json found' $match.Success 'regex did not match'
  if ($match.Success) {
    $trace = $match.Groups[1].Value | ConvertFrom-Json
    $frames = @($trace.frames)
    Check 'frame count >= 2' ($frames.Count -ge 2) ('frames=' + $frames.Count)
    $increasing = $true
    for ($i = 1; $i -lt $frames.Count; ++$i) {
      if ([double]$frames[$i].tick -le [double]$frames[$i - 1].tick) { $increasing = $false; break }
    }
    Check 'frame ticks increase' $increasing 'ticks are not strictly increasing'
    if ($frames.Count -gt 0) {
      Check 'first frame bodies' (($frames[0].p.Count -eq [int]$frames[0].producers) -and ($frames[0].a.Count -eq [int]$frames[0].agents)) ('p=' + $frames[0].p.Count + ' a=' + $frames[0].a.Count)
    }
  }
}
$jsonPath = Join-Path $outDir 'trace.json'
$code = Run-Sim 'trace-json' @('--no-render','--ticks','2000','--seed','42','--audit','--trace-json',$jsonPath,'--trace-json-interval','100')
Check 'trace-json run exit' ($code -eq 0) ('code=' + $code)
if (Test-Path -LiteralPath $jsonPath) {
  $j = Get-Content -Raw -LiteralPath $jsonPath | ConvertFrom-Json
  $jf = @($j.frames)
  Check 'trace-json frame count' ($jf.Count -ge 2) ('frames=' + $jf.Count)
  Check 'trace-json last tick' (([double]$jf[-1].tick) -eq 2000) ('last=' + $jf[-1].tick)
  Check 'trace-json name/meta' (($j.name -eq 'EcoCore trace') -and ([int]$j.meta.seed -eq 42)) ('name=' + $j.name)
} else { Check 'trace-json file' $false $jsonPath }
$missingJson = Join-Path $outDir 'missing\x.json'
$code = Run-Sim 'json-write-fail' @('--no-render','--ticks','10','--trace-json',$missingJson)
Check 'trace-json write failure exit 4' ($code -eq 4) ('code=' + $code)
$code = Run-Sim 'json-bad-interval' @('--no-render','--ticks','10','--trace-json',(Join-Path $outDir 'x.json'),'--trace-json-interval','0')
Check 'trace-json interval validation exit 2' ($code -eq 2) ('code=' + $code)
$densePath = Join-Path $outDir 'dense.html'
$code = Run-Sim 'dense' @('--no-render','--ticks','2500','--seed','42','--audit','--html',$densePath,'--html-interval','1')
Check 'dense trace exit' ($code -eq 0) ('code=' + $code)
$denseOut = [string](Get-Content -Raw -LiteralPath (Join-Path $outDir 'dense.out.txt'))
Check 'dense trace reports downsample' ($denseOut -match 'downsampled=1') $denseOut
if (Test-Path -LiteralPath $densePath) {
  $denseText = Get-Content -Raw -LiteralPath $densePath
  $denseMatch = [regex]::Match($denseText, '(?s)const TRACE = (\{.*\});')
  if ($denseMatch.Success) {
    $dense = $denseMatch.Groups[1].Value | ConvertFrom-Json
    $denseFrames = @($dense.frames)
    Check 'dense trace frame cap' ($denseFrames.Count -le 2000) ('frames=' + $denseFrames.Count)
    Check 'dense trace keeps final tick' (([double]$denseFrames[-1].tick) -eq 2500) ('last=' + $denseFrames[-1].tick)
  } else { Check 'dense trace json' $false 'regex did not match' }
}
$missing = Join-Path $outDir 'missing\x.html'
$code = Run-Sim 'write-fail' @('--no-render','--ticks','10','--html',$missing)
Check 'html write failure exit 4' ($code -eq 4) ('code=' + $code)
$code = Run-Sim 'bad-interval' @('--no-render','--ticks','10','--html',(Join-Path $outDir 'x.html'),'--html-interval','0')
Check 'html interval validation exit 2' ($code -eq 2) ('code=' + $code)
$snap = Join-Path $outDir 'mid.snap'
if (Test-Path -LiteralPath $snap) { Remove-Item -LiteralPath $snap }
$code = Run-Sim 'save' @('--no-render','--ticks','500','--seed','42','--save',$snap)
Check 'save exit' ($code -eq 0) ('code=' + $code)
Check 'save produced file' (Test-Path -LiteralPath $snap) $snap
$loaded = Join-Path $outDir 'loaded.html'
if (Test-Path -LiteralPath $loaded) { Remove-Item -LiteralPath $loaded }
$code = Run-Sim 'loaded' @('--no-render','--load',$snap,'--ticks','1000','--html',$loaded,'--html-interval','100')
Check 'html with load exit' ($code -eq 0) ('code=' + $code)
Check 'html with load file' (Test-Path -LiteralPath $loaded) $loaded
$snap505 = Join-Path $outDir 'load505.snap'
$null = Run-Sim 'save505' @('--no-render','--ticks','505','--seed','42','--save',$snap505)
$statsPath = Join-Path $outDir 'loadstats.csv'
$code = Run-Sim 'loadstats' @('--no-render','--load',$snap505,'--ticks','600','--stats',$statsPath,'--stats-interval','100')
Check 'load stats exit' ($code -eq 0) ('code=' + $code)
if (Test-Path -LiteralPath $statsPath) {
  $statsRows = @(Import-Csv -LiteralPath $statsPath)
  Check 'load stats first row at load tick' ($statsRows.Count -gt 0 -and [double]$statsRows[0].tick -eq 505) ('first=' + $(if ($statsRows.Count -gt 0) { $statsRows[0].tick } else { 'none' }))
} else { Check 'load stats file' $false $statsPath }
# Zero tick budget must not step: the summary must still describe tick 0.
$code = Run-Sim 'zero-ticks' @('--no-render','--ticks','0','--seed','42','--audit')
Check 'zero ticks exit' ($code -eq 0) ('code=' + $code)
$zeroOut = [string](Get-Content -Raw -LiteralPath (Join-Path $outDir 'zero-ticks.out.txt'))
Check 'zero ticks reports tick 0' ($zeroOut -match 'done tick=0 ') $zeroOut
Check 'zero ticks dissipated nothing' ($zeroOut -match 'dissipated=0 ') $zeroOut
$zeroJson = Join-Path $outDir 'zero.json'
if (Test-Path -LiteralPath $zeroJson) { Remove-Item -LiteralPath $zeroJson }
$code = Run-Sim 'zero-trace' @('--no-render','--ticks','0','--seed','42','--trace-json',$zeroJson)
Check 'zero trace exit' ($code -eq 0) ('code=' + $code)
if (Test-Path -LiteralPath $zeroJson) {
  $zj = Get-Content -Raw -LiteralPath $zeroJson | ConvertFrom-Json
  $zf = @($zj.frames)
  Check 'zero trace single frame' ($zf.Count -eq 1) ('frames=' + $zf.Count)
  Check 'zero trace frame at tick 0' (($zf.Count -eq 1) -and ([double]$zf[0].tick -eq 0)) 'unexpected first tick'
  Remove-Item -LiteralPath $zeroJson
}

# A load that lands off the reporting grid must still emit the resumed state first (P2-6).
$offSnap = Join-Path $outDir 'off.snap'
if (Test-Path -LiteralPath $offSnap) { Remove-Item -LiteralPath $offSnap }
$null = Run-Sim 'offsave' @('--no-render','--ticks','505','--seed','42','--save',$offSnap)
$offJson = Join-Path $outDir 'off.json'
if (Test-Path -LiteralPath $offJson) { Remove-Item -LiteralPath $offJson }
$code = Run-Sim 'off' @('--no-render','--load',$offSnap,'--ticks','600','--trace-json',$offJson,'--trace-json-interval','100')
Check 'off-grid load exit' ($code -eq 0) ('code=' + $code)
if (Test-Path -LiteralPath $offJson) {
  $oj = Get-Content -Raw -LiteralPath $offJson | ConvertFrom-Json
  $of = @($oj.frames)
  Check 'off-grid trace starts at load tick' (($of.Count -ge 2) -and ([double]$of[0].tick -eq 505)) ('first=' + $(if ($of.Count -gt 0) { $of[0].tick } else { 'none' }))
  Check 'off-grid trace ends at end tick' (($of.Count -ge 1) -and ([double]$of[-1].tick -eq 600)) ('last=' + $(if ($of.Count -gt 0) { $of[-1].tick } else { 'none' }))
  Remove-Item -LiteralPath $offJson
}

# A missing snapshot path is an I/O failure (4); bad contents stay a snapshot error (5).
$code = Run-Sim 'missing-load' @('--no-render','--load',(Join-Path $outDir 'absent.snap'))
Check 'missing load path exit 4' ($code -eq 4) ('code=' + $code)

if ($script:failures -gt 0) { Write-Host ($script:failures.ToString() + ' M4 check(s) FAILED.'); exit 1 }
Write-Host 'All M4 acceptance checks passed.'
exit 0