# EcoCore v3 架构

这是一个从零建立的 C++17 单线程生态模拟核心，不包含旧版 Ball、Nucleus、World、存档、Mod 或二进制快照接口。

旧版 v2.3.1 的架构与计划文档保留在 [docs/archive/](archive/README.md)，完整源码在 Git 标签 `legacy-v2.3.1`；v3 构建不引用它们。

## 总体架构：世界 → 探测 → 决策 → 行为

这是本项目的目标架构，优先级高于具体文件命名；后续重构不得把四层重新揉回一个模块。

```text
┌──────────────────────────────┐
│ 世界 World                    │
│ Simulation/EnergyLedger/      │
│ Environment/SpatialIndex      │
└──────────────┬───────────────┘
               │ WorldQuery / WorldView（只读样本）
               ▼
┌──────────────────────────────┐
│ 探测 Perception               │
│ Sensors::buildObservation     │
└──────────────┬───────────────┘
               │ Observation（值对象，版本 1）
               ▼
┌──────────────────────────────┐
│ 决策 Decision                 │
│ IPolicy::decide               │
└──────────────┬───────────────┘
               │ Action（值对象）
               ▼
┌──────────────────────────────┐
│ 行为 Behavior                 │
│ Behavior::execute             │
└──────────────┬───────────────┘
               │ 唯一的实体写入 + EnergyLedger
               ▼
             世界
```

严格约束：

1. **世界**只通过 `WorldQuery`/`WorldView`/`Forage` 暴露能力；感知与决策不持有可变世界引用。
2. **探测**只把世界样本转成 `Observation`，不调用策略，不使用 RNG。
3. **决策**只读取 `Observation`、返回 `Action`，不读取世界，不使用 RNG。
4. **行为**是唯一写 Agent 运动/能量/死亡的层；能量必须经过 `EnergyLedger`，收获必须按 id 走 `Forage`。
5. 层间只传值对象：`Observation` / `Action`；不传策略指针给探测，不传世界给决策。
6. 同一输入必须产生同一输出；随机只由世界持有的 PCG32 提供。

模块映射：

| 层 | 目录 | 职责 |
| --- | --- | --- |
| 世界 | `src/world` + `src/sim` | world 契约 + Simulation 状态/管线/审计 |
| 探测 | `src/perception` | Sensors::buildObservation |
| 决策 | `src/decision` | IPolicy / RulePolicy / StubPolicy |
| 行为 | `src/behavior` | Behavior::execute，唯一状态写入者 |
| 实体与编排 | `src/agents` | Agent 实体、AgentSystem 生命周期 |
| 生产 | `src/producers` | Producer / PhotoProducer |
| 应用 | `src/app` | CLI、渲染、统计 |

`agents` 不是第五层：它只提供被动实体和生命周期编排；不得把探测或行为逻辑塞回 AgentSystem。

## 模块

- **core**：值类型、向量与边界数学、PCG32 随机、时间、配置、事件。
- **decision**：定长 Observation、Action 和 IPolicy。规则、占位策略以及后续神经网络实现都走同一接口；决策不可读取世界，不可使用 RNG。
- **world**：能量账本、环境、空间索引、WorldQuery/Forage 接口与只读 WorldView；不拥有实体容器。
- **producers**：Producer 抽象基类与 PhotoProducer。生产者将光照转化为生物量；局部光竞争与密度依赖繁殖由邻居查询驱动。
- **perception**：Sensors 的观测构建；只读取 WorldQuery/WorldView，输出 Observation，不调用策略、不使用 RNG。
- **behavior**：Behavior 的动作执行；通过 WorldQuery 读取世界、通过 Forage 按 id 收获，是 Agent 状态的唯一写入者。
- **agents**：Agent 被动实体与 AgentSystem 生命周期编排；不包含探测/决策/行为逻辑。
- **sim**：Simulation 组合根，持有 Environment、EnergyLedger、Random、ProducerSystem、AgentSystem，实现 WorldQuery + Forage，并负责每 tick 管线、审计与状态哈希。
- **app**：命令行、控制台展示与 CSV 统计导出，不包含模拟规则。

依赖方向：core ← decision；core ← world；perception/behavior → world + decision 契约；agents → world/perception/behavior；producers → world；sim → world/producers/agents/perception/behavior/decision；app → sim。perception/behavior 不 include sim/app，决策层禁止反向依赖 world；M2.5 已删除 world::World，组合根正式位于 sim 层。

## 能量不变量

环境只提供几何边界和光照场，不提供能量。没有随机食物生成、环境能量池或死亡回收接口。

持续的生物量注入只能来自生产者的 Photosynthesis。初始化生物量单独记为 InitialEndowment。

所有能量改变通过 EnergyLedger 完成：

1. injectInitial：初始化一次性注入。
2. injectPrimary：仅允许生产者的光合作用。
3. transfer：生产者到 Agent、繁殖等零和转移，受来源余量与目标容量限制。
4. dissipate：代谢、移动、维持与死亡耗散。
5. kill：终止实体，并将剩余能量计入耗散。

每个 tick 验证：

    injected_total - dissipated_total == sum(all living entity energy)

转移不改变系统总能量。审计失败会终止模拟，不会静默忽略。

## 每 tick 管线

