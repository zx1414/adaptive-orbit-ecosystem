# sweep.ps1 - agentsim 参数扫描工具（任务卡 R2-D）。
#
# 用法:
#   # 单参数扫描
#   .\tools\sweep.ps1 -Param "nuclei" -Values 8,12,24 -Ticks 20000 -Seeds 42,7,123
#
#   # 二维网格
#   .\tools\sweep.ps1 -Grid @{ "nuclei"=8,12; "food"=32,64 } -Ticks 10000
#
# 输出: build\sweep-<时间戳>.csv + 控制台汇总表 + 推荐组合。
#
# 重要: 当前构建里只有一部分参数有 CLI 通道（见 $script:Wired 的白名单）。
# ParamSchema 里的其它键（如 sun_base_level / cyano_photosynthesis_rate）还没接线，
# 传了会被显式警告并跳过该组合（宁可少跑也不静默假跑）。
# 主代理接线后，往 $script:Wired 表里加一行映射即可，无需改其它逻辑。
#
# 串行执行（不并行）: 并行会干扰计时与内存，且本工具的指标与时间无关。
# 每组参数 x 每个种子跑一次 agentsim + analyze.mjs（verdict = 6 条判据汇总）。
# 实现注意: 本文件禁止使用 PS 续行符（生成工具限制），长语句一律拆成多步或用变量。

param(
    [string] $Param = "",
    # 标量数组即可: -Values 0.2,0.35,0.5 或 -Values rule,bandit
    [object[]] $Values = @(),
    # 二维网格: -Grid @{ "nuclei"=8,12; "food"=32,64 }
    [hashtable] $Grid = @{},
    [int] $Ticks = 20000,
    [uint64[]] $Seeds = @(42),
    # 覆盖默认 exe（默认 build\agentsim.exe）
    [string] $Exe = "",
    # 保留每次运行的 trace/stats/verdict 中间文件（默认只留汇总 CSV）
    [switch] $KeepArtifacts,
    # 传给 agentsim 的额外固定参数
    [string[]] $ExtraArgs = @()
)

$ErrorActionPreference = "Stop"
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }  # 管道下中文不乱码
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$nl = [string][char]13 + [string][char]10   # CSV 行尾

if (-not $Exe) { $Exe = Join-Path $root "build\agentsim.exe" }
if (-not (Test-Path $Exe)) {
    Write-Error ("agentsim.exe 不存在: {0}（先跑 tools\build.ps1）" -f $Exe)
    exit 2
}
$analyze = Join-Path $root "tools\analyze.mjs"
if (-not (Test-Path $analyze)) {
    Write-Error ("analyze.mjs 不存在: {0}" -f $analyze)
    exit 2
}
$node = "node"
try { $null = & $node --version } catch {
    Write-Error "node 不可用（sweep 需要 node 跑 analyze.mjs 判 verdict）"
    exit 2
}

function ToInv([object] $v) {
    # 数值 -> 不变文化字符串（避免区域设置把小数点变逗号）
    if ($v -is [double]) { return $v.ToString([System.Globalization.CultureInfo]::InvariantCulture) }
    if ($v -is [float] -or $v -is [single]) { return ([double]$v).ToString([System.Globalization.CultureInfo]::InvariantCulture) }
    return "$v"
}

# ---- 参数 -> CLI 映射白名单 -------------------------------------------------
# 值: scriptblock { param($v) }, 返回该参数对应的 CLI 参数数组。
# 只登记"当前构建里真实生效"的通道；未登记的键在扫描开始前统一警告并跳过。
$script:Wired = @{
    "nuclei" = { param($v) @("--nuclei", (ToInv $v)) }
    "food"   = { param($v) @("--food",   (ToInv $v)) }
    "balls"  = { param($v) @("--food",   (ToInv $v)) }   # ParamSchema 别名
    "policy" = { param($v) @("--policy", ("$v")) }
    "width"  = { param($v) @("--width",  (ToInv $v)) }
    "height" = { param($v) @("--height", (ToInv $v)) }
}

# ---- 组装扫描计划 -----------------------------------------------------------
# $script:plan 元素: @{ Key=组合唯一名; Args=@(); Values=@{参数名=值}; Skipped=bool }
$script:plan = @()
$script:warnedUnknown = @{}

