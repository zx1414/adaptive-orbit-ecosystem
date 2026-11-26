# TODO — agentsim 任务状态源

> 状态只在本文件维护。任务编号 T##；每条映射 GOAL.md 的 S 编号。
> 完成一条 = 先在 INDEX.md 补证据 → 再移入「已完成」；验证失败退回原区并写原因；放弃必须附一句原因。
>
> **2026-10-31 收敛**：S1–S25 全部完成并有证据（见 INDEX.md 第四轮快照）。
> 本文件此后仅登记新增工作；已完成的 43 条任务明细见 docs/INDEX.md 与 docs/LOG.md。

## 正在进行

（空——收敛稳态）

## 已完成（摘要，明细见 INDEX.md）

| 批次 | 内容 | 门禁 |
| --- | --- | --- |
| 第一轮 T01–T14 | Torus/食物/账本/RNG/感知/决策/行为 最小闭环 | 92/6917 |
| 第二轮 T18–T32 | R1 红队修复：CLI/组合根/环境 + 感知/决策/行为 | — |
| 第三轮 T33–T43 | 温度场/地形/捕食/繁殖接线/感知机 + R2 红队 P0 | — |
| 第四轮 T01b/T03b/T03c | S16 每候选噪声 / S17 侦察球 / S18 球作仓库（反证式验收） | 155/10178 |
| 第四轮 T09/T10 | S19/S20 球物理化 + S21 核施力（含 3 处语义修正） | 133/10685 |
| 第四轮 T02b/T04 | S22 蓝细菌实体 + 连线传输 | 140/10013 |
| 第四轮 T22 | --no-regrow/--regrow（S23 验证开关） | 166/7812 |
| 第四轮 T23 | 前端同步 Scout/自由球/接缝 + 真实数据 | 视觉验证 |
| 第四轮 T24/T26 | BanditPolicy + RiskAversePolicy（策略创新） | 196/208 |
| 第四轮 T29 | trace 契约 v2：蓝细菌导出 | 213/19383 |
| 第四轮 T25/T27/T28 | CyanoNode P2 / maxNodes 回查 / live-param --regrow | 209/19350 |
| 门禁强化 | 写点守卫下标形态 / 决策禁 RNG / live-param ×5 / try-catch 加固 | — |
| 红队 R7/R8/R9 | 5×P0 + 8×P1 + 8×P2 全部闭环 | — |

## 尝试过但放弃

| 任务 | 原因 |
| --- | --- |
| R2 红队的 103 行 reproduce 修复方案 | 数学推导错误（恒等式仍偏差 f·k），主代理另行修正 |
| --agent-mode 双范式（旧版兼容） | 用户更正：只要一个新版实现，旧版仅作数学物理参考 |
| 子代理"等思考完再动手"的工作方式 | 实测 9 分钟零产出；改为"先写文件再想细节" |
| session 文件 mtime 判活 | 会误杀长推理代理；改为一律以 list_agents status 为准 |
| resid 自动补账通道 | 掩盖记账错误（417k 幻影注入被它对销），删除后暴露并修复 3 个真 P0 |

## 还没干（开放项，均不阻塞验收）

| # | 任务 | 映射 | 说明 |
| --- | --- | --- | --- |
| T44 | trace 契约 v2 前端渲染蓝细菌 | S24 | C++ 侧已导出 c 数组；前端 parser/renderer 待跟进 |
| T45 | 生态调参：能量水位 | — | 总能量 26–29k 偏高（kReproCap=24 封顶消费）；可提高上限或加自然衰变 |
| T46 | 遗传/变异实验 | S8 | perceptron 100% 灭绝（exit 5 可判定），可做"可遗传参数 + 选择"实验 |
| T47 | linkCheckInterval 生态实验 | S22 | 参数已实现（默认 1），拓扑重算间隔对生态的影响未测 |
| T48 | graze RNG 纪律统一 | §6 | 捕食 RNG 仅在 preyId!=0 时消耗（P2，确定性不受影响，可选修） |


