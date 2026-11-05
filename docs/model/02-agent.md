## 个体（核）层、行为层、基因层、账本与主循环

本文件描述 agentsim 中「核（Nucleus）」这一实体的完整数值模型：基因 → 表型的映射、每 tick 的状态更新方程、摄食/繁殖/死亡分支、账本零和恒等式，以及组合根 `Simulation::step()` 的固定步骤序。所有公式均为源码实际算式（含代码中的近似与钳制），常数给出 `文件:行号` 引用。

时间基准：生产路径恒为 $dt = 1.0$（`src/sim/Simulation.cpp:335`），全文所有"每 tick"量即该 $dt=1$ 下的量。空间为环面（torus），世界尺寸默认 $W=H=1024$（`src/core/Config.h:18-19`）。

---

### 1. 基因组：24 个实数参数 + 遗传色相

#### 1.1 参数清单与字段顺序

`Genome` 由 4 个按球类型下标的数组（各 3 项）与 12 个标量组成，共 24 个可遗传参数，另有 1 个不计入参数计数的遗传色相 `hue`（`src/core/Genome.h:82-103`）。球类型下标固定为 0 = Resource、1 = Scout、2 = Shield，`kBallTypeCount = 3`（`src/core/Genome.h:26`）。

| 序 | 字段 | 含义 | 声明位置 |
|---|---|---|---|
| 1-3 | `affinity[0..2]` | 对应类型球的径向弹簧回拉速率 | `src/core/Genome.h:83` |
| 4-6 | `orbitRadius[0..2]` | 对应类型球的轨道半径偏置 | `src/core/Genome.h:84` |
| 7-9 | `absorbPreference[0..2]` | 回收该类型球时的能量返还倍率 | `src/core/Genome.h:85` |
| 10-12 | `repelStrength[0..2]` | 对应类型球的近距排斥速率 | `src/core/Genome.h:86` |
| 13 | `maxSpeed` | 移动限速 | `src/core/Genome.h:88` |
| 14 | `maxAccel` | 每帧合加速度模长上限；同时是点名施力的加速度上限 | `src/core/Genome.h:89` |
| 15 | `energyThreshold` | 繁殖能量门槛 $k$ | `src/core/Genome.h:90` |
| 16 | `mutationRate` | 个体变异率（自身也可变异） | `src/core/Genome.h:91` |
| 17 | `cyanoPreference` | 捕食意愿（0 = 严格不捕食的哨兵值） | `src/core/Genome.h:92` |
| 18 | `cyanoDigestion` | 消化效率（进转化率与消化冷却） | `src/core/Genome.h:94` |
| 19 | `nucleusSightRadius` | 察觉其他核的社会感知半径 | `src/core/Genome.h:95` |
| 20 | `nucleusPersonalSpace` | 个人空间半径（小于它对邻核施斥力） | `src/core/Genome.h:96` |
| 21 | `nucleusRepelStrength` | 对邻核的斥力强度 | `src/core/Genome.h:97` |
| 22 | `nucleusCohesion` | 聚拢倾向（0 = 独行） | `src/core/Genome.h:98` |
| 23 | `forceRange` | 可施力环距上限 | `src/core/Genome.h:99` |
| 24 | `forceCostK` | 施力代价系数 $k$ | `src/core/Genome.h:100` |
| — | `hue` | 遗传色相 $[0,360)$，不计入 24 参数 | `src/core/Genome.h:102` |

#### 1.2 初始化区间与钳制（Clamp）区间

`GenomeRanges` 为 24 个参数各定义一对 `Init` 区间（均匀初始化）与一对 `Clamp` 区间（变异后的合法域）。**代码中不存在 gamma 映射或指数映射**：24 个基因值均为线性量纲，直接作为物理参数进入算式（落地位置见 §1.5）；唯一的非线性表型映射是轨道半径偏置 $0.5+\text{orbitRadius}/240$（§3.5）。

| 参数 | Init 区间 | Clamp 区间 | 源码位置 |
|---|---|---|---|
| `affinity[i]` | $[0.1,\ 1.0)$ | $[10^{-4},\ 10.0]$ | `src/core/Genome.h:31-32` |
| `orbitRadius[i]` | $[30,\ 200)$ | $[10,\ 300]$ | `src/core/Genome.h:33-34` |
| `absorbPreference[i]` | $[0.5,\ 1.5)$ | $[0.1,\ 3.0]$ | `src/core/Genome.h:35-36` |
| `repelStrength[i]` | $[0.5,\ 3.0)$ | $[0.1,\ 6.0]$ | `src/core/Genome.h:37-38` |
| `maxSpeed` | $[30,\ 100)$ | $[5,\ 300]$ | `src/core/Genome.h:41-42` |
| `maxAccel` | $[1,\ 20)$ | $[0.5,\ 50]$ | `src/core/Genome.h:43-44` |
| `energyThreshold` | $[80,\ 200)$ | $[20,\ 1000]$ | `src/core/Genome.h:45-46` |
| `mutationRate` | $[0.01,\ 0.2)$ | $[0.001,\ 0.5]$ | `src/core/Genome.h:47-48` |
| `cyanoPreference` | $[0.0,\ 1.0)$ | $[0.0,\ 1.0]$ | `src/core/Genome.h:51-52` |
| `cyanoDigestion` | $[0.1,\ 1.0)$ | $[0.05,\ 1.0]$ | `src/core/Genome.h:53-54` |
| `nucleusSightRadius` | $[40,\ 300)$ | $[10,\ 600]$ | `src/core/Genome.h:57-58` |
| `nucleusPersonalSpace` | $[15,\ 80)$ | $[5,\ 200]$ | `src/core/Genome.h:59-60` |
| `nucleusRepelStrength` | $[0.5,\ 6.0)$ | $[0.1,\ 20.0]$ | `src/core/Genome.h:61-62` |
| `nucleusCohesion` | $[0.0,\ 3.0)$ | $[0.0,\ 10.0]$ | `src/core/Genome.h:63-64` |
| `forceRange` | $[30,\ 150)$ | $[10,\ 400]$ | `src/core/Genome.h:67-68` |
| `forceCostK` | $[0.005,\ 0.05)$ | $[10^{-4},\ 1.0]$ | `src/core/Genome.h:69-70` |
| `hue` | $[0,\ 360)$ | 环绕到 $[0,360)$（不钳制） | `src/core/Genome.h:73-74` |

上表为 `GenomeRanges` 的结构默认值；实际传入的 `ranges` 来自 `ColonyConfig::genomeRanges`（`src/core/Config.h:82`），可经资源包参数 `affinity_*` / `orbit_radius_*` / `absorb_preference_*` / `repel_strength_*` / `max_speed` / `max_accel` / `energy_threshold` / `mutation_rate` / `cyano_preference` / `cyano_digestion` / `nucleus_*` / `force_range` / `force_cost_k` 覆写（键 → 区间映射见 `src/app/RuntimeEnv.cpp:169-240`，同族三类型键写同一对区间）。

#### 1.3 出生初始化

$$x_i \leftarrow \text{InitMin}_i + (\text{InitMax}_i - \text{InitMin}_i)\cdot u,\qquad u \sim U[0,1)$$

`genomeRandomize`（`src/core/Genome.cpp:66-97`）按固定遍历顺序恰好抽取 25 次 `nextDouble()`：4 个数组各 3 次（顺序 `affinity → orbitRadius → absorbPreference → repelStrength`，`src/core/Genome.cpp:70-81`），12 个标量按声明序各 1 次（`src/core/Genome.cpp:83-94`），`hue` 最后 1 次（`src/core/Genome.cpp:96`）。

初始化时**不归一化 affinity**（`src/core/Genome.h:105-109`）。组合根出生时依次调用 `genomeRandomize` 与 `genomeNormalizeAffinity`（`src/sim/Simulation.cpp:314-315`）：

$$\hat a_i = \frac{\mathrm{clamp}(a_i,\ a_{\min},\ a_{\max})}{\sum_{j=0}^{2}\mathrm{clamp}(a_j,\ a_{\min},\ a_{\max})},\qquad \sum_i \hat a_i = 1$$

若钳后总和 $\le 0$ 或非有限，三分量均分 $1/3$（`src/core/Genome.cpp:126-135`）。归一化后单个分量可能低于 `affinityClampMin`（除法所致），故调用序必须是"钳制 → 归一化"，之后不得再钳 affinity（`src/core/Genome.h:116-122`）。钳位函数用 `!(v >= lo)` 同时拦截 $v<lo$ 与 NaN，统一落到下界（`src/core/Genome.cpp:17-21`）。

#### 1.4 变异（只作用于子代）

子代基因由父代拷贝后调用 `genomeMutate`（`src/core/Genome.cpp:137-176`）。对第 $i$ 个参数：

$$\Delta x_i = g_i \cdot \bigl(\rho \cdot (C_i^{\max} - C_i^{\min})\bigr)\cdot \kappa,\qquad g_i \sim \mathcal N(0,1)$$

- $\rho$ = 变异前的 `mutationRate` **快照**，全部 sigma 共用同一值（`src/core/Genome.cpp:144`）；
- $C_i^{\max}-C_i^{\min}$ = 该参数 Clamp 区间宽度（步长与合法域成正比）；
- $\kappa$ = `ColonyConfig::mutationCoefficient`，默认 $0.25$（`src/core/Config.h:79`），参数名 `mutation_coefficient`（`src/app/RuntimeEnv.cpp:135`）。

色相单独处理，**不乘** `mutationRate`：

