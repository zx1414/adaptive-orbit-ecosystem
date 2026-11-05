## 决策层：八个策略

决策层是"感知快照 → 动作意图"的映射集合：纯函数 + 有限状态机。它不写任何实体状态、不使用 RNG、不 include 环境/存档/账本层（`src/decision/IDecisionPolicy.h:2-4`）。

### 1 公共契约

#### 1.1 接口签名

```cpp
virtual ActionVector decide(const SensorVector& sensors, const TargetList& targets) = 0;   // 主接口（虚）
ActionVector decide(const SensorVector& sensors);                                          // 兼容重载（非虚）
virtual const char* name() const = 0;
```

- 双参主接口：`src/decision/IDecisionPolicy.h:30`。
- 单参兼容重载是**非虚**函数，函数体内用一个函数局部 `static const TargetList empty{}` 转发到双参版本（`src/decision/IDecisionPolicy.h:34-37`）。因此单参调用等价于"空目标表"，任何合规策略在空表下都不得产出施力意图。
- 每个具体策略都必须写 `using IDecisionPolicy::decide;` 把基类非虚重载引入派生类作用域，否则同名派生 `decide` 会隐藏基类重载（`src/decision/IDecisionPolicy.h:15-17`）。
- `decide` **不是 const 成员函数**（基类声明即无 const 限定，`src/decision/IDecisionPolicy.h:30,34`），因为它可以改自身内部状态（见 1.5）；只有 `name()` 是 const（`src/decision/IDecisionPolicy.h:39`）。

#### 1.2 输入：SensorVector 的 24 维

`SensorVector::kDim = 24`（`src/core/SensorTypes.h:46`），分量语义由感知层 `src/perception/AgentSensors.cpp:81-252` 填充。有效感知半径

$$R = \begin{cases} 0.25\,R_{\text{base}}, & \text{scoutCount} \le 0 \\ \min\left(R_{\text{base}}\sqrt{1+n},\; 4R_{\text{base}}\right), & \text{scoutCount}=n>0 \end{cases}$$

其中 $R_{\text{base}}$ = `cfg.policy.senseRadius`，默认 24.0（`src/core/Config.h:45`），实现见 `src/perception/AgentSensors.cpp:74-79`。

| 下标 | 含义 | 计算式 | 值域 | 源码位置 |
|---|---|---|---|---|
| $s_0,s_1$ | 最近蓝细菌节点方向（候选 0） | $(\Delta x/d,\ \Delta y/d)$， $\Delta$ 为最小环面位移 | 单位向量 | `src/perception/AgentSensors.cpp:137-139` |
| $s_2,s_3$ | 候选 1 方向 | 同上 | 单位向量 | 同上 |
| $s_4,s_5$ | 候选 2 方向 | 同上 | 单位向量 | 同上 |
| $s_6,s_7,s_8$ | 候选 0/1/2 接近度 | $\mathrm{clamp01}(1-d_i/R)$ | $[0,1]$ | `src/perception/AgentSensors.cpp:141-143` |
| $s_9$ | 最近节点归一化距离 | $\min(1, d_0/R)$；无节点恒 0 | $[0,1]$ | `src/perception/AgentSensors.cpp:175-179` |
| $s_{10},s_{11}$ | 速度分量（缩放） | $v_x/(1+\lvert v\rvert),\ v_y/(1+\lvert v\rvert)$ | $\lVert(s_{10},s_{11})\rVert=s_{12}$ | `src/perception/AgentSensors.cpp:182-186` |
| $s_{12}$ | 速度归一化模长 | $\lvert v\rvert/(1+\lvert v\rvert)$ | $[0,1)$ | `src/perception/AgentSensors.cpp:187` |
| $s_{13}$ | 距最近"边界"归一化距离 | $\mathrm{clamp01}\!\left(\dfrac{\min(x,\,W-x,\,y,\,H-y)}{0.5\min(W,H)}\right)$ | $[0,1]$ | `src/perception/AgentSensors.cpp:191-196` |
| $s_{14}$ | 本帧读噪声幅度（元信息，非随机数） | $\text{amp}$ | $[0, 0.6)$（上确界，速度无穷时趋近） | `src/perception/AgentSensors.cpp:201` |
| $s_{15}$ | 保留维 | 0 | $\{0\}$ | `src/perception/AgentSensors.cpp:203` |
| $s_{16}$ | 所在点温度失真度 $d$ | `temp.distortion(pos)` | $[0,1]$ | `src/perception/AgentSensors.cpp:207` |
| $s_{17}$ | 归一化温度采样 | $\mathrm{clamp01}(\text{temp.sample} + \text{terrain.temperatureOffset})$ | $[0,1]$ | `src/perception/AgentSensors.cpp:208-210` |
| $s_{18}$ | 最近邻核距离占基因视距比 | $\mathrm{clamp01}(d_{\text{nbr}}/\text{sightRadius})$；无邻居恒 1 | $[0,1]$ | `src/perception/AgentSensors.cpp:216-233` |
| $s_{19},s_{20}$ | 最近邻核方向；无邻居恒 0（零向量即哨兵） | 单位向量 | $[-1,1]$ | `src/perception/AgentSensors.cpp:221-229` |
| $s_{21}$ | 自身能量比 | $\mathrm{clamp01}(E/E_{\text{cap}})$ | $[0,1]$ | `src/perception/AgentSensors.cpp:236` |
| $s_{22}$ | 可施力球密度 | $\text{count}/8$ | $[0,1]$ | `src/perception/AgentSensors.cpp:238-243` |
| $s_{23}$ | 最近可施力球距离 | $\mathrm{clamp01}(d_{\text{ball}}/R)$；无目标恒 1 | $[0,1]$ | `src/perception/AgentSensors.cpp:245-249` |

读噪声幅度： $k_{\text{readNoiseK}}=0.15$（`src/perception/AgentSensors.cpp:40`）， $k_{\text{TempGain}}=3.0$（`src/perception/AgentSensors.cpp:44`）：

$$\text{amp} = k_{\text{readNoiseK}}\cdot\frac{\lvert v\rvert}{1+\lvert v\rvert}\cdot(1 + k_{\text{TempGain}}\,d)$$

噪声只叠加到 $s_{0..9}$（`src/perception/AgentSensors.cpp:59-66, 144-179`）； $s_{18..23}$ 是**无 RNG 副作用**的确定性尾部。噪声叠加后 $s_{0..5}$ 会重新归一化（`src/perception/AgentSensors.cpp:159-170`），所以策略拿到的候选方向始终是单位向量。

#### 1.3 输入：TargetList（可施力球清单）

| 字段 | 含义 | 源码位置 |
|---|---|---|
| `count` | 有效项数， $\in[0,8]$ | `src/core/SensorTypes.h:33` |
| `items[i].id` | 球实体 id | `src/core/SensorTypes.h:21` |
| `items[i].dx/dy` | 核→球的**最近像**环面位移 | `src/core/SensorTypes.h:22-23` |
| `items[i].dist` | $\sqrt{dx^2+dy^2}$ | `src/core/SensorTypes.h:24` |
| `items[i].type` | 0=Resource 1=Scout 2=Shield | `src/core/SensorTypes.h:25` |

`kMaxForceTargets = 8`（`src/core/SensorTypes.h:18`）。排序契约：**按环面距离升序，`items[0]` 最近**（`src/core/SensorTypes.h:17`）。生产路径由组合根 `src/sim/Simulation.cpp:356-388` 用插入排序维护。组合根只把 `freeBalls_` 中 `ownerId == 0` 的自由球放进清单（`src/sim/Simulation.cpp:363`），且只收录 `genome.forceRange` 内的球（`src/sim/Simulation.cpp:358-366`）。

两点实现与注释的偏差，影响策略可达的施力目标集合：

- `src/sim/Simulation.cpp:395` 的注释写"本核 forceRange 内的球（自由球 + 自己的球）"，但实际循环只遍历 `freeBalls_` 并 `continue` 掉 `ownerId != 0` 的项（`:362-363`），所以**自己名下的轨道球永远不在清单里**，策略只能推自由球。
- `ForceTarget.type` 被填了值（`:377`），但八个策略没有一个读它——`ForageBankPolicy` 只读 `dx/dy`，`RiskAversePolicy` 只读 `dist/dx/dy`。

#### 1.4 输出：ActionVector 的动作空间

动作空间是**连续 3 组**，没有离散动作枚举。

**A 组：移动意图**

| 字段 | 含义 | 行为层消费方式 | 源码位置 |
|---|---|---|---|
| `dirX, dirY` | 期望方向（未归一化也可） | 叠加写噪声 → 单位化 → 乘 `moveK` | `src/core/SensorTypes.h:53-54`；`src/behavior/AgentBehavior.cpp:303-331` |
| `boost` | 冲刺意愿 | 钳到 $[0,1]$ 后放大限速为 $v_{\max}(1+0.5\,\text{boost})$ | `src/core/SensorTypes.h:55`；`src/behavior/AgentBehavior.cpp:320-324` |

**B 组：点名施力意图**

| 字段 | 含义 | 默认 | 源码位置 |
|---|---|---|---|
| `forceSlot` | TargetList 下标； $-1$ = 本帧不施力 | $-1$ | `src/core/SensorTypes.h:65` |
| `forceX, forceY` | 期望加速度矢量（未归一化） | 0 | `src/core/SensorTypes.h:66-67` |

**C 组：社会参数调制**

`mod[4]`，每项 $\in[-1,1]$：`mod[0]`→`nucleusSightRadius`、`mod[1]`→`nucleusPersonalSpace`、`mod[2]`→`nucleusRepelStrength`、`mod[3]`→`nucleusCohesion`（`src/core/SensorTypes.h:69-77`）。基因本身不被改写（表型可塑，非拉马克式），调制每帧重算、不消耗能量。

#### 1.5 调用时机与实例模型

组合根在 `Simulation::step()` 中对**每个存活核每 tick 恰好调用一次**：

```
for (每个存活核 i) {
    TargetList targets = ...;                        // Simulation.cpp:356-388
    SensorVector sv = sense(...);                    // Simulation.cpp:419-421
    ActionVector act = policy_->decide(sv, targets); // Simulation.cpp:422
    AgentBehavior::socialAccelerations(...);         // Simulation.cpp:428  （读 act.mod）
    if (act.forceSlot >= 0) applyNamedForce(...);    // Simulation.cpp:432-445（读 forceSlot/forceX/forceY）
    AgentBehavior::apply(n, act, ...);               // Simulation.cpp:446（读 dirX/dirY/boost）
}
```

