# agentsim 夜班总结（2026-10-31 00:06 – 07:50）

## 一句话

从**零个文件**开始，用「主代理只指挥 + 子代理对抗迭代」的方式，建成一套完整可运行的四层单向解耦生态仿真。
**Phase 1 最小闭环全部达标**，并额外完成**感知机策略**（含接线与进化实验）与**温度场失真**两个后续机制。

## 门禁基线（主代理亲测，可随时复现）

```powershell
cd D:\ScuStu\Q\agentsim
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1
```

| 项目 | 结果 |
| --- | --- |
| 单测 | **57 tests, 4128 checks, 0 failures** |
| check.ps1 | **PASSED**（依赖守卫 + 构建 + 单测 + 双跑确定性，六项全 PASS） |
| 编译 | MSVC `/std:c++17 /W4 /WX /permissive- /utf-8` **零警告** |
| 双跑确定性 | `determinism=True`（1000 tick 逐字节一致） |
| stats.csv | 7 列 `tick,nuclei,food,total_energy,audit_ok,deaths,state_hash` |

### 三个 seed，20000 tick

```
seed 42  : nuclei=20 food=9  total_energy=12349.71860489  audit=ok
seed 7   : nuclei=21 food=14 total_energy=17352.84300438  audit=ok
seed 123 : nuclei=21 food=14 total_energy=20381.00081633  audit=ok
```

### 性能

```
100000 tick -> 10136 ms   (修复二次增长前是 26709 ms，2.6x 提升)
```

### 三策略互异（同一 seed 42，200 tick）

```
rule        nuclei=16 food=23  total_energy=2501.16
stub        nuclei=18 food=415 total_energy=5269.59
perceptron  nuclei=11 food=22  total_energy=1492.28
```

## 已落地的模块

```
src/core/          Vec2.h（Torus 最小环距）PCG32.h/.cpp  Config.h  Types.h  SensorTypes.h（18 维）
src/ledger/        AgentLedger.h/.cpp（零和账本）
src/environment/   AgentEnvironment.h/.cpp（Torus + 食物 + regrowFood）
                   TemperatureField.h/.cpp（高斯源叠加，跨边界连续）
src/perception/    AgentSensors.h/.cpp（18 维，读噪声 ∝ 速度，温度放大失真）
src/decision/      IDecisionPolicy.h  RulePolicy.h/.cpp  StubPolicy  PerceptronPolicy.h/.cpp
src/behavior/      AgentBehavior.h/.cpp（唯一写点：移动/进食/deploy/recycle/reproduce/失控/呼吸）
src/sim/           Simulation.h/.cpp（组合根，固定顺序 sense→decide→behavior→env）
src/app/           Cli.h/.cpp  main.cpp
tests/unit/        57 个测试（torus / rng / ledger / environment / behavior / temperature / sensors / perceptron）
tools/             build.ps1  check.ps1  evolve.ps1
```

## 三轮红队对抗的关键战果

### R1（12 条，5 P1）→ 全部修复
- CLI 数值解析：`--ticks -1` 挂死、`--width nan` 静默接受、超大 `--food` 崩溃 → 全部 exit 2
- CLI 选项从未接线（`--policy` / `--nuclei` / `--agent-mode` 全是死参数）→ 真接线
- 探测层逆向依赖决策层 → 抽 `core/SensorTypes.h` 中立化
- 读通道噪声零消费者 → 噪声叠加到 s[0..9] 语义维（实测决策翻转率 83.8%）
- 行为层缺 deploy/recycle/reproduce → 补齐且全部走账本

### R2（1 P0）—— 最险的一条
`reproduce` 一次触发三条账目，恒等式偏差 = k(f−1)。**红队给的修复方案是错的**（仍偏差 f·k）。
主代理自己推导出正确修法：繁殖是纯零和转移，**只保留 `dissipate("reproduce-loss", k−f·k)`**。
R3 独立验算确认：Δ(inj−dis) = −10 = Δ(living)，恒等成立。

### R3（2 P1 + 8 P2）
- **[P1] 性能二次增长**：死核永不移除（约 137 尸体/千 tick）+ 繁殖 gate 每核调 O(n) 的 `nucleusCount()`。
  实测 20k=1.9s / 50k=8.0s / 100k=26.7s。修复（aliveCount_ 缓存 + 尸体压缩）后 100k=10.1s。
- **[P1] 生态边缘化**：avg 核能 35.2 低于 gate 40，高周转脆弱。
  参数 gate 40→60、p 0.02→0.002 后：avg 核能 617~970，total_energy 12350~20381。

## 过程中踩过的三个坑（值得记录）

1. **并发写冲突**：两个代理同时改 `Simulation.cpp`，调参结果被性能修复覆盖两次。
   最后由主代理**直接下手改两行**并用 `interrupt_agent` 终止仍在写回的代理才收敛。
   教训：同一文件绝不能让两个子代理并行写。
2. **大文件被截断**：子代理写超过 ~100 行的文件会被截断，任务直接失败。
   解法：强制 ≤60 行分段 `writeFileSync` / `appendFileSync`，首段加 UTF-8 BOM。
3. **主代理误判指标**：我一度看到 total_energy 从 168822 掉到 1029，判定「濒死」并让代理去救。
   实际是**容纳量稳态**（三个 seed 末 10000 tick 的 nuclei CV = 0.109/0.111/0.111，阈值 0.35）。
   后来派去调参的代理自己实测后正确地拒绝了改动。已在 INDEX 写入「total_energy 不是健康指标」的澄清。

## 未完事项

| 项 | 状态 |
| --- | --- |
| agent_mode = 0（轨道范式） | ❌ 未实现，CLI 明确 exit 2 报错 |
| 地形（障碍 / 水域 / 温度区） | ❌ 未开始 |
| S7 失控分支 | ⚠️ 代码正确但系统内不可达（球能唯一来源是种子 10，radius 恒 15，gain 0.99） |
| frontend 契约 | ❌ 未开始 |
| s[17] 温度采样值 | ⚠️ 无决策层消费者（reserved） |
| PerceptronPolicy 旧存档兼容 | ⚠️ kDim 16→18 后旧存档静默反序列化 |

## 给用户的两个提醒

1. **goal 工具需要真人回合**：`update_goal` 的 edit / resume 在自动续轮上下文里会被拒绝。
   要改目标或重新武装，得在正常对话里说一句。
2. **电池待机 3 分钟**：AC 插电不睡，但 DC 电池 180 秒就睡，会让整条夜班停摆。
