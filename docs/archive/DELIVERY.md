# agentsim 交付说明（审查用）

> 生成时间：2026-10-31 12:40　|　项目根：`<repo>`
> 本文按用户给定的四层架构逐层核对，供审查后继续开发。

---

## 0. 一页速览

| 项目 | 数值 |
| --- | --- |
| 源码 | 30 个文件（.h/.cpp），2164 行 |
| 单元测试 | 12 个文件，**92 tests / 6917 checks / 0 failures** |
| 文档 | 13 篇（含 3 个 ADR、2 个实验报告、2 个红队报告） |
| 构建 | MSVC `/std:c++17 /W4 /WX /permissive- /utf-8` **零警告** |
| 门禁 | `tools\check.ps1` → **PASSED**（含双跑逐字节确定性） |
| 性能 | 100000 tick ≈ 10.1 s |

### 复现命令

```powershell
cd <repo>
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1
build\unit_tests.exe
build\agentsim.exe --no-render --ticks 20000 --seed 42 --audit
powershell -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1
```

---

## 1. 四层架构核对（用户给定的判据）

用户定义的单向流水线：**环境支撑 → 感知输入 → 逻辑决策 → 物理执行**。

### 实测依赖方向（grep 验证，非文档描述）

```
src/environment/   → 不 include 任何 behavior/decision/perception/sim  ✅ 完全不知道智能体存在
src/ledger/        → 不 include 任何上层                          ✅ 纯结算
src/perception/    → 只 include core/ + environment/              ✅ 只读上层
src/decision/      → 不 include environment/sim/ledger/behavior    ✅ 不认识世界
src/behavior/      → include environment + ledger + decision       ✅ 唯一写点
src/sim/           → 组合根，可 include 全部                        ✅
```

### 逐层对照

| # | 用户规格 | 实现 | 判定 |
| --- | --- | --- | --- |
| L1 | 环形拓扑 Torus 空间查询 | `core/Vec2.h`：`torusDelta`/`torusDistance`/`wrap`，最小环距，跨四边/对角有单测 | ✅ |
| L1 | 基础食物生成 | `environment/AgentEnvironment`：`spawnInitialFood`/`regrowFood`/`queryFood`/`consumeFood` | ✅ |
| L1 | 零和能量账本 | `ledger/AgentLedger`：`injectInitial/injectPrimary/dissipate/transfer`，每 tick `audit(injected-dissipated == Σliving)` | ✅ |
| L1 | 完全不知道智能体 | 依赖 grep 为空 | ✅ |
| L2 | 核读取范围内实体 + 温度 | `perception/AgentSensors`：18 维 `SensorVector` | ✅ |
| L2 | 探测球回传信息 | **无** Ball 感知与回传 | ❌ 未实现 |
| L2 | 纯数据层，无逻辑判断 | 纯函数，不写任何状态 | ✅ |
| L2 | 读数准确性受温度与速度干扰 | 读噪声 `amp = k·speed/(1+speed)·(1+3·distortion)`（速度↑和温度失真↑都单调增） | ✅ |
| L3 | 不认识世界，只吃向量 | 依赖 grep 为空，决策层无 RNG | ✅ |
| L3 | 打分机制（初版规则） | `RulePolicy` / `StubPolicy` / `ThermalPolicy` 三条打分策略 | ✅ |
| L3 | 感知机 `Z = W^T X + b`，`y = sign(Z)` | `PerceptronPolicy`，Z=0 取 +1，`decide()` 无 RNG | ✅ |
| L3 | 输出 ActionIntent，不改物理状态 | 只返回 `ActionVector{dirX,dirY,boost}` | ✅ |
| L4 | 唯一物理写点 | `behavior/AgentBehavior::apply`；`check.ps1` 有 `behavior-is-only-writer` 守卫 | ✅ |
| L4 | 移动 | 写通道扰动 → 速度 → wrap → 耗能 | ✅ |
| L4 | 进食 | `consumeFood` → `ledger.transfer` 零和 | ✅ |
| L4 | 消耗能量产球 | `deploy`，走 `ledger.transfer(n.id, ball.id, seed)` | ✅ |
| L4 | 消耗能量繁殖 | `reproduce`，子代初能 = 0.5×cost ≤ cost | ✅ |
| L4 | 回收球 | `recycle`，返还 `recycleReturn` 比例，差额记 `recycle-loss` | ✅ |
| L4 | 球距过远时失调 | `ballControlGain = 1/(1+(d/h)²)`，`h=60`，球能>220 即 `gain<0.2` 进入漂移 | ✅ 已用生产路径证明可达 |
| L4 | 受速度影响的输出干扰 | 写扰动 `amp = writeNoiseK·speed/(1+speed)`，单调递增 | ✅ |
| L4 | 受温度/速度影响的被动呼吸 | `(respirationBase + respirationK·speed²)·(1 + 1.5·distortion)` | ✅ |