**关键事实**：整个仿真只持有**一个**策略实例 `std::unique_ptr<IDecisionPolicy> policy_`（`src/sim/Simulation.h:141`），所有核按 `nuclei_` 下标顺序共享它。因此有状态的策略（Hysteresis / ForageBank / Bandit / RiskAverse）的内部记忆是**全种群共享的滚动状态**，不是每核独立状态：第 $i$ 个核读到的"上一帧"其实是同一帧里第 $i-1$ 个核写下的（ $i=0$ 时是上一 tick 最后一个核写下的）。这一点直接决定了第 4、7、8、9 节中各策略失效模式的形状。

#### 1.6 策略装配

`src/sim/Simulation.cpp:237-266` 按 `cfg.policy.name` 逐个 if/else 装配，顺序敏感（`usePerceptron` 分支在 `name == "stub"` 之前）：

| `--policy` 值 | 装配类 | 构造实参 | 源码位置 |
|---|---|---|---|
| `perceptron`（或 `usePerceptron`） | PerceptronPolicy | `(perceptronSeed, perceptronScale)` | `src/sim/Simulation.cpp:237-239` |
| `stub` | StubPolicy | 无 | `src/sim/Simulation.cpp:240-241` |
| `thermal`（或 `useThermal`） | ThermalPolicy | `Params{preferredTemp, wThermal, wCaution}` | `src/sim/Simulation.cpp:242-249` |
| `hysteresis` | HysteresisPolicy | 默认 `Params{}` | `src/sim/Simulation.cpp:250-252` |
| `foragebank` | ForageBankPolicy | 默认 `Params{}` | `src/sim/Simulation.cpp:253-255` |
| `bandit` | BanditPolicy | 默认 `(eta=0.05, {}, 0.0)` | `src/sim/Simulation.cpp:256-259` |
| `riskaverse` | RiskAversePolicy | 默认 `Params{}` | `src/sim/Simulation.cpp:260-263` |
| 其他（含默认 `rule`） | RulePolicy | 默认 `(1.0, 0.8, 0.6)` | `src/sim/Simulation.cpp:264-266` |

默认策略名 `cfg.policy.name = "rule"`（`src/core/Config.h:49`）；CLI 合法值集合见 `src/app/Cli.cpp:146-148`。**只有 perceptron / thermal 的参数能从 CLI 改**（`src/app/Cli.cpp:164-192`）；hysteresis / foragebank / bandit / riskaverse 的 `Params` 在生产路径上**恒为默认值**，其 R2 调制强度（`kModFocus` / `kModSight` / `kModExplore` 等）在生产运行中全部为 0。

### 2 动作如何被行为层执行

决策层只写"意图"；全部校验、扣费、实体写入归 `AgentBehavior`（唯一写点）。

#### 2.1 方向与 boost（`AgentBehavior::apply`）

**① 写通道扰动**（`src/behavior/AgentBehavior.cpp:306-312`）：

$$\text{amp} = k_w\cdot\frac{\lvert v\rvert}{1+\lvert v\rvert},\qquad k_w = 0.25$$

$$a.\text{dirX} \mathrel{+}= \text{amp}\,(2u_1 - 1),\qquad a.\text{dirY} \mathrel{+}= \text{amp}\,(2u_2 - 1),\qquad u \sim U(0,1)$$

`bp.writeNoiseK = 0.25`（`src/behavior/AgentBehavior.h:159`）是行为层默认值；生产路径**没有**把它从 Config 镜像进 `bp_`（`src/sim/Simulation.cpp:205-212` 只镜像 8 项，不含它）。

**② 移动积分**（`src/behavior/AgentBehavior.cpp:320-360`）：

$$\text{boost}' = \mathrm{clamp01}(\text{boost}),\qquad v_{\max}^{\text{eff}} = g_{\text{maxSpeed}}\,(1 + 0.5\,\text{boost}')$$

$$\hat u = \frac{(a_x, a_y)}{\lVert(a_x,a_y)\rVert},\qquad \tilde v = \hat u\,k_{\text{moveK}} + a_{\text{social}},\qquad k_{\text{moveK}} = 60.0$$

$$\lVert\tilde v - v_t\rVert > g_{\text{maxAccel}} \Rightarrow \tilde v = v_t + (\tilde v - v_t)\cdot\frac{g_{\text{maxAccel}}}{\lVert\tilde v - v_t\rVert}$$

$$\lVert\tilde v\rVert > v_{\max}^{\text{eff}} \Rightarrow \tilde v \mathrel{*}= \frac{v_{\max}^{\text{eff}}}{\lVert\tilde v\rVert}$$

$$v_{t+1} = \text{damping}\cdot\tilde v,\qquad p_{t+1} = \mathrm{wrap}\left(p_t + v_{t+1}\,dt\right)$$

`moveK = 60.0`（`src/behavior/AgentBehavior.h:147`）；`damping` 默认 1.0（`src/behavior/AgentBehavior.h:154`），由 `cfg.nucleus.damping = 1.0` 构造期镜像（`src/core/Config.h:177`、`src/sim/Simulation.cpp:211`）；`dt = 1.0`（`src/sim/Simulation.cpp:335`）。

`boost` 只缩放**限速上限**，不直接缩放速度；实际速度还受 `damping`、`maxAccel` 与社会加速度共同约束。当 $\lVert v\rVert = 0$ 时 amp = 0，零方向不会被写噪声激活——"原地不动"在行为层是一个稳定不动点。

**③ 耗能**（`src/behavior/AgentBehavior.cpp:362-409`，供后面 RiskAverse 对照）：

$$\text{loco} = c_{\text{loco}}\,\lVert v\rVert^{\beta}\,dt\cdot m_{\text{terrain}},\qquad c_{\text{loco}} = 2\times10^{-4},\ \beta = 2.0$$

$$\text{resp} = \left(\frac{c_{\text{resp}}}{1 + 0.35\,n_{\text{shield}}} + c_{\text{respK}}\lVert v\rVert^2\right)(1 + K_{\text{resp}}\,d)\,dt$$

`locomotionCost = 0.0002`（`src/core/Config.h:171`）、`respirationBase = 0.03`（`src/core/Config.h:170`）、`respirationK = 0.0004`（`src/behavior/AgentBehavior.h:157`）、`respirationTempK = 1.5`（`src/behavior/AgentBehavior.h:158`，生产未镜像，恒为 1.5）。

#### 2.2 点名施力（`AgentBehavior::applyNamedForce`）

校验链与结算（`src/behavior/AgentBehavior.cpp:658-693`）：

1. `slot < 0` → 拒绝（不花能量的唯一方式）；`slot >= targets.count` → 拒绝；`ft.id == 0` → 拒绝。
2. 环距 $d$ 必须 $\le g_{\text{forceRange}}$，用调用方在**施力时刻重取**的环距而非感知快照（`src/sim/Simulation.cpp:439`）。
3. 意图模长 $m_0 = \lVert(\text{forceX},\text{forceY})\rVert$； $m_0 \le 0$ 拒绝。
4. 上限： $m = \min\left(m_0,\ \min(g_{\text{maxAccel}},\, \text{bp.forceMax})\right)$，`bp.forceMax = 8.0`（`src/behavior/AgentBehavior.h:191`）。
5. 代价 $\text{cost} = g_{\text{forceCostK}}\cdot m^2$；若 $\text{cost} > E$ 则 $m \leftarrow \sqrt{E/g_{\text{forceCostK}}}$ 并重算 cost，能量永不为负。
6. `ledger.dissipate("applyForce", cost)`，然后 $v_{\text{ball}} \mathrel{+}= \hat a\,m$。

策略写出的 `forceX/forceY` 的**模长被当作期望加速度大小**，超过 $\min(g_{\text{maxAccel}}, 8)$ 的部分被截断；方向被重新单位化。

#### 2.3 社会调制（`AgentBehavior::socialAccelerations`）

`src/behavior/AgentBehavior.cpp:696-724`：

$$m_k = \mathrm{clamp}(-1,1,\text{mod}[k]),\qquad \text{eff}_k = \text{base}_k\,(1 + \kappa\,m_k),\qquad \kappa = 0.4$$

`kSocialModLimit = 0.4`（`src/behavior/AgentBehavior.cpp:29`）； $\text{base}_k$ 取基因值，非法时回退 170.0 / 47.0 / 3.0 / 1.5（`src/behavior/AgentBehavior.cpp:713-720`）。调制后的有效值只缓存到 `n.effSight/effSpace/effRepel/effCohesion`，**不回写 genome**。

社会力（`src/behavior/AgentBehavior.cpp:731-761`）：

$$d < \text{effSpace} \Rightarrow a \mathrel{+}= \text{effRepel}\left(1 - \frac{d}{\text{effSpace}}\right)\hat u_{j\to i}$$

$$\text{effSpace} \le d < \text{effSight} \ \wedge\ \text{effCohesion} > 0 \Rightarrow a \mathrel{+}= \text{effCohesion}\left(1 - \frac{d}{\text{effSight}}\right)\hat u_{i\to j}$$

社会力不耗能、不消耗 RNG、无能量转移；与社会加速度一起在 `apply` 步骤 2 统一限幅积分（两阶段积分，消除核间顺序依赖）。

#### 2.4 八个策略对三条通道的使用矩阵

| 策略 | dir / boost | forceSlot | mod[] 下标 | 是否读 targets |
|---|---|---|---|---|
| rule | 是 | 否（恒 $-1$） | 否（全 0） | 否（形参未命名，`RulePolicy.cpp:28`） |
| hysteresis | 是 | 否 | `mod[1]`（`kModFocus`，默认 0） | 否 |
| perceptron | 是 | 否 | 否（全 0） | 否 |
| thermal | 是 | 否 | `mod[0]/mod[1]/mod[3]`（默认 0） | 否 |
| foragebank | 是 | **是**（觅食态取 `targets[0]`） | `mod[0]`（默认 0） | 是 |
| bandit | 是（两臂混合） | 否 | `mod[3]`（`kModExplore`，默认 0） | 否（且不透传给两臂） |
| riskaverse | 是 | **是**（Earn 态"扫路"） | `mod[0]/mod[3]`（默认 0） | 是 |
| stub | 否（全 0） | 否 | 否 | 否 |

### 3 RulePolicy（规则打分）

#### 3.1 核心思想

