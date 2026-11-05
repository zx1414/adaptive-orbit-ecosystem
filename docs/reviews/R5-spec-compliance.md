# R5 规格符合性审计（对照 SPEC-SOURCE.md 逐条核对）

- 审计日期：2026-10-31
- 审计性质：只读审计。本次**仅新建本文件**，未修改 src/、tests/、tools/ 及其他 docs/ 下任何文件。
- 唯一判据：`docs/SPEC-SOURCE.md`（用户原始需求书）；参照 `docs/GOAL.md`（含 ADR-003 对感知机的修订）。
- 方法：逐条读源码与测试取证（不采信文档自述），grep 验证接线，并在仓库既有 build 产物上实测关键命令（双跑一致性、策略互异、mode0 拒绝、2 万 tick 审计、全量单测）。
- **总裁定：完全符合 7 条 / 部分符合 5 条 / 未实现 3 条 / 与规格冲突 0 条。**

## 一、逐条核对总表

| # | 规格条目 | 判定 | 证据（文件:行 或 命令） | 缺口说明 |
| --- | --- | --- | --- | --- |
| 1 | 平行范式 `agent_mode=0`/`1` 共存（mode0 为逐字节基线） | **未实现** | src/app/Cli.cpp:174-178：`--agent-mode 0` 解析期直接拒绝（exit 2，"orbital paradigm not implemented yet"）；src/core/Config.h:43-46 注释自认"仅 1 已实现"；src/sim/ 内无任何轨道运动路径；实测 `--agent-mode 0` → exit 2 | mode0 轨道范式一行实现代码都没有，"两范式共存"不成立。S9 被 GOAL 划入初版体系（完整复核绑 P2 出口），但"模式开关+两套状态隔离的基础版"本应随初版落地；TODO#T14 自认"轨道范式本体未实现、S9 完整复核未开始"。只有 mode1 单条路径 |
| 2 | Torus 环形拓扑 + 最小环距 | **完全符合** | src/core/Vec2.h:30-45 `torusDelta`（分量收敛到 [-W/2,W/2]，即最小环距）、:48-51 `torusDistance`；tests/unit/test_torus.cpp:22-69 覆盖 (0,0)→(W-1,0)=1、跨边/对角/半宽上界/反方向；全仓库（food 查询、温度场、地形、行为）统一走该函数，无裸欧氏距离 | 无 |
| 3 | 地形：障碍/水域/温度区**接入行为层** | **未实现**（推迟本身合规，但已写半套零接线） | src/environment/Terrain.h/.cpp 实现 blocked/inWater/moveCostMultiplier/temperatureOffset/resolveMove，10 个单测全过；但 src/sim/Simulation 仅持有成员并构造（Simulation.h:48、Simulation.cpp:32），grep 全 src：Terrain 各查询方法在 Terrain 自身与 tests 之外 **0 个调用点**；INDEX#T42/TODO#T43 自认"未接行为层" | 障碍不挡移动、水域不加耗能、温度区不偏置温度，对仿真行为零影响。SPEC-SOURCE 本将地形列为【后续·初版不实现】，故不算违规；但半成品悬空构成名不副实风险（见"名不副实"节） |
| 4 | 生产者：蓝细菌/阳光/温度 → 食物，吃下转能量 | **部分符合** | src/sim/Simulation.cpp:129 每 tick 定额 `regrowFood(rng_, 2)`；AgentEnvironment.cpp:81-120 均匀随机撒点；进食转能量 AgentBehavior.cpp:194-201（`transfer(kFoodPoolId, ...)` 零和）；grep 蓝细菌/阳光/photosyn **0 命中**；温度场已存在但对食物再生零影响 | "吃下转能量"半句符合；但食物生成是**凭空定额撒点**，不是蓝细菌在阳光/温度作用下产出，无任何光照/温度耦合（ADR-002 明确选了定额再生、否决 logistic，属已留档的简化，但与规格 R2.3 文本仍有差距） |
| 5 | 探测器（核）：读范围内**各实体（类型+位置）**与温度 | **部分符合** | src/perception/AgentSensors.cpp:49-132：senseRadius 内食物的相对环距方向 s[0..5]、接近度 s[6..8]、距离 s[9]；:156-158 温度 s[17]、失真度 s[16]；距离全走最小环距 | 温度✓；但只探测**食物一种实体**，其他核与球对探测层不可见，规格"各实体（类型+位置）"未达成（也无类型维——单实体类型下类型维尚无意义，多实体探测缺失是实质缺口） |
| 6 | 探测球回传周围实体信息 | **未实现** | grep 探测球/probe **0 命中**；Ball 结构（AgentBehavior.h:26-32）只有轨道角/半径/能量/lastOmega，无任何感知或回传路径 | 探测层三件套（核/探测球/源）只落地了核与源（SensorVector），探测球整体缺失，无最简形式 |
| 7 | 决策层初版：手搓死规则 + 打分机制 | **完全符合** | src/decision/RulePolicy.cpp:22-78：score = wP·(1−0.5·s[16])·proximity + wA·alignment + wU·urgency，逐候选打分取 argmax；默认策略（Simulation.cpp:56）；纯确定性无 RNG | 无（另有 StubPolicy 测试辅助实现，不影响本条） |
| 8 | 感知机 Z=W^T X+b，y=+1 if Z>=0 else −1 | **完全符合**（经 ADR-003 授权提前） | src/decision/PerceptronPolicy.cpp:46-50（Z=Σw·s+b）、:52-72（sign 边界 Z=0 取 +1，线性映射回方向；双输出神经元）；decide 无 RNG（test_perceptron.cpp:172-182）；同 seed 与 RulePolicy 输出互异（test_perceptron.cpp:155-169；实测 200 tick rule food=26 vs perceptron food=19）；须显式 `--policy perceptron`（Cli.cpp:105-117） | 无。双输出神经元是规格单输出公式的直接扩展，公式本体逐字符合；ADR-003 走完 supersedes 流程 |
| 9 | 行为层：移动/产球/回收/繁殖，账目零和 | **完全符合** | 移动+移动耗能 AgentBehavior.cpp:145-178；产球 :51-73（deployCost 8 耗散 + 种子 10 经 transfer 零和注入）；回收 :81-96（返还 50%，余量 recycle-loss 耗散）；繁殖 :105-127（子代 = 0.5·cost ≤ cost，S10）；一切能量走 ledger，限额支出保证账实相等；test_behavior.cpp 11 用例；实测 `--ticks 20000 --seed 42 --audit` → audit=ok | 无 |
| 10 | 球远了控制失调（增益随距离衰减，生产可达） | **完全符合** | AgentBehavior.cpp:35-45 `ballControlGain=1/(1+(d/h)²)` 单调衰减；:237-247 gain<0.2 进漂移分支；生产可达性：球充能路径 :203-220（进食 60% 转给最饥球）+ handoffRadius=60（AgentBehavior.h:55-59 含可达性推导）；test_behavior.cpp:38-53（单调）、:249-263（失控分支可达） | 无（handoffRadius 注释里的球能峰值数字略过期，INDEX R4 已登记为 P2） |
| 11 | 被动呼吸耗能（与**速度、温度**相关） | **部分符合** | AgentBehavior.cpp:181：`resp = (respirationBase + respirationK·speed²)·dt`，走 `dissipate("respiration")`，限额支出 | **缺温度因子**。规格明文"与速度、温度相关"，GOAL R5.6 同；行为层根本拿不到温度场（`apply` 无 TemperatureField 参数）；INDEX#T09 自认"温度项待 T17 地形落地后补，不阻塞 S3"。速度项真实且入账 |
| 12 | 温度偏好失真（偏差越大失真越严重，单调） | **部分符合** | TemperatureField.cpp:70-72 `distortion=clamp01(|sample−preferred|·k)`（偏差单调，饱和后封顶）；作用于感知 AgentSensors.cpp:85-93（读噪声 ×(1+3.0·d)）；作用于决策 RulePolicy.cpp:58-64（信任因子 1−0.5d）与 ThermalPolicy；test_sensors.cpp:106-122 实测放大关系 | 规格本把深化列为【后续】，现状属超前地基；但 S12 的"偏差梯度扫描→失真单调变差"验收未做；且失真度 clamp 饱和后不再随偏差增长（上限 1），"偏差越大失真越严重"仅在 [0, 1/k] 区间严格成立 |
| 13 | 双重干扰：读噪声 + 写扰动，强度随速度单调 | **完全符合** | 读：AgentSensors.cpp:26-29（amp=0.15·s/(1+s)，单调有界，s=0 恒 0）、:107-108 叠加到决策真实消费的 s[0..9]；写：AgentBehavior.cpp:137-143（amp=0.25·s/(1+s) 加到 dirX/dirY）；测量：test_behavior.cpp:94-119（读通道 21 档速度直接断言 amp 严格单调抬升、静止恰 0）、:62-90（写通道：速度 1 vs 40 各 64 trial，vel.x 散布严格更宽） | 轻微：写通道的测量是统计散布代理而非幅度直接断言（写噪声幅度公式只能靠读码核实） |
| 14 | 五模块单向解耦（名称与依赖方向与规格一致） | **部分符合** | 模块名逐一对应：environment/AgentEnvironment、perception/AgentSensors、decision/IDecisionPolicy(+Rule/Perceptron/Thermal)、behavior/AgentBehavior、ledger/AgentLedger，全部在 namespace agentsim；依赖 environment→perception→decision→behavior→ledger 无环（SensorTypes.h 中立头上移，G1 修复）；check.ps1:16-32 守卫 decision→environment/sim/ledger 零 include + perception→decision 逆向断言 | "行为层唯一写点"不严格：Simulation.cpp:70-83 组合根出生引导直接写 n.pos/n.energy/n.cap（前两者被 check.ps1:58 WARN 豁免，`n.cap = 200.0` 因守卫正则（check.ps1:57）不覆盖 `.cap` 而完全逃逸）；"由守卫强制，不靠自觉"存在盲区 |
| 15 | 自传 RNG + 固定种子双跑逐字节一致 | **完全符合** | src/core/PCG32.h/.cpp 手写 PCG（无 <random>/rand，check.ps1:34-43 守卫）；getState/setState 快照恢复（test_rng.cpp:44-77）；stateHash 覆盖 pos/vel/energy/speed/alive/ball/food（Simulation.cpp:189-215）；**本次审计实测**：`agentsim.exe --no-render --ticks 500 --seed 42` 双跑 stdout 逐字节一致（done tick=500 nuclei=20 food=23 total_energy=1921.84675274 audit=ok） | 无 |