function Add-Combos([string[]] $names, [object[][]] $valLists) {
    # 笛卡尔积: names[i] 各取 valLists[i] 一个值
    $idx = New-Object int[] $names.Length
    while ($true) {
        $vals = @{}
        $tagParts = @()
        for ($i = 0; $i -lt $names.Length; $i++) {
            $vals[$names[$i]] = $valLists[$i][$idx[$i]]
            $tagParts += ("{0}={1}" -f $names[$i], (ToInv $valLists[$i][$idx[$i]]))
        }
        $cli = @()
        $ok = $true
        for ($i = 0; $i -lt $names.Length; $i++) {
            $nm = $names[$i]
            if (-not $script:Wired.ContainsKey($nm)) {
                if (-not $script:warnedUnknown.ContainsKey($nm)) {
                    Write-Host ("[警告] 参数 '{0}' 当前构建没有 CLI 通道（未接线），相关组合将被跳过。" -f $nm) -ForegroundColor Yellow
                    Write-Host "        需要主代理接线后，在 sweep.ps1 的 Wired 表里加映射。" -ForegroundColor Yellow
                    $script:warnedUnknown[$nm] = $true
                }
                $ok = $false
                break
            }
            $cli += (& $script:Wired[$nm] $valLists[$i][$idx[$i]])
        }
        $script:plan += @{ Key = ($tagParts -join ","); Args = $cli; Values = $vals; Skipped = (-not $ok) }
        # 进位
        $carry = $names.Length - 1
        while ($carry -ge 0) {
            $idx[$carry]++
            if ($idx[$carry] -lt $valLists[$carry].Length) { break }
            $idx[$carry] = 0
            $carry--
        }
        if ($carry -lt 0) { break }
    }
}

if ($Param -ne "") {
    if ($Values.Count -eq 0) { Write-Error "给了 -Param 但没给 -Values"; exit 2 }
    Add-Combos @($Param) @(, $Values)
} elseif ($Grid.Count -gt 0) {
    $names = @($Grid.Keys)
    $lists = @()
    foreach ($n in $names) {
        $v = $Grid[$n]
        if ($v -isnot [System.Collections.IEnumerable] -or $v -is [string]) { $v = @($v) }
        $lists += , @($v)
    }
    Add-Combos $names $lists
} else {
    Write-Error "必须给 -Param/-Values 或 -Grid"
    exit 2
}

if ($script:plan.Count -eq 0) { Write-Error "扫描计划为空"; exit 2 }
$runCount = $script:plan.Count * $Seeds.Count
Write-Host ("sweep: {0} 组合 x {1} seeds = {2} 次运行（串行）" -f $script:plan.Count, $Seeds.Count, $runCount)
Write-Host ("      ticks={0}  exe={1}" -f $Ticks, $Exe)

# ---- 输出文件 ---------------------------------------------------------------
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$outCsv = Join-Path $root ("build\sweep-{0}.csv" -f $stamp)
New-Item -ItemType Directory -Force -Path (Join-Path $root "build") | Out-Null

# ---- 指标计算 ---------------------------------------------------------------
function Get-EnergySlope([string] $statsPath) {
    # stats.csv: tick,nuclei,food,total_energy,audit_ok,deaths,state_hash
    # 线性回归斜率（单位: 每 tick）；行数 < 2 时返回 NaN
    $xs = New-Object System.Collections.Generic.List[double]
    $ys = New-Object System.Collections.Generic.List[double]
    foreach ($line in [System.IO.File]::ReadLines($statsPath)) {
        if ($line -match '^(?<t>[0-9]+),(?<n>-?[0-9.]+),(?<f>-?[0-9.]+),(?<e>-?[0-9.eE+]+),') {
            $xs.Add([double]::Parse($Matches.t, [System.Globalization.CultureInfo]::InvariantCulture))
            $ys.Add([double]::Parse($Matches.e, [System.Globalization.CultureInfo]::InvariantCulture))
        }
    }
    $n = $ys.Count
    if ($n -lt 2) { return [double]::NaN }
    $mx = ($xs | Measure-Object -Average).Average
    $my = ($ys | Measure-Object -Average).Average
    $num = 0.0; $den = 0.0
    for ($i = 0; $i -lt $n; $i++) {
        $dx = $xs[$i] - $mx
        $num += $dx * ($ys[$i] - $my)
        $den += $dx * $dx
    }
    if ($den -le 0) { return [double]::NaN }
    return $num / $den
}

function Get-NucleiCv([string] $statsPath) {
    # 末 20% 行的 核数标准差/均值（总体口径，与 analyze.mjs 判据 2 同口径）
    $all = New-Object System.Collections.Generic.List[double]
    foreach ($line in [System.IO.File]::ReadLines($statsPath)) {
        if ($line -match '^[0-9]+,(?<n>[0-9]+),') {
            $all.Add([double]::Parse($Matches.n, [System.Globalization.CultureInfo]::InvariantCulture))
        }
    }
    $n = $all.Count
    if ($n -eq 0) { return [double]::NaN }
    $start = [Math]::Min($n - 1, [int][Math]::Floor($n * 0.8))
    $tail = $all.GetRange($start, $n - $start)
    $m = ($tail | Measure-Object -Average).Average
    if ([Math]::Abs($m) -lt 1e-12) { return [double]::NaN }
    $ss = 0.0
    foreach ($v in $tail) { $ss += ($v - $m) * ($v - $m) }
    return [Math]::Sqrt($ss / $tail.Count) / $m
}

