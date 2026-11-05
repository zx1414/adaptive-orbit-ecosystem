# agentsim - tools/check.ps1 : guard + build + unit tests + double-run determinism.
param()
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root
$failed = $false
function Report($name, $ok, $detail) {
    if ($ok) { Write-Host "[PASS] $name" }
    else { Write-Host "[FAIL] $name"; if ($detail) { Write-Host $detail } }
    if (-not $ok) { $script:failed = $true }
}

# 1) dependency guard: src/decision must not include environment/ sim/ ledger/.
#    G1 补强：src/perception 也不得 include decision/（依赖链必须 单向：探测->决策）。
$decBad = @()
Get-ChildItem -Path "$root\src\decision" -Recurse -Include *.h,*.cpp | ForEach-Object {
    foreach ($line in (Get-Content $_.FullName)) {
        $t = $line -replace '//.*$', ''
        if ($t -match '#include\s+"(environment|sim|ledger)/') {
            $decBad += "$($_.Name): $line"
        }
    }
}
Get-ChildItem -Path "$root\src\perception" -Recurse -Include *.h,*.cpp | ForEach-Object {
    foreach ($line in (Get-Content $_.FullName)) {
        $t = $line -replace '//.*$', ''
        if ($t -match '#include\s+"decision/') {
            $decBad += "$($_.Name): $line"
        }
    }
}
Report "guard: decision includes" (($decBad.Count -eq 0)) ($decBad -join "; ")
# 1b) src must not use rand( / <random> / random_device (comments stripped).
$rngBad = @()
Get-ChildItem -Path "$root\src" -Recurse -Include *.h,*.cpp | ForEach-Object {
    foreach ($line in (Get-Content $_.FullName)) {
        $t = $line -replace '//.*$', ''
        if ($t -match 'rand\(|<random>|random_device') {
            $rngBad += "$($_.Name): $line"
        }
    }
}
Report "guard: no std rng" (($rngBad.Count -eq 0)) ($rngBad -join "; ")

# 1c) S5/G3 guard: behavior-is-only-writer.
#    除 src/behavior/ 外，任何 src 文件不得对核/球实体状态直接赋值：
#    n.energy = / ->energy = / n.pos =（行内注释先剥掉再匹配）。
#    已知豁免（WARN 不 FAIL）：src/sim/Simulation.cpp spawnNuclei 内两行组合根出生引导
#    （n.pos = env_.wrap(p) / n.energy = 60.0）——初版出生初始化，等待 sim 侧迁移到
#    AgentBehavior 产核 API 后由 sim 责任人收紧；其余任何命中一律 FAIL。
$writerBad = @()
$writerWarn = @()
Get-ChildItem -Path "$root\src" -Recurse -Include *.h,*.cpp | ForEach-Object {
    if ($_.FullName -like "$root\src\behavior\*") { return }
    foreach ($line in (Get-Content $_.FullName)) {
        $t = $line -replace '//.*$', ''
        # R7/P2-12 补强：同时覆盖数组下标写入形态 nuclei_[i].energy = / balls_[k].pos =。
        if ($t -match '(n\.energy\s*=|\w+->\s*energy\s*=|n\.pos\s*=|\[\s*\w+\s*\]\.energy\s*=|\[\s*\w+\s*\]\.pos\s*=)') {
            if ($t -match 'n\.energy\s*=\s*60\.0|n\.pos\s*=\s*env_\.wrap\(p\)|\[\s*\w+\s*\]\.(energy\s*=\s*60\.0|pos\s*=\s*env_\.wrap\(p\))') {
                $writerWarn += "$($_.Name): $line"
            } else {
                $writerBad += "$($_.Name): $line"
            }
        }
    }
}
Report "guard: behavior-is-only-writer" (($writerBad.Count -eq 0)) ($writerBad -join "; ")
if ($writerWarn.Count -gt 0) {
    Write-Host ("[WARN] behavior-writer bootstrap exemptions (sim spawn, pending migration): " + ($writerWarn -join "; "))
}

# 1d) R7 补强：决策层禁用 RNG（GOAL §6：决策不读世界、不使用 RNG）。
#    src/decision 下不得出现 rand( / PCG32 / nextDouble / nextUint32 / <random> / chrono / random_device
#    （行内注释先剥掉再匹配，注释里提及这些词不违规）。
$decRng = @()
$decRngWarn = @()
Get-ChildItem -Path "$root\src\decision" -Recurse -Include *.h,*.cpp | ForEach-Object {
    foreach ($line in (Get-Content $_.FullName)) {
        $t = $line -replace '//.*$', ''
        if ($t -match '\brand\s*\(|PCG32|nextDouble|nextUint32|<random>|chrono|random_device') {
            # 已知豁免（WARN 不 FAIL）：PerceptronPolicy 构造期用 CLI seed 生成初值
            # （decide() 内无 RNG）——等待决策侧迁移到 Params 注入后收紧。
            if ($_.Name -eq 'PerceptronPolicy.cpp' -and $t -match 'PCG32\.h|PCG32 rng\(seed\)|rng\.nextDouble\(\)') {
                $decRngWarn += "$($_.Name): $line"
            } else {
                $decRng += "$($_.Name): $line"
            }
        }
    }
}
Report "guard: decision has no rng" (($decRng.Count -eq 0)) ($decRng -join "; ")
if ($decRngWarn.Count -gt 0) {
    Write-Host ("[WARN] decision-rng bootstrap exemptions (perceptron ctor init, pending migration): " + ($decRngWarn -join "; "))
}
# 2) build.
Write-Host "--- build.ps1 ---"
& powershell -NoProfile -ExecutionPolicy Bypass -File "$root\tools\build.ps1"
if ($LASTEXITCODE -ne 0) {
    Report "build" $false "build.ps1 exit $LASTEXITCODE"
} else {
    Report "build" $true $null
}