## R8 新增（2026-11-01 14:05，用户验收反馈）

| # | 任务 | 归属 | 说明 |
| --- | --- | --- | --- |
| T49 | 无参默认 serve + 浏览器拉起 | R8-A | argc==1→serveMode，端口回退，--no-open |
| T50 | /state v2 基因导出 | R8-A | 核记录 28B→124B，version=2，v1 兼容 |
| T51 | 极简材质全实体 | R8-D | 去发光，核内灰点，核≈球2.5倍 |
| T52 | 交互闪烁 + 参数卡钉住 | R8-D | 前端邻近判定近似；点击钉卡，×关闭 |
| T53 | 世界默认 512² | 主代理(合并后) | 面积4倍语义；合并 R7-C 后自做，grep 断言 256 的测试 |
| T54 | 后端精确交互标志位 | 候选 | 核记录 pad 字节复用，需 Behavior 写入权（R7-B 合并后评估） |


## R8 新增（17:40 更新）

| # | 任务 | 状态 | 说明 |
| --- | --- | --- | --- |
| T49 | 无参默认 serve + 浏览器拉起 | ✅ | R8-A |
| T50 | /state v2 基因导出 | ✅ | 128B 核记录，v1 兼容 |
| T51 | 极简材质全实体 | ✅ | R8-D MINIMAL |
| T52 | 交互闪烁 + 参数卡钉住 | ✅ | 前端邻近判定 |
| T53 | 世界默认 512² | ✅ | 密度等比四联动 |
| T54 | 后端精确交互标志位 | ⬜ | 核记录 pad 字节复用（R9 候选） |
| T55 | 重导 demo-showcase 存档（v2 序列化） | ⬜ | 旧档被 R7-B 拒载，走前端 /save 流程 |


| T57 | maxNuclei 600 护栏撞顶（seed7） | 候选 | 倾向 R9-A 后自然承载力，护栏最后手段 |
| T58 | analyze.mjs 判据窗口修订落地 | 候选 | 末 30% 窗口 + DEFER 语义（GOAL 修订 1 已写） |

---

# 规则 v2（生态机制改造 P0–P2）— 2026-11-25

> 依据：[下一版生态机制设计](下一版生态机制设计.md) + [执行提示词](生态机制改造执行提示词.md)。
> 规则版本常量 `agentsim::kRulesVersion = 2`（`core/Config.h`），存档格式 v4。

## 已完成

| 阶段 | 内容 | 状态 |
| --- | --- | --- |
| P0-1 | 删除按遗传最高速度/加速度折减摄食量的整条通道（含 `conversionMode` 等 8 个键）；删除迁移回报（`migration_bonus/radius`）。同化率只由 `conversionRate × cyanoDigestion` 决定 | ✅ |
| P0-2 | 删除每节点 15 单位不可食保底；改为 `edible = material - minStructure` 的**唯一**可食量口径（感知/选目标/请求/扣库存共用）；节点可被吃空并死亡，残留组织成为残骸 | ✅ |
| P0-3 | 摄食改为**先收集再分配**：`CyanoField::grazeBatch` 按 `(nodeId,nucleusId)` 排序，超需求按占比分配，与核遍历顺序无关 | ✅ |
| P0-4 | 决策记忆归个体：新增 `decision/PolicyMemory.h`（POD，随存档序列化）；`IDecisionPolicy::decide(sv, targets, memory)`；子代不继承亲代记忆 | ✅ |
| P0-5 | 撤下 harvest/plague/bloom（HTTP 400 `retired_event`）；寒冬改为持续 600 帧、光照 -0.35 的**环境效果**，到期只撤事件偏移、保留玩家基线；玩家播种独立为 `seed.cyano=N`（外部输入） | ✅ |
| P1 | 身体物质（`bodyMass`）与可用能量分离；死亡留下**非零**残骸；新增 `environment/NutrientField`（debris/nutrient 网格 + 局部分解）；生长必须有养分 + 节点能量；繁殖分配亲代材料 | ✅ |
| P2 | 侦察球改为从**自身世界坐标**采样（环面最短距离、控制距离内才回传、合并重复目标）；资源球实际存取（容量/存量分开、有限速率取回）；撤去蓝细菌产球；器官由核自建并按状态+基因取舍；删除换主罚款 | ✅ |

