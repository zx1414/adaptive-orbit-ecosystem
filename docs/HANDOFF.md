# EcoCore v3 交接文档（M4 完成）

> 新会话先读本文件，再读 `README.md`、`docs/ARCH.md`、`docs/BALANCE.md`、`docs/ROADMAP.md`、
> `docs/M2.5-DESIGN.md`、`docs/M3-PLAN.md`、`docs/M4-PLAN.md`、`docs/REVIEW_PROTOCOL.md`。

## 1. 项目意图

从零重建的 C++17 单线程生态模拟核心，旧版 v2 冻结在 Git 标签 `legacy-v2.3.1`，不参与构建。

硬约束：环境不产生能量；生产者是唯一初级注入者；所有能量变化经过 EnergyLedger；
决策层可插拔；单线程、PCG32、固定迭代顺序、稳定 EntityId、可复现状态哈希；
零第三方依赖，MSVC `/W4 /WX /permissive- /utf-8`。

## 1.1 四层架构（最高优先级）

```text
世界（World）→ 探测（Perception）→ 决策（Decision）→ 行为（Behavior）→ 世界
```

- 世界：`src/world`（WorldQuery/Forage/WorldView/Energy/Environment/SpatialIndex）+ `src/sim`
  （Simulation 组合根、每 tick 管线、审计、状态哈希、快照）。
- 探测：`src/perception`（Sensors）。只输出 Observation，不调用策略、不使用 RNG。
- 决策：`src/decision`。只读 Observation 输出 Action，不读世界、不使用 RNG。
- 行为：`src/behavior`。唯一写 Agent 运动/能量/死亡的层；能量必须走 EnergyLedger，收获按 id 走 Forage。
- `src/agents`：Agent 被动实体与 AgentSystem 生命周期，不是第五层。
- `src/producers`：生产与生产者系统。

不可违反：探测/决策/行为不得合并回 agents；层间只传值对象；不把策略指针给探测、
不把世界引用给决策；不得绕过账本。

## 2. 仓库状态

- 工作目录：`D:\ScuStu\Q\自适应环绕核生态模拟与生存游戏`
- 分支：`main`。
- 已有提交：`cbe93a1`（M1）、`66b5c49`（M2）、`201a18b`（M2.5 交接）、`9690e5f`（M2.5）、
  `ebd4d92`（v2.3.1 归档）、`36102ba`（四层拆分）、`1e582da`（对抗审查协议）、`515848f`（M3 快照）、M4 提交见 git log。
- 工作树：M4 完成后提交；构建/单测/验收通过。
- 构建产物：`build/simulator.exe`、`build/unit_tests.exe`；CMake 目录 `build-cmake/`。

## 3. 架构与不变量

目录：

- `src/core`：Types、Config、Math、PCG32、Time、Events。
- `src/decision`：Observation、Action、IPolicy、PolicyRegistry、RulePolicy、StubPolicy。
- `src/world`：EnergyLedger、Environment、SpatialIndex、WorldQuery、Forage、WorldView。
- `src/producers`：Producer/PhotoProducer、ProducerSystem。
- `src/perception`：Sensors。
- `src/behavior`：Behavior。
- `src/agents`：Agent、AgentSystem。
- `src/sim`：Simulation、Snapshot、SnapshotStore。
- `src/app`：Cli、ConsoleRenderer、StatsWriter、main。

依赖：core ← decision；core ← world；perception/behavior → world + decision 契约；
agents → world/perception/behavior；producers → world；sim → 全部；app → sim。
perception/behavior 不 include sim/app；decision 不依赖 world。

能量：`injectInitial` 仅初始化；`injectPrimary` 仅 PhotoProducer；transfer 零和；每 tick
`injected - dissipated == sum(living energy)`；审计容差 `1e-9 * max(1, injected)`，失败退出码 3。

决策：Observation v1 28 维；`decide` 禁止 RNG；空槽距离 -1；随机只来自 Simulation 的 PCG32。

