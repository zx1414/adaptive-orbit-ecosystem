# EcoCore v3 架构

这是一个从零建立的 C++17 单线程生态模拟核心，不包含旧版 Ball、Nucleus、World、存档、Mod 或二进制快照接口。

旧版 v2.3.1 的架构与计划文档保留在 [docs/archive/](archive/README.md)，完整源码在 Git 标签 `legacy-v2.3.1`；v3 构建不引用它们。

## 模块

- **core**：值类型、向量与边界数学、PCG32 随机、时间、配置、事件。
- **decision**：定长 Observation、Action 和 IPolicy。规则、占位策略以及后续神经网络实现都走同一接口；决策不可读取世界，不可使用 RNG。
- **world**：能量账本、环境、空间索引、WorldQuery/Forage 接口与只读 WorldView；不拥有实体容器。
- **producers**：Producer 抽象基类与 PhotoProducer。生产者将光照转化为生物量；局部光竞争与密度依赖繁殖由邻居查询驱动。
- **agents**：Agent、Sensors、Behavior 与 AgentSystem。Sensors 只构造观测；Behavior 是行为执行的唯一位置，通过 WorldQuery 读取世界、通过 Forage 按 id 收获；寿命与密度依赖繁殖在 AgentSystem 中结算。
- **sim**：Simulation 组合根，持有 Environment、EnergyLedger、Random、ProducerSystem、AgentSystem，实现 WorldQuery + Forage，并负责每 tick 管线、审计与状态哈希。
- **app**：命令行、控制台展示与 CSV 统计导出，不包含模拟规则。

依赖方向：core ← decision；core ← world；world ← producers/agents（只依赖 world 的接口与账本）；sim → world/producers/agents；app → sim。agents/producers 不 include sim，决策层禁止反向依赖 world。M2.5 已删除 world::World，组合根正式位于 sim 层。

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

## 当前 M2.5 范围

- sim::Simulation 组合根；world/agents/producers 只保留接口与系统，不存在编译期环。
- 生产者局部光竞争：`shading = 1 / (1 + crowding * localDensity + competition * localBiomass / cap)`。
- 生产者与 Agent 的密度依赖繁殖阈值；高密度先抑制出生，而不是等 max 截断。
- Agent 饱和收获：`harvest * food / (food + halfSaturation)`，并保留 `harvestReserve`。
- 所有感知/收获/邻居查询接入 SpatialIndex；Sensors 只取最近两个槽位，复用 scratch 缓冲。
- 默认 max 仍为 300/200；标定配置用 1000 作为安全阀，平衡点由资源与密度决定。
- 单元测试 19 个，集成冒烟与 tools/acceptance.ps1；平衡结果见 BALANCE.md，设计见 M2.5-DESIGN.md。

后续里程碑见 ROADMAP.md。