对 3 个食物候选取加权分，取最高分候选的方向；分数 = 接近度（受温度失真打折）+ 与当前航向的对齐度 + 紧迫度。纯确定性，零内部状态。

#### 3.2 完整算式

设 $p_i = \mathrm{clamp01}(s_{6+i})$、 $\mathbf d_i = (s_{2i}, s_{2i+1})$、 $d = \mathrm{clamp01}(s_{16})$、 $e = \mathrm{clamp01}(s_{12})$。

紧迫度与航向单位向量（`src/decision/RulePolicy.cpp:42-50`）：

$$\text{urg} = 1 - e,\qquad \hat h = \begin{cases}(s_{10}/s_{12},\ s_{11}/s_{12}), & s_{12} > 10^{-9}\\ (0,0), & \text{否则}\end{cases}$$

候选打分（`src/decision/RulePolicy.cpp:57-77`）：

$$\text{align}_i = \begin{cases}\mathbf{d}_i\cdot\hat h, & s_{12} > 10^{-9}\\ 0, & \text{否则}\end{cases}$$

$$\text{trust} = 1 - 0.5\,d$$

$$\text{score}_i = w_P\cdot\text{trust}\cdot p_i \;+\; w_A\cdot\text{align}_i \;+\; w_U\cdot\text{urg}$$

选择：遍历 $i = 0,1,2$，跳过 $s_{6+i} \le 0$ 的空槽；第一个有效候选无条件选中（`first` 标志），之后严格 `score > bestScore` 才替换——**并列时下标小者优先**（`src/decision/RulePolicy.cpp:71-76`）。因为第一个候选用 `first` 无条件置位，即使所有候选分数为负也总会选出一个方向。

输出（`src/decision/RulePolicy.cpp:79-93`）：

$$a.\text{dirX} = d_{i^*,x},\qquad a.\text{dirY} = d_{i^*,y},\qquad a.\text{boost} = \text{urg}$$

拥挤让速（仅当 `s[19] != 0 || s[20] != 0`，即感知层报告存在邻居时）：

$$s_r = \mathrm{clamp01}(s_{18}),\qquad a.\text{boost} \mathrel{*}= 0.5 + 0.5\,s_r \ \in [0.5, 1]$$

（`src/decision/RulePolicy.cpp:88-93`）。`if (!(sr > 0.0)) sr = 0.0` 让 NaN 归 0，按"完全贴脸"处理。

无任何候选（ $s_6, s_7, s_8$ 全 $\le 0$）时直接返回默认构造的 `ActionVector`：方向零、boost 0（`src/decision/RulePolicy.cpp:32-38`）。

#### 3.3 内部状态

无。`decide` 只读成员 `wP_ / wA_ / wU_`，整个函数体内没有任何成员写入（`src/decision/RulePolicy.cpp:28-95`）。同一输入永远产出同一输出。

#### 3.4 参数表

| 符号 | 名称 | 默认值 | 单位/范围 | 源码位置 |
|---|---|---|---|---|
| $w_P$ | 接近度权重 | 1.0 | 无量纲，无钳制 | `src/decision/RulePolicy.h:18` |
| $w_A$ | 对齐权重 | 0.8 | 无量纲，无钳制 | `src/decision/RulePolicy.h:18` |
| $w_U$ | 紧迫度权重 | 0.6 | 无量纲，无钳制 | `src/decision/RulePolicy.h:18` |
| — | 温度信任折扣系数 | 0.5 | 硬编码 | `src/decision/RulePolicy.cpp:69` |
| — | 航向有效阈值 | $10^{-9}$ | 硬编码 | `src/decision/RulePolicy.cpp:49` |
| — | 拥挤让速下限 | 0.5 | 硬编码 | `src/decision/RulePolicy.cpp:92` |

三个权重**没有清洗**：构造函数直接赋值（`src/decision/RulePolicy.cpp:25-26`），不像其它策略那样做非负钳制。生产路径用默认值，CLI 无法改。

#### 3.5 失效模式

- **无食物 = 完全静默**：候选全空时输出零方向、零 boost，没有任何探索项，核只能等（`src/decision/RulePolicy.cpp:36-38`）。
- **高失真下系统性误判**：trust 最低只到 0.5，接近度项永远压不到 0，仍会按被污染读数扑向错误方向。
- **紧迫度是伪能量**： $1 - s_{12}$ 只表示"跑得慢"，与真实能量无因果关系；静止且能量充足时 urg = 1（最大冲刺意愿），高速滑行时 urg ≈ 0。
- **不施力、不调制**：对 R2 两条新通道零表达（`src/decision/RulePolicy.h:9-10`）。

### 4 HysteresisPolicy（滞回选择）

#### 4.1 核心思想

在规则打分骨架上加一条"现任保护"规则：挑战者必须比现任高出 `hysteresisGap` 才允许换目标，用分数轴上一条宽度为 gap 的滞回带消除两候选接近时的每帧方向翻转。

#### 4.2 完整算式

`clean(x) = isfinite(x) ? x : 0.0`（`src/decision/HysteresisPolicy.cpp:29`）。候选得分（与 RulePolicy 同源，但**缺紧迫度项**，且做双边归一化，`src/decision/HysteresisPolicy.cpp:54-69`）：

$$p_i = \text{clean}(s_{6+i}),\qquad \mathbf{v} = (\text{clean}(s_{10}),\text{clean}(s_{11})),\qquad \mathbf{d}_i = (\text{clean}(s_{2i}),\text{clean}(s_{2i+1}))$$

$$\text{align}_i = \begin{cases}\dfrac{\mathbf{d}_i\cdot\mathbf{v}}{\lVert\mathbf{d}_i\rVert\,\lVert\mathbf{v}\rVert}, & \lVert\mathbf{d}_i\rVert > 10^{-9} \wedge \lVert\mathbf{v}\rVert > 10^{-9}\\ 0, & \text{否则}\end{cases}$$

$$\text{score}_i = w_P\,p_i + w_A\,\text{align}_i$$

**第 1 步：找当帧最高分**（`src/decision/HysteresisPolicy.cpp:76-83`）。遍历有效槽位（ $p_i > 0$），条件 `best < 0 || sc > bestScore`，并列时下标小者优先。若一个有效候选都没有：输出零向量，且**不修改** `lastChoice_`（食物只是暂时离开视野，回来时延续旧选择），并按需写 `mod[1] = clampSym1(-kModFocus)`（`src/decision/HysteresisPolicy.cpp:84-88`）。

**第 2 步：滞回判定**（`src/decision/HysteresisPolicy.cpp:89-94`）：

$$\text{若 } c = \text{lastChoice} \in [0,2] \ \wedge\ s_{6+c} > 0:\quad \text{score}_{\text{best}} \le \text{score}_c + \text{gap} \ \Rightarrow\ i^* \leftarrow c$$

否则 $i^* = \text{best}$。随后 `lastChoice_ = i*`。

**第 3 步：输出**（`src/decision/HysteresisPolicy.cpp:95-110`）：

$$\hat u = \frac{\mathbf{d}_{i^*}}{\lVert\mathbf{d}_{i^*}\rVert}\quad (\lVert\cdot\rVert > 10^{-9}),\qquad a.\text{boost} = p_{i^*}$$

$$a.\text{mod}[1] = \mathrm{clamp}\!\left(-1,1,\ k_{\text{ModFocus}}\,(2p_{i^*} - 1)\right)\quad (k_{\text{ModFocus}} \ne 0)$$

注意实现与头文件注释不一致：`src/decision/HysteresisPolicy.h:28-29` 的注释写"focus 高 → `+k*focus`、focus 低 → `-k*(1-focus)`"，但代码是 `k*(2*prox-1)`，即 $p = 1$ 时 $+k$、 $p = 0$ 时 $-k$；无候选分支固定写 $-k$。

#### 4.3 内部状态

| 变量 | 含义 | 更新方程 | 初值 | 源码位置 |
|---|---|---|---|---|
| `lastChoice_` | "现任"候选下标 | 每帧 `lastChoice_ = i*`；无候选帧不变 | $-1$ | `src/decision/HysteresisPolicy.h:50`；`.cpp:94` |

`reset()` 把 `lastChoice_` 置 $-1$（`src/decision/HysteresisPolicy.h:46`）。

#### 4.4 参数表

构造期清洗：非有限归 0，`gap / wProximity / wAlignment` 负值归 0，`kModFocus` 夹到 $[-1,1]$（`src/decision/HysteresisPolicy.cpp:40-52`）。

| 符号 | 名称 | 默认值 | 单位/范围 | 源码位置 |
|---|---|---|---|---|
| gap | 切换门槛 | 0.08 | $\ge 0$（负值归 0） | `src/decision/HysteresisPolicy.h:25` |
| $w_P$ | 接近度权重 | 1.0 | $\ge 0$ | `src/decision/HysteresisPolicy.h:26` |
| $w_A$ | 对齐权重 | 0.8 | $\ge 0$ | `src/decision/HysteresisPolicy.h:27` |
| $k_{\text{ModFocus}}$ | 聚焦调制强度 | 0.0（关闭） | $[-1,1]$ | `src/decision/HysteresisPolicy.h:30` |

#### 4.5 失效模式

- **"现任"是全种群共享的**：`lastChoice_` 挂在共享实例上（1.5 节），下一个核读到的"现任"是同一帧里上一个核的选择，滞回语义在种群层面被稀释成"群体惯性"。核数 $n = 1$ 时才是设计意图。
- **gap 造成目标粘滞**：食物快速闪现/消失时，滞回带让它继续扑向已变差的目标，直到挑战者优势足够。
- **永不施力、不做社会调制**（除可选的 `mod[1]`）。
- **无紧迫度项**：能量耗尽与能量充足时行为完全一致，boost 只等于接近度。

### 5 PerceptronPolicy（感知机）

#### 5.1 核心思想

两个线性神经元对 24 维感知做加权和，输出二维方向与一维 boost。**权重在整个运行期固定**，只由构造期的种子与幅度决定。

#### 5.2 完整算式

权重矩阵 $\mathbf{W}\in\mathbb{R}^{2\times d}$、偏置 $\mathbf{b}\in\mathbb{R}^2$， $d$ = "已配置前缀" `kDim_`（默认 24）。

前向（`src/decision/PerceptronPolicy.cpp:64-79`）：

$$Z_i = b_i + \sum_{j=0}^{d-1} w_{ij}\,s_j \;+\; \sum_{j=18}^{23} w_{ij}\,s_j$$

