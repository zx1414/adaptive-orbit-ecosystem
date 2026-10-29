# EcoCore v3 交接文档（M2 → M2.5）

> 新会话请先读本文件，再读 `README.md`、`docs/ARCH.md`、`docs/BALANCE.md`、`docs/ROADMAP.md`。

## 1. 项目意图

从零重建的 C++17 单线程生态模拟核心，旧版 v2 已冻结在 Git 标签 `legacy-v2.3.1`，不参与构建。

硬约束：

- 环境不产生能量，只有边界与光照场。
- 生产者是唯一初级能源注入者；所有能量变化经过 `EnergyLedger`。
- 决策层可插拔：28 维 Observation + Action + IPolicy；后续接神经网络。
- 单线程、PCG32、固定迭代顺序、稳定 EntityId、可复现状态哈希。
- 零第三方依赖，MSVC `/W4 /WX /permissive- /utf-8`。

## 2. 仓库状态

- 工作目录：`D:\ScuStu\Q\自适应环绕核生态模拟与生存游戏`
- 分支：`main`，工作树干净。
- 提交：`cbe93a1`（M1 清空重建）、`66b5c49`（M2 统计/参数化/refuge）。
- 旧版快照：标签 `legacy-v2.3.1`；`legacy/` 被 `.gitignore` 忽略。
- 构建产物：`build/simulator.exe`、`build/unit_tests.exe`；CMake 构建目录 `build-cmake/`。

## 3. 架构与不变量

目录：

- `src/core`：Types、Config、Math、PCG32、Time、Events。
- `src/decision`：Observation、Action、IPolicy、PolicyRegistry、RulePolicy、StubPolicy。
- `src/world`：EnergyLedger、Environment、SpatialIndex、WorldQuery、WorldView、World。
- `src/producers`：Producer/PhotoProducer、ProducerSystem。
- `src/agents`：Agent、Sensors、Behavior、AgentSystem。
- `src/app`：Cli、ConsoleRenderer、StatsWriter、main。

能量规则：

- `injectInitial` 只在 World 初始化期间有效，初始化结束调用 `sealInitial()`。
- `injectPrimary` 私有，friend `producers::PhotoProducer`。
- `transfer` 零和；`dissipate`/`kill` 记账；每 tick `injected - dissipated == sum(living energy)`。
- 审计容差：`1e-9 * max(1, injected)`；死亡残差、亚 epsilon 残差都必须入账。

决策规则：

- Observation 版本 1，28 维；字段表见 `docs/ARCH.md`。
- `decide` 内禁止 RNG；随机只来自 World 持有的 PCG32。
- 空槽距离为 -1；距离 0 是有效目标。

已知边界：

- `World` 目前是组合根，持有 ProducerSystem/AgentSystem；`agents` 的 AgentSystem/Behavior 仍直接 include `world/World.h`，模块级存在环。
- `SpatialIndex` 每 tick 重建，但 `queryProducers`、`queryAgents`、`nearestProducer` 仍线性扫描，索引基本未被查询使用。
- 20k tick、500 实体时 bench 约 73 秒，主要成本是每个 Agent 对全部生产者/Agent 的 O(n²) 感知。

## 4. 构建、运行、验证

