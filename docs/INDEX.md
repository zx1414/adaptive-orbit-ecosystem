# INDEX —— 已完成证据登记

> **无证据不得声称完成**：TODO.md 中任何条目移入"已完成"之前，必须先在本表登记
> 可执行的验证动作（命令、测试名、脚本、diff 空输出、日志路径）。禁止主观描述（如"基本完成"）。
> 对应成功标准编号以 GOAL.md 为准。
> 证据来源：下列命令与测试均由主代理于 2026-10-31 实测（输出原样记录）；文档维护者同日
> 独立复跑了 A3 双跑逐字节比较与 A1/A2 grep，结果一致；「第三轮门禁快照」各命令由文档维护者同日独立复跑，输出与主代理基线一致。长命令原文见文末附录；R1 修复后（T08/T10/T11/T13、T18–T25）的证据见「第二轮门禁快照」，P0 修复与繁殖接线/温度场/C 系列落盘后（T27–T32）的证据统一见文末「第三轮门禁快照」。

| 任务 | 对应成功标准 | 证据 | 验证方式 |
| --- | --- | --- | --- |
| T01 Torus 坐标与最小环距 | S1 | tests/unit/test_torus.cpp：torus_wrap_basics、torus_delta_cross_edges、torus_delta_diagonal、torus_distance_never_exceeds_half_extent；命令 `build\unit_tests.exe` → `19 tests, 3175 checks, 0 failures` [exit 0] | 执行 `build\unit_tests.exe`，断言 0 failures 且上列 4 个 torus 用例全部在列并通过 |
| T02 基础食物生成与吃取 | S3, S5 | tests/unit/test_environment.cpp：env_spawn_food_deterministic_and_counted、env_query_food_across_edge、env_consume_food_and_stats；`build\agentsim.exe --no-render --ticks 5 --seed 42 --audit` → `done tick=5 nuclei=12 food=53 total_energy=1403.84000000 audit=ok policy=rule` [exit 0]；进食路径 src/behavior/AgentBehavior.cpp:77-89 为纯 `ledger.transfer(kFoodPoolId=0, ...)` 零和搬运（含 D2 修复：regrowFood 再生、eatRadius=14、每 tick 限食 2） | 执行 `build\unit_tests.exe` 与上述审计跑，断言 audit=ok 且 0 failures |
| T03 RNG 与确定性 | S2 | tests/unit/test_rng.cpp：rng_fixed_seed_reproducible、rng_different_seeds_differ、rng_snapshot_state_roundtrip、rng_double_in_unit_range、rng_set_state_resumes_midstream；`tools\check.ps1` → [PASS] guard:no-std-rng、[PASS] double-run-determinism → check PASSED [exit 0] | 执行 `tools\check.ps1`，断言以上两项 PASS 且退出码 0 |
| T04 探测向量 SensorVector | S4 | src/perception/AgentSensors.h:15：`sense` 纯函数输出 16 维 SensorVector（只读环境，不写实体状态）；`tools\check.ps1` → [PASS] guard:decision-includes（断言 src/decision 对 environment/sim/ledger 零 include） | 执行 `tools\check.ps1`，断言 guard:decision-includes PASS |
| T05 | 规则打分决策 | S4, S8 | 附录 A2（ADR-003 改判版）grep：`: public IDecisionPolicy` 命中 RulePolicy.h 两处（RulePolicy 与 StubPolicy）与 PerceptronPolicy.h 一处；`neural|nnet` 在 src/ 下 0 命中；`tools\check.ps1` → [PASS] guard:decision-includes | 执行附录 A2 改判版四条 grep 并核对预期输出 + check.ps1 退出码 0 |
| T06 移动与行为写点 | S5 | 附录 A1 写点 grep：实体状态写命中仅出现在 AgentEnvironment.cpp（Food 在环境层内部构造/消耗，环境自有状态）与 Simulation.cpp:47-50（组合根出生初始化）；运行期核/球状态写全部位于 src/behavior/AgentBehavior.cpp；`build\agentsim.exe --no-render --ticks 2000 --seed 42 --audit` → audit=ok [exit 0] | 执行附录 A1 grep 并核对命中范围；再跑上述审计命令断言 audit=ok |
| T07 能量账本零和 | S3 | tests/unit/test_ledger.cpp：ledger_inject_and_dissipate_accounting、ledger_transfer_zero_sum、ledger_audit_pass_within_tolerance、ledger_audit_failure_is_detected、ledger_reset_clears_state；`build\agentsim.exe --no-render --ticks 2000 --seed 42 --audit` → `done tick=2000 nuclei=12 food=47 total_energy=18129.92000000 audit=ok policy=rule` [exit 0]；`--ticks 20000` → `done tick=20000 nuclei=12 food=34 total_energy=169041.92000011 audit=ok policy=rule` [exit 0]（含 D1 修复：食物池 kFoodPoolId=0 一级注入、进食纯 transfer、限额支出、死亡仅正余额 dissipate，见 ADR-002） | 执行 `build\unit_tests.exe`（5 个 ledger 用例）+ 2000/20000 tick 审计跑，断言 audit=ok |
| T09 被动呼吸耗能 | S3 | 呼吸走 `ledger.dissipate("respiration", spend)`（src/behavior/AgentBehavior.cpp:67-75；respirationBase=0.008 + respirationK·speed²，限额支出 spend=min(需求,余额)，见 ADR-002 决定③）；`build\agentsim.exe --no-render --ticks 2000 --seed 42 --audit` → audit=ok [exit 0]。注：温度项待 T17 地形落地后补，不阻塞 S3 验收 | 执行上述审计跑，断言 audit=ok；呼吸账路径按 src/behavior/AgentBehavior.cpp:67-75 复核 |
| T12 CLI/统计输出与双跑验收 | S2, S3 | CLI 旗标 --ticks/--seed/--audit/--agent-mode/--stats/--no-render/--policy 等定义于 src/app/Cli.cpp；双跑：`build\agentsim.exe --no-render --ticks 1000 --seed 42 --audit` 跑两次重定向 stdout → 逐字节一致（determinism=True，末行 `done tick=1000 nuclei=12 food=45 total_energy=9745.92000000 audit=ok policy=rule`）；`tools\check.ps1` → [PASS] double-run-determinism | 执行附录 A3 双跑 + `fc.exe /b run1.txt run2.txt` 断言无差异（或看 check.ps1 的 double-run-determinism 项） |
| T08 行为三件套（产球/回收/繁殖） | S10, S5 | tests/unit/test_behavior.cpp（11 用例）关键项：reproduce_child_energy_within_parent_spend、reproduce_refused_when_energy_or_population_limits、deploy_ball_debits_energy_and_creates_ball、recycle_ball_returns_energy_without_total_gain、recycle_empty_balls_is_refused、apply_respects_energy_conservation_with_balls；账目 reason：deploy / recycle-loss / reproduce；子核初能 = 0.5·cost ≤ cost（S10）；`build\unit_tests.exe` → `30 tests, 3275 checks, 0 failures` [exit 0] | 执行 `build\unit_tests.exe`，断言 0 failures 且上列用例全部在列并通过 |
| T10 双重干扰挂速度 | S6 | test_behavior.cpp：write_noise_strength_increases_with_speed（同指令同种子族、速度档 1 vs 40 各 64 trial，高速档 vel.x 散布严格更宽）、read_noise_amp_monotonic_and_zero_at_rest（s[14] = amp 元信息，speed=0 恰为 0，speed 0→40 共 21 档 ≥20 次严格抬升）；读侧 amp = kReadNoiseK·speed/(1+speed) 叠加 s[0..9] 语义维（R1-G2，见 T24） | 执行 `build\unit_tests.exe`，断言上述两用例在列并通过 |
| T11 球远失控衰减 | S7 | test_behavior.cpp：ball_gain_zero_distance_is_one、ball_gain_half_at_reference_radius、ball_gain_decays_far_and_monotonic（ballControlGain = 1/(1+(d/r)²)，d 0→2000 步进 50 单调不增且 ≥10 对严格递减）；R1-G3 落地后 deployBall 可产球，失控分支可达 | 执行 `build\unit_tests.exe`，断言 3 个 ball_gain 用例在列并通过 |
| T13 全量门禁 | S4, S5, S8, S11 | `tools\check.ps1` → 六项全 PASS：guard: decision includes（S4，含 perception→decision 逆向断言）、guard: no std rng、guard: behavior-is-only-writer（S5 写点 grep）、build、unit_tests、double-run determinism → check PASSED [exit 0]；S8 的可插拔断言按附录 A2 改判版人工执行复核（RulePolicy 与 PerceptronPolicy 并存、均不 include 环境层、decide 无 RNG、默认 rule） | 执行 `tools\check.ps1` 断言六项 PASS 且 exit 0；执行附录 A2 改判版四条 grep 核对预期 |
| T18 R1-F1 CLI 数值解析加固 | S2 支撑（CLI 契约） | src/app/Cli.cpp parseU64/parseDouble 校验 errno==ERANGE 与 std::isfinite、显式拒绝前导负号；`build\agentsim.exe --no-render --ticks -1 --seed 42` → exit 2；`--width nan` → exit 2；`--food 99999999999999999999` → exit 2 | 逐一执行三条非法参数命令，断言均 exit 2 且 stderr 给出明确报错 |
| T19 R1-F2 CLI 选项接线 | S2/S3 支撑（CLI 契约）；非 S9 | Config.h 新增 PolicyConfig.name / AgentConfig.agentMode / ColonyConfig.nuclei、food；Cli.cpp 写入、Simulation.cpp 读取；`build\agentsim.exe --no-render --ticks 200 --nuclei 3 --audit` → `done tick=200 nuclei=3 food=96 total_energy=4167.20000000 audit=ok policy=rule` [exit 0]；`--policy stub` 与 `--policy rule` 输出不同；`--agent-mode 0` → exit 2（orbital paradigm not implemented yet）。⚠ mode0 轨道范式未实现，S9 完整复核仍挂 T14 | 执行上述三条命令，断言 nuclei=3、两 policy 输出有差异、--agent-mode 0 退出码 2 |
| T20 R1-F3 食物生成钳制 | S3 支撑（食物上限契约） | src/environment/AgentEnvironment.cpp spawnInitialFood 与 regrowFood 同受 config.food.max 钳制；`build\agentsim.exe --no-render --ticks 1 --seed 42 --food 8192 --audit` → food ≤ 4096 [exit 0]（R1 审查时曾为 8168） | 执行上述命令断言输出 food 值不超过 4096 且 exit 0 |
| T21 R1-F4 --stats 失败显式化 | S2 支撑（CLI 契约） | --stats 目标文件不可打开（目录不存在/无权限）时 stderr 报 cannot open stats file 且 exit 2 | 在无 build\ 子目录的 cwd 运行 `agentsim.exe --no-render --ticks 5 --stats build\stats.csv`，断言 exit 2 与告警输出 |
| T22 R1-stateHash 补齐字段 | S2/S3 支撑（状态哈希覆盖完整性） | src/sim/Simulation.cpp stateHash 现覆盖 n.speed、n.alive、b.lastOmega（原缺失）；同哈希不同未来状态的缺口消除 | 复核 stateHash 字段清单含 speed/alive/lastOmega；`build\agentsim.exe --no-render --ticks 2000 --seed 42 --audit` → audit=ok [exit 0] |
| T23 R1-G1 感知/决策依赖解向 | S4, G2 | 新建 src/core/SensorTypes.h（SensorVector/ActionVector 中立定义）；decision/IDecisionPolicy.h 反向 include；src/perception 零 include decision/；`tools\check.ps1` → [PASS] guard: decision includes（含新增 perception→decision 逆向断言）[exit 0] | 对 src\perception 全部 .h/.cpp 用 Select-String 'decision/' 断言 0 命中；check.ps1 六项 PASS |
| T24 R1-G2 读噪声进语义维 | S6 | src/perception/AgentSensors.cpp：amp = kReadNoiseK·speed/(1+speed)，噪声 (2u-1)·amp 叠加 s[0..9] 方向/接近度语义维；s[14] 记 amp 元信息；speed=0 → amp=0；test_behavior.cpp read_noise_amp_monotonic_and_zero_at_rest 通过 | 执行 `build\unit_tests.exe` 断言该用例通过；查看 AgentSensors.cpp 复核 amp 公式与叠加维 |
| T25 R1-G3 行为三件套补齐 | S10, S5 | 即 T08 落地（证据见上行 INDEX#T08）：deploy / recycle / reproduce 路径与账目 reason（deploy / recycle-loss / reproduce），子核初能 = 0.5·cost ≤ cost（S10） | 执行 `build\unit_tests.exe`，断言 0 failures |
| T27 P0 修复：reproduce 账目零和化 | S3 | tests/unit/test_behavior.cpp：reproduce_keeps_ledger_audit_identity（每次繁殖校验恒等式）；账目路径 src/behavior/AgentBehavior.cpp:121-123 仅 `ledger.dissipate("reproduce-loss", kReproCost - child.energy)`，无 injectInitial、无 "reproduce" 耗散（R2 P0：原三记录恒等式偏差 = k(f−1)）；`build\agentsim.exe --no-render --ticks 20000 --seed 42 --audit` → `done tick=20000 nuclei=19 food=19 total_energy=857.96952048 audit=ok policy=rule` [exit 0]（修复前 nuclei 恒 12、繁殖从未发生） | 执行 `build\unit_tests.exe` 断言该用例通过；再跑 20000 tick 审计命令断言 audit=ok 且 nuclei>12 |
| T28 繁殖接线与种群稳定 | S10, S3 | src/sim/Simulation.cpp:20-22（kReproEnergyGate=40 / kReproCap=24 / kReproChanceP=0.02）与 :94 调 AgentBehavior::reproduce；test_behavior.cpp：simulation_reproduction_grows_population_over_ticks、reproduce_child_energy_within_parent_spend（子核初能 = 0.5·kReproCost ≤ kReproCost，S10）；20000 tick seed 42 → nuclei=19（接线前恒 12），nuclei 16~24 稳定振荡 | 执行 `build\unit_tests.exe` 断言两用例通过；20000 tick 审计跑断言 nuclei ∈ [13,24] 且 audit=ok |
| T29 球注能与失控分支可达 | S7 | src/behavior/AgentBehavior.cpp:16（kBallSeedEnergy=10）、:67（deploy 时 `ledger.transfer(n.id, b.id, kBallSeedEnergy)` 零和注能）、:207-210（b.radius = 10 + 0.5·ball.energy 随球能单调增长）；test_behavior.cpp：deploy_seed_energy_zero_sum_via_ledger、ball_radius_grows_with_energy_and_gain_drops_below_0_2（球充能后 ballControlGain 可降至 <0.2 → S7 失控分支自此可达） | 执行 `build\unit_tests.exe` 断言两用例在列并通过 |
| T30 温度场实现 | S12 支撑 | src/environment/TemperatureField.h/.cpp：场 = 基线温度 + 高斯源叠加，sample/distortion 走 Torus 最小环距（跨边界连续），输出 ∈ [0,1]；tests/unit/test_temperature.cpp 7 用例：temp_empty_field_equals_baseline、temp_single_source_peak_at_center、temp_decay_monotonic_with_distance、temp_continuous_across_boundary、temp_deterministic_same_seed、temp_sample_and_distortion_in_unit_range、temp_distortion_zero_at_preferred | 执行 `build\unit_tests.exe` 断言 7 个 temp_ 用例全部在列并通过 |
| T31 温度失真作用于感知 | S12 | src/core/SensorTypes.h:15（kDim 16→18：s[16]=失真度、s[17]=归一化温度）；src/perception/AgentSensors.cpp:23（kTempGain=3.0）与 :87（读噪声 amp ×= 1 + kTempGain·d）；src/decision/RulePolicy.cpp:58-64（信任因子 1−0.5·s[16]，失真大时降低 proximity 权重）；tests/unit/test_sensors.cpp 6 用例：sensors_kdim_is_18_and_temperature_dims_match_field、sensors_temperature_distortion_amplifies_read_noise、sensors_no_noise_at_rest_without_distortion、sensors_output_finite_all_dims、rulepolicy_temperature_trust_reduces_proximity_weight、sensors_default_temp_arg_backward_compatible | 执行 `build\unit_tests.exe` 断言 6 个用例在列并通过；按上述源码行号复核接线 |
| T32 C 系列 P2（boost 消费/方向重归一化/help/state_hash） | S5, S2 支撑 | C4：src/behavior/AgentBehavior.cpp:145-150 消费 action.boost（effectiveMax = maxSpeed·(1+0.5·boost)，NaN/越界钳 [0,1]）；C5：src/perception/AgentSensors.cpp:112-125 s[0..5] 加噪后重归一化（零向量保持零）；C6：`build\agentsim.exe --help` 含 `--perceptron-seed S` 与 `--perceptron-scale F`；C7：--stats 表头 6 列 `tick,nuclei,food,total_energy,audit_ok,state_hash`（src/app/main.cpp:45），双跑比较 state_hash 列即可验收确定性 | 执行 --help 核对两旗标；`build\agentsim.exe --no-render --ticks 2 --stats` 断言表头列数 = 6；`build\unit_tests.exe` 断言 apply_respects_energy_conservation_with_balls 通过 |