> 上表 22 项中 **21 项符合，1 项未实现**（探测球回传）。这与 R5 规格审计的结论一致（该审计当时还未计入本轮的呼吸温度因子，故它判为"部分符合"）。

---

## 2. 目录结构

```
src/
  core/         Vec2.h（Torus 几何）PCG32.h/.cpp  Config.h  Types.h  SensorTypes.h（18 维）
  ledger/       AgentLedger.h/.cpp
  environment/  AgentEnvironment.h/.cpp   TemperatureField.h/.cpp   Terrain.h/.cpp
  perception/   AgentSensors.h/.cpp
  decision/     IDecisionPolicy.h  RulePolicy.h/.cpp  PerceptronPolicy.h/.cpp  ThermalPolicy.h/.cpp
  behavior/     AgentBehavior.h/.cpp
  sim/          Simulation.h/.cpp
  app/          Cli.h/.cpp  main.cpp
tests/unit/     test_framework.h + 10 个测试文件 + main.cpp
tools/          build.ps1  check.ps1  evolve.ps1  evolve_thermal.ps1
docs/           GOAL.md TODO.md INDEX.md SPEC-SOURCE.md NIGHT-SHIFT.md NIGHT-SUMMARY.md
                DECISIONS/ADR-001..003
                experiments/E1-perceptron-evolution.md  E2-thermal-policy-evolution.md
                reviews/R1-20261031-redteam.md  R5-spec-compliance.md
```

---

## 3. 四种决策策略的实测差异

同一 seed 42，200 tick：

| 策略 | nuclei | food | total_energy |
| --- | --- | --- | --- |
| `rule` 规则打分（默认） | 14 | 26 | 2377.99842024 |
| `stub` 空动作对照 | 18 | 415 | 5269.58765030 |
| `perceptron` 感知机 | 12 | 19 | 999.61413916 |
| `thermal` 趋温 | 13 | 22 | 2694.09183957 |

四者**互不相同**，证明全部真正接线（这是 R1 红队发现的"死参数"问题的回归防线）。

---

## 4. 多重稳态基线（20000 tick，无地形）

| seed | nuclei | food | total_energy |
| --- | --- | --- | --- |
| 42 | 16 | 22 | 1480.96247761 |
| 7 | 12 | 27 | 1232.98942785 |
| 123 | 9 | 25 | 1336.08215699 |

全开地形（obstacles 5 / waters 3 / thermal 2）：

| seed | nuclei | food | total_energy |
| --- | --- | --- | --- |
| 42 | 9 | 31 | 1311.71896830 |
| 7 | 8 | 31 | 1224.29907579 |
| 123 | 13 | 24 | 1428.90501064 |

---

## 5. 已知缺口（诚实清单，按严重度）

| # | 缺口 | 影响 | 状态 |
| --- | --- | --- | --- |
| 1 | **探测球没有感知与回传** | 用户规格的 L2 三件套（核/探测球/源）缺一 | 未实现 |
| 2 | **`agent_mode=0` 轨道范式整体缺失** | 规格第一条"平行范式共存"只有 mode1 | CLI 明确 exit 2 报错，不静默忽略 |
| 3 | 食物是"定额撒点再生"，非蓝细菌光合 | 规格 L1 的"蓝细菌/阳光/温度"没有建模成实体 | 未实现（当前是抽象食物） |
| 4 | `s[16]` 温度失真不含地形温度区偏移 | 温度区上"失真度"与"实际温度"脱节 | 已留档取舍 |
| 5 | 地形半径是绝对值，不随世界尺寸缩放 | 换 `--width/--height` 会失准 | 已记录 |
| 6 | `ballDrift` 耗散系数、若干注释数字 | 非功能问题 | 已记录 |
| 7 | 感知机旧存档（16 维）静默反序列化 | 无外部存档，暂无影响 | 已记录 |

完整清单见 `docs/TODO.md` 的"还没干"分区。

---

## 6. 过程记录（值得看的三个教训）

1. **死参数反复出现**：三轮红队各发现一批"CLI 解析了但没人读"的参数
   （`--policy`/`--nuclei`/`--agent-mode` → `--thermal-pref` → `--thermal-w-caution`）。
   现在每次都要跑"改参数看输出是否变化"的验证。