**第二个求和项是独立追加的**（`extended()`，`src/decision/PerceptronPolicy.cpp:32-37`），而第一个求和已经覆盖 $j < d$。因此当 $d = 24$（默认构造与种子构造的实际取值）时， $s_{18..23}$ 的贡献被**计入两次**，等效权重为 $2w_{ij}$；只有 $d \le 18$（旧文本反序列化得到的对象）时才是单次计入。这是实现的真实算式。

输出映射（`src/decision/PerceptronPolicy.cpp:77-90`）：

$$a_x = \mathrm{clamp01}\!\left(\frac{Z_0+1}{2}\right)\cdot 2 - 1 \;=\; \mathrm{clamp}(Z_0, -1, 1)$$

$$a_y = \mathrm{clamp01}\!\left(\frac{Z_1+1}{2}\right)\cdot 2 - 1 \;=\; \mathrm{clamp}(Z_1, -1, 1)$$

**这不是 $\mathrm{sign}(Z)$**：头注释写 $y = \mathrm{sign}(Z)$（`src/decision/PerceptronPolicy.h:2-3`），但代码是线性截断的饱和函数，只有 $\lvert Z\rvert \ge 1$ 时才退化为 $\pm 1$； $Z = 0$ 时输出 0 而非 $+1$（`tests/unit/test_perceptron.cpp:66-76` 用断言钉住了这一点；`src/decision/PerceptronPolicy.cpp:5` 的注释也写的是 $Z = 0 \to +1$ 这一点，与实现在 $Z=0$ 处的数值 0 并不一致）。

$$\text{boost} = \begin{cases}0.5, & Z_0 + Z_1 > 0\\ 0.0, & \text{否则}\end{cases}$$

方向归一化（`src/decision/PerceptronPolicy.cpp:83-90`）：

$$\lVert(a_x,a_y)\rVert < 10^{-12} \Rightarrow a.\text{dir} = (0,0);\qquad \text{否则}\ a.\text{dir} = \frac{(a_x,a_y)}{\lVert(a_x,a_y)\rVert}$$

权重随机初始化（仅构造期，`src/decision/PerceptronPolicy.cpp:52-62`）：

$$w_{ij} = (2u - 1)\cdot\text{scale},\qquad b_i = (2u - 1)\cdot\text{scale},\qquad u \sim \mathrm{PCG32.nextDouble}()$$

`perceptronSeed = 12345`、`perceptronScale = 0.1`（`src/core/Config.h:52-53`）。遍历顺序是 $i$ 外层、 $j$ 内层，先 $b_i$ 后 $w_{i\cdot}$，固定顺序，同种子逐位可复现（`tests/unit/test_perceptron.cpp:99-107`）。

参数版构造（`src/decision/PerceptronPolicy.cpp:40-50`）只做维度钳制：`kDim_` $\in[1,24]$、`kOutputs_` $\in[1,2]$；`kDim_` 之后的列恒为 0（结构体默认零初始化，`src/decision/PerceptronPolicy.h:29`）。

#### 5.3 内部状态

`decide` 期无状态（`tests/unit/test_perceptron.cpp:172-182` 断言同一实例多次调用输出逐位不变）。权重与偏置是构造期常量。

#### 5.4 权重是否可遗传：**否**

三条证据：

1. `Genome` 的 24 个可遗传参数（`src/core/Genome.h:82-103`）里**没有**感知机权重/偏置字段；`genomeRandomize` / `genomeMutate` 的遍历顺序里也没有（`src/core/Genome.cpp:36-46`）。
2. `PerceptronPolicy(seed, scale)` 只在 `Simulation` 构造期被调用**一次**（`src/sim/Simulation.cpp:238-239`），此后没有任何代码改写 `w_` / `b_`。
3. `serialize()` / `deserialize()` 在生产路径上**无调用点**：全仓 grep 只命中定义处（`src/decision/PerceptronPolicy.cpp:95`、`:114`）与单测。繁殖函数 `AgentBehavior::reproduce` 只变异 `child.genome`（`src/behavior/AgentBehavior.cpp:273`），不触碰策略。

结论：权重从出生到死亡恒定，且全部个体共用**同一组**权重（共享实例，1.5 节）。`src/decision/PerceptronPolicy.h:5-10` 说的"进化押注在权重上"指的是**实验者通过 `--perceptron-seed` 扫参来选**，不是仿真内的遗传。

#### 5.5 参数表

| 符号 | 名称 | 默认值 | 单位/范围 | 源码位置 |
|---|---|---|---|---|
| $d$ | 已配置输入维 `kDim` | 24 | $[1,24]$ | `src/decision/PerceptronPolicy.h:27` |
| `kOutputs` | 输出神经元数 | 2 | $[1,2]$ | `src/decision/PerceptronPolicy.h:28` |
| $\mathbf{W}$ | 权重矩阵 | 全 0（Params 构造）/ 随机（种子构造） | $\mathbb{R}^{2\times24}$ | `src/decision/PerceptronPolicy.h:29` |
| $\mathbf{b}$ | 偏置 | 全 0 / 随机 | $\mathbb{R}^2$ | `src/decision/PerceptronPolicy.h:30` |
| seed | 初值种子 | 12345 | uint64 | `src/core/Config.h:52` |
| scale | 初值幅度 | 0.1 | $>0$， $w,b \in [-\text{scale},\text{scale}]$ | `src/core/Config.h:53` |
| — | boost 非零值 | 0.5 | 硬编码 | `src/decision/PerceptronPolicy.cpp:79` |
| — | 归一化零阈值 | $10^{-12}$ | 硬编码 | `src/decision/PerceptronPolicy.cpp:84` |

#### 5.6 失效模式

- **随机权重几乎必然导向无效觅食**：初值在 $[-0.1,0.1]$ 均匀、24 维线性叠加后 $Z$ 的模长通常远小于 1，输出方向对输入几乎不敏感；实测 8 策略对比中 6/6 seed 全灭（第 11 节）。
- **$s_{18..23}$ 双计**：默认 $d = 24$ 下扩展维权重效果是名义值的两倍，任何按"单次求和"做的调参直觉都会偏。
- **无门控**：没有"无目标时静止"的分支，即使 $s_{6..8}$ 全 0 也照样按权重投票产生方向。
- **boost 只有 0 / 0.5 两档**，与能量状态无关。
- 不施力、不调制社会参数。

### 6 ThermalPolicy（趋温）

#### 6.1 核心思想

把温度适宜度作为"资源品质"的代理信号：既影响候选打分（是否值得保持当前航向），又通过失真门控切换 boost。

#### 6.2 中间量

温度适宜度（`src/decision/ThermalPolicy.cpp:78-82`）：

$$\text{suit} = \mathrm{clamp01}\!\left(1 - \frac{\lvert s_{17} - T_{\text{pref}}\rvert}{0.5}\right)$$

失真规避因子（`src/decision/ThermalPolicy.cpp:84-86`）：

$$\text{caution} = 1 - \mathrm{clamp01}(s_{16})$$

舒适度（`src/decision/ThermalPolicy.cpp:90-94`），其中 $d = \mathrm{clamp01}(s_{16})$：

$$c = \mathrm{clamp}\!\left(-1,1,\ \text{suit}\,(1 - 0.5\,d) - 0.5\,d\right)$$

`clampSym1` 对 NaN 归 $-1$（`src/decision/ThermalPolicy.cpp:44-48`）。

#### 6.3 候选打分

`src/decision/ThermalPolicy.cpp:110-151`。设 $p_i = \mathrm{clamp01}(\text{clean}(s_{6+i}))$、 $\mathbf{v} = (\text{clean}(s_{10}),\text{clean}(s_{11}))$：

$$\text{stay}_i = \mathrm{clamp}\!\left(-1,1,\ \frac{\mathbf{d}_i\cdot\mathbf{v}}{\lVert\mathbf{d}_i\rVert\lVert\mathbf{v}\rVert}\right)\quad(\text{分母} > 10^{-9}),\qquad \text{否则}\ 0$$

$$\text{thermal}_i = \text{suit}\cdot(0.5 + 0.5\,\text{stay}_i)$$

$$\text{doubt} = 0.5\,w_C\,(1 - \text{caution}) \ \ge 0,\qquad \text{trustFood} = \frac{1}{1+\text{doubt}},\qquad \text{trustStay} = 1 + \text{doubt}$$

$$\text{score}_i = w_P\,p_i\,\text{trustFood} \;+\; w_T\,\text{thermal}_i\,\text{trustStay}$$

设计要点（`src/decision/ThermalPolicy.cpp:96-109, 133-147` 的两段修复注释）： $s_{17}$ 与 $s_{16}$ 都是"自身所在点"的标量，对三个候选相同，直接加进总分对 argmax 是常数，因此温度项被改造成**候选级**（乘 `0.5+0.5*stay`）；失真规避也不能作为独立加项（同理对 argmax 无影响），而是通过 `trustFood / trustStay` 调节两个候选级信号的相对强度。两式恒正，任何 $w_C \ge 0$ 都不会让接近度项变负。

#### 6.4 decide 流程

`src/decision/ThermalPolicy.cpp:155-212`：

**① 无目标早退**：若 `!(clean(s9) > 0)` 或三个候选全空 → 输出零方向、boost 0，但仍写出社会调制 $a.\text{mod}[0] = k_{\text{Sight}}\,c$、 $a.\text{mod}[1] = k_{\text{Space}}\,c$、 $a.\text{mod}[3] = k_{\text{Cohesion}}\,c$（`src/decision/ThermalPolicy.cpp:164-171`）。

**② 取最高分**：条件 `first || score > bestScore`，并列下标小者优先（`src/decision/ThermalPolicy.cpp:174-187`）。

**③ 方向**： $\hat u$ 是 $\mathbf{d}_{i^*}$ 的单位化向量；长度 $\le 10^{-9}$ 或非有限 → $(0,0)$（`src/decision/ThermalPolicy.cpp:189-197`）。

**④ 失真门控**（`kHighDistortionBoost = 0.3`、`kDistortionBoostGate = 0.5`，`src/decision/ThermalPolicy.cpp:51-52, 202`）：

$$a.\text{boost} = \begin{cases}0.3, & \mathrm{clamp01}(s_{16}) > 0.5\\ 0.0, & \text{否则}\end{cases}$$

注意这与注释描述的"降速"方向相反：**高失真时反而给 0.3 的正冲刺意愿，低失真时 boost = 0**。这是代码事实（`src/decision/ThermalPolicy.cpp:199-202`），尽管同处注释把它写作"高失真区降速，减少噪声中的无谓机动"。