## 二、重大缺口 Top 5（按对用户意图的破坏程度排序）

1. **mode0 轨道范式整体缺失（#1，最高优先）**。规格把"轨道范式（agent_mode=0，逐字节基线）与自治体范式共存"列为第一条底层规则，S9 属初版体系；当前 CLI 直接拒绝 mode0，双范式承诺零兑现。最小修复：Simulation 增加确定性轨道路径（核按固定解析轨道运动，不感知不决策、函数体定型后冻结），CLI 放行 mode0，补 mode0/1 交替与状态隔离单测。
2. **探测球回传缺失（#6）**。探测层"核/探测球/源"三件套缺一，且无最简形式。最小修复：给 Ball 增加感知半径与回传缓存，`sense()` 把各球周围实体并入 SensorVector 扩展维（GOAL R3.2 明确允许初版最简形式）。
3. **呼吸耗能缺温度因子（#11）**。规格明文"与速度、温度相关"，而温度场已在环境层现成，接线成本极低却未做。最小修复：`apply` 增加温度/失真入参，呼吸项乘 (1+k·distortion) 或加 |T−T_pref| 线性项，补"同速度不同温度呼吸量单调"单测。
4. **生产者与阳光/温度零耦合（#4）**。"蓝细菌+阳光+温度→食物"被简化成与温度场完全无关的定额撒点，温度场成了只影响干扰的摆设。最小修复：regrowFood 对候选点按温度适宜度加权接受/拒绝（仍走传入 rng，保持确定性），让温度实际塑造食物空间分布。
5. **地形半成品零接线（#3）**。类、10 个单测、CLI 旗标俱全，生产路径 0 消费者——最典型的"写了但没用"。最小修复：按 TODO#T43，移动步套 `resolveMove`，locomotion/呼吸乘 `moveCostMultiplier`，感知温度采样叠加 `temperatureOffset`。

