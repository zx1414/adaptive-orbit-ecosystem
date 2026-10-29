# EcoCore v3 架构

这是一个从零建立的 C++17 单线程生态模拟核心，不包含旧版 Ball、Nucleus、World、存档、Mod 或二进制快照接口。

## 模块

- **core**：值类型、向量与边界数学、PCG32 随机、时间、配置、事件。
- **decision**：定长 Observation、Action 和 IPolicy。规则、占位策略以及后续神经网络实现都走同一接口；决策不可读取世界，不可使用 RNG。
- **world**：实体容器、空间查询、环境与每 tick 管线。应用层只能读取 WorldView。
- **producers**：Producer 抽象基类与 PhotoProducer。生产者将光照转化为生物量。
- **agents**：Agent、Sensors、Behavior 与 AgentSystem。Sensors 只构造观测，Behavior 是行为执行的唯一位置。
- **app**：命令行与控制台展示，不包含模拟规则。

依赖方向：core ← decision；core ← world；world 提供 Energy、Environment、SpatialIndex、WorldQuery 与 WorldView；World 是 M1 的组合根，持有 ProducerSystem 与 AgentSystem；producers/agents 通过 world 的查询、环境与账本接口参与模拟，不拥有世界状态；app 负责装配与驱动。决策层禁止反向依赖 world。M2 计划把组合根抽到独立 sim 层，消除 world 聚合头对 agents/producers 的编译期依赖。

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

1. 重建生产者与 Agent 空间索引。
2. 按稳定 ID 顺序执行生产者的光合、维持与繁殖。
3. 按稳定 ID 顺序构建 Agent 观测、调用策略、执行动作。
4. 清理死亡实体。
5. 按固定顺序处理 Agent 繁殖与策略变异。
6. 推进 tick，执行能量审计与结束判定。

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

## 当前 M1 范围

- 生产者与 Agent 的最小生态闭环。
- 规则与固定行为两种可替换策略。
- 无头模拟、控制台视图、能量摘要与状态哈希。
- 单元测试与集成验收。

后续里程碑见 ROADMAP.md。