$$\Delta h = g\cdot \sigma_{\text{hue}}\cdot \kappa,\qquad \sigma_{\text{hue}}=8.0\ \text{度}$$

（`src/core/Genome.cpp:172`； $\sigma$ 定义在 `src/core/Genome.h:74`。）标准正态样本用 Box–Muller，每次恰好消耗 2 次 `nextDouble()`：

$$g = \sqrt{-2\ln u_1}\cdot\cos(2\pi u_2),\qquad u_1 = 1-\text{nextDouble}\in(0,1],\quad u_2=\text{nextDouble}\in[0,1)$$

（`src/core/Genome.cpp:52-57`；取 $1-u$ 以避免 $\ln 0$。）变异收尾固定执行 `genomeClamp` 与 `genomeNormalizeAffinity`（`src/core/Genome.cpp:174-175`）；hue 在 `genomeClamp` 内环绕而非钳制（`src/core/Genome.cpp:118`）：

$$h \leftarrow \mathrm{fmod}(h, 360),\quad \text{若 } h<0\ \text{则}\ h \mathrel{+}= 360$$

**重组/交叉：没有实现。** `genomeMutate` 是唯一的遗传算子，子代基因组 = 父代拷贝 + 加性高斯扰动（`src/core/Genome.cpp:137-176`，唯一调用点 `src/behavior/AgentBehavior.cpp:273`）。变异只作用于子代，亲代基因逐位不动（`src/behavior/AgentBehavior.cpp:250,267-273`）。

#### 1.5 基因 → 表型的落地位置

| 基因 | 生效位置 | 生效算式 |
|---|---|---|
| `maxSpeed` | `apply` 步骤 2 | `effectiveMax = maxSpeed * (1 + 0.5*boost)`（`src/behavior/AgentBehavior.cpp:323-348`） |
| `maxAccel` | 同步骤 2；`applyNamedForce` | 加速度模长上限 `min(maxAccel, bp.forceMax)`（`src/behavior/AgentBehavior.cpp:333-341,675`） |
| `energyThreshold` | 繁殖门槛 | $k=\text{threshold}\times\text{reproductionCostRatio}$（`src/behavior/AgentBehavior.cpp:260-262`） |
| `mutationRate` | 子代变异步长 | §1.4（`src/core/Genome.cpp:144-169`） |
| `cyanoPreference` | 捕食意愿闸 + 每口量 | `pref > 0` 才捕食； $w=\text{pref}\cdot f\cdot E_{\text{prey}}$（`src/sim/Simulation.cpp:478,524-525`） |
| `cyanoDigestion` | 转化率基数 + 消化冷却 | $0.6\times\text{dig}$ 进 `cyanoConversionRate`；冷却 $\lceil 1/(0.5+\text{dig})\rceil$（`src/sim/Simulation.cpp:504-509,533-534`） |
| `orbitRadius[t]` | 球轨道目标半径 | $R^{*}=(10+0.5E)\,(0.5+\text{orbitRadius}[t]/240)$（`src/behavior/AgentBehavior.cpp:482-484`） |
| `affinity[t]` | 径向弹簧速率 | `springRate = 0.3*affinity[t]`（`src/behavior/AgentBehavior.cpp:485`） |
| `repelStrength[t]` | 近距排斥速率 | `repelRate = 0.3*repelStrength[t]`（`src/behavior/AgentBehavior.cpp:486`） |
| `absorbPreference[t]` | 回收返还率 | $\text{give}=\min(E\cdot 0.5\cdot\text{pref},\ E)$（`src/behavior/AgentBehavior.cpp:220-227`） |
| `nucleusSightRadius` | 社会聚拢视野 | $\text{eff}=\text{base}(1+0.4\,\text{mod}[0])$（`src/behavior/AgentBehavior.cpp:713-714`） |
| `nucleusPersonalSpace` | 斥力半径 | $\text{eff}=\text{base}(1+0.4\,\text{mod}[1])$（`src/behavior/AgentBehavior.cpp:715-716`） |
| `nucleusRepelStrength` | 斥力强度 | $\text{eff}=\text{base}(1+0.4\,\text{mod}[2])$（`src/behavior/AgentBehavior.cpp:717-718`） |
| `nucleusCohesion` | 聚拢强度 | $\text{eff}=\text{base}(1+0.4\,\text{mod}[3])$（`src/behavior/AgentBehavior.cpp:719-720`） |
| `forceRange` | 施力距离校验 | `torusDist > forceRange` 即拒绝（`src/behavior/AgentBehavior.cpp:622,669`） |
| `forceCostK` | 施力代价 | $\text{cost}=k\,\lvert a\rvert^{2}$（`src/behavior/AgentBehavior.cpp:629,680`） |
| `hue` | 渲染 / 轨迹导出 | 无物理作用（`src/app/main.cpp:576,1451`） |

#### 1.6 非法基因守卫

`Genome` 是 POD，直接构造或夹具改写都可能产生 NaN/负数。所有消费点统一经过 `geneOr`（`src/behavior/AgentBehavior.cpp:59-61`）：

$$x_{\text{use}} = \begin{cases} x_{\text{gene}}, & x_{\text{gene}} \ge 0\ \wedge\ x_{\text{gene}} \le 10^{300}\\ x_{\text{fallback}}, & \text{NaN / 负值 / } +\infty\end{cases}$$

该守卫把 $0$ 视为合法值。基因非法时回退到 `BehaviorParams` 的退役常数（如 `maxSpeed` → 40、`maxAccel` → `bp.moveK = 60`、`forceCostK` → 0.01）。

#### 1.7 RNG 消耗契约（确定性）

| 操作 | `nextDouble()` 次数 | 位置 |
|---|---|---|
| `genomeRandomize` | 恰好 25 | `src/core/Genome.cpp:45` |
| `genomeMutate` | 恰好 50（25 个 Box–Muller 高斯） | `src/core/Genome.cpp:46` |
| `reproduce` 成功路径 | 52（变异 50 + 出生偏移 2） | `src/behavior/AgentBehavior.cpp:249,280-282` |
| `reproduce` 拒绝路径 | 0（先检查后消耗） | `src/behavior/AgentBehavior.cpp:256-262` |
| 出生位置退避 | 每次尝试 2 | `src/sim/Simulation.cpp:289-290` |

---

### 2. 个体状态变量

#### 2.1 `Nucleus` 结构

| 字段 | 类型 | 默认值 | 语义 | 源码位置 |
|---|---|---|---|---|
| `id` | `EntityId`（uint64） | 0 | 全局唯一实体号，由 `nextNucleusId_` 从 1 起递增分配 | `src/behavior/AgentBehavior.h:105`、`src/sim/Simulation.h:157` |
| `pos` | `Vec2` | {0,0} | 世界坐标，恒在 $[0,W)\times[0,H)$ | `src/behavior/AgentBehavior.h:106` |
| `vel` | `Vec2` | {0,0} | 速度（世界单位/tick） | `src/behavior/AgentBehavior.h:107` |
| `energy` | double | 0（出生 60.0） | 体能，与球能分离计量 | `src/behavior/AgentBehavior.h:108`、`src/sim/Simulation.cpp:309` |
| `cap` | double | 0.0 | 菌落容量，每帧末重算并硬截断 | `src/behavior/AgentBehavior.h:109` |
| `balls` | `vector<Ball>` | 空 | 隶属球，上限 3 | `src/behavior/AgentBehavior.h:110`、`src/behavior/AgentBehavior.cpp:16` |
| `speed` | double | 0 | 上一帧实际速度的缓存 | `src/behavior/AgentBehavior.h:111` |
| `alive` | bool | true | 死亡后置 false，尸体延迟压缩 | `src/behavior/AgentBehavior.h:112` |
| `detached_` | `vector<Ball>` | 空 | 自然脱离交接缓冲，组合根每帧取走 | `src/behavior/AgentBehavior.h:115` |
| `genome` | `Genome` | 见 §1 | 24 参数 + hue | `src/behavior/AgentBehavior.h:120` |
| `effSight / effSpace / effRepel / effCohesion` | double | 0 | 本帧生效社会参数缓存（调试/trace 用） | `src/behavior/AgentBehavior.h:123-126` |
| `digestCooldown` | int | 0 | 消化冷却帧； $>0$ 期间不捕食 | `src/behavior/AgentBehavior.h:130` |
| `carrion_` | `Carrion` | {0,0}, 0 | 死亡时暂存的营养包（30% 遗产） | `src/behavior/AgentBehavior.h:134`、`src/behavior/AgentBehavior.h:98-101` |
| `lastMealPos` | `Vec2` | {0,0} | 上次成功捕食位置（迁移回报判定用） | `src/behavior/AgentBehavior.h:138` |
| `hasMealPos` | bool | false | 是否已有进食点 | `src/behavior/AgentBehavior.h:139` |

**代码中不存在**的字段（需求提及但源码无此概念，如实说明）：

- 核**没有质量、没有半径、没有朝向（heading）**。核的"体型"由标量 `cap`（菌落容量）代表（§3.7）；半径概念只存在于 `Ball`。
- 核**没有谱系编号或亲代 id**：`Nucleus` 无 `parentId` / `generation` 字段，子代只得到新 `id`（`src/sim/Simulation.cpp:563`）。谱系只能靠 `genome` 距离与 `hue` 相似性事后推断。
- 核**没有年龄变量**：无 `age`，也无老死分支（§3.8）。

#### 2.2 `Ball` 结构（核的附属实体）