# ---- 主循环 -----------------------------------------------------------------
$columns = @()
foreach ($p in $script:plan) { foreach ($k in $p.Values.Keys) { if ($columns -notcontains $k) { $columns += $k } } }

$header = ($columns + @("seed","nuclei_final","total_energy_final","energy_slope","cyano_final","nuclei_cv","alive","exit_code","verdict")) -join ","
[System.IO.File]::WriteAllText($outCsv, $header + $nl)

function Write-CsvLine([string[]] $fields) {
    [System.IO.File]::AppendAllText($outCsv, ($fields -join ",") + $nl)
}

function ParamColumns([hashtable] $vals) {
    $line = @()
    foreach ($c in $columns) { $line += (ToInv $vals[$c]) }
    return $line
}

function FmtNum([double] $v, [string] $spec) {
    if ([double]::IsNaN($v)) { return "nan" }
    return $v.ToString($spec, [System.Globalization.CultureInfo]::InvariantCulture)
}

$rows = @()
$runIdx = 0
foreach ($p in $script:plan) {
    foreach ($seed in $Seeds) {
        $runIdx++
        $tag = ("[{0}/{1}] {2} seed={3}" -f $runIdx, $runCount, $p.Key, $seed)
        if ($p.Skipped) {
            Write-Host ("{0}  SKIP(参数未接线)" -f $tag) -ForegroundColor Yellow
            Write-CsvLine (@(ParamColumns $p.Values) + @("$seed","","","","","","","-1","SKIP_PARAM_NOT_WIRED"))
            continue
        }

        $statsPath = Join-Path $root ("build\sweep-run-{0}.stats.csv" -f $stamp)
        $tracePath = Join-Path $root ("build\sweep-run-{0}.trace.json" -f $stamp)
        $jsonPath  = Join-Path $root ("build\sweep-run-{0}.verdict.json" -f $stamp)
        Remove-Item $statsPath, $tracePath, $jsonPath -ErrorAction SilentlyContinue

        $interval = [Math]::Max(1, [int]($Ticks / 100))
        $argList = @("--no-render","--ticks","$Ticks","--seed","$seed") + $p.Args + $ExtraArgs +
                   @("--stats", $statsPath, "--stats-interval", "1",
                     "--trace", $tracePath, "--trace-interval", "$interval")
        $sw = [System.Diagnostics.Stopwatch]::StartNew()
        $stdout = @()
        $exitCode = -1
        try {
            # stderr（灭绝告警等）直接透传控制台，不转 ErrorRecord
            $stdout = @(& $Exe @argList)
            $exitCode = $LASTEXITCODE
        } catch {
            Write-Host ("{0}  运行异常: {1}" -f $tag, $_.Exception.Message) -ForegroundColor Red
            $exitCode = -2
        }
        $sw.Stop()

        if ($exitCode -ne 0 -and $exitCode -ne 5) {
            # 2=参数错 3=审计失败 4=IO —— 记录并跳过 analyze（5=灭绝仍可分析部分 trace）
            $msg = ("{0}  FAIL exit={1} ({2}s)" -f $tag, $exitCode, [int]$sw.Elapsed.TotalSeconds)
            Write-Host $msg -ForegroundColor Red
            if ($stdout.Count -gt 0) { Write-Host ("        stdout 末行: " + ($stdout | Select-Object -Last 1)) }
            Write-CsvLine (@(ParamColumns $p.Values) + @("$seed","","","","","","","$exitCode","RUN_ERROR"))
            continue
        }

        # 指标
        $slope = Get-EnergySlope $statsPath
        $cv = Get-NucleiCv $statsPath
        $nFinal = ""; $eFinal = ""
        $lastLine = $null
        foreach ($l in [System.IO.File]::ReadLines($statsPath)) { $lastLine = $l }
        if ($lastLine -match '^[0-9]+,(?<n>[0-9]+),(?<f>[0-9.]+),(?<e>-?[0-9.eE+]+),') {
            $nFinal = $Matches.n
            $eFinal = $Matches.e
        }
        $cyanoFinal = ""
        $lastOut = ($stdout | Select-Object -Last 1)
        if ("$lastOut" -match 'cyano=(?<c>[0-9]+)') { $cyanoFinal = $Matches.c }  # v3 主干 stdout

        # verdict: analyze.mjs --json
        $verdict = "ANALYZE_ERROR"
        $passN = -1; $failN = -1; $skipN = -1
        if (Test-Path $tracePath) {
            $null = & $node $analyze $tracePath --json $jsonPath 2>$null
            $aExit = $LASTEXITCODE
            if ((Test-Path $jsonPath) -and $aExit -ne 2) {
                try {
                    $j = Get-Content $jsonPath -Raw -Encoding UTF8 | ConvertFrom-Json
                    $passN = [int]$j.summary.PASS
                    $failN = [int]$j.summary.FAIL
                    $skipN = [int]$j.summary.SKIP
                    # verdict 规则: 有任何 FAIL -> FAIL；否则（全 PASS/SKIP）-> PASS
                    if ($failN -gt 0) { $verdict = "FAIL" } else { $verdict = "PASS" }
                } catch {
                    $verdict = "ANALYZE_ERROR"
                }
            }
        }
        $alive = "false"; if ($exitCode -eq 0) { $alive = "true" }
        $msg = ("{0}  exit={1} nuclei={2} slope={3} cv={4} verdict={5} ({6}P/{7}S/{8}F) ({9}s)" -f $tag, $exitCode, $nFinal, (FmtNum $slope "0.######"), (FmtNum $cv "0.####"), $verdict, $passN, $skipN, $failN, [int]$sw.Elapsed.TotalSeconds)
        Write-Host $msg

        $slopeCsv = ""; if (-not [double]::IsNaN($slope)) { $slopeCsv = $slope.ToString("0.########", [System.Globalization.CultureInfo]::InvariantCulture) }
        $cvCsv = ""; if (-not [double]::IsNaN($cv)) { $cvCsv = $cv.ToString("0.########", [System.Globalization.CultureInfo]::InvariantCulture) }
        Write-CsvLine (@(ParamColumns $p.Values) + @("$seed", $nFinal, $eFinal, $slopeCsv, $cyanoFinal, $cvCsv, $alive, "$exitCode", $verdict))
        $rows += [pscustomobject]@{
            Combo = $p.Key; Seed = $seed; Nuclei = $nFinal; Energy = $eFinal
            Slope = $slope; Cv = $cv; Cyano = $cyanoFinal; Alive = $alive
            Exit = $exitCode; Verdict = $verdict; Pass = $passN; Skip = $skipN; Fail = $failN
        }

        if (-not $KeepArtifacts) {
            Remove-Item $statsPath, $tracePath, $jsonPath -ErrorAction SilentlyContinue
        }
    }
}