构建：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1
```

单测与总检查：

```powershell
build\unit_tests.exe
powershell -NoProfile -ExecutionPolicy Bypass -File tools/check.ps1
```

CMake（VS2022 自带 CMake 也可）：

```powershell
cmake -S . -B build-cmake -G "Visual Studio 17 2022" -A x64
cmake --build build-cmake --config Release
ctest --test-dir build-cmake -C Release --output-on-failure
```

基准运行：

```powershell
build\simulator.exe --no-render --ticks 20000 --seed 42 --audit
```

统计 CSV：

```powershell
build\simulator.exe --no-render --ticks 20000 --seed 42 --audit --stats build/stats.csv --stats-interval 100
```

CSV 列：tick、producers、agents、total_energy、injected、dissipated、transferred、
mean_producer_energy、mean_agent_energy、producer_births、producer_deaths、
agent_births、agent_deaths、gene_0..gene_N、audit_ok。

## 5. M1/M2 已完成内容

M1：

- 零产能环境、PhotoProducer、Agent、Sensors、Behavior、RulePolicy/StubPolicy、World 管线。
- EnergyLedger 零和、初始化封存、逐 tick 审计、状态哈希、CLI、控制台。
- 14 个单元测试；集成冒烟覆盖确定性、零生产者灭绝、stub、状态哈希、CSV、退出码。

M2：

- 统计导出：种群、能量、出生/死亡、基因均值、审计标志。
- CLI 参数化：光合、维持、繁殖、上限、感知半径、变异、收获、猎物保留量、遮荫、寿命。
- 实验机制：局部遮荫、Agent 到龄死亡、收获保留猎物最低能量。
- 平衡实验记录在 `docs/BALANCE.md`。

关键验证数据：

| 项目 | 结果 |
| --- | --- |
| 默认基线 20k | 300/200，总能量 49690.20045，audit ok |
| 默认基线 5k 双跑 | 状态哈希一致 |
| 推荐配置 20k seed42/7 | 39099.37 / 45001.42，300/200，audit ok |
| 推荐配置 50k seed42 | 300/200，总能量 37351.13，4161 次 Agent 到龄死亡，audit ok |
| 单元测试 | 14 passed, 0 failed |
| CMake/CTest | 通过 |

推荐配置命令：

```powershell
build\simulator.exe --no-render --ticks 50000 --seed 42 --audit --producer-crowding 0.5 --producer-radius 120 --agent-harvest 0.2 --agent-reserve 10 --agent-lifespan 3000 --stats build/balanced.csv --stats-interval 1000
```

## 6. 已知问题与优先级

P1（M2.5 要解决）：

1. 种群最终由 `ProducerConfig::max` / `AgentConfig::max` 决定，不是资源承载能力。
2. 只开遮荫+寿命、不加 reserve 时，Agent 会把生产者采空，13k–20k tick 内崩溃。
3. O(n²) 感知：20k tick 约 73 秒，长跑/进化不可接受。
4. `world` 与 `agents/producers` 的模块级依赖环。

P2：

- `SpatialIndex` 已维护但未接入查询。
- `stateHash` 未覆盖 policy spec id、cap、age。
- 无存档、Web UI、Mod、NN。
- `--ticks 0` 等边界只做了基础校验。

## 7. M2.5 目标

> 让生产者和 Agent 在低于硬上限的位置形成可持续的稳定共存，而不是靠 cap 或 reserve 兜底。

验收：3 个 seed（42/7/123）各 20k tick：

1. 两个种群都不灭绝。
2. 5k tick 预热后，至少一个种群在 80% 的采样点低于其上限的 80%。
3. 总能量有界，无单调爆炸/归零。
4. 每 tick 审计通过。
5. 同 seed 双跑哈希一致。
6. 推荐配置 50k tick 耐久不崩溃。
7. 结果写入 `docs/BALANCE.md`，CSV 可复现。

## 8. 技术方案（按优先级）

### 8.1 资源竞争

当前 `PhotoProducer::tick`：

`gain = photo * light * (1 - energy/cap) * 1/(1 + crowding * neighborCount)`

下一步把“邻居计数”升级为局部资源竞争：

- 用 `SpatialIndex` 查询邻域内生产者生物量总和，传入 `ProducerContext`。
- 每个生产者的有效光 = 环境光 / (1 + competition * localBiomass)，或显式局部光预算。
- 保证确定性：邻域查询结果按 id 排序；不引入 RNG。

### 8.2 密度依赖繁殖

- 繁殖阈值由固定 `repro` 改为 `repro * (1 + densityFactor * localDensity)`，或高密度时降低繁殖概率。
- 目标：高密度自动抑制出生，而不是等 cap 拦截。

### 8.3 Agent 功能反应

- 收获量改为饱和函数：`harvest * food / (food + halfSaturation)`，`food` 可用目标能量或局部生产者密度。
- 保留 `harvestReserve`，避免采空最后一个生产者。
- 可让繁殖阈值随能量状态变化，形成真正的能量预算。

### 8.4 弱化硬上限

- 校准配置把 `producer.max` / `agent.max` 设为 1000 级安全阀，让平衡点由资源决定。
- 保留 cap 防止数值爆炸，但不再作为主要平衡机制。

### 8.5 解耦与性能

- 新建 `src/sim/Simulation.*` 作为组合根，把 `World` 移出 `world` 层；或至少引入 `world::HarvestAccess`，让 `Behavior`/`AgentSystem` 不再 include `World.h`。
- 更新 `build.ps1`（自动 glob，通常无需改）和 `CMakeLists.txt`（显式文件列表，必须改）。
- `SpatialIndex` 增加 payload/index 查询，接入 `queryProducers`、`queryAgents`、`nearestProducer`。
- 或先做低风险优化：Sensors 用 `partial_sort` 取前 2、去掉重复排序、复用 scratch 缓冲。
- 目标：20k tick、500 实体 < 20 秒（当前约 73 秒）。

## 9. 子代理与 ZCode

当前会话的 `zcode-start-plan` 子代理路由被 Session 级 allowlist 拒绝：

`child LLM route "zcode-start-plan/GLM-5.3-Flash" is not allowed for this Session`

账号额度已解锁不代表 provider 白名单已放开。新会话请先做一次探针：

- provider：`zcode-start-plan`
- model：`GLM-5.3-Flash`
- prompt：只回复 OK，不调用工具。

若仍被拒绝，退回 `workbuddy` / `glm-5.3-flash`（同款模型）。子代理链路不稳定，失败就重试；
任务要小、文件范围要明确，不要让低智能代理做大范围重构。

## 10. 新会话开场提示（可直接粘贴）

```text
继续 EcoCore v3 项目，工作目录：D:\ScuStu\Q\自适应环绕核生态模拟与生存游戏。
先读 docs/HANDOFF.md、docs/BALANCE.md、docs/ARCH.md、docs/ROADMAP.md，再读源码。
当前 M1/M2 已提交（cbe93a1、66b5c49），14 个单测和 tools/check.ps1 通过。
下一步目标 M2.5：资源竞争 + 密度依赖繁殖 + Agent 功能反应，
让种群在低于硬上限处稳定共存；同时抽出 sim 组合根、接入空间索引。
验收：3 个 seed 各 20k tick 不灭绝、至少一个种群 80% 采样点低于 80% 上限、审计通过、双跑哈希一致。
你负责指挥，子代理优先用 zcode-start-plan/GLM-5.3-Flash；若 Session 拒绝则用 workbuddy/glm-5.3-flash。
子代理链接不稳定，失败重试；任务拆小，你负责审查和关键代码。
```

## 11. 关键文件索引

- 能量：[src/world/Energy.h](src/world/Energy.h)、[src/world/Energy.cpp](src/world/Energy.cpp)
- 世界管线：[src/world/World.cpp](src/world/World.cpp)
- 生产者：[src/producers/Producer.cpp](src/producers/Producer.cpp)、[src/producers/ProducerSystem.cpp](src/producers/ProducerSystem.cpp)
- Agent：[src/agents/AgentSystem.cpp](src/agents/AgentSystem.cpp)、[src/agents/Behavior.cpp](src/agents/Behavior.cpp)
- 决策：[src/decision/Observation.h](src/decision/Observation.h)、[src/decision/RulePolicy.cpp](src/decision/RulePolicy.cpp)
- 统计：[src/app/StatsWriter.cpp](src/app/StatsWriter.cpp)
- 平衡记录：[docs/BALANCE.md](docs/BALANCE.md)
- 架构：[docs/ARCH.md](docs/ARCH.md)
- 路线图：[docs/ROADMAP.md](docs/ROADMAP.md)