| 字段 | 默认值 | 语义 | 源码位置 |
|---|---|---|---|
| `id` | 0 | 全局唯一球 id | `src/behavior/AgentBehavior.h:50` |
| `angle` | 0 | 相对核的轨道角（rad， $[0,2\pi)$） | `src/behavior/AgentBehavior.h:51` |
| `radius` | 10.0 | 轨道半径 = 球到核的最小环距 | `src/behavior/AgentBehavior.h:52` |
| `energy` | 0 | 球储能（改它必须走账本） | `src/behavior/AgentBehavior.h:53` |
| `lastOmega` | 0 | 上一帧实际角速度（失控时唯一保留状态） | `src/behavior/AgentBehavior.h:54` |
| `type` | Resource | 0/1/2 | `src/behavior/AgentBehavior.h:55` |
| `cap` | 60.0 | 单球仓库容量（仅 Resource 计入菌落容量） | `src/behavior/AgentBehavior.h:56`、`src/behavior/AgentBehavior.cpp:45` |
| `pos` / `vel` | {0,0} | 世界坐标 / 速度 | `src/behavior/AgentBehavior.h:58-59` |
| `ownerId` | 0 | 宿主核 id；0 = 自由球 | `src/behavior/AgentBehavior.h:60` |
| `r7Lane` | false | 积分车道：true = 世界积分权威，false = 极坐标轨道模型 | `src/behavior/AgentBehavior.h:68` |

球半径（负载半径）恒为

$$R_{\text{load}}(E) = 10 + 0.5\cdot\max(E,0)$$

（`src/behavior/AgentBehavior.cpp:482-483,809`、`src/sim/Simulation.cpp:832,837`）。**这是能量到几何长度的唯一映射，不是球体积/质量关系**：代码中没有 $\tfrac{4}{3}\pi r^{3}$ 之类的密度换算，球半径与储能是线性关系。

#### 2.3 色相继承规则

- 出生： $h=\mathrm{wrap}(360\cdot u)$， $u\sim U[0,1)$，即 $[0,360)$ 均匀（`src/core/Genome.cpp:96`）。
- 子代： $h_{\text{child}}=\mathrm{wrap}\bigl(h_{\text{parent}}+g\cdot\sigma_{\text{hue}}\cdot\kappa\bigr)$， $\sigma_{\text{hue}}=8.0$（`src/core/Genome.cpp:172,118`）。
- 色相**不影响任何物理量**，仅用于前端渲染与 `--trace-genome` 导出（`src/app/main.cpp:576,1451`）。蓝细菌节点有独立的色相继承通道（ $\sigma=20$，`src/core/Config.h:285`），与核色相互不相干。

---
### 3. 每 tick 行为层：`AgentBehavior::apply`

入口签名见 `src/behavior/AgentBehavior.h:206-210`，实现 `src/behavior/AgentBehavior.cpp:287-611`。死核直接返回、不耗能（`src/behavior/AgentBehavior.cpp:291-293`）。代码内部编号 1→7，其中 5 号（进食）整段已删除，6 号拆为 6a/6b/6c。

#### 3.1 步骤 1：写通道扰动

$$A = k_{w}\cdot\frac{s}{1+s},\qquad s = \lvert vel\rvert$$

$$d_x \leftarrow d_x + A\,(2u_1-1),\qquad d_y \leftarrow d_y + A\,(2u_2-1),\qquad u_i\sim U[0,1)$$

（`src/behavior/AgentBehavior.cpp:306-312`） $A$ 对 $s$ 单调递增且上界为 $k_w$（`BehaviorParams::writeNoiseK` $=0.25$，`src/behavior/AgentBehavior.h:159`）； $A=0$ 时一次随机数都不抽。

#### 3.2 步骤 2：移动、限加速、限速、阻尼、环面推进

冲刺意愿 `boost` 先钳到 $[0,1]$（NaN/负值归 0，`src/behavior/AgentBehavior.cpp:320-322`）：

$$v_{\max}^{\text{eff}} = \text{maxSpeed}\cdot(1+0.5\cdot\text{boost}),\qquad \text{boost}\in[0,1]$$

方向单位化（零向量安全）：

$$\hat d = \frac{(d_x,d_y)}{\lVert(d_x,d_y)\rVert}$$

速度意图与社会加速度合成（ $k_{\text{move}}=60.0$）：

$$\tilde v = \hat d\cdot k_{\text{move}} + a_{\text{ext}}$$

**限加速**（上限取基因 `maxAccel`；基因非法时回退到 `bp.moveK` 而非 `bp.maxAccel`，见 `src/behavior/AgentBehavior.cpp:333`）：

$$\lVert \tilde v - v_{t-1}\rVert > a_{\max}\ \Rightarrow\ \tilde v \leftarrow v_{t-1} + (\tilde v - v_{t-1})\cdot\frac{a_{\max}}{\lVert \tilde v - v_{t-1}\rVert}$$

（`src/behavior/AgentBehavior.cpp:331-341`）

**限速**（只缩不放，`src/behavior/AgentBehavior.cpp:342-348`）：

$$\lVert \tilde v\rVert > v_{\max}^{\text{eff}}\ \Rightarrow\ \tilde v \leftarrow \tilde v\cdot\frac{v_{\max}^{\text{eff}}}{\lVert\tilde v\rVert}$$

**阻尼**（`nucleus_damping`，默认 $1.0$，`src/core/Config.h:177`、`src/app/RuntimeEnv.cpp:260`）：

$$v_t = \tilde v\cdot\lambda_{\text{damp}},\qquad \lambda_{\text{damp}}\in[0,1]$$

**位置推进 + 环面 wrap**（地形非空时额外过障碍求解）：

$$p_t = \mathrm{resolveMove}\bigl(p_{t-1},\ \mathrm{wrap}(p_{t-1} + v_t\,dt)\bigr)$$

$$\mathrm{wrap}(x) = \mathrm{fmod}(x,W);\quad \text{负值 } +W;\quad \ge W \text{ 时 } -W$$

（`src/core/Vec2.h:14-27`；`src/behavior/AgentBehavior.cpp:352-360`）`resolveMove` 先在 from→to 线段的 $1/4$、 $1/2$、 $3/4$ 处采样，命中障碍则从被挡点起做 8 次二分回退（`src/environment/Terrain.cpp:119-154`）。

**转向限制的实现方式**：没有显式角速度上限，转向由"每帧合加速度模长 $\le a_{\max}$"间接约束——高速核无法急转，低速核可以（`src/behavior/AgentBehavior.cpp:326-341`）。也没有惯性项： $\lambda_{\text{damp}}=1.0$ 时速度完全由本帧指令决定。

#### 3.3 步骤 3：运动代谢

$$\text{loco} = k_{\text{loco}}\cdot s^{\,\beta}\cdot dt\cdot \mu_{\text{terrain}}$$

- $k_{\text{loco}}$ = `locomotionCost`，默认 $0.0002$（`src/core/Config.h:171`，参数 `speed_cost_k`）；
- $\beta$ = `speedCostExponent`，默认 $2.0$（`src/core/Config.h:172`，参数 `speed_cost_exponent`）；
- $\mu_{\text{terrain}}$ = `Terrain::moveCostMultiplier(n.pos)`：水域内 $1.8$，否则 $1.0$（`src/environment/Terrain.cpp:103-105`、`src/core/Config.h:119`）。

代码在有地形时走 `std::pow`， $\beta=2.0$ 时特化为 $s\cdot s$ 以保证逐位恒等（`src/behavior/AgentBehavior.cpp:368-370`）。限额支出：

$$\text{spend} = \min(\text{loco},\ E),\qquad E \leftarrow E - \text{spend},\qquad \text{ledger.dissipate}("\text{locomotion}",\ \text{spend})$$

（`src/behavior/AgentBehavior.cpp:371-379`） $s$ 用的是**本帧更新后**的 `n.speed`（`src/behavior/AgentBehavior.cpp:353`）， $\mu_{\text{terrain}}$ 用的是**移动后**的位置。

#### 3.4 步骤 4：被动呼吸（基础项 + 速度项，再乘温度因子）

$$d = \mathrm{clamp01}(\text{tempDistortion}),\qquad T = 1 + k_T\cdot d$$

$$B = \begin{cases}\dfrac{k_{\text{resp}}}{1 + 0.35\,n_{\text{shield}}}, & n_{\text{shield}} > 0\\[4pt] k_{\text{resp}}, & n_{\text{shield}} = 0\end{cases}$$

$$\text{resp} = \bigl(B + k_{\text{resp2}}\cdot s^{2}\bigr)\cdot T\cdot dt$$

$$\text{spend} = \min(\text{resp},\ E),\qquad E \leftarrow E - \text{spend},\qquad \text{ledger.dissipate}("\text{respiration}",\ \text{spend})$$

（`src/behavior/AgentBehavior.cpp:385-409`）

- $k_{\text{resp}}$ = `respirationBase`：`BehaviorParams` 结构默认 $0.008$（`src/behavior/AgentBehavior.h:156`），构造期被 `NucleusTuning::respirationBase` 覆盖为 $0.03$（`src/core/Config.h:170`、`src/sim/Simulation.cpp:205`；参数 `basal_cost` 与 `respiration_base` 映射同一字段，`src/app/RuntimeEnv.cpp:126-127`）。**生产路径取 0.03。**
- $k_{\text{resp2}}$ = `respirationK` $=0.0004$（`src/behavior/AgentBehavior.h:157`）。该字段**没有** Config 通道，不受资源包覆盖。
- $k_T$ = `respirationTempK` $=1.5$（`src/behavior/AgentBehavior.h:158`），同样无 Config 通道。
- $n_{\text{shield}}$ = 宿主名下 Shield 球个数（`src/behavior/AgentBehavior.cpp:393-396`）；系数 $0.35$ 为硬编码。

