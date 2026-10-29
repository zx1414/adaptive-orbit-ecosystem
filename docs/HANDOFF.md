# EcoCore v3 交接文档（M2.5 完成）

> 新会话请先读本文件，再读 `README.md`、`docs/ARCH.md`、`docs/BALANCE.md`、`docs/ROADMAP.md`、`docs/M2.5-DESIGN.md`。

## 1. 项目意图

从零重建的 C++17 单线程生态模拟核心，旧版 v2 已冻结在 Git 标签 `legacy-v2.3.1`，不参与构建。

硬约束：

- 环境不产生能量，只有边界与光照场。
- 生产者是唯一初级能源注入者；所有能量变化经过 `EnergyLedger`。
- 决策层可插拔：28 维 Observation + Action + IPolicy；后续接神经网络。
- 单线程、PCG32、固定迭代顺序、稳定 EntityId、可复现状态哈希。
- 零第三方依赖，MSVC `/W4 /WX /permissive- /utf-8`。

## 1.1 用户目标架构（最高优先级）

用户明确要求的是四层架构：

```text
世界（World）
  │ 只读查询/样本
  ▼
探测（Perception）
  │ Observation 值对象
  ▼
决策（Decision）
  │ Action 值对象
  ▼
行为（Behavior）
  │ 唯一实体写入 + 账本
  ▼
世界
```

对应目录：

- 世界：`src/world`（WorldQuery/Forage/WorldView/Energy/Environment/SpatialIndex）+
  `src/sim`（Simulation 组合根，实现世界状态与每 tick 管线）。
- 探测：`src/perception`（Sensors::buildObservation）。
- 决策：`src/decision`（IPolicy/RulePolicy/StubPolicy）。
- 行为：`src/behavior`（Behavior::execute）。
- 实体与编排：`src/agents`（Agent 被动实体、AgentSystem 生命周期），不是第五层。
- 生产：`src/producers`。

不可违反的规则：

1. 探测不调用策略、不使用 RNG，只输出 Observation。
2. 决策不读取世界、不使用 RNG，只输出 Action。
3. 行为是唯一写 Agent 状态的层；能量必须走 EnergyLedger，收获必须按 id 走 Forage。
4. 层间只传值对象；不把策略指针传给探测，不把世界引用传给决策。
5. 不允许把 Sensors/Behavior 合并回 agents，或让 AgentSystem 直接实现决策/行为。

## 2. 仓库状态

- 工作目录：`D:\ScuStu\Q\自适应环绕核生态模拟与生存游戏`
- 分支：`main`。
- 已有提交：`cbe93a1`（M1）、`66b5c49`（M2 统计/参数化）、`201a18b`（M2.5 交接文档）、`9690e5f`（M2.5 实现与文档）、`ebd4d92`（v2.3.1 架构归档）、本次提交（四层模块拆分）。
- 工作树：干净；M2.5 构建/单测/验收通过。
- 构建产物：`build/simulator.exe`、`build/unit_tests.exe`；CMake 构建目录 `build-cmake/`。

## 3. 架构与不变量

目录：

- `src/core`：Types、Config、Math、PCG32、Time、Events。
- `src/decision`：Observation、Action、IPolicy、PolicyRegistry、RulePolicy、StubPolicy。
- `src/world`：EnergyLedger、Environment、SpatialIndex、WorldQuery、Forage、WorldView。
- `src/producers`：Producer/PhotoProducer、ProducerSystem。
- `src/perception`：Sensors（探测）。
- `src/behavior`：Behavior（行为）。
- `src/agents`：Agent 被动实体、AgentSystem 生命周期编排。
- `src/sim`：Simulation（唯一组合根，实现 WorldQuery + Forage）。
- `src/app`：Cli、ConsoleRenderer、StatsWriter、main。

依赖方向：core ← decision；core ← world；perception/behavior → world + decision 契约；agents → world/perception/behavior；producers → world；sim → 全部；app → sim。
perception/behavior 不 include sim/app；decision 不依赖 world。`world::World` 已删除，组合根位于 sim。

能量规则：

- `injectInitial` 只在初始化期间有效，初始化结束调用 `sealInitial()`。
- `injectPrimary` 私有，friend `producers::PhotoProducer`。
- `transfer` 零和；`dissipate`/`kill` 记账；每 tick `injected - dissipated == sum(living energy)`。
- 审计容差 `1e-9 * max(1, injected)`；失败退出码 3。

决策规则：

- Observation 版本 1，28 维；`decide` 内禁止 RNG。
- 空槽距离为 -1；距离 0 是有效目标。
- Agent 的移动/收获/繁殖随机性只来自 Simulation 持有的 PCG32。

## 4. M2.5 机制

- 生产者局部光竞争：`shading = 1 / (1 + crowding * localDensity + competition * localBiomass / cap)`。
- 生产者密度繁殖：`threshold = repro * (1 + densityRepro * localDensity)`。
- Agent 饱和收获：`intake = harvest * food / (food + halfSaturation)`，`food = target.energy - reserve`。
- Agent 密度繁殖：`threshold = repro * (1 + densityRepro * localAgentDensity)`。
- 所有系数默认 0，默认 20k 基线保持 300/200、总能量 49690.20045、5k 哈希 `ae357d4f52c5f4fa`。
- 推荐配置把 producer/agent max 设为 1000 作安全阀，平衡点由资源与密度决定。

## 5. 验收数据

