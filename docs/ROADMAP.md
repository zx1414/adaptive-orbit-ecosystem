# 路线图

## M0：架构骨架

目录、模块边界、构建脚本、无第三方测试框架。已完成设计。

## M1：最小生态闭环

能量账本、零产能环境、PhotoProducer、Agent、Sensors、Behavior、RulePolicy、StubPolicy、World 管线、CLI、测试。

验收：能量守恒、零生产者灭绝、策略替换、确定性、无 NaN、MSVC /W4 零警告。

## M2：进化与统计（统计/参数化完成）

将策略基因组、繁殖和变异参数化；导出种群与基因趋势；对光合率、繁殖阈值、代谢与种群上限做平衡标定。

已完成：CSV 统计（种群、能量、出生/死亡、基因均值、审计）、CLI 平衡参数、可选局部遮荫/寿命/猎物保留量。

平衡结论：默认基线在 20k tick 内稳定存活但触及种群上限；遮荫+寿命在无猎物保留量时 13k–20k tick 内崩溃；reserve=10 的推荐配置在 50k tick 内稳定。详见 docs/BALANCE.md。

## M2.5：资源竞争（已完成）

已完成：

- 生产者局部光竞争：局部生物量除以容量后进入 `1 / (1 + crowding * density + competition * biomass)`。
- 生产者与 Agent 的密度依赖繁殖阈值；高密度先抑制出生，不再依赖 max 截断。
- Agent 饱和收获（halfSaturation）并保留 `harvestReserve`。
- 组合根抽到 `sim::Simulation`，删除 `world::World`；agents 只依赖 WorldQuery/Forage。
- SpatialIndex 接入感知、收获、邻居与密度查询；20k 默认耗时约 16 秒。

验收：seed 42/7/123 各 20k 不灭绝，审计逐 tick 通过，5k 预热后两个种群 100% 采样点低于 0.8×1000；
同 seed 双跑哈希一致；seed 42 50k 耐久通过。结果见 docs/BALANCE.md。

## M3：持久化

新版本存档与快照；保存实体 ID、策略类型、基因组、策略内部状态和 PCG32 状态；验证读档续跑与不中断运行一致。

## M4：可视化

基于 WorldView 的新展示层或 Web UI。不复用旧 v2 快照协议。

## M5：神经网络

新增 NNetPolicy，保持 Observation/Action 契约；加载带维度与版本校验的权重文件。优先使用神经进化，后续再考虑强化学习。

## M6：扩展接口

若需要 Mod，重新定义只读视图 + 事件 + EnergyLedger 的扩展 API。禁止扩展模块绕过账本或修改决策层内部状态。