## 附录：长命令原文（供复制执行）

### A1 S5 写点 grep（行为层唯一写点的证据）

```powershell
Get-ChildItem src -Recurse -Include *.cpp,*.h |
  Where-Object { $_.FullName -notmatch 'behavior' } |
  Select-String -Pattern '\.(pos|vel|energy|alive|speed|angle|radius|cap|balls)\s*=[^=]|\.balls\.(push_back|clear)'
```

预期（实测 2026-10-31）：命中仅出现在 AgentEnvironment.cpp（Food 构造/消耗，环境自有状态）与
Simulation.cpp:47-50（组合根出生初始化）；运行期核/球状态写全部位于 src/behavior/AgentBehavior.cpp。

### A2 S8 决策层可插拔断言（ADR-003 改判版 · 2026-10-31）

> 原断言「仅注册 RulePolicy / perceptron 零命中」已被 `docs/DECISIONS/ADR-003-感知机提前引入.md` 作废：
> PerceptronPolicy 已作为 IDecisionPolicy 第二实现接线（`--policy perceptron`）。改判后的断言：
> **决策层同时存在 RulePolicy 与 PerceptronPolicy；两者均不 include 环境层、decide 无 RNG；
> 默认策略为 rule，perceptron 需显式 `--policy perceptron` 启用**（src/app/Cli.cpp:102-114）。