**⑤ 社会调制**（`src/decision/ThermalPolicy.cpp:204-210`）：

$$a.\text{mod}[0] = \mathrm{clamp}\!\left(-1,1,\ k_{\text{Sight}}\,c\right),\quad a.\text{mod}[1] = \mathrm{clamp}\!\left(-1,1,\ k_{\text{Space}}\,c\right),\quad a.\text{mod}[3] = \mathrm{clamp}\!\left(-1,1,\ k_{\text{Cohesion}}\,c\right)$$

`mod[2]`（斥力）不参与（`src/decision/ThermalPolicy.cpp:206`）。

#### 6.5 内部状态

无。`decide` 只读 `p_`。

#### 6.6 参数表

构造期清洗：三个权重非有限归 0、负值归 0；`preferredTemp` 夹到 $[0,1]$；三个 `kMod*` 夹到 $[-1,1]$（`src/decision/ThermalPolicy.cpp:56-75`）。

| 符号 | 名称 | 默认值 | 单位/范围 | 源码位置 |
|---|---|---|---|---|
| $w_P$ | 接近度权重 | 1.0 | $\ge 0$ | `src/decision/ThermalPolicy.h:26` |
| $w_T$ | 温度适宜度权重 | 1.2 | $\ge 0$ | `src/decision/ThermalPolicy.h:27` |
| $w_C$ | 失真规避权重 | 0.8 | $\ge 0$ | `src/decision/ThermalPolicy.h:28` |
| $T_{\text{pref}}$ | 最适温度 | 0.5 | $[0,1]$ | `src/decision/ThermalPolicy.h:29` |
| $k_{\text{Sight}}$ | 视野调制强度 | 0.0（关闭） | $[-1,1]$ | `src/decision/ThermalPolicy.h:33` |
| $k_{\text{Space}}$ | 个人空间调制强度 | 0.0（关闭） | $[-1,1]$ | `src/decision/ThermalPolicy.h:34` |
| $k_{\text{Cohesion}}$ | 聚拢调制强度 | 0.0（关闭） | $[-1,1]$ | `src/decision/ThermalPolicy.h:35` |

生产路径的取值来自 `PolicyConfig`：`thermalPreferredTemp = 0.5`、`thermalWThermal = 1.2`、`thermalWCaution = 0.8`（`src/core/Config.h:56-59`），可分别由 `--thermal-pref` / `--thermal-w-thermal` / `--thermal-w-caution` 覆盖（`src/app/Cli.cpp:178-192`），装配见 `src/sim/Simulation.cpp:242-249`。三个 `kMod*` **没有**配置入口，恒为 0。

#### 6.7 失效模式

- **温度项对排序的影响受候选几何限制**： $\text{suit}$ 单因子决定 $w_T$ 项幅度，若 $w_T\,\text{suit}$ 小于接近度分差，`--thermal-pref` 改不动 argmax（这正是 `src/decision/ThermalPolicy.cpp:96-109` 记载的 R4-P1 修复所针对的问题）。
- **$s_{17}$ 恒定时倾向于"保持航向"**： $\text{stay}_i$ 奖励顺路候选，可能长期沿直线滑过食物。
- **boost 二值化且方向反直觉**：只有 0 或 0.3 两档；低失真（干净环境）时 boost = 0，意味着它在最该冲刺的环境里**永不冲刺**。
- **$s_9$ 早退**： $s_9$ 依赖候选 0 的距离，一旦 $s_9$ 被写 0（无节点）而 $s_{6..8}$ 仍有残留正值，会被早退拦住。

### 7 ForageBankPolicy（觅食-蓄能两态）

#### 7.1 核心思想

用合成的"能量代理量"把行为切成两态：代理量低 → 全力冲刺最近食物；代理量高 → 停止扩张、逆速缓行；中间带保持上一帧状态形成滞回。

#### 7.2 能量代理量

`src/decision/ForageBankPolicy.cpp:65-71`：

$$\text{margin} = \mathrm{clamp01}(\text{clean}(s_{13})),\qquad \text{thrift} = 1 - \mathrm{clamp01}(\text{clean}(s_{12}))$$

$$e = w_M\cdot\text{margin} + (1 - w_M)\cdot\text{thrift} \ \in [0,1]$$

默认 $w_M = 0.8$，因此 $e$ 主要由"离边界多远"（ $s_{13}$）决定，速度项只占 20%。 $s_9$（最近食物距离）**刻意不进代理**——它反映环境机会而非自身储备，计入会破坏滞回稳定性（`src/decision/ForageBankPolicy.h:13-14`）。

#### 7.3 两态滞回

`src/decision/ForageBankPolicy.cpp:76-81`：

$$e < \text{forageBelow} \Rightarrow \text{foraging} = 1;\qquad e > \text{bankAbove} \Rightarrow \text{foraging} = 0;\qquad \text{中间带保持上帧}$$

只有**严格不等**才切换； $e$ 恰等于阈值时保持。默认 `forageBelow = 0.3`、`bankAbove = 0.7`（`src/decision/ForageBankPolicy.h:38-39`）。

#### 7.4 三支输出

**① 觅食态**（`src/decision/ForageBankPolicy.cpp:82-108`）：

$$\hat u = \frac{(s_0,s_1)}{\lVert(s_0,s_1)\rVert}\ (\lVert\cdot\rVert > 10^{-9}),\qquad a.\text{boost} = 1.0$$

$$\text{若 } k_{\text{Pull}} > 0 \ \wedge\ \text{targets.count} > 0:\quad a.\text{forceSlot} = 0,\quad (a.\text{forceX},a.\text{forceY}) = k_{\text{Pull}}\cdot\frac{(t_{dx},t_{dy})}{\lVert(t_{dx},t_{dy})\rVert}$$

$$a.\text{mod}[0] = \mathrm{clamp}\!\left(-1,1,\ k_{\text{Sight}}\right)\quad(k_{\text{Sight}} \ne 0)$$

注意 `forceSlot = 0` 是**硬编码的 TargetList 下标**（契约：`items[0]` 最近），不是实体 id；施力方向恒为"把球拉向核"（沿核→球位移）。

**② 蓄能态（有速度）**（`src/decision/ForageBankPolicy.cpp:109-121`）：

$$a.\text{dir} = -\frac{(s_{10},s_{11})}{\lVert(s_{10},s_{11})\rVert},\qquad a.\text{boost} = 0,\qquad a.\text{mod}[0] = \mathrm{clamp}\!\left(-1,1,\ -k_{\text{Sight}}\right)$$

`forceSlot` 保持 $-1$。

**③ 蓄能态 + 速度近零 = 死锁保护**（`src/decision/ForageBankPolicy.cpp:122-155`）： $\lVert(s_{10},s_{11})\rVert \le 10^{-9}$ 时**强制置 `foraging_ = true`**，然后执行与觅食态完全一致的分支（方向 = 最近候选、boost 1.0、同样的施力与 $+k_{\text{Sight}}$ 调制）。

这是 P0 修复：核出生速度为 0，出生点的代理量又常落在 $[0.3, 0.7]$ 中间带（注释记载 seed42 的 12 个核全部 $e \in [0.316, 0.696]$，`src/decision/ForageBankPolicy.cpp:126-128`），保持默认蓄能态会永久静止，导致本策略与 StubPolicy **逐帧完全相同**（注释称 seed42 跑 2000 tick 数值一字不差，`src/decision/ForageBankPolicy.cpp:128-129`）。

#### 7.5 内部状态

| 变量 | 含义 | 更新方程 | 初值 | 源码位置 |
|---|---|---|---|---|
| `foraging_` | 是否觅食态 | 7.3 节的滞回；速度近零时强制置 `true` | `false`（蓄能） | `src/decision/ForageBankPolicy.h:62`；`.cpp:77-81, 134` |

`reset()` 置 `foraging_ = false`（`src/decision/ForageBankPolicy.h:58`）。

#### 7.6 参数表

构造期清洗：阈值 `clamp01` 且 `forageBelow > bankAbove` 时**交换**恢复良序；`wMargin` 夹 `clamp01`；`kPullBoost` 非负化；`kModSight` 夹 $[-1,1]$（`src/decision/ForageBankPolicy.cpp:46-63`）。

| 符号 | 名称 | 默认值 | 单位/范围 | 源码位置 |
|---|---|---|---|---|
| `forageBelow` | 觅食态进入阈值 | 0.3 | $[0,1]$ | `src/decision/ForageBankPolicy.h:38` |
| `bankAbove` | 蓄能态进入阈值 | 0.7 | $[0,1]$ | `src/decision/ForageBankPolicy.h:39` |
| $w_M$ | "处境余裕"权重 | 0.8 | $[0,1]$ | `src/decision/ForageBankPolicy.h:40` |
| $k_{\text{Pull}}$ | 觅食态施力强度 | 0.0（关闭） | $\ge 0$ | `src/decision/ForageBankPolicy.h:42` |
| $k_{\text{Sight}}$ | 视野调制强度 | 0.0（关闭） | $[-1,1]$ | `src/decision/ForageBankPolicy.h:43` |

装配时不给参数（`src/sim/Simulation.cpp:253-255`），所以生产路径尽用默认值： $k_{\text{Pull}} = k_{\text{Sight}} = 0$，施力与调制通道在生产中始终关闭。

#### 7.7 失效模式

- **"能量代理"不是能量**： $s_{13}$ 只表示"离地图边界多远"，而世界是环面拓扑，边界只是坐标卷绕缝，与风险无关（`src/perception/AgentSensors.cpp:189-190` 的注释也承认它是"占位的空间位置特征"）。真实能量维 $s_{21}$ 在默认参数下**完全不参与决策**。
- **蓄能态方向是"逆速"而非"回中心"**： $s_{13}$ 不含方位信息，严格反推中心不可行，"原地蓄能"只是净位移最小化（`src/decision/ForageBankPolicy.h:15-17`）。
- **状态全种群共享**：`foraging_` 只有一份（1.5 节），一个核的饥饿会把下一个核直接推入觅食态。
- **滞回带宽固定 0.4**：中间带很宽，环境缓慢漂移时可能长期卡在旧态。

### 8 BanditPolicy（Hedge / EXP3-lite 双臂加权）

#### 8.1 核心思想

把 RulePolicy 与 ThermalPolicy 当作两个"臂"，按历史回报做指数加权更新（对数空间 Hedge），输出方向是两臂单位方向按权重线性混合后再单位化。