## 三、名不副实之处（宣称了但实际没做/过期）

- **Terrain.h:2-4** 注释宣称"障碍阻挡移动（resolveMove 推出）；水域提高移动耗能；温度区给局部温度偏移"——生产代码 0 调用，纯死代码（INDEX#T42 自己标了 ▲，但类注释无任何提示）。
- **AgentLedger.h:18** 注释的 dissipate reason 枚举仍是旧值（move/spawnBall/reproduce/recycleBall），与 AgentBehavior.cpp 实际 9 处调用实参（locomotion/respiration/deploy/...）不一致（TODO#T44 已登记未修）。
- **INDEX#T04** 行仍写"16 维 SensorVector"，代码实际已是 18 维（kDim=18，T31 之后未回改文档）。
- **AgentSensors** 的规格语义是"读范围内各实体"，实现只读食物；s[13]（距边界维）在 Torus 上是注释自认的"占位"维。
- **Config.h:32** `usePerceptron` 注释"当前未接线"已过期（Simulation.cpp:42 早已接线）。
- **check.ps1:57** 写点守卫正则不覆盖 `.cap =`，Simulation.cpp:80 `n.cap = 200.0` 逃逸守卫；且 2 处出生引导写点靠 WARN 豁免——"S5 由守卫强制"存在盲区。