```powershell
# ① 实现注册情况：RulePolicy.h 两处（RulePolicy 与 StubPolicy）+ PerceptronPolicy.h 一处，共 3 处命中
Select-String -Path src\decision\*.h -Pattern ': public IDecisionPolicy'
# 预期：PerceptronPolicy.h:13、RulePolicy.h:8、RulePolicy.h:20（实测 2026-10-31）

# ② 越界词汇：neural|nnet 在 src/ 下 0 命中（perceptron 已合法，不再列入禁词）
Get-ChildItem src -Recurse -Include *.h,*.cpp | Select-String -Pattern 'neural|nnet'
# 预期：0 命中（实测 2026-10-31：0）

# ③ 决策层不 include 环境层：0 命中（决策层只 include IDecisionPolicy.h / SensorTypes.h / PCG32.h / 标准库）
Get-ChildItem src\decision -Recurse -Include *.h,*.cpp | Select-String -Pattern '#include "(environment|sim|ledger)/"'
# 预期：0 命中（实测 2026-10-31：0）

# ④ decide 无运行期 RNG：决策层 .cpp 中 rand()/srand() 0 命中；
#    PerceptronPolicy.cpp:37 的 PCG32 仅在构造期用种子初始化权重，decide() 内无 RNG
Get-ChildItem src\decision -Recurse -Include *.cpp | Select-String -Pattern 'rand\(|srand\('
# 预期：0 命中（实测 2026-10-31：0）
```