#### 8.2 两臂与混合

`src/decision/BanditPolicy.cpp:137-156`。先对输入做非有限清洗 $s_j \leftarrow \text{clean1}(s_j)$，然后：

$$\text{ar} = \text{RulePolicy.decide}(s),\qquad \text{at} = \text{ThermalPolicy.decide}(s)$$

两处调用走的是**基类非虚单参重载**（空 TargetList），因此两臂拿不到施力目标；本策略也不把 targets 透传（`src/decision/BanditPolicy.cpp:144-145`）。

鲁棒单位化（先按最大分量缩放再求模，防 $x^2$ 溢出，`src/decision/BanditPolicy.cpp:84-96`）：

$$\text{unitOrZero}(x,y) = \begin{cases}(0,0), & m \le 10^{-12},\ m = \max(\lvert x\rvert,\lvert y\rvert)\\ \dfrac{(x/m,\ y/m)}{\sqrt{(x/m)^2+(y/m)^2}}, & \text{否则}\end{cases}$$

$$a.\text{dir} = \text{unitOrZero}\!\left(w_R\hat u_R + w_T\hat u_T\right),\qquad a.\text{boost} = \mathrm{clamp01}\!\left(w_R\,\text{ar.boost} + w_T\,\text{at.boost}\right)$$

#### 8.3 奖励定义（本策略的核心）

回报由两部分合成，全部只消费本帧 $s$、自存的上一帧 `prevS_`、上一帧自身输出（`src/decision/BanditPolicy.cpp:158-199`）。

**① 环境回报 $r_{\text{Env}}$**：

$$\text{curProx} = \max_{j=0,1,2}\mathrm{clamp01}(s_{6+j}),\qquad \text{prevProx} = \max_{j=0,1,2}\mathrm{clamp01}(\text{prevS}_{6+j})$$

$$\text{gain} = \text{curProx} - \text{prevProx},\qquad \text{speed} = \mathrm{clamp01}(s_{12})$$

$$r_{\text{Env}} = \mathrm{clamp}\!\left(-1,1,\ 0.6\,\text{gain} + 0.4\,\text{speed}\,(2\,\text{curProx} - 1)\right)$$

代码写的是 $0.4\cdot\text{speed}\cdot(2\,\text{curProx} - 1)$（`src/decision/BanditPolicy.cpp:170`），等价于 $0.4\,(\text{eff} - \text{waste})$，其中 $\text{eff} = \text{speed}\cdot\text{curProx}$、 $\text{waste} = \text{speed}\,(1 - \text{curProx})$（文件头 `src/decision/BanditPolicy.cpp:16-21` 记的正是这个展开形式）。

**② 每臂信用分配**：

$$\text{agree}_i = \mathrm{clamp}\!\left(-1,1,\ \hat u_i^{\text{prev}}\cdot a^{\text{prev}}\right)$$

$$\text{pot}_i = \frac{\sum_j p_j\,\max\!\left(0,\ \hat u_i^{\text{prev}}\cdot\hat d_j^{\text{prev}}\right)}{\sum_j p_j},\qquad p_j = \mathrm{clamp01}(\text{prevS}_{6+j})$$

若 $\sum_j p_j \le 10^{-12}$ 则 $\text{pot}_i = 0.5$（中性，不奖不罚）（`src/decision/BanditPolicy.cpp:179-196`）。

$$r_i = \mathrm{clamp}\!\left(-1,1,\ 0.5\,r_{\text{Env}}\,\text{agree}_i + (\text{pot}_i - 0.5)\right)$$

势能项是必需的：两臂体系下

$$\text{agree}_R - \text{agree}_T = (w_R - w_T)\left(1 - \hat u_R\cdot\hat u_T\right)$$

权重相等时两臂 agreement 恒相等，"结果 × 同意度"永远打不破 0.5/0.5 的对称（`src/decision/BanditPolicy.cpp:23-37`）。 $\text{pot}_i$ 只依赖上一帧感知，是每臂独立的反事实信号，提供打破对称所需的梯度。

#### 8.4 权重更新（Hedge / EXP3-lite）

`src/decision/BanditPolicy.cpp:201-217`：

$$L_i \leftarrow L_i + \mathrm{clamp}\!\left(-100,\,100,\ \eta\,r_i\right),\qquad L_i(0) = \ln 0.5$$

softmax（max-shift 稳定）→ 反塌零地板 → 重归一化：

$$p_i = \frac{e^{L_i - \max(L_R,L_T)}}{\sum_k e^{L_k - \max}},\qquad w_i \leftarrow \max(p_i,\ 10^{-6}),\qquad w_i \leftarrow \frac{w_i}{w_R + w_T}$$

`kWeightFloor = 1e-6`（`src/decision/BanditPolicy.cpp:99`），`kMaxLogStep = 100.0`（`src/decision/BanditPolicy.cpp:101`）。地板使得归一化后任一权重 $\ge \frac{10^{-6}}{1+10^{-6}} \approx 10^{-6}$。

首帧无历史：`hasPrev_ = false` 时**整段奖励估计与更新被跳过**，权重停在 0.5/0.5（`src/decision/BanditPolicy.cpp:159`）。

#### 8.5 内部状态

| 变量 | 含义 | 更新方程 | 初值 | 源码位置 |
|---|---|---|---|---|
| `logWR_` | 臂 R 对数权重 | $L_R \mathrel{+}= \mathrm{clamp}(\pm 100,\ \eta r_R)$ | $\ln 0.5$ | `src/decision/BanditPolicy.h:60` |
| `logWT_` | 臂 T 对数权重 | $L_T \mathrel{+}= \mathrm{clamp}(\pm 100,\ \eta r_T)$ | $\ln 0.5$ | `src/decision/BanditPolicy.h:61` |
| `wRule_` | 归一化权重 | softmax + 地板 + 重归一化 | 0.5 | `src/decision/BanditPolicy.h:62` |
| `wThermal_` | 归一化权重 | 同上 | 0.5 | `src/decision/BanditPolicy.h:63` |
| `hasPrev_` | 是否有上一帧 | 每帧末置 `true` | `false` | `src/decision/BanditPolicy.h:64` |
| `prevS_` | 上一帧清洗后的感知 | 每帧末 `prevS_ = s` | 全 0 | `src/decision/BanditPolicy.h:65` |
| `prevARule_` | 上一帧臂 R 原始输出 | 每帧末覆盖 | 默认 `ActionVector` | `src/decision/BanditPolicy.h:66` |
| `prevAThermal_` | 上一帧臂 T 原始输出 | 每帧末覆盖 | 默认 `ActionVector` | `src/decision/BanditPolicy.h:67` |
| `prevOut_` | 上一帧实际输出 | 每帧末覆盖 | 默认 `ActionVector` | `src/decision/BanditPolicy.h:68` |

`reset()` 把全部状态恢复到构造初态（`src/decision/BanditPolicy.cpp:125-135`）。

#### 8.6 参数表

| 符号 | 名称 | 默认值 | 单位/范围 | 源码位置 |
|---|---|---|---|---|
| $\eta$ | Hedge 学习率 | 0.05 | 构造期夹到 $[0,10]$ | `src/decision/BanditPolicy.h:42`；`.cpp:119-120` |
| $k_{\text{Explore}}$ | 探索调制强度 | 0.0（关闭） | $[-1,1]$ | `src/decision/BanditPolicy.h:43`；`.cpp:121-122` |
| — | 反塌零地板 | $10^{-6}$ | 硬编码 | `src/decision/BanditPolicy.cpp:99` |
| — | 单步对数上限 | 100.0 | 硬编码 | `src/decision/BanditPolicy.cpp:101` |
| — | $r_{\text{Env}}$ 配比 | 0.6 / 0.4 | 硬编码 | `src/decision/BanditPolicy.cpp:170` |
| — | 信用项权重 | 0.5 | 硬编码 | `src/decision/BanditPolicy.cpp:198` |
| — | 势能中性值 | 0.5 | 硬编码 | `src/decision/BanditPolicy.cpp:181-182` |

探索调制（`src/decision/BanditPolicy.cpp:229-232`）：

$$\text{uncertainty} = 4\,w_R\,w_T \in [0,1],\qquad a.\text{mod}[3] = \mathrm{clamp}\!\left(-1,1,\ k_{\text{Explore}}\cdot\text{uncertainty}\right)$$

$w_R = w_T = 0.5$ 时 uncertainty = 1；单臂饱和时趋 0。生产路径 `kModExplore` 恒 0（`src/sim/Simulation.cpp:256-259` 用默认参数构造）。

#### 8.7 失效模式

- **回报是"接近度变化"而非真实能量收支**： $r_{\text{Env}}$ 看不见移动与呼吸耗散，"值不值得"只是几何代理。
- **势能项不依赖真实结果**：只依赖上一帧感知与两臂方向，权重可以在没有真实反馈的情况下漂移。
- **共享状态在这里危害最大**：`prevS_ / prevOut_` 是"同一帧上一个核"的数据（1.5 节），跨核信用分配在物理上没有意义； $n > 1$ 时两臂权重实际是按种群交错序列在更新，Hedge 的理论保证不成立。
- **$\eta$ 上限 10 时单步对数差可达约 10**，几十帧内就饱和到地板（`tests/unit/test_bandit.cpp:494-520` 覆盖了极端 $\eta$ 下 softmax 下溢的数值稳定性）。
- **不施力**：混合两臂的施力矢量被明确弃用（`src/decision/BanditPolicy.cpp:49-51`）。

### 9 RiskAversePolicy（EROI 能量回报率状态机）

#### 9.1 核心思想

显式"记账"：用能量的投入产出比（EROI）驱动一个三模式状态机（Conserve / Mid / Earn），回报率高的地段全速抢收、低的地段省能巡航。

#### 9.2 风险度量是什么：**既不是方差也不是分位数**

代码中没有方差、标准差、分位数、VaR 或 CVaR 的任何实现。风险度量是**标量 EROI 的指数滑动平均**，即 $\mathrm{EMA}[\text{收益}/\text{成本}]$（`src/decision/RiskAversePolicy.cpp:117-128`）。

设 $\text{prox} = \text{clean}(s_6)$、 $d = \mathrm{clamp01}(\text{clean}(s_{16}))$、 $v^2 = s_{10}^2 + s_{11}^2$（恰为 $s_{12}^2$，注释说明"免再读一维"）：

$$\text{gain} = k_E\cdot\max\!\left(0,\ \text{prox} - \text{prevProx}\right)$$