# ---- 汇总 -------------------------------------------------------------------
Write-Host ""
Write-Host "================ sweep 汇总 ================"
Write-Host $header
foreach ($r in $rows) {
    $msg = ("{0}  seed={1}  nuclei={2}  energy={3}  slope={4}  cv={5}  exit={6}  {7} ({8}P/{9}S/{10}F)" -f $r.Combo, $r.Seed, $r.Nuclei, $r.Energy, (FmtNum $r.Slope "0.####"), (FmtNum $r.Cv "0.####"), $r.Exit, $r.Verdict, $r.Pass, $r.Skip, $r.Fail)
    Write-Host $msg
}

# 推荐排序: RUN_ERROR/SKIP 不参与；先 FAIL 少，再 PASS 多，再 |slope| 小
# （Sort-Object 的多键排序属性先装进数组，避免续行符）
$sortSpec = @(
    @{ Expression = { [int]$_.Fail }; Ascending = $true },
    @{ Expression = { [int]$_.Pass }; Ascending = $false },
    @{ Expression = { if ([double]::IsNaN($_.Slope)) { [double]::MaxValue } else { [Math]::Abs($_.Slope) } }; Ascending = $true }
)
$candidates = @($rows | Where-Object { $_.Verdict -eq "PASS" -or $_.Verdict -eq "FAIL" })
if ($candidates.Count -gt 0) {
    $best = @($candidates | Sort-Object -Property $sortSpec)[0]
    $msg = ("推荐组合: {0} (seed={1})  —— 判据 {2}P/{3}S/{4}F, slope={5}, cv={6}" -f $best.Combo, $best.Seed, $best.Pass, $best.Skip, $best.Fail, (FmtNum $best.Slope "0.######"), (FmtNum $best.Cv "0.####"))
    Write-Host ""
    Write-Host $msg
    Write-Host "注意: verdict 的 PASS 只表示当前判据集内无 FAIL；SKIP（缺 genome）多时含金量有限，读 CSV 而不是只看这个词。"
} else {
    Write-Host ""
    Write-Host "没有可评分的运行（全部 RUN_ERROR / SKIP_PARAM_NOT_WIRED）。"
}
Write-Host ""
Write-Host ("CSV 已写出: {0}" -f $outCsv)
exit 0
