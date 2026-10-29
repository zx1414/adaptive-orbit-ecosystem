# 路线图

## M0：架构骨架

目录、模块边界、构建脚本、无第三方测试框架。已完成设计。

## 架构红线（所有里程碑）

世界 → 探测 → 决策 → 行为，严格单向：

- `src/world` + `src/sim`：世界状态与只读查询。
- `src/perception`：只构建 Observation，不调用策略、不使用 RNG。
- `src/decision`：只读 Observation、输出 Action，不读取世界、不使用 RNG。
- `src/behavior`：唯一写 Agent 状态的层，能量必须走 EnergyLedger。
- `src/agents`：被动实体与生命周期，不得塞入探测/决策/行为逻辑。

任何里程碑都不得把四层合并，或让探测/决策绕过值对象直接访问对方。

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

## M3：持久化（已完成）

版本化二进制快照（magic/version/长度/FNV-1a 校验和）：保存 SimConfig、tick、PCG32 全状态、
EnergyLedger 全部统计与 sealed、Producer/Agent 的 id/位置/速度/能量/cap/age、nextId、births/deaths、
策略 spec.id/genome 与预留 policy state。读档不重新注入初始能量，恢复后立即审计。

CLI `--save` / `--load`；损坏、截断、版本不匹配返回退出码 5，load + 配置覆盖返回 2。
`stateHash` 扩展覆盖 policy spec id/版本/维度、cap、age、nextId、births/deaths 与完整账本统计。

验收：seed 42/7 的 20k 直跑与 10k 存档 + 10k 续跑 stateHash、总能量、种群、注入/耗散、audit 全部一致；
`tools/m3_acceptance.ps1` 全 PASS；单元测试 30 个。

## M4：可视化（最小版本已完成）

app::HtmlTraceWriter 只读 WorldView，按 `--html-interval` 采样，生成单文件 HTML：
内嵌 JSON、Canvas 回放、播放/暂停/滑块、tick/种群/总能量；无外部 URL/脚本，离线可打开。
CLI `--html` / `--html-interval`，兼容 `--load`/`--save`；写失败退出码 4。

验收：`tools/m4_acceptance.ps1` 全 PASS（帧数/tick 递增/JSON/无外部 URL/错误码）。

后续可扩展：真实浏览器 UI、服务器推送、多轨迹对比；仍不得复用 v2 快照协议。

## M5：神经网络

新增 NNetPolicy，保持 Observation/Action 契约；加载带维度与版本校验的权重文件。优先使用神经进化，后续再考虑强化学习。

## M6：扩展接口

若需要 Mod，重新定义只读视图 + 事件 + EnergyLedger 的扩展 API。禁止扩展模块绕过账本或修改决策层内部状态。