## 4. 已实现机制

### M2.5

- 生产者局部光竞争、密度依赖繁殖；Agent 饱和收获 + reserve + 密度依赖繁殖；max 仅安全阀。
- 所有系数默认 0，默认 20k 保持 300/200、总能量 49690.20045。

### M4 最小可视化

- app::HtmlTraceWriter 只读 WorldView，按 `--html-interval` 采样（上限 2000 帧），生成单文件 HTML：
  内嵌 `const TRACE = {...}`、Canvas 回放、播放/暂停/滑块、tick/种群/总能量。
- 生产者绿、Agent 橙，亮度随 energy/cap；无外部 URL/脚本/字体，离线可打开。
- CLI `--html` / `--html-interval`（>0，默认 100），兼容 `--load`/`--save`；写失败退出码 4，参数错误 2。

### M3 快照

- 二进制格式：32 字节头（magic `ECOCORE3`、version=1、headerSize、payloadSize、FNV-1a checksum），
  payload 全部 little-endian；字符串/数组带长度上限。
- 保存：SimConfig 全字段、tick、Producer/Agent nextId/births/deaths、PCG32 全状态、
  EnergyLedger 全部统计与 sealed、Producer id/kind/pos/energy/cap、
  Agent id/pos/vel/energy/cap/age、policy spec.id/版本/维度/genome/policy state。
- 读档：校验头/版本/长度/校验和/语义；构造未初始化的 Simulation，直接替换状态，
  **不调用 initialize/injectInitial**；重建索引与 view，立即 audit。
- CLI：`--save <path>`、`--load <path>`，`--ticks` 为绝对结束 tick；
  load 拒绝世界/seed/平衡参数覆盖；错误码 2 参数、3 审计、4 I/O、5 快照无效。
- stateHash 覆盖 policy spec id/版本/维度、cap、age、nextId、births/deaths、transferred 与全部 reason 统计。
  M3 后默认 5k 哈希为 `fea1413153222893`（旧值仅覆盖旧字段）。

## 5. 验收数据

### M2.5（推荐配置，1000/1000 安全阀）

| seed | tick | producers | agents | total_energy |
| --- | --- | --- | --- | --- |
| 42 | 20000 | 202 | 43 | 22027.60411 |
| 7 | 20000 | 214 | 111 | 27331.11735 |
| 123 | 20000 | 255 | 71 | 28980.44196 |
| 42 | 50000 | 202 | 65 | 23826.62187 |

### M3（默认配置）

| seed | tick | producers | agents | total_energy | direct/resumed hash |
| --- | --- | --- | --- | --- | --- |
| 42 | 20000 | 300 | 200 | 49690.20045 | `869ad9e17baf19ae` |
| 7 | 20000 | 300 | 200 | 49687.76436 | `b8b02aa8e02ce634` |

- 10k 存档 + 10k 续跑与 20k 直跑的 stateHash、总能量、种群、injected/dissipated/transferred、audit 全部一致。
- `tools/m3_acceptance.ps1` 全部 PASS；损坏/空文件/版本篡改退出码 5；load+seed 覆盖退出码 2。
- `tools/m4_acceptance.ps1` 14 项 PASS；HTML 帧/tick 递增/JSON/无外部 URL/错误码。
- `tools/acceptance.ps1` 14 项 PASS（M2.5 回归）。

## 6. 性能

- 默认 20k tick、500 实体约 16 秒（M2 约 102 秒）。
- SpatialIndex 接入全部感知/收获/邻居/密度查询；`queryUnsorted`；id 向量映射；
  Sensors top-2 + scratch；cellSize=200。

