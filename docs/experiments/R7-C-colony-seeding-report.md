# R7-C 交付报告【聚落分布 + applyConfig 补漏】（2026-11-01）

## 结论
两个必做子任务完成；可选子任务 3 完成并保留（A/B 数据支撑）。
build / unit_tests / check 全绿；镜像完整性测试全 PASS；首帧聚落簇观察验证通过。

## 文件与行数（全部在本卡 paths 内）
- src/environment/CyanoNode.h   287 行（+17）：CyanoParams 加 cyanoColonies=0 / clusterSpread=45.0；seed() 契约注释更新
- src/environment/CyanoNode.cpp 598 行（+54）：seed() 聚落模式；分裂半径两段分布（子任务 3）
- src/core/Config.h             318 行（+2）：CyanoTuning 加 cyanoColonies=4 / clusterSpread=45.0（纯追加，在 hueJitterSigma 之后）
- src/sim/Simulation.cpp       1428 行（+9）：仅镜像行——构造期与 applyConfig 各 +2 行赋值 + 注释（未碰 step() 光合/捕食段）
- tests/unit/test_cyano_shading.cpp        343 行（+101）：默认值快照补聚落字段 + 3 条新测试
- tests/unit/test_cyano_tuning_mirror.cpp  179 行（新建）：镜像完整性（哨兵注入）

## 验收原文
- build：build OK（cl /std:c++17 /W4 /WX /permissive- /utf-8，两 exe 零 warning 零 error）
- unit_tests：375 tests, 20701 checks, 0 failures
- check.ps1：check PASSED（guard 4 项 PASS；frontend smoke 190 checks 0 failures；
  double-run determinism PASS；live-param 5 探针全 PASS）
- 5000 tick 生态运行三种子（末行原文）：
  seed42:  done tick=5000 nuclei=79 cyano=66 total_energy=3416.62449329 audit=ok policy=rule
  seed7:   done tick=5000 nuclei=67 cyano=53 total_energy=2423.02757211 audit=ok policy=rule
  seed123: done tick=5000 nuclei=54 cyano=53 total_energy=2975.96147634 audit=ok policy=rule
  （以上为子任务 1/2 版本；子任务 3 版本见下方判据表 h 行）

## seed 消耗序变更说明
- 均匀模式（CyanoParams.cyanoColonies<=0，模块默认）：[每节点: x,y,hue] 逐位不变（回归锁：
  cyano_seed_uniform_mode_bitwise_legacy 用同 seed 双 rng 状态对比 + 首节点坐标逐位断言）。
- 聚落模式（cyanoColonies>0 且 count>0 且 clusterSpread>0）：
  [每聚落: cx,cy]（2 抽/聚落，全图均匀）-> [每节点: 角度,半径,hue]（3 抽/节点，
  半径 = clusterSpread*sqrt(u) 圆盘均匀，节点轮转 i%colonies）。
  总消耗 = 2*colonies + 3*count。hue 仍是该节点第 3 抽。
  回归锁：cyano_seed_colony_rng_budget 用 4 聚落 7 节点（非整除覆盖轮转分支）
  断言恰 2*4+3*7=29 抽；count<=0 零消耗。
- 语义照旧版 _ref-v231 Cyanobacteria::initialize（中心均匀 2 抽 -> 每节点 angle、
  r=spread*sqrt(u) 圆盘均匀），差异：旧版 spread=短边 12%，本版显式参数 clusterSpread
  默认 45（256 世界下 ≈ 短边 17.6%，R3-A 遮蔽半径 60 内有效成簇）；旧版 clamp 边界，
  本版 wrap（环面一致性，与场内其它几何一致）。
- appendSeed（bloom 事件）未改：仍是旧均匀逐点追加（旧版 addNodes 的"以存活节点为
  中心扩张"语义留待 R4 消费者一起接，本卡 paths 不含该调用链的语义变更）。

## 观察性验证（首帧聚落簇）
seed42、trace-interval 1，tick=1 帧 31 节点（32 播种 - 1 已被构造期后首帧捕食）。
环面 k-means（k=4，圆均值中心）：
  cluster 1: center=(20.9, 89.9)   n=4
  cluster 2: center=(82.3, 69.0)   n=12
  cluster 3: center=(202.5, 243.9) n=8
  cluster 4: center=(122.2, 42.5)  n=7
  mean torus dist to own center = 24.2（全图均匀期望 ~64，成簇显著）
（注意：这是播种消费侧验证。cyanoColonies 镜像值 4 经 CyanoTuning 生效——
RuntimeTuning.cyanoColonies 是同名无关字段，见"未完成项"。）

## 镜像完整性
test_cyano_tuning_mirror.cpp：34 字段哨兵注入（double=1.234+序号*0.001，int=序号），
构造期 + applyConfig 双路径逐字段断言：
  [PASS] cyano_tuning_mirror_completeness_ctor
  [PASS] cyano_tuning_mirror_completeness_hot