$$\text{budget} = \frac{k_M\,v^2 + k_R\left(1 + k_T\,d\right)}{32}$$

$$\text{obs} = \frac{\text{gain}}{\max(\text{budget},\ 10^{-6})}$$

$$\text{eroi} \leftarrow \alpha\,\text{obs} + (1 - \alpha)\,\text{eroi}$$

$$\text{eroi} \leftarrow \text{eroi}\cdot(1 - k_D\,d),\qquad \text{eroi} \leftarrow \mathrm{clamp}\!\left(0,\ 64,\ \text{eroi}\right)$$

四个必须点明的细节：

1. "成本"是除以 32 的**归一化预算**， $N = 32$ 与 $\alpha = 2/(N+1) = 0.125$ 同源（`src/decision/RiskAversePolicy.cpp:8-11`）。
2. 温度折扣乘在**EMA 之后**、且每帧都乘一次，因此它不是一次性折扣而是改变 EMA 的不动点。设每帧乘子 $m = 1 - k_D d$，则稳态

$\text{eroi}^* = \frac{\alpha\,m\,\text{obs}}{1 - (1-\alpha)\,m}$

$d = 0$ 时 $m = 1$、 $\text{eroi}^* = \text{obs}$； $d = 1$ 时 $m = 0.5$、 $\text{eroi}^* \approx 0.111\,\text{obs}$（代入 $\alpha = 0.125$）。即满失真把 EROI 稳态压到不失真时的约 11%。
3. 增益只取候选 0（ $s_6$）；BanditPolicy 取三候选最大值。两者不是同一个统计量。
4. 上界 `kEroiMax = 64.0`（`src/decision/RiskAversePolicy.cpp:62`）、下界 0（`!(eroi_ > 0.0)` 顺带把 NaN 归 0，`src/decision/RiskAversePolicy.cpp:126`）。

#### 9.3 三模式状态机（滞回带 $h$）

`src/decision/RiskAversePolicy.cpp:131-141`：

| 当前模式 | 迁移条件 | 目标模式 |
|---|---|---|
| Earn (2) | $\text{eroi} < E_{\text{high}} - h$ | Mid (1) |
| Conserve (0) | $\text{eroi} > E_{\text{low}} + h$ | Mid (1) |
| Mid (1) | $\text{eroi} > E_{\text{high}} + h$ | Earn (2) |
| Mid (1) | $\text{eroi} < E_{\text{low}} - h$ | Conserve (0) |
| 其他 | — | 保持 |

默认 $E_{\text{high}} = 1.2$、 $E_{\text{low}} = 0.6$、 $h = 0.1$。**Earn 与 Conserve 不能直接互转**，必须先经 Mid。模式帧计数 `frameInMode_` 在模式变化时先清零再自增（`src/decision/RiskAversePolicy.cpp:142-146`）；初值 `mode_ = 1`（中性 Mid 起步，`src/decision/RiskAversePolicy.h:94`）。

#### 9.4 动作输出

方向先由候选 0 维护 `lastDirX_ / lastDirY_`（`src/decision/RiskAversePolicy.cpp:148-155`）：

$$\text{hasFood} = \lVert(s_0,s_1)\rVert > 10^{-9};\qquad \text{hasFood} \Rightarrow (\text{lastDirX},\text{lastDirY}) = \frac{(s_0,s_1)}{\lVert(s_0,s_1)\rVert}$$

**Earn (2)**：方向 = lastDir（每帧更新）， $a.\text{boost} = 1.0$（`src/decision/RiskAversePolicy.cpp:157-163`）。

**Conserve (0)**：方向每 `dirRefreshInterval = 3` 帧刷新一次——仅当 `frameInMode_ % 3 == 1` 时把 lastDir 拷进缓存，其余帧用缓存；**本帧无食物方向则输出零向量**（不拿陈旧方向盲走）。 $a.\text{boost} = \text{cruiseBoost} = 0.3$（`src/decision/RiskAversePolicy.cpp:164-175`）。

**Mid (1)**：方向 = lastDir（每帧更新），boost 线性插值（`src/decision/RiskAversePolicy.cpp:176-190`）：

$$t = \mathrm{clamp01}\!\left(\frac{\text{eroi} - E_{\text{low}}}{E_{\text{high}} - E_{\text{low}}}\right),\qquad a.\text{boost} = \text{cruiseBoost} + (1 - \text{cruiseBoost})\,t$$

$E_{\text{high}} \le E_{\text{low}}$ 时 $t = 0$（`span > 1e-9` 保护）。

#### 9.5 扫路施力

仅在 `mode == 2 && kPushBoost > 0 && targets.count > 0 && 方向非零` 时执行（`src/decision/RiskAversePolicy.cpp:199-233`）：

1. 线性扫出 `targets.items` 中 `dist` 最小的下标 `nearest`。
2. 球方向单位向量 $\hat u_t = (t_{dx},t_{dy})/\lVert\cdot\rVert$；期望方向单位向量 $\hat e$。
3. **挡路判定**： $\hat u_t\cdot\hat e \ge k_{\text{PushAlignMin}}$（默认 0.3）。
4. 合力 $\mathbf{f} = \hat u_t + 0.5\,\hat e$，单位化后乘 $k_{\text{PushBoost}}$：

$$a.\text{forceSlot} = \text{nearest},\qquad (a.\text{forceX},a.\text{forceY}) = k_{\text{PushBoost}}\cdot\frac{\mathbf{f}}{\lVert\mathbf{f}\rVert}$$

侧向偏置系数 `kPushSide = 0.5` 是硬编码常量（`src/decision/RiskAversePolicy.cpp:65`）。Conserve / Mid 态**绝不施力**。

#### 9.6 社会调制

`src/decision/RiskAversePolicy.cpp:235-249`：

$$a.\text{mod}[3] = \begin{cases}+k_{\text{Cohesion}}, & \text{mode} = 2\\ -k_{\text{Cohesion}}, & \text{mode} = 0\\ 0, & \text{mode} = 1\end{cases},\qquad a.\text{mod}[0] \text{ 同构（用 } k_{\text{Sight}}\text{）}$$

参数非 0 时**按模式无条件写入**（Mid 态写 0，即显式表示"不调制"）。`mod[2]` 不参与（`src/decision/RiskAversePolicy.cpp:235-236`）。

#### 9.7 内部状态

| 变量 | 含义 | 更新方程 | 初值 | 源码位置 |
|---|---|---|---|---|
| `eroi_` | EROI 估计 | 9.2 节 | 0.0 | `src/decision/RiskAversePolicy.h:87` |
| `prevProx_` | 上帧候选 0 接近度 | 每帧 `= prox` | 0.0 | `src/decision/RiskAversePolicy.h:88` |
| `lastDirX_ / lastDirY_` | 最近有效食物方向 | 有食物时覆盖 | 0.0 | `src/decision/RiskAversePolicy.h:89-90` |
| `cachedDirX_ / cachedDirY_` | Conserve 低频方向缓存 | 每 3 帧刷新 | 0.0 | `src/decision/RiskAversePolicy.h:91-92` |
| `hasCachedDir_` | 缓存是否建立 | 首次刷新置 `true` | `false` | `src/decision/RiskAversePolicy.h:93` |
| `mode_` | 0/1/2 | 9.3 节 | 1（Mid） | `src/decision/RiskAversePolicy.h:94` |
| `frameInMode_` | 当前模式内帧数 | 迁移时清零后自增 | 0 | `src/decision/RiskAversePolicy.h:95` |
| `frameInModeMode_` | `frameInMode_` 所属模式 | 迁移时同步 | 1 | `src/decision/RiskAversePolicy.h:96` |

`reset()` 见 `src/decision/RiskAversePolicy.cpp:253-264`。

#### 9.8 参数表

构造期清洗：全部非有限归 0； $E_{\text{high}} < E_{\text{low}}$ 时交换；`cruiseBoost、alpha、kTempDiscount` 夹 $[0,1]$；`dirRefreshInterval` 下限 1；`kPushAlignMin` 夹 $[0,1]$；调制强度夹 $[-1,1]$（`src/decision/RiskAversePolicy.cpp:69-103`）。

| 符号 | 名称 | 默认值 | 单位/范围 | 源码位置 |
|---|---|---|---|---|
| $E_{\text{high}}$ | Earn 阈值 | 1.2 | $\ge 0$ | `src/decision/RiskAversePolicy.h:51` |
| $E_{\text{low}}$ | Conserve 阈值 | 0.6 | $\ge 0$ | `src/decision/RiskAversePolicy.h:52` |
| $\alpha$ | EMA 系数（ $N = 32$） | 0.125 | $[0,1]$ | `src/decision/RiskAversePolicy.h:53` |
| $h$ | 滞回带宽度 | 0.1 | $\ge 0$ | `src/decision/RiskAversePolicy.h:54` |
| `cruiseBoost` | Conserve 冲刺意愿 | 0.3 | $[0,1]$ | `src/decision/RiskAversePolicy.h:55` |
| $N$ | Conserve 方向刷新间隔 | 3 | 帧， $\ge 1$ | `src/decision/RiskAversePolicy.h:56` |
| $k_E$ | 单位食物能量 | 10.0 | 能量/单位接近度 | `src/decision/RiskAversePolicy.h:57` |
| $k_M$ | 移动耗能率 | 1.0 | — | `src/decision/RiskAversePolicy.h:58` |
| $k_R$ | 基础呼吸率 | 0.05 | 1/帧 | `src/decision/RiskAversePolicy.h:59` |
| $k_T$ | 呼吸温度因子 | 2.0 | — | `src/decision/RiskAversePolicy.h:60` |
| $k_D$ | 温度失真折扣 | 0.5 | $[0,1]$ | `src/decision/RiskAversePolicy.h:61` |
| $k_{\text{PushBoost}}$ | 扫路施力强度 | 0.0（关闭） | $\ge 0$ | `src/decision/RiskAversePolicy.h:63` |
| $k_{\text{PushAlignMin}}$ | 挡路判定点积阈值 | 0.3 | $[0,1]$ | `src/decision/RiskAversePolicy.h:64` |
| $k_{\text{Cohesion}}$ | 聚拢调制强度 | 0.0（关闭） | $[-1,1]$ | `src/decision/RiskAversePolicy.h:65` |
| $k_{\text{Sight}}$ | 视野调制强度 | 0.0（关闭） | $[-1,1]$ | `src/decision/RiskAversePolicy.h:66` |
| — | EROI 上界 | 64.0 | 硬编码 | `src/decision/RiskAversePolicy.cpp:62` |
| — | 预算归一化分母 | 32.0 | 硬编码 | `src/decision/RiskAversePolicy.cpp:121` |
| — | 侧向偏置权重 | 0.5 | 硬编码 | `src/decision/RiskAversePolicy.cpp:65` |