温度失真度由环境层给出（`src/environment/TemperatureField.cpp:54-72`）：

$$\text{tempDistortion}(p) = \mathrm{clamp01}\Bigl(\bigl|\,\mathrm{sample}(p) - T_{\text{pref}}\,\bigr|\cdot k_{\text{dist}}\Bigr)$$

$$\mathrm{sample}(p) = \mathrm{clamp01}\Bigl(T_{\text{base}} + \sum_k A_k\exp\Bigl(-\frac{\lVert \Delta_k(p)\rVert^{2}}{2 r_k^{2}}\Bigr)\Bigr)$$

默认 $T_{\text{base}}=0.5$、 $T_{\text{pref}}=0.5$、 $k_{\text{dist}}=2.0$（`src/core/Config.h:94-96`）。**代码中没有 Q10 公式**：温度对代谢的影响是上述线性乘性因子 $(1+k_T d)$，不是 $Q_{10}^{(T-T_0)/10}$。

#### 3.5 步骤 6：球控制（轨道模型与失控漂移）

对每个 `r7Lane == false` 的球（`src/behavior/AgentBehavior.cpp:428`）计算控制增益

$$G(d) = \frac{1}{1 + (d/h)^{2}},\qquad h = \text{handoffRadius} = 60.0$$

（`src/behavior/AgentBehavior.cpp:65-75`； $h$ 见 `src/behavior/AgentBehavior.h:164`，无 Config 通道。） $G<0.2 \iff d>2h=120$ 即失控阈值（`src/behavior/AgentBehavior.h:160-163`）。

**受控分支** $G\ge 0.2$（`src/behavior/AgentBehavior.cpp:487-504`）：

$$\omega = G,\qquad \theta \leftarrow \mathrm{fmod}(\theta + \omega\,dt + 2\pi,\ 2\pi)$$

轨道目标半径与径向松弛：

$$R^{*} = \Bigl(10 + 0.5\max(E_b,0)\Bigr)\cdot\Bigl(0.5 + \frac{\text{orbitRadius}[t]}{240}\Bigr)$$

$$r' = \begin{cases} r + (R^{*}-r)\cdot\min(1,\ 0.3\,a_t\,dt), & r < R^{*}\ (\text{用近距排斥速率外推})\\[2pt] r + (R^{*}-r)\cdot\min(1,\ 0.3\,s_t\,dt), & r \ge R^{*}\ (\text{用径向弹簧速率回拉})\end{cases}$$