推荐配置见 `tools/acceptance.ps1` 顶部 `$BalanceArgs`，或：

```powershell
build\simulator.exe --no-render --ticks 20000 --seed 42 --audit `
  --producer-max 1000 --agent-max 1000 `
  --producer-competition 0.2 --producer-radius 400 --producer-density-repro 0.02 `
  --agent-move-k 0.00002 --agent-harvest 0.5 --agent-reserve 10 `
  --agent-half-saturation 20 --agent-density-repro 0.01 --agent-density-radius 200 `
  --agent-lifespan 3000
```

| seed | tick | producers | agents | total_energy | audit |
| --- | --- | --- | --- | --- | --- |
| 42 | 20000 | 202 | 43 | 22027.60411 | ok |
| 7 | 20000 | 214 | 111 | 27331.11735 | ok |
| 123 | 20000 | 255 | 71 | 28980.44196 | ok |
| 42 | 50000 | 202 | 65 | 23826.62187 | ok |

- `tools/acceptance.ps1` 14 项全部 PASS：不灭绝、逐 tick 审计、5000 tick 后两个种群 100% 采样点低于 0.8×1000、双跑哈希一致、50k 耐久通过。
- seed 7 双跑及额外 3 次重复哈希均为 `2faf3ce0d75683b9`。
- Agent 平衡数量对 seed/局部聚集敏感（43–111），但 50k 未灭绝；这是后续调参点，不是审计或确定性缺陷。

## 6. 性能

- 默认 20k tick、500 实体：约 15.9 秒（M2 约 102 秒）。
- 关键改动：感知/收获/邻居/密度查询全部走 SpatialIndex；`queryUnsorted`；id→实体向量映射；
  Sensors 只取最近两个槽位并复用 scratch；索引 cellSize=200。
- 目标“20k < 20 秒”已达成。

## 7. 构建、运行、验证

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1
build\unit_tests.exe
powershell -NoProfile -ExecutionPolicy Bypass -File tools/check.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/acceptance.ps1
```

- 单测：19 passed（新增空间索引、局部竞争、密度繁殖、饱和收获、M2.5 双跑确定性）。
- `tools/check.ps1`：MSVC /W4 /WX 构建、依赖守卫（decision/core/world/agents 不得依赖 sim）、单测、集成冒烟。
- CMake：`cmake -S . -B build-cmake -G "Visual Studio 17 2022" -A x64`，构建后 `ctest`。

## 8. 已知问题与下一步

- Agent 平衡数量偏低且随 seed 波动；优先调 `--agent-harvest / --agent-half-saturation / --agent-density-repro`，
  不要放宽 max。
- `stateHash` 仍未覆盖 policy spec id、cap、age（M2 遗留）。
- 无存档、Web UI、Mod、NN。
- M3：存档/快照，保存 sim 组合根、实体 id、策略基因组、策略内部状态与 PCG32 状态，验证读档续跑一致。

## 9. 子代理与 ZCode

本会话探针确认可用路由：

- provider：`zcode-start-plan-acct-4e049fa5-29da-4e8e-9d84-4f95ff6e657c`
- model：`GLM-5.3-Flash`
- 另有 `zcode-start-plan-acct-1e99ffd8-0ecc-4b88-b477-7be8d2f78439` 同款。

注意：子代理链路可能长时间无输出；任务要小、文件范围明确，必要时中断后接手。`workbuddy` 在当前 Session 不可用。

## 10. 关键文件索引

- 组合根：[src/sim/Simulation.h](src/sim/Simulation.h)、[src/sim/Simulation.cpp](src/sim/Simulation.cpp)
- 查询/收获接口：[src/world/WorldQuery.h](src/world/WorldQuery.h)、[src/world/Forage.h](src/world/Forage.h)
- 空间索引：[src/world/SpatialIndex.h](src/world/SpatialIndex.h)
- 生产者：[src/producers/Producer.cpp](src/producers/Producer.cpp)、[src/producers/ProducerSystem.cpp](src/producers/ProducerSystem.cpp)
- Agent：[src/agents/AgentSystem.cpp](src/agents/AgentSystem.cpp)、[src/agents/Behavior.cpp](src/agents/Behavior.cpp)、[src/agents/Sensors.cpp](src/agents/Sensors.cpp)
- 配置/CLI：[src/core/Config.h](src/core/Config.h)、[src/app/Cli.cpp](src/app/Cli.cpp)
- 验收：[tools/acceptance.ps1](tools/acceptance.ps1)、[docs/BALANCE.md](docs/BALANCE.md)
- 设计：[docs/M2.5-DESIGN.md](docs/M2.5-DESIGN.md)、[docs/ARCH.md](docs/ARCH.md)

## 11. 新会话开场提示

```text
继续 EcoCore v3 项目，工作目录：D:\ScuStu\Q\自适应环绕核生态模拟与生存游戏。
先读 docs/HANDOFF.md、docs/ARCH.md、docs/BALANCE.md、docs/ROADMAP.md、docs/M2.5-DESIGN.md，再读源码。
M2.5 已完成并通过 19 个单测、tools/check.ps1、tools/acceptance.ps1（14 项）。
下一步是 M3 存档/快照：保存 sim 组合根、实体 id、策略基因组与 PCG32 状态，验证读档续跑一致。
子代理优先 zcode-start-plan-acct-…/GLM-5.3-Flash；任务拆小，关键代码由指挥者审查。
```