> 同 seed 互异（S8 其余半条）由 tests/unit/test_perceptron.cpp 的 perceptron_differs_from_rule_policy 与
> 第三轮快照 #4 三策略 200 tick 统计互异共同佐证。

### A3 固定种子双跑逐字节比较（S2）

```powershell
build\agentsim.exe --no-render --ticks 1000 --seed 42 --audit > run1.txt
build\agentsim.exe --no-render --ticks 1000 --seed 42 --audit > run2.txt
fc.exe /b run1.txt run2.txt
# 预期：no differences encountered（实测 2026-10-31：逐字节一致，determinism=True）
```

## 关于 total_energy 指标的澄清（生态稳态 ≠ 濒死）

> **背景**：曾有评审把 20000 tick seed 42 下 total_energy 从 168822 降到 1029 误判为「生态濒死」。
> 实际这是**容纳量稳态**：繁殖把能量不断抽走并耗散，囤积多反而说明繁殖没在做。避免后来者再误判，特此澄清。

**结论：total_energy 不是生态健康指标。** 判断生态是否存活请看 nuclei 时序的变异系数（CV）与是否灭绝：

- 三个 seed 的末 10000 tick nuclei 变异系数分别为 **0.109 / 0.111 / 0.111**（稳态阈值 0.35）；
- nuclei 在 16~24 振荡、food 在 12~25 振荡，全程 audit=ok，无灭绝；
- seed 42 每 2000 tick 采样（`--stats --stats-interval 2000`，可复跑）：
  nuclei = 21,22,21,21,18,23,17,20,20,19（min 17 / max 23），全部 audit_ok=1；
- 健康方向的对照：E1 实验中最优感知机组（pseed=1, scale=0.5）total_energy=28450 且 nuclei=6，
  说明「高 total_energy + 低种群」恰是能量囤积态（吸收多、繁殖少），不是更健康。

复核命令：`build\agentsim.exe --no-render --ticks 20000 --seed 42 --audit --stats --stats-interval 2000` [exit 0]，
对 stats.csv 的 nuclei 列（去掉 tick≤10000 的行）算 CV，应 < 0.35。


## 第二轮门禁快照（R1 修复落盘后 · 2026-10-31 · 主代理实测）

> 本节为 R1 红队 12 条修复全部落盘后的门禁输出，是上表新条目（T08/T10/T11/T13、T18–T25）的统一证据基线；输出原样记录。

| # | 命令 | 实测输出 | exit |
| --- | --- | --- | --- |
| 1 | `tools\check.ps1` | [PASS] guard: decision includes、[PASS] guard: no std rng、[PASS] guard: behavior-is-only-writer、[PASS] build、[PASS] unit_tests、[PASS] double-run determinism（六项全 PASS）→ check PASSED | 0 |
| 2 | `build\unit_tests.exe` | 30 tests, 3275 checks, 0 failures | 0 |
| 3 | `build\agentsim.exe --no-render --ticks 2000 --seed 42 --audit` | done tick=2000 nuclei=12 food=37 total_energy=17910.08000000 audit=ok policy=rule | 0 |
| 4 | `build\agentsim.exe --no-render --ticks 200 --nuclei 3 --audit` | done tick=200 nuclei=3 food=96 total_energy=4167.20000000 audit=ok policy=rule | 0 |

---

## 第三轮门禁快照（R2 修复与温度场落盘后 · 2026-10-31 · 主代理实测，文档维护者同日独立复跑一致）

> 本节是 T27–T32 证据的统一基线：R2 红队 P0（reproduce 账目零和化）修复、繁殖接线（C2）、
> 球注能（C3）与 C4–C7、温度场 + 温度失真接线后的当前树门禁输出；输出原样记录。

| # | 命令 | 实测输出 | exit |
| --- | --- | --- | --- |
| 1 | `tools\check.ps1` | [PASS] guard: decision includes、[PASS] guard: no std rng、[PASS] guard: behavior-is-only-writer、[PASS] build、[PASS] unit_tests（57 tests, 4128 checks, 0 failures）、[PASS] double-run determinism（六项全 PASS）→ check PASSED | 0 |
| 2 | `build\unit_tests.exe` | 57 tests, 4128 checks, 0 failures | 0 |
| 3 | `build\agentsim.exe --no-render --ticks 20000 --seed 42 --audit` | done tick=20000 nuclei=19 food=19 total_energy=857.96952048 audit=ok policy=rule | 0 |
| 4 | `build\agentsim.exe --no-render --ticks 200 --seed 42 --policy rule --audit`<br>`build\agentsim.exe --no-render --ticks 200 --seed 42 --policy stub --audit`<br>`build\agentsim.exe --no-render --ticks 200 --seed 42 --policy perceptron --audit` | rule：done tick=200 nuclei=23 food=21 total_energy=1139.70024888 audit=ok policy=rule<br>stub：done tick=200 nuclei=24 food=401 total_energy=5187.87197000 audit=ok policy=stub<br>perceptron：done tick=200 nuclei=12 food=14 total_energy=757.41594585 audit=ok policy=perceptron（三策略统计互异，S8 同 seed 互异佐证） | 0 / 0 / 0 |

---

## T33–T36 证据（第四轮）