其中 $a_t$ = `affinity[t]`、 $s_t$ = `repelStrength[t]`（`src/behavior/AgentBehavior.cpp:473-486`）。位置与半径同源重建： $p \leftarrow \mathrm{wrap}(p_{\text{nuc}} + r'\hat\theta)$，`b.radius` $\leftarrow \mathrm{torusDistance}(p_{\text{nuc}}, p)$（`src/behavior/AgentBehavior.cpp:502-504`）。

**失控分支** $G<0.2$（`src/behavior/AgentBehavior.cpp:505-514`）：

$$\theta \leftarrow \mathrm{fmod}(\theta + \omega_{\text{last}}\,dt + 2\pi,\ 2\pi),\qquad p \leftarrow \mathrm{wrap}(p + v\,dt),\qquad r \leftarrow \mathrm{torusDistance}(p_{\text{nuc}}, p)$$

当 $E_b > \text{detachEnergy} = 120.0$（`src/behavior/AgentBehavior.h:183`）时记入待脱离列表。失控判据用的是**当前真实环距** `env.torusDistance(n.pos, b.pos)`，不是 `b.radius`（`src/behavior/AgentBehavior.cpp:453-464`）。

**两分支共用：球缓慢散热**

$$\text{drift} = k_{\text{loco}}\cdot r\cdot 0.05\cdot dt,\qquad \text{take}=\min(\text{drift}, E_b),\qquad E_b\leftarrow E_b-\text{take},\qquad \text{ledger.dissipate}("\text{ballDrift}",\text{take})$$

（`src/behavior/AgentBehavior.cpp:518-523`；系数 $0.05$ 硬编码， $k_{\text{loco}}$ 同上。）

**6a 自然脱离结算**：待脱离球经 `detachBall` 从 `balls` 移入 `detached_`，能量逐位不变、**账面零操作**（`src/behavior/AgentBehavior.cpp:533-541,152-159`，倒序摘除以保持下标有效）。

#### 3.6 步骤 6b：内部动作触发（产球 / 回收）

每帧**无条件**抽取一次 roll（无论是否触发都消耗 1 次 RNG，使 RNG 流与能量状态解耦，`src/behavior/AgentBehavior.cpp:546-548`）：

$$\text{roll}\sim U[0,1);\qquad \text{roll} < p_{\text{act}} = 0.10$$

（ $p_{\text{act}}$ = `kActionRollP`，`src/behavior/AgentBehavior.cpp:39`）判定用参考容量：

$$\text{capRef} = \begin{cases} n.\text{cap}, & n.\text{cap} > 0\\ 4\cdot\text{energyThreshold}, & \text{否则}\end{cases}$$

（`src/behavior/AgentBehavior.cpp:550-551`）分支优先级为 产球 > 回收（无繁殖：繁殖需要种群容器，由组合根承担）：

- **产球**： $E > 0.6\cdot\text{capRef}$ 时调 `deployBall`；当前无 Scout 球则产 Scout，否则产 Resource（`src/behavior/AgentBehavior.cpp:552-563`）。
- **回收**： $E < 0.25\cdot\text{capRef}$ 且球数 $>0$ 时调 `recycleBall`（`src/behavior/AgentBehavior.cpp:564-566`）。

常数：`kDeployTriggerFrac` $=0.6$、`kRecycleEnergyFrac` $=0.25$、`kReproTriggerFrac` $=0.9$（**声明但未使用**，繁殖由组合根独立判定，`src/behavior/AgentBehavior.cpp:36-38`）。

**产球 `deployBall`**（`src/behavior/AgentBehavior.cpp:92-146`）：

- 门槛 $E > 8.0$ 且 `balls.size() < 3`（`kDeployCost`、`kMaxBalls`，`src/behavior/AgentBehavior.cpp:15-16`）；
- 扣 $8.0$ 并 `dissipate("deploy", 8.0)`；
- 若扣后 $E > 10.0$：再扣 $10.0$、球得 $10.0$、`transfer(n.id, b.id, 10.0)`（`kBallSeedEnergy`，`src/behavior/AgentBehavior.cpp:20,120-124`）；
- 球 id $=(n.\text{id}\ll 16)\ |\ \mathtt{0xB000}\ |\ (\text{serial}\ \&\ \mathtt{0x0FFF})$，serial 为进程级单调计数（`src/behavior/AgentBehavior.cpp:111-113`）；
- $\theta = u\cdot 2\pi$（1 次 RNG）， $R = R_{\text{load}}(E_b)\cdot(0.5+\text{orbitRadius}[t]/240)$；
- $p = \mathrm{wrap}\bigl(p_{\text{nuc}} + R(\cos\theta,\sin\theta)\bigr)$， $v=\{0,0\}$，`ownerId = n.id`，`r7Lane = true`（`src/behavior/AgentBehavior.cpp:114-144`）。

**回收 `recycleBall`**（`src/behavior/AgentBehavior.cpp:211-235`）取末位球：

$$\text{give} = \min\bigl(E_b\cdot 0.5\cdot \text{absorbPreference}[t],\ E_b\bigr),\qquad \text{loss} = E_b - \text{give}$$

$$\text{ledger.transfer}(b.\text{id}\to n.\text{id},\ \text{give});\quad n.E \mathrel{+}= \text{give};\quad \text{ledger.dissipate}("\text{recycle-loss}",\ \text{loss})$$

另记一条金额为 0 的 `dissipate("recycle", 0.0)` 作为事件痕迹（`src/behavior/AgentBehavior.cpp:233`）。

#### 3.7 步骤 6c：容量结算

$$n.\text{cap} = \text{nativeCap} + \sum_{b\in\text{balls},\ b.\text{type}=\text{Resource}} b.\text{cap}$$

（`src/behavior/AgentBehavior.cpp:80-86`；`nativeCap` $=200.0$，`src/behavior/AgentBehavior.h:180`，无 Config 通道；球 `cap` $=60.0$。）

$$E > n.\text{cap}\ \Rightarrow\ \text{excess}=E-n.\text{cap},\quad E\leftarrow n.\text{cap},\quad \text{ledger.dissipate}("\text{capOverflow}",\ \text{excess})$$

（`src/behavior/AgentBehavior.cpp:573-580`）该步排在 6b 之后、死亡判定之前，本帧溢出不留到下一帧。

#### 3.8 步骤 7：能量下限与死亡

$$E \le 0\ \Rightarrow\ \text{死亡}$$

（`src/behavior/AgentBehavior.cpp:582`）

$$\text{left} = \max(E,0),\qquad E\leftarrow 0,\qquad \text{alive}\leftarrow\text{false}$$

$$\text{carrion} = k_{\text{death}}\cdot\text{left},\quad k_{\text{death}}=0.3\ (\texttt{src/behavior/AgentBehavior.cpp:34}),\qquad \text{lost} = (1-k_{\text{death}})\cdot\text{left}$$

$$\text{ledger.dissipate}("\text{death}",\ \text{lost});\qquad n.\text{carrion\_} = \{\,p_{\text{death}},\ 0.3\cdot\text{left}\,\}$$

（`src/behavior/AgentBehavior.cpp:588-599`）球随核死亡全额耗散：每个 $E_b>0$ 的球 `dissipate("death", E_b)` 后清零，容器清空、速度归零（`src/behavior/AgentBehavior.cpp:600-609`）。

**核没有老死、也没有被捕食致死通道**：核的唯一死亡判定是 $E\le0$；`Nucleus` 无年龄字段；核与核之间不转移任何能量（`src/core/Genome.h:12-15`）。

---

### 4. 摄食：核捕食蓝细菌节点

捕食在**组合根**内、`apply()` 之后执行（`src/sim/Simulation.cpp:478-537`），因为 `CyanoField` 属环境层而行为层不能 include 它。行为层只保留唯一写点 `absorbPrey`（`src/behavior/AgentBehavior.cpp:768-774`）。

三级闸门（`src/sim/Simulation.cpp:478`）：

1. `n.alive == true`；
2. `n.genome.cyanoPreference > 0`（ $=0$ 为严格哨兵，绝不捕食）；
3. `n.digestCooldown == 0`。

目标：半径 $r_p$ = `cyanoPredationRadius`（默认 $14.0$，`src/core/Config.h:184`，参数 `cyano_predation_radius`）内**最近**的存活节点，平方距离扫描、无随机（`src/sim/Simulation.cpp:490-497`）。

每口请求量：

$$\text{want} = \text{cyanoPreference}\cdot f\cdot E_{\text{prey}},\qquad f = \text{grazeBiteFraction} = 0.15$$

（`src/core/Config.h:193`、`src/sim/Simulation.cpp:524-525`）`want` **与核自身能量无关**（`src/sim/Simulation.cpp:510`）。

转化率（`src/environment/CyanoNode.cpp:75-103`）， $\text{base} = k_{\text{conv}}\cdot\text{cyanoDigestion}$， $k_{\text{conv}}=0.6$：

$$\text{rate} = \begin{cases} \text{base}, & \text{mode}=0\\ \text{base}\cdot\dfrac{v_{\text{ref}}}{v_{\text{ref}}+\text{maxSpeed}}, & \text{mode}=1\ \text{（默认）}\\[6pt] \text{base}\cdot\dfrac{a_{\text{ref}}}{a_{\text{ref}}+\text{maxAccel}}, & \text{mode}=2\\[6pt] \text{base}\cdot w_v\dfrac{v_{\text{ref}}}{v_{\text{ref}}+\text{maxSpeed}}\cdot w_a\dfrac{a_{\text{ref}}}{a_{\text{ref}}+\text{maxAccel}}, & \text{mode}=3\end{cases}$$

随后钳到 $[\text{conversionMinRate},\ \text{conversionMaxRate}] = [0.05,\ 1.0]$（`src/core/Config.h:281-282`）。默认 $v_{\text{ref}}=40$、 $a_{\text{ref}}=60$、 $w_v=w_a=1$、 $k_{\text{conv}}=0.6$（`src/core/Config.h:275-280`）。`conversionExponent` 被显式忽略（`src/environment/CyanoNode.cpp:80`）。

迁移回报（默认关闭）：若本口猎物位置距上次进食点 $>\text{migrationRadius}=120$，则

$$\text{effRate} = \min\bigl(1,\ \text{rate}\cdot(1+\text{migrationBonus})\bigr)$$

`migrationBonus` 默认 $0.0$（`src/core/Config.h:199-200`、`src/sim/Simulation.cpp:516-523`）。

实际取得量（`src/environment/CyanoNode.cpp:550-573`）：

$$\text{edible} = E_{\text{node}} - E_{\text{refuge}},\qquad \text{avail}=\max(\text{edible},0)$$

$$\text{taken} = \min\bigl(\max(\text{want}\cdot\text{effRate},\,0),\ \text{avail}\bigr)$$

$E_{\text{refuge}}$ = `grazeRefugeEnergy` 默认 $15.0$（`src/core/Config.h:302`）。捕食避难所使节点永远保留储备、不会被吃死： $E \ge$ refuge 恒成立（`src/environment/CyanoNode.cpp:562-569`）。

进账（`src/behavior/AgentBehavior.cpp:768-774`）：

$$n.E \mathrel{+}= \text{taken},\qquad \text{ledger.transfer}(\text{preyId}\to n.\text{id},\ \text{taken})$$

零和搬运，不改注入/耗散总额。

**损耗去向**： $1-\text{effRate}$ 部分根本没有离开节点（`taken` 已按转化率折算），即"没吃到的部分"留在蓝细菌体内，不构成耗散。因此核的同化效率就是 $\text{effRate}$ 本身，不存在第二个同化系数。

捕食后：

$$n.\text{digestCooldown} = \left\lceil \frac{1}{0.5 + \text{cyanoDigestion}}\right\rceil,\qquad n.\text{lastMealPos} = n.\text{pos}$$

（`src/sim/Simulation.cpp:529-534`）冷却在每帧末统一递减（`src/sim/Simulation.cpp:541`），不耗能。**没有捕食失败/逃脱判定**，摄食也不产生任何几何量变化。

---

### 5. 生长与繁殖

#### 5.1 没有"能量 → 质量"的生长通道

代码中**不存在** mass 变量，也没有"能量转质量"的方程。核的尺寸语义只有两条：

1. `cap`（菌落容量）每帧由 Resource 球数量重算，是能量上限而非质量（`src/behavior/AgentBehavior.cpp:80-86,573`）；
2. 球的负载半径 $R_{\text{load}}(E)=10+0.5E$ 随球能线性增长（`src/behavior/AgentBehavior.cpp:482-483`）。

因此"生长"在实现上等价于：能量积累 → 触发产球（§3.6）→ Resource 球抬高 `cap` → 可继续积累能量。

#### 5.2 繁殖（`AgentBehavior::reproduce`）

在组合根中批量执行（`src/sim/Simulation.cpp:553-571`），行为层实现见 `src/behavior/AgentBehavior.cpp:251-286`。

$$k = \text{energyThreshold}\cdot\text{reproductionCostRatio},\qquad \text{reproductionCostRatio}\ \text{默认}\ 1.0$$

（`src/core/Config.h:173`，参数 `reproduction_cost_ratio`。）判据 $E > k$（严格大于，`src/behavior/AgentBehavior.cpp:262`）。

零和分割：

$$E_{\text{parent}} \leftarrow E_{\text{parent}} - k,\qquad E_{\text{child}} = f\cdot k,\qquad \text{ledger.dissipate}("\text{reproduce-loss}",\ (1-f)k)$$

$$f = \texttt{kChildEnergyFraction} = 0.5$$

（`src/behavior/AgentBehavior.cpp:27,277-278`） $f$ 是硬编码常量：`NucleusTuning::childEnergyRatio`（`src/core/Config.h:174`，参数 `child_energy_ratio`）**当前无消费者**，改它不影响行为。

子代继承（`src/behavior/AgentBehavior.cpp:267-282`）：

- 全字段拷贝（含 `genome`、`cap`、`speed`、`vel`、`lastMealPos`）；
- `id` 换新；`balls` 与 `detached_` 清空（球是亲代资产）；
- `genome` 经 `genomeMutate`（必变异，§1.4）；
- `digestCooldown` 归零；
- 出生位置： $p = \mathrm{wrap}\bigl(p_{\text{parent}} + (2u_x-1)\delta,\ (2u_y-1)\delta\bigr)$， $\delta=\texttt{kBirthOffset}=12.0$（`src/behavior/AgentBehavior.cpp:31,280-282`）。均匀方形分布，不是圆盘。`NucleusTuning::reproductionOffset` 默认 $25.0$ 同样**无消费者**。

种群护栏（不是生态承载力）：`aliveCount_ < maxNuclei`，默认 $1200$（`src/core/Config.h:75`；采样与执行各检查一次，`src/sim/Simulation.cpp:548-549,562`）。

**繁殖冷却：没有。** 唯一时间约束是门槛 $E>k$ 本身：一次繁殖扣掉 $k$，需再攒回 $k$ 才能生第二个。消化冷却只限制捕食，不限制繁殖。

#### 5.3 亲代与子代的能量守恒（S10 红线）

$$E_{\text{child}} = f\cdot k \le k = \text{亲代实付}$$

差额 $(1-f)k$ 记 `dissipate("reproduce-loss")`，不做 `injectInitial`（`src/behavior/AgentBehavior.cpp:283`）。

---

### 6. 上限与钳制汇总

| 量 | 上界 / 钳制 | 触发点 | 源码位置 |
|---|---|---|---|
| 核速度 | $v_{\max}^{\text{eff}}=\text{maxSpeed}(1+0.5b)$， $\text{maxSpeed}\in[5,300]$、 $b\in[0,1]$，最大 $450$ | 只缩不放 | `src/behavior/AgentBehavior.cpp:320-348` |
| 核加速度 | $\lVert\Delta v\rVert\le\text{maxAccel}\in[0.5,50]$ | 超出按比例缩 | `src/behavior/AgentBehavior.cpp:333-341` |
| 核能量 | $E\le \text{cap}=200+60\,n_{\text{resource}}$， $n_{\text{resource}}\le3$，最大 $380$ | 超出部分 `capOverflow` 耗散 | `src/behavior/AgentBehavior.cpp:80-86,573-580` |
| 核体能非负 | $\text{spend}=\min(\text{cost},E)$；死亡时 $E\leftarrow\max(E,0)$ | 三处限额支出 | `src/behavior/AgentBehavior.cpp:374,404,588` |
| 球数 | 每核 $\le3$ | 产球/收编/换主均检查 | `src/behavior/AgentBehavior.cpp:16,96,172,959` |
| 球速度 | $\max(\text{类型上限},\ v_{\text{nuc}}\cdot\text{followBoost})$；Resource 20 / Scout 30 / Shield 15，followBoost 1.5 | 按比例缩 | `src/behavior/AgentBehavior.cpp:913-929`、`src/core/Config.h:228-232` |
| 球负载半径 | $R_{\text{load}}=10+0.5E_b$，无硬上界 | 线性 | `src/behavior/AgentBehavior.cpp:482-483` |
| 施力加速度 | `applyForce` $\le 8.0$；`applyNamedForce` $\le\min(\text{maxAccel},8.0)$ | 钳制 | `src/behavior/AgentBehavior.cpp:626,675`、`src/behavior/AgentBehavior.h:191` |
| 自由球池 | $\ge256$ 时回收最老的蓝细菌源球 | 溢出即耗散 | `src/sim/Simulation.cpp:69,741-756` |

---

### 7. 核与核之间：社会力（零能量转移）

`AgentBehavior::socialAccelerations`（`src/behavior/AgentBehavior.cpp:703-762`）只累加加速度，不改位置、不耗能、不消耗 RNG，是两阶段积分的"算加速度"阶段：先对全体核算出 $a_{\text{social}}$，再在 `apply` 里统一积分，消除"谁先动谁占便宜"的顺序依赖（`src/sim/Simulation.cpp:425-428`）。

本帧生效值（决策层 `mod[4]` 调制表型，基因本身不被改写）：

$$x_{\text{eff}} = x_{\text{gene}}\cdot\bigl(1 + 0.4\cdot\mathrm{clamp}(\text{mod},-1,1)\bigr),\qquad x\in\{\text{sight},\text{space},\text{repel},\text{cohesion}\}$$

（`src/behavior/AgentBehavior.cpp:29,708-720`）对每个邻居 $j$（ $d$ 为环面最小距离）：

$$d < \text{effSpace}_i:\qquad a_i \mathrel{+}= \text{effRepel}_i\Bigl(1-\frac{d}{\text{effSpace}_i}\Bigr)\cdot\widehat{u}_{j\to i}$$

$$\text{effSpace}_i \le d < \text{effSight}_i\ \wedge\ \text{effCohesion}_i>0:\qquad a_i \mathrel{+}= \text{effCohesion}_i\Bigl(1-\frac{d}{\text{effSight}_i}\Bigr)\cdot\widehat{u}_{i\to j}$$

（`src/behavior/AgentBehavior.cpp:744-760`）两式互斥（else-if）。死核不施力也不受力（`src/behavior/AgentBehavior.cpp:725,733`）。

**硬分离在 `Simulation::step` 里单独 pass——实际代码中该 pass 未实现**：`src/behavior/AgentBehavior.cpp:303` 声称"硬分离在 Simulation::step 里单独 pass"，但 `src/sim/Simulation.cpp` 全文无相关代码；`NucleusTuning::minSeparation` / `repelK` / `forageK` / `forageRadius` / `wanderK` / `wanderTurnRate` 均无消费者，仅被 `RuntimeEnv` 写入配置（`src/app/RuntimeEnv.cpp:261-266`）。

---

### 8. 账本零和恒等式

#### 8.1 恒等式

$$\text{injected}_{\text{total}} - \text{dissipated}_{\text{total}} = \sum_{\text{活体}} E$$

（`src/ledger/AgentLedger.h:2-4`）三个计数器 `injected_` / `dissipated_` / `transferred_`，其中 `transfer` **只累加流量、不改总量**（`src/ledger/AgentLedger.cpp:19-22`），因此不影响恒等式。

#### 8.2 $\sum_{\text{活体}}E$ 的精确组成

`Simulation::totalEnergy()`（`src/sim/Simulation.cpp:1001-1021`）：

$$\sum_{\text{活体}} E = \sum_{n:\ \text{alive}} \Bigl(n.E + \sum_{b\in n.\text{balls}} E_b + \sum_{b\in n.\text{detached\_}} E_b\Bigr) + \sum_{b\in\text{freeBalls\_}} E_b + \sum_{c\in\text{carrion\_}} E_c + E_{\text{cyano}}$$

死核（`alive == false`）**不计入**；其 `carrion_` 在尸体压缩前已被搬到组合根 `carrion_` 容器（`src/sim/Simulation.cpp:586-590`），故仍被计入。

#### 8.3 断言位置与失败行为

审计在每次 `step()` 末尾执行一次（`src/sim/Simulation.cpp:791-792`）：

$$\text{tol} = 10^{-9}\cdot\max\bigl(1,\ \text{injected}_{\text{total}}\bigr)$$

$$\text{auditOk} \leftarrow \Bigl|\,\bigl(\text{injected}-\text{dissipated}\bigr) - \sum_{\text{活体}}E\,\Bigr| \le \text{tol}$$

（`src/ledger/AgentLedger.cpp:29-32`）

另有两条**局部**审计在同一处把 `auditOk_` 置 false：

1. 蓝细菌收支闭合： $\bigl|\Delta E_{\text{cyano}} - (\text{photo} - \text{metab} - \text{repro})\bigr| > 10^{-6}\max(1,|E_{\text{cyano}}^{t-1}|)$（`src/sim/Simulation.cpp:771-777`）；
2. 尸体流水闭合： $\bigl|\text{carrionEnergyIn} - \text{carrionEnergyOut}\bigr| > 10^{-9}\max(1,\text{carrionEnergyIn})$（`src/sim/Simulation.cpp:785-788`）。

失败后的行为：

- `Simulation::runTo` 立即返回 `false`（`src/sim/Simulation.cpp:987-993`）；
- CLI 主循环置 `auditFailed`，先写完本帧 stats/trace 再 `break`（`src/app/main.cpp:1355,1465`），最后 `return 3`（`src/app/main.cpp:1479`）；
- serve 模式事件执行后同样检查，失败 `std::exit(3)`（`src/app/main.cpp:472-476`）；
- 读档（`deserializeState`）结束时用同一容差重算一次审计（`src/sim/Simulation.cpp:1701-1702`）。

#### 8.4 账目原因枚举

| reason | 触发点 | 源码位置 |
|---|---|---|
| `locomotion` | 运动代谢 | `src/behavior/AgentBehavior.cpp:377` |
| `respiration` | 被动呼吸 | `src/behavior/AgentBehavior.cpp:407` |
| `deploy` | 产球固定成本 8.0 | `src/behavior/AgentBehavior.cpp:98` |
| `recycle` / `recycle-loss` | 回收事件痕迹（0 额）/ 未返还部分 | `src/behavior/AgentBehavior.cpp:231-233` |
| `reproduce-loss` | 繁殖差额 $(1-f)k$ | `src/behavior/AgentBehavior.cpp:278` |
| `applyForce` | 施力代价 $k\lvert a\rvert^{2}$ | `src/behavior/AgentBehavior.cpp:643,691` |
| `ballDrift` | 球散热（核名下 + 自由球） | `src/behavior/AgentBehavior.cpp:522`、`src/sim/Simulation.cpp:628` |
| `capOverflow` | 超出菌落容量 | `src/behavior/AgentBehavior.cpp:578` |
| `death` | 死亡 70% + 球能全额；尸体溢出/无分解者 | `src/behavior/AgentBehavior.cpp:594,603`、`src/sim/Simulation.cpp:681,686` |
| `ball_loss` | 换主/失主对原主扣款 | `src/behavior/AgentBehavior.cpp:980` |
| `cyanoMetabolism` / `cyanoReproduction` / `cyanoBallExpire` | 蓝细菌侧 | `src/sim/Simulation.cpp:708-709,755` |
| `event:winter` / `event:plague` | 事件通道 | `src/app/main.cpp:445,460` |

注入通道：`injectInitial`（核出生 60.0，`src/sim/Simulation.cpp:329`）、`injectPrimary`（蓝细菌光合，`src/sim/Simulation.cpp:707`；事件丰收，`src/app/main.cpp:431`）。

---
### 9. 主循环：`Simulation::step()` 的固定步骤

`tick_` 从 0 起，`step()` 末尾自增（`src/sim/Simulation.cpp:790`）；`runTo(end)` 循环至 `tick_ >= end` 或审计失败（`src/sim/Simulation.cpp:987-993`）。

**迭代顺序（确定性）**：所有实体遍历一律按容器下标升序（`nuclei_`、`cyano_.nodes()`、`freeBalls_`），无排序、无哈希遍历；平局（等距）取小下标（`src/sim/Simulation.cpp:652,966`）。尸体压缩只删除 `!alive` 元素并保持相对顺序（`src/sim/Simulation.cpp:603-608`），故迭代序在压缩前后一致。

固定步骤：

1. **逐核主循环**（`i` 升序，跳过死核，`src/sim/Simulation.cpp:339-552`）：
   1. 统计 Scout 球数 `scoutCount`（`src/sim/Simulation.cpp:346-349`）；
   2. 构造 `TargetList`：`freeBalls_` 中落在 `genome.forceRange` 内的球，按环距升序插入排序取前 8（`src/sim/Simulation.cpp:356-388`）；
   3. 最近邻核扫描（视野 `nucleusSightRadius` 内，平方距离，`src/sim/Simulation.cpp:396-417`）；
   4. `energyRatio = E / cap`（`cap <= 0` 时 0，`src/sim/Simulation.cpp:418`）；
   5. `sense(...)` → 24 维 `SensorVector`（含读噪声，消耗 RNG，`src/sim/Simulation.cpp:419-421`）；
   6. `policy_->decide(sv, targets)` → `ActionVector`（决策层零 RNG、零实体写入，`src/decision/IDecisionPolicy.h:27-30`）；
   7. `socialAccelerations` → `socialAcc`（`src/sim/Simulation.cpp:428`）；
   8. `applyNamedForce`（仅当 `act.forceSlot >= 0`，`src/sim/Simulation.cpp:432-445`）；
   9. `apply(n, act, ..., socialAcc)` → 移动/耗能/球控/动作/容量/死亡（`src/sim/Simulation.cpp:446-447`）；
   10. 捕食 + `absorbPrey` + 设置消化冷却（`src/sim/Simulation.cpp:478-537`）；
   11. 死亡计数 `--aliveCount_; ++deathCount_`（`src/sim/Simulation.cpp:539`）；
   12. 消化冷却递减（`src/sim/Simulation.cpp:541`）；
   13. 繁殖闸收集下标（`E > energyThreshold` 且 `aliveCount_ < maxNuclei`，`src/sim/Simulation.cpp:548-551`）。
2. **批量繁殖**：`nuclei_.reserve(size + count)` 后按收集序逐亲代 `reproduce`，执行期复查护栏；成功后 `++aliveCount_`（`src/sim/Simulation.cpp:553-571`）。
3. **交接缓冲结算**：全体核的 `detached_` 移入 `freeBalls_`（能量原样）；死核的 `carrion_` 移入组合根 `carrion_` 并累加 `carrionEnergyIn_`（`src/sim/Simulation.cpp:578-592`）。此步**必须在尸体压缩之前**，否则死核 `detached_` 里的球能量会随 erase 凭空消失（`src/sim/Simulation.cpp:572-577`）。
4. **`ballWorldStep()`**：球力场求和 + 世界积分（只推进 `r7Lane` 球，`src/sim/Simulation.cpp:813-946`）。
5. **`ballOwnership()`**：`detachRadius` 内最近核持有，换主/失主结算（`src/sim/Simulation.cpp:955-985`）。
6. **尸体压缩**：`deadCount > 2*aliveCount_` 时删除死核（`src/sim/Simulation.cpp:603-608`）。
7. **自由球**：非 R7 车道的球做漂移积分 + 速度阻尼 $0.99$；所有自由球按 $E\cdot2\times10^{-4}$ 逐帧散热（`src/sim/Simulation.cpp:609-630`）。
8. **自由球拾取**：按核顺序、按球顺序，`adoptBall` 收编 `adoptRadius = 18` 内的自由球（`src/sim/Simulation.cpp:631-647`、`src/behavior/AgentBehavior.h:182`）。
9. **尸体营养分配**：每个 `Carrion` 找 $200$ 半径内最近存活蓝细菌节点，`fertilize` 接受量走 `transfer(0, best, accepted)`，溢出或找不到分解者全额 `dissipate("death")`（`src/sim/Simulation.cpp:653-691`）。
10. `env_.tick()`（`src/sim/Simulation.cpp:692`）。
11. **蓝细菌推进**：以世界最暗点光照 `sunLevelAt(cfg.sun, tick_, H, H)` 驱动 `cyano_.step()`，按报告记 `injectPrimary` / `dissipate`；模块产出的自由球以实体形式 push 进 `freeBalls_`（**不再注入**，能量只是换持有者，`src/sim/Simulation.cpp:702-739`）。
12. **自由球池上限**：`size >= 256` 时回收最老的蓝细菌源球，能量 `dissipate("cyanoBallExpire")`（`src/sim/Simulation.cpp:741-756`）。
13. **两条局部审计**（蓝细菌收支、尸体流水，`src/sim/Simulation.cpp:771-788`）。
14. `++tick_`；`auditOk_ = ledger_.audit(tol, totalEnergy())`（`src/sim/Simulation.cpp:790-792`）。

**每 tick 的 RNG 消耗点（顺序固定）**：`sense` 读噪声（每候选 3 次 × 至多 3 个候选 + s[9] 1 次，`src/perception/AgentSensors.cpp:134-179`）→ `apply` 写噪声 2 次 → `apply` 动作 roll 1 次 → 产球 1 次（若触发）→ 批量繁殖 52 次/亲代 → `cyano_.step` 的固定消耗序（`src/environment/CyanoNode.h:8-32`）。捕食段、社会力段、球力场段**零 RNG**（`src/sim/Simulation.cpp:465-467`）。

---

### 10. 参数总表

#### 10.1 行为层常量（`AgentBehavior.cpp` 匿名命名空间，无 Config 通道）

| 符号 | 名称 | 默认值 | 单位/范围 | 源码位置 |
|---|---|---|---|---|
| $k_{\text{deploy}}$ | 产球固定成本 | 8.0 | 能量 | `src/behavior/AgentBehavior.cpp:15` |
| $n_{\text{ball}}^{\max}$ | 每核球数上限 | 3 | 个 | `src/behavior/AgentBehavior.cpp:16` |
| $R_0$ | 球基准半径 | 10.0 | 世界单位 | `src/behavior/AgentBehavior.cpp:17` |
| $E_{\text{seed}}$ | 产球种子能量 | 10.0 | 能量 | `src/behavior/AgentBehavior.cpp:20` |
| $k_{\text{rec}}$ | 回收返还率 | 0.5 | — | `src/behavior/AgentBehavior.cpp:22` |
| $f$ | 子代能量分成 | 0.5 | $[0,1]$ | `src/behavior/AgentBehavior.cpp:27` |
| $k_{\text{mod}}$ | 社会参数调制上限 | 0.4 | — | `src/behavior/AgentBehavior.cpp:29` |
| $\delta$ | 出生偏移半宽 | 12.0 | 世界单位 | `src/behavior/AgentBehavior.cpp:31` |
| $k_{\text{death}}$ | 尸体营养比例 | 0.3 | $[0,1]$ | `src/behavior/AgentBehavior.cpp:34` |
| $p_{\text{act}}$ | 内部动作触发概率 | 0.10 | $[0,1]$ | `src/behavior/AgentBehavior.cpp:39` |
| $c_{\text{ball}}$ | Resource 球容量 | 60.0 | 能量 | `src/behavior/AgentBehavior.cpp:45` |
| — | Shield 呼吸减免系数 | 0.35 | — | `src/behavior/AgentBehavior.cpp:398` |
| — | 球散热系数 | 0.05 | — | `src/behavior/AgentBehavior.cpp:518` |
| — | 自由球池衰减系数 | 0.0002 | 1/tick | `src/sim/Simulation.cpp:624` |
| — | 尸体搜索半径 | 200.0 | 世界单位 | `src/sim/Simulation.cpp:57` |
| — | 自由球池上限 | 256 | 个 | `src/sim/Simulation.cpp:69` |
| — | 自由球速度阻尼 | 0.99 | — | `src/sim/Simulation.cpp:615-616` |

#### 10.2 行为参数（`BehaviorParams` 与 Config 镜像）

| 符号 | 名称 | 默认值（生产路径） | 单位/范围 | 源码位置 |
|---|---|---|---|---|
| $k_{\text{move}}$ | 方向→速度增益 | 60.0 | 速度 | `src/behavior/AgentBehavior.h:147` |
| $k_{\text{loco}}$ | 运动代谢系数 | 0.0002 | 能量/速度 $^{\beta}$ | 结构默认 `src/behavior/AgentBehavior.h:152`；Config `src/core/Config.h:171`；镜像 `src/sim/Simulation.cpp:206` |
| $\beta$ | 运动代谢指数 | 2.0 | $[1,4]$ | `src/core/Config.h:172` |
| $\lambda_{\text{damp}}$ | 核速度阻尼 | 1.0 | $[0,1]$ | `src/core/Config.h:177` |
| $r_{\text{ratio}}$ | 繁殖成本比 | 1.0 | $[0,2]$ | `src/core/Config.h:173` |
| $k_{\text{resp}}$ | 基础呼吸 | 0.03 | 能量/tick | `src/core/Config.h:170`（结构默认 0.008：`src/behavior/AgentBehavior.h:156`） |
| $k_{\text{resp2}}$ | 呼吸速度项系数 | 0.0004 | 能量/(速度²·tick) | `src/behavior/AgentBehavior.h:157` |
| $k_T$ | 呼吸温度强度 | 1.5 | — | `src/behavior/AgentBehavior.h:158` |
| $k_w$ | 写通道扰动强度 | 0.25 | — | `src/behavior/AgentBehavior.h:159` |
| $h$ | 球控制参考半径 | 60.0 | 世界单位 | `src/behavior/AgentBehavior.h:164` |
| $\text{nativeCap}$ | 核原生能量上限 | 200.0 | 能量 | `src/behavior/AgentBehavior.h:180` |
| $r_{\text{adopt}}$ | 自由球拾取半径 | 18.0 | 世界单位 | `src/behavior/AgentBehavior.h:182` |
| $E_{\text{detach}}$ | 自然脱离阈值 | 120.0 | 能量 | `src/behavior/AgentBehavior.h:183` |
| $r_p$ | 捕食判定半径 | 14.0 | 世界单位 | `src/core/Config.h:184`（参数 `cyano_predation_radius`） |
| $f$ | 每口口粮系数 | 0.15 | — | `src/core/Config.h:193` |
| $a_{\max}^{\text{force}}$ | 施力加速度硬上限 | 8.0 | 世界单位/tick² | `src/behavior/AgentBehavior.h:191` |
| `migrationBonus` | 迁移回报 | 0.0 | $\ge 0$ | `src/core/Config.h:199` |
| `migrationRadius` | 迁移回报距离阈值 | 120.0 | 世界单位 | `src/core/Config.h:200` |
| `forceRadius` | 施力半径 fallback | 40.0 | 世界单位 | `src/behavior/AgentBehavior.h:188` |
| `forceCostK` | 施力代价 fallback | 0.01 | — | `src/behavior/AgentBehavior.h:192` |

#### 10.3 种群与遗传

| 符号 | 名称 | 默认值 | 单位/范围 | 源码位置 |
|---|---|---|---|---|
| $N_0$ | 初始核数 | 12 | 个 | `src/core/Config.h:70` |
| $N_{\max}$ | 核数性能护栏 | 1200 | 个 | `src/core/Config.h:75` |
| $\kappa$ | 全局变异系数 | 0.25 | $\ge 0$ | `src/core/Config.h:79` |
| $E_0$ | 核出生能量 | 60.0 | 能量 | `src/sim/Simulation.cpp:309` |
| $d_{\text{sep}}$ | 出生最小间距 | 20.0 | 世界单位 | `src/sim/Simulation.cpp:274` |
| $0.8\cdot\text{nativeCap}$ | 初始可育上限 | 160.0 | 能量 | `src/sim/Simulation.cpp:321` |
| $\sigma_{\text{hue}}$ | 子代色相抖动 | 8.0 | 度 | `src/core/Genome.h:74` |

#### 10.4 球力场（`BallFieldParams`，经 `BallTuning` 镜像）

| 字段 | 名称 | 结构默认 → 生产值 | 单位/范围 | 源码位置 |
|---|---|---|---|---|
| `damping` | 球速度阻尼 | 0.90 → 0.92 | $[0.5,1]$ | `src/behavior/AgentBehavior.h:76`、`src/core/Config.h:220` |
| `radialK` | 径向弹簧系数 | 0.03 → 0.05 | — | `src/behavior/AgentBehavior.h:77`、`src/core/Config.h:221` |
| `tangentialK` | 切向驱动 | 2.0 → 1.0 | — | `src/behavior/AgentBehavior.h:78`、`src/core/Config.h:222` |
| `nearRepelRadius` | 近核排斥半径 | 25.0 → 8.0 | 世界单位 | `src/behavior/AgentBehavior.h:79`、`src/core/Config.h:223` |
| `ballRepelRadius` | 球间排斥半径 | 10.0 → 6.0 | 世界单位 | `src/behavior/AgentBehavior.h:80`、`src/core/Config.h:224` |
| `ballRepelK` | 球间排斥强度 | 1.2 → 0.5 | — | `src/behavior/AgentBehavior.h:81`、`src/core/Config.h:225` |
| `sameOwnerAttractRadius` | 同宿主吸引半径 | 40.0 → 0.0（关闭） | 世界单位 | `src/behavior/AgentBehavior.h:82`、`src/core/Config.h:226` |
| `sameOwnerAttractK` | 同宿主吸引强度 | 0.15 → 0.05 | — | `src/behavior/AgentBehavior.h:83`、`src/core/Config.h:227` |
| `maxSpeedShield` / `Resource` / `Scout` | 球类型限速 | 25 / 50 / 80 → 15 / 20 / 30 | 世界单位/tick | `src/behavior/AgentBehavior.h:84-86`、`src/core/Config.h:228-230` |
| `followCoupling` | 跟随弹簧耦合 | 15.0 → 4.0 | — | `src/behavior/AgentBehavior.h:87`、`src/core/Config.h:231` |
| `followBoost` | 跟随加速倍率 | 1.5 → 1.5 | — | `src/behavior/AgentBehavior.h:88`、`src/core/Config.h:232` |
| `detachRadius` | 归属保持半径 | 400.0 → 60.0 | 世界单位 | `src/behavior/AgentBehavior.h:89`、`src/core/Config.h:219` |
| `lossCost` | 失球惩罚 | 5.0 → 0.2 | 能量 | `src/behavior/AgentBehavior.h:90`、`src/core/Config.h:218` |

结构默认值为 v2.3.1 标定、仅供行为层独立单测；生产路径由 `Simulation` 构造期与 `applyConfig` 从 `cfg_.ball` 逐字段覆盖（`src/sim/Simulation.cpp:217-231,1119-1133`）。

#### 10.5 球世界积分与归属的算式

球力场（`AgentBehavior::ballFieldAccel`，`src/behavior/AgentBehavior.cpp:801-858`）。记 $d$ = 源核到球的环面位移模， $R^{*}$ = 轨道目标半径， $a_t$ = `affinity[t]`， $s_t$ = `repelStrength[t]`：

$$a \mathrel{+}= \hat u\cdot\bigl(-a_t\,(d - R^{*})\cdot k_{\text{rad}}\cdot dt\bigr)\qquad\text{（径向弹簧）}$$

$$a \mathrel{+}= \hat t\cdot\bigl(a_t\,k_{\text{tan}}\cdot dt\bigr),\qquad \hat t = (-\hat u_y,\ \hat u_x)\qquad\text{（切向逆时针驱动）}$$

$$d < r_{\text{near}}:\quad a \mathrel{+}= \hat u\cdot s_t\Bigl(1-\frac{d}{r_{\text{near}}}\Bigr)dt\qquad\text{（近核排斥）}$$

$$a \mathrel{+}= \widehat{u}_{o}\cdot\bigl(-a_t\,(d_o - R^{*})\cdot k_{\text{rad}}k_{\text{follow}}\cdot dt\bigr),\qquad d_o < r_{\text{detach}}\qquad\text{（跟随耦合）}$$

球间作用（`src/behavior/AgentBehavior.cpp:868-893`）， $\hat u$ 由 $o\to b$ 位移单位化：

$$d < r_{\text{rr}}:\quad a \mathrel{+}= \hat u\,k_{\text{rr}}\Bigl(1-\frac{d}{r_{\text{rr}}}\Bigr)dt$$

$$r_{\text{rr}} \le d < r_{\text{ar}}\ \wedge\ \text{同宿主}\ (\text{ownerId 非 0 且相等}):\quad a \mathrel{-}= \hat u\,k_{\text{ar}}\Bigl(1-\frac{d}{r_{\text{ar}}}\Bigr)dt$$

积分（`src/behavior/AgentBehavior.cpp:904-934`）：

$$v \leftarrow (v + a\,dt)\cdot\lambda_{\text{ball}};\qquad \lVert v\rVert > v_{\max}\ \Rightarrow\ v \leftarrow v\cdot\frac{v_{\max}}{\lVert v\rVert};\qquad p\leftarrow\mathrm{wrap}(p + v\,dt)$$

$$v_{\max} = \max\bigl(v_{\text{type}},\ v_{\text{owner}}\cdot\text{followBoost}\bigr)\quad\text{（宿主存活且 followBoost} > 0\text{）}$$

归属结算（`src/sim/Simulation.cpp:955-985` + `src/behavior/AgentBehavior.cpp:951-996`）：每颗 R7 车道球取 `detachRadius` 内**最近**存活核（平局取小下标）；宿主变化时对原主限额扣 `lossCost`（`dissipate("ball_loss")`），换主走 `transferBall`（能量原样），失主进 `freeBalls_`。目标宿主已满 3 球时不交接也不惩罚（`src/behavior/AgentBehavior.cpp:957-962`）。

---

### 11. 已删除或未实现的机制（避免误读）

| 机制 | 状态 | 依据 |
|---|---|---|
| 核吃食物球（`queryFood` / `consumeFood`） | 整链路删除；核唯一食物是蓝细菌节点 | `src/behavior/AgentBehavior.cpp:410-418` |
| 战斗 / 避战（`attack*` / `avoid*` 四参数） | AMEND-1 删除，24 参数中无对应项 | `src/core/Genome.h:4-6` |
| 种群硬顶 `kReproCap = 24` | 删除；承载力由能量收支决定 | `src/core/Config.h:72-75` |
| 繁殖随机闸（ $0.2\%$/tick） | 删除；改为能量门槛确定性事件 | `src/sim/Simulation.cpp:29-33` |
| 核觅食引力 / 随机游走（`forage_k` / `wander_k` 等） | 参数存在但**无消费者** | 全仓仅 `src/app/RuntimeEnv.cpp:263-266` 写入配置 |
| 核硬分离 pass（`minSeparation` / `repelK`） | 注释声称存在，`Simulation::step` 中**未实现** | `src/behavior/AgentBehavior.cpp:303` 对照 `src/sim/Simulation.cpp:334-793` |
| `childEnergyRatio` / `reproductionOffset` / `nucleusInitEnergyRatio` | 参数存在但**无消费者**，行为层用硬编码常量 | `src/behavior/AgentBehavior.cpp:27,31`、`src/sim/Simulation.cpp:309` |
| `kReproTriggerFrac` | 常量声明但**未使用** | `src/behavior/AgentBehavior.cpp:37` |
| `conversionExponent` | 参数传入但被显式 `(void)` 丢弃 | `src/environment/CyanoNode.cpp:80` |
| Q10 温度标度 | **不存在**；温度效应为线性乘性因子 $(1+k_Td)$ | `src/behavior/AgentBehavior.cpp:388` |
| 核质量 / 半径 / 朝向 / 年龄 / 谱系号 | **不存在**这些字段 | `src/behavior/AgentBehavior.h:104-140` |
| 交叉 / 重组 | **不存在**，唯一遗传算子是加性高斯变异 | `src/core/Genome.cpp:137-176` |
| 核之间的能量转移 | **不存在**（社会力只产生斥力/聚拢） | `src/core/Genome.h:12-15` |