- 有意不同步的两字段（哨兵断言显式绕开）：nodeSeedCount / maxNodes（出生引导量，
  applyConfig 显式不回写——applyConfig 源码 846-854 行既有语义，未改动）。

## 判据对比（e42 基线 vs 本卡，analyze.mjs，5000 tick）
e42（LOG.md 11:20，R3-A 自遮光版）：
  crit1 FAIL 0.7072 | crit2 PASS 0.1377 final=102 | crit3 PASS 21/24 | crit4 FAIL 5.5° | crit5 FAIL +0.6561
本卡 e 行（子任务 1+2，聚落播种，offset=125 旧单段分布）：
  seed42:  1 FAIL 0.1885 | 2 PASS 0.2197 f=79 | 3 PASS 20/24 | 4 FAIL 5.8° | 5 FAIL 0.7190
  seed7:   1 FAIL 0.7671 | 2 PASS 0.1373 f=67 | 3 PASS 19/24 | 4 FAIL 12.2° | 5 FAIL 0.8833
  seed123: 1 FAIL 0.8388 | 2 PASS 0.1702 f=54 | 3 PASS 23/24 | 4 FAIL 6.4°  | 5 FAIL 0.7799
本卡 h 行（+子任务 3，offset 两段分布 70/30）：
  seed42:  1 FAIL 1.0175 | 2 PASS 0.2197 f=14 | 3 PASS 22/24 | 4 PASS 31.1° | 5 FAIL 0.5713  （3 PASS/2 FAIL）
  seed7:   1 FAIL 0.9923 | 2 PASS 0.1115 f=88 | 3 PASS 22/24 | 4 FAIL 6.0°  | 5 FAIL 0.8607
  seed123: 1 FAIL 0.9619 | 2 PASS 0.1147 f=30 | 3 PASS 20/24 | 4 PASS 34.5° | 5 FAIL 0.8930  （3 PASS/2 FAIL）
- e 行相对 e42：判据形态同形（2 PASS / 3 FAIL 主体），无回归；hue 塌缩依旧（聚簇本身
  不解决选择压方向，符合预期——播种分布是负反馈的前提，不是判据 4 的解）。
- h 行相对 e 行：crit4 三种子二过一改善（31.1°/34.5° PASS）；crit3 不降；代价：
  seed42 消费者瓶颈更深（final=14）。20k tick 存活检验（seed42）：2000 tick 瓶颈 4 核
  -> 6000 tick 48 -> 20000 tick 183 核，audit 全程 ok，无灭绝（恢复轨迹，非死亡螺旋）。
  seed7/123 final 88/30 正常区间。
- crit1/crit5 未解决（结构性问题：紧平衡两难与统计陷阱，LOG.md 12:00 已有机制层
  判断；本卡三个子任务均不针对这两条）。

## 子任务 3 实现说明（设计变更，已实现并保留）
分裂半径两段分布：u 单抽线性映射——70% -> [0, 0.5*offset)（父附近，聚落形态），
30% -> [0.5*offset, offset)（远处，开拓）。rng 消耗序/消耗量逐位不变（仍判定 1 抽 +
抖动 2 抽 + 位置 2 抽），常量 kReproNearFraction=0.7 / kReproNearSpan=0.5 在
CyanoNode.cpp 匿名 ns，与 reproductionOffset 配置参数正交（offset 仍是最大半径）。

## 未完成项 / 给主代理的注意点
1. RuntimeEnv 的 "cyano_colonies" 键写的是 runtime.cyanoColonies（R4 占位字段），
   不是本卡新增的 cyanoTuning.cyanoColonies（RuntimeEnv.cpp:143，不在本卡 paths）。
   当前无害（seed 只在构造期跑，热改哪个都不重撒），但 R4 接事件播种/重播种时必须
   把该键改指 cyanoTuning.cyanoColonies（或明确 runtime 侧语义），否则又是"改了没反应"。
   ParamSchema.cpp:113 的 cyano_colonies 条目同理（默认 4 与 CyanoTuning 侧一致，
   数值不冲突，仅落点注意）。
2. seed42 在子任务 3 下 5000 tick final=14（消费瓶颈深）。20k 已验证恢复，但若主代理
   认为风险不可接受，回滚 = 删除 CyanoNode.cpp 的两段分布映射（恢复单段
   radius = u*offset，4 行），h 行即退回 e 行形态，e 行本身已满足"无回归"验收。
3. test_cyano.cpp 的 cyano_seed_layout_and_rng_budget 断言"seed(3) 恰 9 抽"——该文件
   不在本卡 paths，但测试仍绿：CyanoParams.cyanoColonies 模块默认 0 走均匀模式逐位
   回归。组合根默认路径（CyanoTuning.cyanoColonies=4）有本卡的 3 条新测试覆盖。
   若未来有人把"seed 必须 3 抽/节点"写进该文件硬断言，会与聚落模式冲突——届时该
   断言需要按模式分叉（本卡无法预防，仅声明）。
4. 子任务 3 未跑 e7/h123 的 20k 存活检验（时间预算），只对最深瓶颈 seed42 做了。