| 任务 | 对应成功标准 | 证据 | 验证方式 |
| --- | --- | --- | --- |
| T33 性能：消除死核累积与 O(n) gate 扫描 | S11 | `Simulation.cpp` 引入 `aliveCount_` 缓存替代每核 `nucleusCount()` 全表扫描；`nuclei_` 在 `deadCount > 2 * aliveCount_` 时 `remove_if` 压缩。实测耗时：修复前 20000=1919ms / 50000=8037ms / 100000=26709ms（2.5x tick → 4.2x 耗时，二次增长）；修复后 20000=1971ms / 50000=5005ms / 100000=10136ms（2x tick → 2x 耗时，已线性） | `foreach ($t in @(20000,50000,100000)) { $sw=[Diagnostics.Stopwatch]::StartNew(); & .\build\agentsim.exe --no-render --ticks $t --seed 42 --audit \| Out-Null; $sw.Stop(); "$t -> $($sw.ElapsedMilliseconds) ms" }`，断言 100000 耗时 < 15000ms 且随 tick 近似线性 |
| T34 死亡可观测 | S2 | `Simulation` 新增 `deathCount_`（`wasAlive && !n.alive` 时自增），`stats.csv` 由 6 列扩为 7 列，表头 `tick,nuclei,food,total_energy,audit_ok,deaths,state_hash` | `build\agentsim.exe --no-render --ticks 200 --seed 42 --audit --stats build\final.csv` → exit 0；`Get-Content build\final.csv \| Select-Object -First 2` 断言表头 7 列且 deaths 列存在 |
| T35 繁殖参数调优 | S10, S3 | `kReproEnergyGate` 40.0→60.0、`kReproChanceP` 0.02→0.002（`Simulation.cpp:20/22`）。实测 seed 42/7/123 在 20000 tick 下：`nuclei=20/21/21`、`total_energy=12349.71860489 / 17352.84300438 / 20381.00081633`、`audit=ok`；avg 核能由调参前 35.2 升至 617~970 | `foreach ($s in @(42,7,123)) { & .\build\agentsim.exe --no-render --ticks 20000 --seed $s --audit }`，断言三条 `audit=ok` 且 `total_energy/nuclei > 100` |
| T36 `--stats` 路径回归修复 | S11 | `Cli.cpp` 中 `--stats` 由纯开关改为可选路径：下一参数不以 `-` 开头则写入 `statsPath`，否则回落 `build/stats.csv`；help 改为 `--stats [path]` | `build\agentsim.exe --no-render --ticks 200 --stats build\t_a.csv` / `--stats` / `--stats-interval 50 --stats build\t_b.csv` 三种写法均 exit 0 且生成对应 CSV |

---

## 第四轮门禁快照（2026-10-31 08:00，主代理实测）

| # | 命令 | 输出摘要 | 退出码 |
| --- | --- | --- | --- |
| 1 | `tools\check.ps1` | 六项全 PASS（guard: decision includes、guard: no std rng、guard: behavior-is-only-writer、build、unit_tests、double-run determinism）→ check PASSED | 0 |
| 2 | `build\unit_tests.exe` | 57 tests, 4128 checks, 0 failures | 0 |
| 3 | `--no-render --ticks 20000 --seed 42 --audit` | done tick=20000 nuclei=20 food=9 total_energy=12349.71860489 audit=ok policy=rule | 0 |
| 4 | `--no-render --ticks 100000 --seed 42 --audit` | done tick=100000 nuclei=20 food=12 total_energy=56294.65727035 audit=ok policy=rule（10136 ms） | 0 |
| 5 | 三策略 `--ticks 200 --seed 42 --policy {rule,stub,perceptron}` | rule nuclei=16 energy=2501.16 / stub nuclei=18 food=415 energy=5269.59 / perceptron nuclei=11 energy=1492.28 | 0 |
| 6 | 同 seed 1000 tick 双跑 | determinism=True | 0 |

---


---

## T37、T40–T42 证据（第五轮 · 2026-10-31）

> 证据由文档维护者于 2026-10-31 独立复跑核实（命令输出见「第五轮门禁快照」）；主代理探针数据单独注明。