**参数与真实行为层数值不同源**（必须注明）：策略里的 $k_E = 10$ / $k_M = 1.0$ / $k_R = 0.05$ / $k_T = 2.0$ 是策略自带的占位常量；行为层实际是 `locomotionCost = 0.0002`（`src/core/Config.h:171`）、`respirationBase = 0.03`（`src/core/Config.h:170`）、`respirationK = 0.0004`（`src/behavior/AgentBehavior.h:157`）、`respirationTempK = 1.5`（`src/behavior/AgentBehavior.h:158`）。头注释说的"与 AgentBehavior 真实公式同形"指的是函数形式（ $v^2$、 $1 + K d$），不是数值。生产路径恒用默认 `Params{}`（`src/sim/Simulation.cpp:260-263`）。

#### 9.9 失效模式

- **EROI 量纲不可比**： $\text{gain}$ 是"接近度差 × 10"， $\text{budget}$ 是 $v^2/32$ 量级， $\text{obs}$ 可以轻松超过 64 上界，导致 `eroi_` 长期饱和在 64 → 永久 Earn 态（退化为"全速追最近食物 + boost 1"）。
- **温度折扣压死一切**：持续 $d > 0$ 时逐帧乘性衰减，即使食物充足也可能掉进 Conserve； $d = 0$ 时该项恒等。
- **增益只看候选 0**：候选 0 在两个 tick 之间换人（最近节点变了）会制造虚假的"接近度增益"。
- **Conserve 刷新周期与帧计数耦合**：`frameInMode_ % 3 == 1` 意味着进入 Conserve 的第 1、4、7… 帧刷新；若方向频繁失效，缓存会过期但不清空。
- **共享状态**：`eroi_ / mode_ / prevProx_` 全种群一份（1.5 节）。
- **扫路先选"最近"再判"挡路"**：最近但不在路径上的球会占住唯一施力名额，真正挡路的球不会被处理（每帧只施力一个目标）。

### 10 StubPolicy（空策略）

`src/decision/RulePolicy.cpp:99-101`：整个函数体是 `return {};`。返回默认构造的 `ActionVector`： $a.\text{dir} = (0,0)$、 $a.\text{boost} = 0$、 $a.\text{forceSlot} = -1$、 $a.\text{force} = 0$、 $a.\text{mod} = 0$。它是"默认保守契约"的测试基准（`src/decision/RulePolicy.h:32-33`）。第 11 节的实测中它反而能量效率最高。

### 11 同一随机种子下的实测行为差异

以下数据来自仓库内已归档的实验报告，不是本次测量，只引用、不重跑。

#### 11.1 E4：八策略全量对比

来源 `docs/experiments/E4-八策略对比.md`（2026-10-31，`build/agentsim.exe`，命令 `--no-render --ticks 20000 --seed S --policy P --audit --stats`，6 个 seed × 8 策略 = 48 次运行，蓝细菌生态，regrow 默认 false）。

| 策略 | 6 seed 灭绝次数 | total_energy 均值 | sd | 均值 food |
|---|---|---|---|---|
| stub | 0/6 | 12982.32 | 208.61 | 56.5 |
| foragebank | 0/6 | 12690.85 | 280.79 | 16.0 |
| riskaverse | 0/6 | 12560.47 | 113.60 | 10.5 |
| rule | 0/6 | 12527.61 | 356.76 | 2.5 |
| hysteresis | 0/6 | 12507.30 | 274.63 | 0.0 |
| bandit | 0/6 | 12490.05 | 248.72 | 5.5 |
| thermal | 0/6 | 12478.84 | 219.87 | 4.0 |
| perceptron | **6/6** | 9093.15 | 71.24 | 4.2 |

出处：`docs/experiments/E4-八策略对比.md:81-90`（章节标题 `:77`）（该表由报告给出，本文不重算）。

perceptron 灭绝时刻（`--stats` 每 10 tick 采样中首次 `nuclei==0`）：seed 42→90、7→80、123→60、5→90、11→70、999→140 tick；峰值核数 12–14，即全生命周期只发生 0–2 次繁殖（`docs/experiments/E4-八策略对比.md:98-107`）。

`--policy` 有效性对照（同 seed 只改策略）：seed=42 时 rule 与 stub 的 nuclei 都是 24，但 food 分别为 0 与 53、total_energy 分别 12361.72 与 12571.61；seed=123 时 perceptron 灭绝（exit 5, nuclei=0）、thermal 存活 24 核（exit 0）（`docs/experiments/E4-八策略对比.md:109-123`）。

报告自身标注的局限（`docs/experiments/E4-八策略对比.md:125-131`）：每格单次运行、nuclei 顶格截尾、seed 只有 6 个，7 个幸存策略的 total_energy 排名在 sd 量级内**统计上不可区分**。

需要额外说明一点：该报告依赖的 `kMaxNuclei = 24` 硬上限在当前源码中已不存在——`src/behavior/AgentBehavior.cpp:25` 注明该常量已删除，种群上限改由 `ColonyConfig::maxNuclei`（默认 1200，`src/core/Config.h:75`）承担，承载力由能量收支自然决定。因此"全部幸存者顶格到 24"属于历史构建的行为，当前源码复跑不会出现该截尾。

#### 11.2 E1：感知机权重扫描

来源 `docs/experiments/E1-perceptron-evolution.md`（seed=42，2000 tick，`--perceptron-seed` 8 档 × `--perceptron-scale` 5 档 + rule/stub 基线 = 42 组）。

结论：8 个种子中有 7 个在全部 scale 下精确落在 total_energy = 17840.00 的"吸收态"（nuclei=12、无繁殖无死亡），仅 pseed=1 显著偏离（5 个 scale 平均 25976.0）；最优组 pseed=1 / scale=0.5 为 28450.00，仍低于 stub 的 40850.00（`docs/experiments/E1-perceptron-evolution.md:19-53`）。这与 5.6 节的"权重恒定、随机初值主导行为"一致。

#### 11.3 数据缺口

仓库内**没有**针对 hysteresis / foragebank / bandit / riskaverse 四者的逐策略参数扫描实验（这四者的 `Params` 也没有 CLI 入口，见 1.6 节），因此无法给出"同一 seed 下这四者的逐步轨迹差异"。E4 只提供了 20000 tick 终值的 48 行表。

### 12 决策层相关的可调配置总表

| 配置字段 | 含义 | 默认值 | 影响范围 | 源码位置 |
|---|---|---|---|---|
| `PolicyConfig::name` | 策略名 | `rule` | 装配 | `src/core/Config.h:49` |
| `senseRadius` | 感知基准半径 | 24.0 | 全部（经 $R$） | `src/core/Config.h:45` |
| `usePerceptron` | 装配感知机 | `false` | perceptron | `src/core/Config.h:51` |
| `perceptronSeed` | 初值种子 | 12345 | perceptron | `src/core/Config.h:52` |
| `perceptronScale` | 初值幅度 | 0.1 | perceptron | `src/core/Config.h:53` |
| `useThermal` | 装配趋温 | `false` | thermal | `src/core/Config.h:55` |
| `thermalPreferredTemp` | 最适温度 | 0.5 | thermal、bandit(臂) | `src/core/Config.h:56` |
| `thermalWThermal` | 温度权重 | 1.2 | thermal、bandit(臂) | `src/core/Config.h:58` |
| `thermalWCaution` | 失真规避权重 | 0.8 | thermal、bandit(臂) | `src/core/Config.h:59` |
| `policy.damping` | （PolicyConfig 遗留字段） | 0.9 | 无消费者 | `src/core/Config.h:46` |
| `policy.mutation` | （PolicyConfig 遗留字段） | 0.05 | 无消费者 | `src/core/Config.h:47` |

`policy.damping` 与 `policy.mutation` 在生产路径上只出现在 `Cli.cpp` / `SaveStore.cpp` 的读写处，没有任何策略或行为层消费它们（全仓 grep 结果：只有 `src/app/Cli.cpp:161`、`src/app/SaveStore.cpp:126,163-165,252-254` 命中）。

对外可用的 CLI 开关（`src/app/main.cpp:75-80`、`src/app/Cli.cpp:143-192`）：`--policy`、`--perceptron-seed`、`--perceptron-scale`、`--thermal-pref`、`--thermal-w-thermal`、`--thermal-w-caution`。其余策略参数无 CLI 入口。

### 13 横切不变量与红线

| 不变量 | 内容 | 校验/依据位置 |
|---|---|---|
| 零 RNG | `decide()` 内不出现任何随机数调用；perceptron 的 PCG32 只在构造期 | `src/decision/IDecisionPolicy.h:28-29`、各 cpp 文件头注释 |
| 不读世界 | 只消费传入的 `SensorVector` / `TargetList` 与自存历史 | `src/decision/IDecisionPolicy.h:28-29` |
| 不写实体 | 返回值是纯值类型，不持有任何实体指针 | `src/core/SensorTypes.h:52-78` |
| NaN 安全 | 非有限输入一律清洗为 0 或走最保守分支，全 NaN 输入不产生 NaN 输出 | `src/decision/HysteresisPolicy.cpp:29`、`ThermalPolicy.cpp:34`、`ForageBankPolicy.cpp:28`、`RiskAversePolicy.cpp:46`、`BanditPolicy.cpp:63` |
| 确定性 | 同输入同状态 → 逐位相同输出；同 seed 双跑一致 | 各 cpp 文件头注释；`tests/unit/test_perceptron.cpp:79-96`、`test_thermal.cpp:140-149` 等 |
| 默认保守 | `forceSlot = -1`、`mod` 全 0 是 `ActionVector` 的默认成员值，"不花能量"由默认值保证 | `src/core/SensorTypes.h:58-77` |
| 表型可塑 | `mod[]` 只改本帧生效值，不回写 genome | `src/behavior/AgentBehavior.cpp:696-724`、`src/behavior/AgentBehavior.h:121-126` |
| 唯一写点 | 决策层表达意图，校验/扣费/状态写入全在行为层 | `src/core/SensorTypes.h:58-64`、`src/behavior/AgentBehavior.cpp:647-693` |