## 7. 构建与验证

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1
build\unit_tests.exe
powershell -NoProfile -ExecutionPolicy Bypass -File tools/check.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/acceptance.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/m3_acceptance.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File tools/m4_acceptance.ps1
```

- 单测：30 passed（空间索引/竞争/密度/饱和收获/快照格式/存读续跑/损坏拒绝）。
- CMake：VS2022 内置 cmake；`cmake -S . -B build-cmake -G "Visual Studio 17 2022" -A x64`，
  `cmake --build build-cmake --config Release`，`ctest --test-dir build-cmake -C Release`。
- 注意：CMake 内置 cmake 不在 PATH 时用
  `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`。

## 8. 已知问题与下一步

- M3 红队子代理链路多次失败；已做本地对抗测试（截断/魔数/版本/校验和/空文件/配置覆盖），
  仍建议下一轮恢复链路后重跑独立红队。
- Agent 平衡数量随 seed 波动（M2.5）；优先调 harvest/halfSaturation/densityRepro。
- 快照 policy state 已预留，但当前 RulePolicy/StubPolicy 为空；NNetPolicy 需要覆盖序列化。
- M4 最小可视化已完成；后续可扩展真实浏览器 UI/服务器推送，但不得复用 v2 协议。
- 下一步 M5：NNetPolicy（MLP 前向、权重文件维度/版本校验、genome/state 序列化），保持四层契约。
- 四层架构红线与 REVIEW_PROTOCOL 继续有效。

## 9. 子代理与 ZCode

- 账号 4e049fa5（旅行者3329）GLM-5.3-Flash **额度已用尽**。
- 账号 1e99ffd8（旅行者2638）仍有额度，但链路多次无输出/失败；任务要短、文件范围明确，
  失败就中断后接手。探针只回复 OK。
- 当前 Session 不可用 `workbuddy`。

## 10. 关键文件索引

- 组合根：[src/sim/Simulation.h](src/sim/Simulation.h)、[src/sim/Simulation.cpp](src/sim/Simulation.cpp)
- 快照：[src/sim/Snapshot.h](src/sim/Snapshot.h)、[src/sim/SnapshotStore.cpp](src/sim/SnapshotStore.cpp)
- 查询/收获：[src/world/WorldQuery.h](src/world/WorldQuery.h)、[src/world/Forage.h](src/world/Forage.h)
- 空间索引：[src/world/SpatialIndex.h](src/world/SpatialIndex.h)
- 生产者：[src/producers/Producer.cpp](src/producers/Producer.cpp)、[src/producers/ProducerSystem.cpp](src/producers/ProducerSystem.cpp)
- Agent：[src/agents/AgentSystem.cpp](src/agents/AgentSystem.cpp)
- 探测/行为：[src/perception/Sensors.cpp](src/perception/Sensors.cpp)、[src/behavior/Behavior.cpp](src/behavior/Behavior.cpp)
- 可视化：[src/app/HtmlTraceWriter.cpp](src/app/HtmlTraceWriter.cpp)、[tools/m4_acceptance.ps1](tools/m4_acceptance.ps1)
- 配置/CLI：[src/core/Config.h](src/core/Config.h)、[src/app/Cli.cpp](src/app/Cli.cpp)
- 验收：[tools/acceptance.ps1](tools/acceptance.ps1)、[tools/m3_acceptance.ps1](tools/m3_acceptance.ps1)
- 设计/协议：[docs/M3-PLAN.md](docs/M3-PLAN.md)、[docs/REVIEW_PROTOCOL.md](docs/REVIEW_PROTOCOL.md)

## 11. 新会话开场提示

```text
继续 EcoCore v3，工作目录：D:\ScuStu\Q\自适应环绕核生态模拟与生存游戏。
先读 docs/HANDOFF.md、docs/ARCH.md、docs/ROADMAP.md、docs/M3-PLAN.md、docs/REVIEW_PROTOCOL.md。
M3 存档/快照已完成：29 个单测、tools/check.ps1、tools/acceptance.ps1、tools/m3_acceptance.ps1 通过。
M4 最小可视化已完成（--html 单文件 Canvas 轨迹）。下一步 M5 NNetPolicy。
子代理优先 zcode-start-plan-acct-1e99ffd8-.../GLM-5.3-Flash；任务拆小，实现与红队隔离。
```