| 任务 | 对应成功标准 | 证据 | 验证方式 |
| --- | --- | --- | --- |
| T37 S7 失控分支可达（球充能 + handoffRadius 150→60） | S7 | 根因：gain=1/(1+(d/h)²)<0.2 ⇔ d>2h，球半径 r=10+0.5·energy（src/behavior/AgentBehavior.cpp:207-210），原 h=150 需球能>580（实测峰值 280，不可达）；修复：AgentBehavior.h:59 `handoffRadius = 60.0`（球能>220 即可达，含推导注释）+ AgentBehavior.cpp:203-214 球充能（kBallChargeFraction=0.6、kBallEnergyCap=800，核进食后把 60% 所得经 `ledger.transfer` 转给能量最少的球，零和）；test_behavior.cpp：deploy_seed_energy_zero_sum_via_ledger、ball_radius_grows_with_energy_and_gain_drops_below_0_2；主代理探针（已删除）实测 charges=1512、chargedTotal=9072、maxBallE=280，证明充能路径生效且球能峰值 280>220 阈值 | 读 AgentBehavior.h:55-60 与 AgentBehavior.cpp:203-214 复核数值与零和路径；执行 `build\unit_tests.exe` 断言 75 tests 0 failures 且两用例在列 |
| T41 ThermalPolicy（第四个 IDecisionPolicy，趋温） | S8 | src/decision/ThermalPolicy.h/.cpp：score = wProximity·s[6+i] + wThermal·(1−\|s[17]−pref\|/0.5) + wCaution·(1−s[16])，取最高分候选方向；s[16]>0.5 时 boost=0.3 降速；决策期零 RNG、不 include 环境层；CLI：--policy thermal / --thermal-pref / --thermal-w-thermal / --thermal-w-caution（src/app/Cli.cpp:105-145）；tests/unit/test_thermal.cpp 8 用例：thermal_no_target_returns_zero、thermal_prefers_preferred_temperature、thermal_caution_penalizes_distortion、thermal_boost_gate_on_distortion、thermal_output_direction_unit_or_zero、thermal_all_nan_input_is_safe、thermal_deterministic_repeat_calls、thermal_differs_from_rule_policy；附录 A2 扩展 grep：`: public IDecisionPolicy` 现 4 处命中（ThermalPolicy.h:13 新增），src/decision 对 environment/sim/ledger 的 include 与 rand(/srand( 均 0 命中（含 ThermalPolicy）；200 tick seed42 `--policy thermal` → nuclei=19 food=10 total_energy=2146.17486987，与三旧策略互异（见第五轮快照 #4） | 执行 `build\unit_tests.exe` 断言 8 个 thermal_ 用例在列；重跑附录 A2 ①③④ grep 断言 4/0/0 命中；执行 `build\agentsim.exe --no-render --ticks 200 --seed 42 --policy thermal --audit` 断言 exit 0 且与 rule/stub/perceptron 输出互异 |
| T42 Terrain 地形（障碍/水域/温度区；未接行为层） | S14（部分） | src/environment/Terrain.h/.cpp：只读查询 blocked / inWater / moveCostMultiplier / temperatureOffset / resolveMove（障碍推出沿 from→to 二分回退 8 次），距离全走 Torus 最小环距；src/core/Config.h:74-86 TerrainConfig（obstacleCount/waterCount/thermalZoneCount **默认全 0**，默认不生成任何地形，回归安全）；CLI：--terrain-obstacles / --terrain-waters / --terrain-thermal（src/app/Cli.cpp:149-160）；tests/unit/test_terrain.cpp 10 用例（terrain_empty_terrain_inert_everywhere、terrain_blocks_across_torus_boundary、terrain_resolve_move_pushes_out_of_obstacle、terrain_default_simconfig_generates_nothing 等）；实测 `build\agentsim.exe --no-render --ticks 200 --seed 42 --terrain-obstacles 3 --terrain-waters 2 --terrain-thermal 1 --audit` → `done tick=200 nuclei=15 food=24 total_energy=2419.77449704 audit=ok policy=rule` [exit 0]。⚠ resolveMove hook 未接行为层，S14 不计完成（见 TODO#T43） | 执行 `build\unit_tests.exe` 断言 10 个 terrain_ 用例在列；执行上述地形烟囱命令断言 audit=ok 且 exit 0；复核 Config.h 三 count 默认值 = 0 |
| T40 reason 枚举注释修正（部分） | —（文档一致性） | src/behavior/AgentBehavior.h:4 已改为实际枚举 locomotion / respiration / deploy / recycle / recycle-loss / reproduce-loss / ballDrift / death，与 AgentBehavior.cpp 中 9 处 `ledger.dissipate(...)` 调用（56/92/94/124/176/187/253/278/285 行）一一对应；⚠ src/ledger/AgentLedger.h:18 仍为旧枚举（move/spawnBall/reproduce/recycleBall），残留登记为 TODO#T44 | 打开两处头文件核对注释；对 AgentBehavior.cpp grep `dissipate\(` 断言 reason 实参与新枚举一致 |

---

## 基线变更说明（第五轮 · 球充能启用导致 total_energy 基线变更）

> **T37 球充能启用后，20000 tick seed42（rule）的 total_energy 基线从 12349.71860489（第四轮快照 #3）降至 1740.43027179（第五轮快照 #5，约 −86%）。** 主代理已亲手验证：全程 audit=ok、账本零和恒等式无偏差——这是能量在核/球间的再分布与耗散路径变化（充能经 `ledger.transfer` 把能量从核推给球，不改变注入/耗散总额；totalEnergy 统计口径为核+球+食物池之和，src/sim/Simulation.cpp:151-159），**不是账目丢失或泄漏**。
>
> 新基线下的曲线平稳振荡，无崩溃、无灭绝（主代理 `--stats --stats-interval 2000` 采样实测）：
> - total_energy 每 2000 tick：1662 / 1641 / 1875 / 1966 / 2133 / 1614 / 1679 / 2000 / 1924 / 1740；
> - nuclei 17~20、food 11~22 振荡，累计 deaths=440，全程 audit_ok=1；
> - 三个 seed 的 20000 tick 末态全部 audit=ok（见第五轮快照 #5/#6）。
>
> **影响范围**：本文件此前所有 total_energy 数值（含第三/四轮门禁快照、T33–T36 证据、「关于 total_energy 指标的澄清」一节及 T16/T27/T35 行内数值）均为球充能启用前的**旧基线，仅适用于当时的树**，不可与第五轮及之后的数据直接对比；后续一切对比以第五轮快照为新基线。

---

## 第五轮门禁快照（T37/T40–T42 落盘后 · 2026-10-31 · 主代理实测，文档维护者同日独立复跑一致）

> 本节是 T37（球充能 + handoffRadius）、T41（ThermalPolicy）、T42（Terrain）落地后的统一基线；六条命令均由文档维护者于同日逐条独立复跑，输出与主代理一致。

| # | 命令 | 实测输出 | exit |
| --- | --- | --- | --- |
| 1 | `tools\check.ps1` | 六项全 PASS（guard: decision includes、guard: no std rng、guard: behavior-is-only-writer、build、unit_tests（75 tests, 4516 checks, 0 failures）、double-run determinism）→ check PASSED | 0 |
| 2 | `build\unit_tests.exe` | 75 tests, 4516 checks, 0 failures（57 + thermal 8 + terrain 10；checks 4128→4516） | 0 |
| 3 | 1000 tick 同 seed 双跑（附录 A3） | 逐字节一致，determinism=True | 0 |
| 4 | 四策略 `--no-render --ticks 200 --seed 42 --policy {rule\|stub\|perceptron\|thermal} --audit` | rule：nuclei=14 food=26 total_energy=2378.67034546 audit=ok<br>stub：nuclei=18 food=415 total_energy=5269.59199130 audit=ok<br>perceptron：nuclei=12 food=19 total_energy=999.72777224 audit=ok<br>thermal：nuclei=19 food=10 total_energy=2146.17486987 audit=ok（四策略统计互异） | 0 / 0 / 0 / 0 |
| 5 | `--no-render --ticks 20000 --seed 42 --audit` | done tick=20000 nuclei=18 food=15 total_energy=1740.43027179 audit=ok policy=rule（**新基线**，见「基线变更说明」） | 0 |
| 6 | `--no-render --ticks 20000 --seed {7\|123} --audit` | seed 7：nuclei=17 food=12 total_energy=1633.88919315 audit=ok<br>seed 123：nuclei=16 food=12 total_energy=1799.60976441 audit=ok | 0 / 0 |

## 夜班最终总结

见 [NIGHT-SUMMARY.md](NIGHT-SUMMARY.md)。

---

## R4 红队修复（2026-10-31 10:00，主代理亲自主持）

R4 报 2 条 P1 + 5 条 P2。**主代理直接修复了 2 条 P1**（派出的代理两次卡死，均由主代理接手）。

### P1-1 `--thermal-pref` 结构性失效 → 已修复

**故障**：`--thermal-pref 0.5` 与 `--thermal-pref 0.05` 的 200 tick 输出**逐字节相同**。

**根因**：`ThermalPolicy::candidateScore` 里的 `thermalSuitability(s)` 与 `cautionFactor(s)` 都只读 s[16]/s[17]，
这两维是**核自身位置**的全局量 —— 对所有候选是同一个常数，**不参与 argmax 相对比较**，
因此 `preferredTemp` 无论取什么值都不可能改变输出。

**修复**（`ThermalPolicy.cpp::candidateScore`）：把温度项改成**候选级**——

```
stay_i     = dot(候选方向 i, 当前速度方向)   ∈ [-1,1]   // s[10], s[11]
thermal_i  = suitability * (0.5 + 0.5 * stay_i)
score_i    = wProximity * prox_i * (0.5 + 0.5*caution) + wThermal * thermal_i + wCaution * caution
```

物理含义：适宜度高时更愿意**保持当前方向**（途经温度变化平滑）；适宜度低时该项整体缩小，
候选间温差被压平，行为退回就近取食。

**验证**：`--thermal-pref 0.5` → `nuclei=17 food=24 energy=2416.05`；
`--thermal-pref 0.05` → `nuclei=12 food=20 energy=2698.64`。**输出已不同 ✓**

### P1-2 `--thermal-w-thermal` / `--thermal-w-caution` 是死参数 → 已修复

**故障**：Cli.cpp 解析后写入 `CliOptions.thermalWThermal/WCaution`，但**全仓库无任何读者**。

**修复**：
- `Config.h` 的 `PolicyConfig` 新增 `thermalWThermal = 1.2` / `thermalWCaution = 0.8`；
- `Cli.cpp` 直接写入 `config.policy`（不再写入无人读的 CliOptions mirror 字段）；
- `Simulation.cpp` 装配 ThermalPolicy 时写入 `tp.wThermal/tp.wCaution`。

**验证**：`--thermal-w-thermal 9 --thermal-w-caution 9` → `nuclei=13 food=25 energy=2688.07`，
与默认 `nuclei=17 food=24 energy=2416.05` **不同 ✓**

### 回归安全

`--policy rule`（默认路径）在修复后跑 20000 tick 三 seed，数值与修复前**逐位一致**：

```
seed 42  : nuclei=18 food=15 total_energy=1740.43027179
seed 7   : nuclei=17 food=12 total_energy=1633.88919315
seed 123 : nuclei=16 food=12 total_energy=1799.60976441
```

### 未修的 P2（已登记 TODO T43–T45）

- Terrain 查询零生产消费者（障碍不挡人、水域不耗能、温度区不偏置）—— 接入行为层属下一阶段
- ballDrift 耗散系数 0.0001 形同虚设（20k 累计仅 0.107）
- handoffRadius 注释数字过期（280 → 实测 280~305）
- 繁殖 roll 短路（依赖能量门的条件性 RNG 消费）
- Terrain 二参构造缺世界尺寸

---

## 第六轮门禁快照（2026-10-31 10:00）

| # | 命令 | 输出摘要 | 退出码 |
| --- | --- | --- | --- |
| 1 | `tools\check.ps1` | 六项全 PASS → check PASSED | 0 |
| 2 | `build\unit_tests.exe` | 75 tests, 4516 checks, 0 failures | 0 |
| 3 | 四策略 `--ticks 200 --seed 42` | rule 2378.67 / stub 5269.59 / perceptron 999.73 / thermal 2416.05（互异） | 0 |
| 4 | `--ticks 20000 --seed {42,7,123}` | 1740.43 / 1633.89 / 1799.61，全部 audit=ok | 0 |
| 5 | 同 seed 1000 tick 双跑 | determinism=True | 0 |
| 6 | `--thermal-pref 0.5` vs `0.05` | 输出不同（2416.05 vs 2698.64） | 0 |

---

## R6 红队两个 P0 的修复（2026-10-31 12:10，主代理亲自主持）

R6 报 2 条 P0 + 4 条 P1 + 6 条 P2。两个 P0 都已修复并由主代理验证。

### P0-1 `ThermalPolicy` 的 trustFood 变负导致 argmax 反转 → 已修复

**故障**：上一版信任调制 `trustFood = 1 - 0.5*wCaution*(1-caution)` 在 `wCaution*(1-caution) > 2` 时变负
（例如 wCaution=3, caution=0 → 1-1.5 = **-0.5**）。proximity 项变负后，
**argmax 会偏好最远/最差的候选** —— 语义完全反转。红队探针 `probe6_wc` 实测 wc≥3 时输出从 (+1,0) 翻到 (-1,0)。

**修复**：改用恒正的软调制

```
doubt     = 0.5 * wCaution * (1 - caution)   // >= 0
trustFood = 1 / (1 + doubt)                  // ∈ (0,1]  恒正
trustStay = 1 + doubt                        // >= 1
score     = wProximity*proximity*trustFood + wThermal*thermal*trustStay
```

**验证**（五个 wCaution 值，200 tick seed42，互异且不崩）：

| wCaution | nuclei | food | total_energy |
| --- | --- | --- | --- |
| 0 | 14 | 25 | 1960.71206383 |
| 0.8 | 14 | 25 | 2018.82291236 |
| 3 | 14 | 26 | 2006.59801467 |
| 10 | 15 | 24 | 1768.91899973 |
| 100 | 13 | 26 | 1995.54748486 |

### P0-2 核出生在障碍内导致全开地形灭绝 → 已修复

**故障**：`spawnNuclei` 只做核间退避、**不查 `terrain_.blocked`**。seed7 实测 2/12 个核出生在障碍内。
后果：`blocked(from)` 使 resolveMove 恒返回 from → 位置永久冻结、每 tick 照付呼吸税、
吃不到食物、无法繁殖 → 饿死。最终 `--ticks 20000 --seed 123 --terrain-obstacles 5 --terrain-waters 3 --terrain-thermal 2`
→ **nuclei=0, food=4096**（食物堆到上限无人吃）。

**修复**（三件）：
1. `spawnNuclei` 撒点加 `!terrain_.blocked(p)`；200 次尝试失败后放宽核间退避，但**仍不得在障碍内**。
2. `AgentEnvironment` 新增 `BlockedFn` 函数指针谓词（不依赖 Terrain，保持分层），
   `spawnInitialFood` / `regrowFood` 拒绝落在障碍内的采样点；
   `Simulation` 构造时以 static 包装绑定 `terrain_.blocked`。
3. `resolveMove` 在线段 1/4、1/2、3/4 处采样（用 `from + t*torusDelta` 保持 Torus 语义），
   命中障碍即以其为二分上界，**修复高速隧穿**（原实现只查端点，boost 满速可直穿 r=24 障碍圆心）。

**验证**：

```
全开地形 20000 tick：
  seed 42 : nuclei=9  food=31 total_energy=1311.71896830 audit=ok
  seed 7  : nuclei=8  food=31 total_energy=1224.29907579 audit=ok
  seed 123: nuclei=13 food=24 total_energy=1428.90501064 audit=ok   ← 修复前为 0

无地形 20000 tick（与修复前逐位一致）：
  seed 42 : nuclei=16 food=22 total_energy=1480.96247761
  seed 7  : nuclei=12 food=27 total_energy=1232.98942785
  seed 123: nuclei=9  food=25 total_energy=1336.08215699
```

### 用户提醒的验证频率问题

用户指出了「提醒堆积」——20 分钟一次的唤醒频率远高于实际推进速度。
已将 cron 从 `*/20 * * * *` 改为 `0 */2 * * *`（每 2 小时）。

---

## 第七轮门禁快照（2026-10-31 12:07）

| # | 命令 | 输出摘要 | 退出码 |
| --- | --- | --- | --- |
| 1 | `tools\check.ps1` | check PASSED | 0 |
| 2 | `build\unit_tests.exe` | **92 tests, 6917 checks, 0 failures** | 0 |
| 3 | 无地形 `--ticks 20000 --seed {42,7,123}` | 16 / 12 / 9 核，全部 audit=ok，与修复前逐位一致 | 0 |
| 4 | 全开地形 `--ticks 20000 --seed {42,7,123}` | 9 / 8 / 13 核，全部 audit=ok | 0 |
| 5 | 同 seed 1000 tick 双跑 | determinism=True | 0 |

## 第四轮验收快照（2026-10-31 深夜，主代理实测；S16–S25 时代）

> 基线演进：92/6917 → 155/10178 → 161/7792 → 166/7812 → 196/11695 → **208/19149**（当前）。
> checks 数随数据规模浮动（如 `cyano_energy_cap_respected` 的断言数 = 存活节点数 × 2），
> **不作为跨版本覆盖度比较指标**；以 tests 只增不减 + 0 failures 为准。

| 任务 | 对应成功标准 | 证据 | 验证方式 |
| --- | --- | --- | --- |
| T01b S16 每候选独立噪声 | S6, S16 | `tests/unit/test_sensors.cpp`: sense_noise_is_per_candidate / sense_zero_speed_no_rng_consumption / sense_deterministic_same_seed；**反证实验**：换回旧实现该测试 FAIL（红队 R7 与主代理各自独立复现） | `build\unit_tests.exe`；反证见 docs/WORKFLOW.md §4.4 |
| T03b S17 侦察球感知 + 接线 | S17 | effectiveSenseRadius(0)=¼盲区、(3)=×2、(1000)=4×上限；**接线证据**：Simulation::step 数 `type==Scout` 传入 sense；**生态证据**：trace 球 type 集合含 1（Scout），修复前只有 0 | `build\unit_tests.exe`；`agentsim.exe --trace` + node 解析 types |
| T03c S18 球作仓库 | S18 | colony_cap_* 7 条；**生产证据**：3000 tick trace 中核 cap 取值 {200,260,320} = nativeCap + Resource 球数×60 | `build\unit_tests.exe` + trace 扫描 |
| T09 S19/S20 球物理化 | S19, S20 | detach_preserves_energy / detach_zero_ledger_impact / adopt_zero_sum 等 8 条；**生产证据**：自由球帧 1223（修复前 0）、最大真实环距 134.9 > 阈值 120 | `build\unit_tests.exe` + trace 扫描 |
| T10 S21 核施力 + 接线 | S21 | force_* 8 条；**接线证据**：pushFreeBall 在 Simulation 自由球循环调用（adopt 失败分支） | `build\unit_tests.exe` + grep 调用点 |
| T02b S22 蓝细菌接线 | S22, S23 | cyano_* 18 条 + wiring_cyano_* 6 条；**生产证据**：20k×6 seed 全 audit=ok（42/7/123/5/11/999）；尸体压缩后 `nodes().size() <= maxNodes(64)` | `build\unit_tests.exe`；`agentsim.exe --ticks 20000 --audit` |
| T22 --no-regrow / S23 验收 | S23 | cli_no_regrow_* 5 条；**S23 实测**：regrow 默认 false（蓝细菌取代食物，ADR-005），20k×3 seed food=0 且 nuclei=24 满员、audit=ok；`--regrow` 可回退 | `agentsim.exe --ticks 20000 --audit`（默认即无人工撒食） |
| T24 BanditPolicy | S8 | bandit_* 29 条（Hedge/EXP3-lite 对数空间加权、回报=接近度增益+速度-能量权衡、1e-6 地板防塌零）；`--policy bandit` 20k audit=ok | `build\unit_tests.exe`；`agentsim.exe --policy bandit --ticks 20000 --audit` |
| T26 RiskAversePolicy | S8 | riskaverse_* 9 条（EROI EMA + 温度折扣 + 三模式滞回）；`--policy riskaverse` 20k audit=ok | `build\unit_tests.exe`；`agentsim.exe --policy riskaverse --ticks 20000 --audit` |
| T25 CyanoNode P2 | S22 | cyano_ball_spawn_near_parent（产球距产者 10~30）/ cyano_link_interval_1_same_as_always / cyano_link_interval_2_differs | `build\unit_tests.exe` |
| T23 前端同步 | S24, S25 | 真实 600 帧蓝细菌生态轨迹打包（4.2MB 单文件）；Scout 紫 / 自由球虚线外圈 / 环面接缝箭头；node --check 5 js 全过 | `node frontend/tools/bundle.mjs`；headless Edge 截图（主代理实核） |
| 门禁强化 | §6 | guard:decision-has-no-rng（PerceptronPolicy 构造期窄豁免）；live-param 4 探针（剥 policy= 后比较）；写点守卫覆盖 `arr[i].energy =` 形态 | `tools\check.ps1`（8 守卫 + 4 探针全 PASS） |
| 红队 R7/R8 修复 | S3, §6 | R7：5×P1（trace 精度/audit 中止/错误码/球type/内存上限）+ 2×P2；R8：2×P0（产球幻影注入 417k、credited/gain 分叉）+ 饿死蒸发 + 尸体占坑 + 能量坟场 + stateHash；**resid 对销通道删除** | `agentsim.exe --ticks 20000 --audit` × 6 seed 全 ok；R8 报告与主代理独立复现记录见 LOG.md |