2. **主代理误判指标**：曾因 `total_energy` 从 168822 掉到 1029 判定"濒死"，
   实际是容纳量稳态。已在 `INDEX.md` 写入澄清：**总能量不是生态健康指标**。
3. **并发写冲突**：两个子代理同时改同一文件，结果反复被覆盖。
   教训：同一文件绝不并行派两个代理；主代理接手时先 `interrupt_agent` 再改。

---

## 7. 下一步建议（供审查后选择）

按用户规格的缺口优先级：

1. **探测球**（缺口 #1）——给 Ball 加感知半径，`sense()` 并入球周实体维，凑齐 L2 三件套。
2. **蓝细菌实体化**（缺口 #3）——把抽象食物换成"蓝细菌节点 + 阳光 + 温度"生产者。
3. **`agent_mode=0` 轨道范式**（缺口 #2）——按 v2.3.1 语义实现第二条路径。
4. `s[16]` 与地形温度区复合（缺口 #4）——一行修复。

---

## 8. R7 红队独立复核（2026-10-31 13:06）

R7 的任务是**独立验证两个 P0 是否真的修好**（不采信修复者的数据）。结论：

### A. P0-1（trustFood 反转）→ **真修好 ✅**

- 自验 `candidateScore`：wc=100、caution=0 → `doubt=50` → `trustFood=0.0196 > 0`（探针实测）。
- 64 组双候选组合（wc∈{0,3,10,100} × 失真∈{0,.5,.9,1} × 速度 4 向）**无一数值反转**。
- 注：探针中 18 个"选远"场景全部是速度指向远候选（stay 项语义所致），v=0 时 16/16 选近。

### B. P0-2（出生/食物/隧穿）→ **真修好 ✅，留 2 个 P1 边角**

| 验证项 | 结果 |
| --- | --- |
| 出生点 blocked | seed 42/7/123 × obst 20/28/40 → 48 核 **0 blocked** |
| 食物落障碍 | spawn + 40000 tick regrow → **0 blocked**，无死循环 |
| 隧穿（139 万条线段） | 两端合法且穿障时落点在障碍内 = **0** |
| 20k × 6 与 50k × 6 全开地形 | **全存活**，曲线波动不归零 |
| obst=20 压力 20k | 全存活 |
| 无地形回归 | 12/12（4 策略 × 3 seed × 20k）与"仅回退 R6"版本**逐字节一致** |
| 确定性 | 地形与 obst20 双跑 `state_hash` 逐字节一致 |

> `stub` seed7/123 灭绝是 stub 恒零动作的既有语义（还原版同样灭绝），**非回归**。

### R7 新报的 3 个 P1（主代理已逐条验证属实，尚未修）

| # | 位置 | 问题 | 最小修复 |
| --- | --- | --- | --- |
| P1-1 | `Terrain.cpp:131-137` | 隧穿修复只采样 t=1/4,1/2,3/4，**掠过弦**（弦心距障碍 <15u）不被拦截。实测 step 46/60 时 564/1240（≈5%）穿障线段仍落到障碍远端 | 采样加密到 k/8（k=1..7），残漏 15u → ≤7.5u |
| P1-2 | `ThermalPolicy.cpp:113` | `trustStay = 1 + doubt` **无上界**。d=1、wc=100 时 stay 项是接近项的 25.5 倍；"近候选恰在速度反方向"时恒选远候选（未反转但语义退化）。默认 wc=0.8 时约 11% 偏置 | `trustStay = 2.0 - trustFood`（上界 2） |
| P1-3 | `Simulation.cpp:97-105` | 200 次放宽分支**仍可能把核写进障碍**（`n.pos` 留最后一次采样），破坏"出生必不在障碍内"承诺。默认 13.8% 覆盖率下概率约 1e-39/核，实测 48 核 0 命中 | fallback 结束时仍 blocked 则继续采样，或用 `resolveMove` 推到最近自由点 |

另有 2 个 P2：

| # | 位置 | 问题 |
| --- | --- | --- |
| P2-1 | `AgentEnvironment.cpp:33-44` | `blockedAt` 在 minSep 退避**之后**才检查，障碍点先白耗一次 RNG + 一次 minSep 全扫 |
| P2-2 | `AgentEnvironment.h:30,73-74` | `BlockedFn + void* ctx` 裸指针无所有权，`AgentEnvironment` 可拷贝（拷贝后 ctx 悬垂）。当前唯一调用点安全 |

### 一个过程问题

R7 指出修复者遗留的 `probe6_wc.cpp` **仍是旧公式且从未编译过**（.obj 不存在）——
这是"留了探针代码但没跑"的糊弄痕迹。虽然主修复本身真实有效，但说明**子代理的自检报告不能全信**。