1. 生产者系统按稳定 ID 顺序执行光合、维持与繁殖；需要时用空间索引查询邻居，计算局部生物量竞争与密度繁殖阈值。
2. Agent 系统按稳定 ID 顺序构建观测、调用策略、执行动作；观测查询与收获目标都走空间索引，收获通过 Forage 接口按 id 完成。
3. 清理死亡实体，重建生产者与 Agent 空间索引（id→实体映射同步更新）。
4. 按固定顺序处理 Agent 繁殖与策略变异；繁殖阈值可随局部 Agent 密度提高。
5. 推进 tick，执行能量审计与结束判定，更新 WorldView。

Sensors 和 IPolicy::decide 均不使用 RNG。所有随机来自世界持有的 PCG32。

## Observation v1（28 维）

`decision::obs` 定义字段下标，`Observation::version == kObservationVersion`。策略必须拒绝版本不匹配的观测。

| 下标 | 含义 |
| --- | --- |
| 0 | 自身能量 / 容量 |
| 1 | 速度 / 最大速度 |
| 2–5 | 到 minX/maxX/minY/maxY 的距离 / 感知半径 |
| 6 | 光照 × 0.5 |
| 7 | sin(昼夜相位) |
| 8–11 | 最近生产者：dx/感知半径、dy/感知半径、距离/感知半径（空槽为 -1）、能量/容量 |
| 12–15 | 第二近生产者，字段同上 |
| 16–19 | 最近其他 Agent：dx、dy、距离（空槽为 -1）、相对能量 |
| 20–23 | 第二近其他 Agent，字段同上 |
| 24 | 感知半径内 Agent 数 / 4 |
| 25–26 | sin/cos(昼夜相位) |
| 27 | 保留，v1 恒为 0 |

空槽距离使用 -1 哨兵，因此“距离为 0 的有效目标”不会被误判为空。新增字段必须提升版本号并同步更新所有策略。

## 存档 / 快照（M3）

快照格式为版本化二进制，固定 32 字节头：`magic "ECOCORE3"`、格式版本、头长度、payload 长度、
FNV-1a 校验和；payload 使用手写 little-endian 读写，不依赖主机字节序或第三方库。

保存内容：

- SimConfig 全字段（world/light/producer/agent/policy）。
- tick、生产者/Agent 的 nextId、births、deaths。
- PCG32 的 state/increment/cachedNormal/hasCachedNormal。
- EnergyLedger 的 injected/dissipated/transferred、10 类 reason 的 in/out、sealed 标志。
- 生产者：id、kind、位置、energy、cap。
- Agent：id、位置、速度、energy、cap、age、策略 spec.id/版本/维度、genome、预留 policy state。

读档路径：

1. 校验头、版本、长度、校验和；任何损坏立即返回错误（CLI 退出码 5）。
2. 解析并做语义校验：有限值、cap>0、energy∈[0,cap]、id 唯一、nextId 大于已有 id、
   策略 id 可创建且 spec/genome 匹配、账本已封存。
3. 构造未初始化的 Simulation，直接替换实体、账本、RNG、tick；
   **不调用 initialize/injectInitial**。
4. 重建空间索引与 WorldView，立即执行能量审计；审计失败拒绝加载。

`Simulation::stateHash` 已覆盖 policy spec.id/版本/维度、energy cap、age、nextId、births/deaths、
账本 transferred 与全部 reason 统计，作为续跑一致性的比较依据。

## 可视化（M4）

app::HtmlTraceWriter 只读 WorldView，按 `--html-interval` 采样帧（上限 2000），运行结束生成
单个 UTF-8 HTML：内嵌 `const TRACE = {...}` JSON、Canvas、播放/暂停/滑块、tick/种群/总能量。
生产者绿色、Agent 橙色，亮度随 energy/cap；页面无外部 URL/脚本/字体，可双击离线打开。

JSON 数据源：`--trace-json <path>`、`--trace-json-interval <n>`（>0，默认 100），复用同一个
trace::Buffer 序列化，输出 `{name, meta, bounds, frames}`；前端 `frontend/` 直接消费该文件。

CLI：`--html <path>`、`--html-interval <n>`（>0，默认 100）；与 `--load`/`--save` 兼容。
构造时探测输出可写，失败立即退出 4；帧满 2000 时等步长降采样并保留末帧，stdout 输出
`html_frames`/`downsampled`。参数错误退出码 2。该层只依赖 WorldView，不修改模拟状态。

## 当前 M4 范围

- sim::Simulation 组合根；world/agents/producers 只保留接口与系统，不存在编译期环。
- 生产者局部光竞争：`shading = 1 / (1 + crowding * localDensity + competition * localBiomass / cap)`。
- 生产者与 Agent 的密度依赖繁殖阈值；高密度先抑制出生，而不是等 max 截断。
- Agent 饱和收获：`harvest * food / (food + halfSaturation)`，并保留 `harvestReserve`。
- 所有感知/收获/邻居查询接入 SpatialIndex；Sensors 只取最近两个槽位，复用 scratch 缓冲。
- 默认 max 仍为 300/200；标定配置用 1000 作为安全阀，平衡点由资源与密度决定。
- M3 快照：--save/--load、完整状态恢复、损坏/版本拒绝；10k 存档 + 10k 续跑 == 20k 直跑。
- M4 最小可视化：--html 单文件 Canvas 轨迹；--trace-json 纯 JSON 前端数据源；无服务器/第三方依赖。
- 单元测试 30 个，集成冒烟、tools/acceptance.ps1、tools/m3_acceptance.ps1、tools/m4_acceptance.ps1；
  平衡结果见 BALANCE.md，M3/M4 设计见 M3-PLAN.md、M4-PLAN.md。

后续里程碑见 ROADMAP.md。