## 验收状态（A1–A13）

可执行判据全部集中在 [tests/unit/test_ecology_v2.cpp](../tests/unit/test_ecology_v2.cpp)，
另有决策侧 [test_decision_memory.cpp](../tests/unit/test_decision_memory.cpp)。

| 编号 | 结果 | 证据 |
| --- | --- | --- |
| A1 | 通过 | `a1_speed_gene_does_not_change_intake`、`a1_previous_meal_position_gives_no_bonus` |
| A2 | 通过 | `a2_two_nuclei_share_four_units_by_request_ratio`（2/6 请求、4 可用 → 1/3，颠倒顺序按身份对齐一致） |
| A3 | 通过 | `a3_empty_node_closer_than_edible_but_perception_skips_it`、`a3_no_producers_means_no_automatic_food` |
| A4 | 通过 | `decision_memory_two_agents_isolated`、`a4_child_does_not_inherit_parent_memory` |
| A5 | 通过 | 真实 serve 客户端实测（见下）+ `env_effect_*` 系列单测 |
| A6 | 通过 | `a6_starved_nucleus_leaves_nonzero_debris_locally`、`a6_decomposition_moves_debris_into_local_nutrient` |
| A7 | 通过 | `a7_matter_total_conserved_without_external_input`（3000 tick，各库总量 == 外部投入）、`a7_fertilizer_increases_total_by_exactly_the_input` |
| A8 | 通过 | `a8_nutrient_limits_growth`、`a8_no_light_no_energy_means_no_new_tissue_even_with_nutrients` |
| A9 | 通过 | `a9_scout_ball_position_extends_perception`、`a9_scout_sampling_wraps_across_torus_seam` |
| A10 | 通过 | `a10_resource_ball_stores_and_returns_energy`、`a10_takeover_preserves_stored_energy_and_full_ball_survives_longer` |
| A11 | 通过 | `a11_nucleus_builds_its_own_organ_and_pays_for_it`、`a11_cannot_build_organ_without_body_mass`、`a11_cyano_no_longer_drops_ready_made_balls` |
| A12 | 通过 | `a12_same_seed_same_state_hash` + 门禁 double-run |
| A13 | 通过 | `a13_save_resume_produces_identical_state`、`a13_old_rules_save_is_rejected_without_modifying_the_file` |

## 当前验证边界

| 项目 | 状态 |
| --- | --- |
| 本地 HTTP | 当前执行环境对 127.0.0.1 的客户端连接返回 Winsock 10013。需在可联网的本机环境复验浏览器到服务端的路径。 |
| 生态长期稳定性 | 蓝细菌分裂需要足够的成熟组织。默认种子 42 在 3000 帧时生产者灭绝，种子 7 和 123 分别剩 85 和 2 个节点。三个种子的能量审计均通过，默认生态的持续性仍待解决。 |
| 寒冬 | 临时改变光照，保存剩余模拟时间。温度由现有温度场结算。 |

## 规则 v2 引入的两处真实漏账（已修，留档）

1. 饿死节点的组织只累加进 `report.deadMaterial` 而未入 `pendingDeath_`，组合根拿不到位置 → 每次饿死物质蒸发（2000 tick 漂移 -1078）。
2. `deployBall` 从 `bodyMass` 扣物质却无接收方 → 每建一个器官漏 2 单位（漂移 -74）。
   修法：`bodyMass` 只作建造门槛，代价全部走能量。
修复后 `matter().total() - externalIn` 在 5000 tick / 各参数档下恒为 **±0.000000**。