## 四、做得好的地方

- `torusDelta` 数学正确且是全仓库唯一距离权威，跨边界/对角/半宽上界测试齐全。
- 账本零和纪律严格：唯一一级注入（kFoodPoolId）、限额支出、繁殖纯零和转移；本次审计复跑 2 万 tick audit=ok 确认。
- 感知机公式逐字符合、decide 零 RNG、权重 serialize 位精确往返，提前引入走完了 supersedes ADR 流程。
- 双通道噪声 speed/(1+s) 单调有界、静止恒 0；读通道有元信息维 s[14] 可直接测量单调性。
- check.ps1 一条命令可复跑"守卫+构建+单测+双跑比较"，本次审计逐项独立核实。

## 五、本次审计实测记录（2026-10-31，既有 build 产物）

| 命令 | 结果 |
| --- | --- |
| `build\agentsim.exe --no-render --ticks 500 --seed 42`（×2） | stdout 逐字节一致：`done tick=500 nuclei=20 food=23 total_energy=1921.84675274 audit=ok policy=rule` |
| `--ticks 200 --seed 42 --policy rule` vs `--policy perceptron` | nuclei=14/food=26 vs nuclei=12/food=19，输出互异（S8） |
| `--agent-mode 0` | exit 2：unsupported --agent-mode 0: orbital paradigm not implemented yet |
| `--ticks 20000 --seed 42 --audit` | `done tick=20000 nuclei=19 food=18 total_energy=1729.17185834 audit=ok`，exit 0 |
| `build\unit_tests.exe` | 75 tests, 4516 checks, 0 failures |
