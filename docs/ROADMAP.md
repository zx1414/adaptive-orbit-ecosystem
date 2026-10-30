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
CLI `--html` / `--html-interval` 与 `--trace-json` / `--trace-json-interval`，兼容 `--load`/`--save`；写失败退出码 4。
`--trace-json` 输出前端（`frontend/`，独立维护）可直接消费的纯 JSON；HTML 与 JSON 共用 trace::Buffer。

验收：`tools/m4_acceptance.ps1` 全 PASS（帧数/tick 递增/JSON/无外部 URL/错误码）。

后续可扩展：真实浏览器 UI、服务器推送、多轨迹对比；仍不得复用 v2 快照协议。

## M4.5：设计纠正（已完成）

M1 的 `cbe93a1` 是推倒重来而非渐进解耦，把 v2.3.1 的"蓝细菌-核-小球"生态换成了"静止生产者-独立 Agent"，
丢掉了轨道物理、三型器官、22 项遗传参数与蓝细菌网络。原始设计要求的 `agent_mode` 共存开关从未实现。

决策（用户确认）：**放弃 v2 的 mod 兼容与逐字节基线，不做双模式**，直接实现单模式目标模型。
语义基准是 Git 标签 `legacy-v2.3.1`，移植时逐函数对照。详见 [M5-PLAN.md](M5-PLAN.md)。

## M5：环境层——蓝细菌与阳光

新增 `src/environment`：`CyanoNode`（位置、能量、id、存活、年龄）。
每 tick：光合 → 代谢耗散 → 死亡清理 → 概率繁殖。`nodesNear` 零分配查询，`consumeNode` 返回能量并标记死亡。

**光合是唯一的持续初级注入**（`injectPrimary`）；节点间能量传输零和。复用现有 `world::Environment::lightAt`。
本期不做节点连接网络与聚落。

验收：光合是唯一注入源；节点能量守恒；每 tick 审计通过。

## M6：核与小球——轨道物理与器官

删除 `src/agents` 与 `src/producers`，新增 `src/colony`：

- `Ball{pos, vel, type, ownerId, energy, cap}`，`BallType = Resource/Scout/Shield`。
- `Nucleus{pos, vel, energy, nativeCap, id, age, params, wanderAngle}`。
- `NucleusParams`：精简 8 项（orbitRadius、affinity、maxSpeed、maxAccel、energyThreshold、mutationRate、harvestRate、spotRadius）。

轨道物理写在 `Behavior`：径向弹簧 + 切向驱动 + 球间排斥。**所有球共用核的单一 `orbitRadius`**，
球本身没有个体力学参数，`type` 只决定功能。

能量存在于资源球内：核与球之间零和转账；球脱离只改 `ownerId`，能量随球走，可被任意核拾取；
`colonyCap = nativeCap + Σ(resourceBall.cap)`，超容量立即裁剪并记账。

侦察：每个侦察球独立采样，强度随距离衰减并叠加 PCG32 噪声；核汇总取最显著的目标；无侦察球时外部通道全零。

护盾只保留类型与计数，不参与计算。核间战斗不实现。

验收：单核单球半径收敛到 `orbitRadius` ±20%、切向速度非零；拆卸资源球后容量下降且审计平衡；
带能量的球脱离与被拾取时总能量不变；同 seed 双跑哈希一致。

## M7：决策与遗传

`Observation` 升到 v2（约 24 维）：自身能量比、储能比、三型球计数、最近节点、最近他核、边界、昼夜相位。
`Action` 增加造球；`RulePolicy` 的阈值取自 `NucleusParams`，保持纯函数零随机。
繁殖时子核继承参数并按 `mutationRate` 变异、按 `ParamRanges` 裁剪。

验收：5000 tick 后参数均值出现可测漂移；不同 seed 产生不同轨道特征；无球的核无法长期存活。

## M8：神经网络

新增 NNetPolicy，保持 Observation/Action 契约；加载带维度与版本校验的权重文件。优先神经进化，后续再考虑强化学习。

## M9：扩展接口

若需要 Mod，重新定义只读视图 + 事件 + EnergyLedger 的扩展 API。禁止扩展模块绕过账本或修改决策层内部状态。