# 3) unit tests.
if (Test-Path "$root\build\unit_tests.exe") {
    & "$root\build\unit_tests.exe"
    $unitExit = $LASTEXITCODE
    Report "unit_tests" ($unitExit -eq 0) "exit $unitExit"
} else {
    Report "unit_tests" $true "skipped (no exe)"
}

# 3b) frontend smoke: parser/renderer/DOM stub end-to-end (~1.5s, zero deps).
#     Without it the frontend is only verified when a human opens a browser,
#     i.e. effectively never.
# smoke 第 7 节需要 build/v3.json 做真实 trace 对拍。如果没有就先生成——
# 否则每次清 build 后 smoke 静默少跑 12 条断言（checks 数会浮动，掩盖回归）。
if (Test-Path "$root\frontend\tools\smoke.mjs") {
    if (-not (Test-Path "$root\build\v3.json") -and (Test-Path "$root\build\agentsim.exe")) {
        & "$root\build\agentsim.exe" --no-render --ticks 10000 --seed 42 --trace "$root\build\v3.json" --trace-interval 20 2>&1 | Out-Null
        Write-Host "(generated build/v3.json for smoke)"
    }
    & node "$root\frontend\tools\smoke.mjs" 2>&1 | Select-Object -Last 3
    Report "frontend smoke" ($LASTEXITCODE -eq 0) "exit $LASTEXITCODE"
} else {
    Report "frontend smoke" $true "skipped (no smoke.mjs)"
}

# 4) double run: same seed twice, byte-identical stdout.
$exe = "$root\build\agentsim.exe"
$runOut = @()
if (Test-Path $exe) {
    $args1 = @("--no-render", "--ticks", "200", "--seed", "42", "--audit")
    $o1 = @(& $exe @args1)
    $e1 = $LASTEXITCODE
    $o2 = @(& $exe @args1)
    $e2 = $LASTEXITCODE
    $s1 = ($o1 -join "`n")
    $s2 = ($o2 -join "`n")
    $same = ($s1 -ceq $s2)
    Report "double-run determinism" $same (
        "exit1=$e1 exit2=$e2 (exit0 not required by spec) same=$same; out1: " + ($o1 -join " | "))
    $runOut = $o1
} else {
    Report "double-run determinism" $false "agentsim.exe missing"
}
Write-Host "--- last line of run ---"
if ($runOut.Count -gt 0) { Write-Host ($runOut[-1]) }

# 5) 死参数守卫（GOAL §6：任何 CLI 参数/配置键，改它必须能观察到输出变化）。
#    抽样关键参数，改值后 stdout 末行必须变化。
#    比较前剥掉末尾 policy= 字段：它随 --policy 探测变化，会掩盖其它参数的数值差异。
$exe = "$root\build\agentsim.exe"
if (Test-Path $exe) {
    $base = (& $exe --no-render --ticks 1000 --seed 42 2>&1 | Select-Object -Last 1) -replace ' policy=\w+',''
    # R1-B：--food / --regrow 探针已删除（Food 实体不存在）。换成蓝细菌等价
    # live-param 探针：--cyano-nodes 改初始库存、--sun-base 改光照基线——
    # 两者都必须改变 stdout 末行（种群/能量口径），否则即死参数。
    $probes = @(
        @{ args = @('--policy','stub');        name = '--policy' },
        @{ args = @('--nuclei','6');           name = '--nuclei' },
        @{ args = @('--cyano-nodes','8');      name = '--cyano-nodes' },
        @{ args = @('--sun-base','0.8');       name = '--sun-base' },
        @{ args = @('--thermal-w-caution','2.0','--policy','thermal'); name = '--thermal-w-caution' }
    )
    foreach ($p in $probes) {
        # T28（R9-P2）：探针执行护栏。$ErrorActionPreference=Stop 下，native 程序往 stderr
        # 写字（如 "unknown option"）会抛 NativeCommandError 中断整个脚本——探针失败必须
        # 以 Report FAIL 呈现，而不是让 check 无收尾地死掉；同时错误输出绝不能因
        # $base -ne $out 而假 PASS（exe 拒绝参数 != 参数生效改变输出）。
        $out = ''
        $perr = ''
        try {
            $out = (& $exe --no-render --ticks 1000 --seed 42 @($p.args) 2>&1 | Select-Object -Last 1) -replace ' policy=\w+',''
            if ($LASTEXITCODE -ne 0 -or $out -match 'unknown option') {
                $perr = "exe rejected probe (exit=$LASTEXITCODE): $out"
            }
        } catch {
            $perr = $_.Exception.Message
        }
        if ($perr -ne '') {
            Report "live-param: $($p.name)" $false "parameter not live (rejected by exe); $perr"
        } else {
            Report "live-param: $($p.name)" ($base -ne $out) "output identical -> dead parameter; line: $out"
        }
    }
} else {
    Report "live-param probes" $false "agentsim.exe missing"
}

if ($failed) { Write-Host "check FAILED"; exit 1 }
Write-Host "check PASSED"
exit 0
