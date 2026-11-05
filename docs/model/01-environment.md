## 环境层与感知层

本域覆盖四部分：`environment/`（环面几何、阳光、蓝细菌、温度场、地形）、`perception/`（24 维传感器）、`core/Vec2.h`（环面几何原语）、以及组合根 `sim/Simulation.cpp` 中属于环境层的接线段（阳光取值、光合入账、尸体分配、捕食扫描）。

---

### 0. 环面几何原语（`core/Vec2.h`）

世界是环面（torus），宽 `W = world.width`、高 `H = world.height`，默认各 1024（`src/core/Config.h:18-19`）。所有环境/感知层的距离计算都经由下列函数，无直线距离分支。

| 函数 | 定义 | 源码位置 |
|---|---|---|
| `wrap(v, w, h)` | $r_x = \mathrm{fmod}(v_x, w)$，若 $r_x<0$ 则 $r_x \mathrel{+}= w$，再若 $r_x \ge w$ 则 $r_x \mathrel{-}= w$； $y$ 同法。 $w \le 0$（或 $h \le 0$）时该分量原样返回 | `src/core/Vec2.h:14-27` |
| `torusDelta(a, b, w, h)` | $d_x = \mathrm{fmod}(b_x-a_x, w)$；若 $d_x > w/2$ 则 $d_x \mathrel{-}= w$，若 $d_x < -w/2$ 则 $d_x \mathrel{+}= w$（ $y$ 同法）。结果分量落在 $[-w/2, w/2]$、 $[-h/2, h/2]$ | `src/core/Vec2.h:30-45` |
| `torusDeltaWrapped(a, b, w, h)` | 与上式等价但去掉 `fmod`：前提是 $a,b$ 已在 $[0,w)\times[0,h)$ 内，只需一次比较 + 一次加减（性能优化，逐位同值） | `src/core/Vec2.h:55-70` |
| `torusDistance(a, b, w, h)` | $\sqrt{d_x^2 + d_y^2}$， $d = \mathrm{torusDelta}(a,b)$ | `src/core/Vec2.h:73-76` |

`length(v) = \sqrt{\mathrm{dot}(v,v)}`（`src/core/Vec2.h:80`）；`normalized(v)` 零向量安全，返回 `{0,0}`（`src/core/Vec2.h:83-87`）。

环境层对外只转发这三项（`src/environment/AgentEnvironment.cpp:15-25`）。`AgentEnvironment` 自身只剩 `SimConfig` 只读引用、tick 计数（`src/environment/AgentEnvironment.cpp:13`）与一个可选障碍谓词 `blockedAt()`（`src/environment/AgentEnvironment.h:25-27`，R1-B 删除 Food 后生产路径无调用方）。环面坐标的唯一权威是 `AgentEnvironment`。

---

### 1. 阳光模型 sunLevel（环境层的唯一外部驱动量）

#### 1.1 计算式

权威实现在 `Simulation.cpp` 匿名命名空间（`src/sim/Simulation.cpp:100-127`），配置结构 `SunConfig`（`src/core/Config.h:34-42`）：

$$
\text{base}(y) = \text{baseLevel} - \text{verticalGradient}\cdot\frac{y}{H}\quad(\text{仅当 verticalGradient}\neq 0 \wedge H>0)
$$

$$
\text{day}(\text{tick}) = \text{dayAmplitude}\cdot\sin\!\left(\frac{2\pi\cdot\text{tick}}{\text{dayPeriodFrames}}\right)\quad(\text{仅当 dayPeriodFrames}>0 \wedge \text{dayAmplitude}\neq0)
$$

$$
\text{noise}(\text{tick},y) = \text{noiseAmplitude}\cdot\left(\text{hash01}(g_t, g_y, 0) - 0.5\right)
$$

其中 $g_y = \lfloor y\cdot\text{noiseFrequency}\rfloor$， $g_t = \lfloor \text{tick}/\text{noiseSpeed}\rfloor$（`noiseSpeed` $\le 0$ 时 $g_t=0$）。哈希为 splitmix64 finalizer（`src/sim/Simulation.cpp:83-89`），取 53 位归一到 $[0,1)$（`src/sim/Simulation.cpp:91-97`），零 RNG 消耗。

$$
\text{sunLevel} = \min\!\big(1,\ \max(0,\ \text{base} + \text{day} + \text{noise})\big)
$$

钳制实现为 `if (!(v > 0.0)) v = 0.0; if (v > 1.0) v = 1.0;`（`src/sim/Simulation.cpp:124-126`），NaN 归 0。

#### 1.2 生产调用点：全图单一标量，取"最暗点"

每帧调用一次， $y$ 与 $H$ 都传世界高度：

`const double sunLevel = sunLevelAt(cfg_.sun, tick_, cfg_.world.height, cfg_.world.height);`

（`src/sim/Simulation.cpp:702-703`）

因此传入 `CyanoField::step()` 的 `sunLevel` 只有**一个全局标量**，逐节点光合全部读同一个值：

- 垂直梯度退化为常数减项： $y=H \Rightarrow \text{base} = \text{baseLevel} - \text{verticalGradient}\cdot\tfrac{H}{H} = \text{baseLevel} - \text{verticalGradient}$，即整幅世界统一压暗，**不产生按 $y$ 的空间差异**。
- 网格噪声退化为 "每 tick 一个标量"： $g_y = \lfloor H\cdot\text{noiseFrequency}\rfloor$ 与位置无关，只有 $g_t$ 随时间变化。

空间光照差异不进入生态收支：前端 `/state` 头部的阳光字段是另一套简化估算（`serveSunEstimate`：只含 `baseLevel` + 白天正弦，无梯度无噪声，`src/app/main.cpp:664-674`），感知的 `s[17]` 采的是**温度**场（温度场不含光），二者都不回灌光合。

#### 1.3 参数

| 符号 | 名称 | 默认值 | 单位/范围 | 源码位置 |
|---|---|---|---|---|
| `baseLevel` | 基础光强 | 0.5 | 归一化 | `src/core/Config.h:35` |
| `dayAmplitude` | 昼夜正弦幅度 | 0.5 | — | `src/core/Config.h:36` |
| `dayPeriodFrames` | 昼夜周期 | 628.0 | 帧； $\le 0$ 时昼夜项恒 0 | `src/core/Config.h:37` |
| `verticalGradient` | 垂直梯度 | 0.0 | 系数 × $y/H$ | `src/core/Config.h:38` |
| `noiseAmplitude` | 阳光噪声幅度 | 0.0（关闭） | — | `src/core/Config.h:39` |
| `noiseFrequency` | 噪声空间频率 | 0.125 | 格宽 $=1/\text{freq}$ | `src/core/Config.h:40` |
| `noiseSpeed` | 噪声时间步进 | 1.0 | 每 speed 帧换一格 | `src/core/Config.h:41` |

热改键：`sun_base_level / sun_day_amplitude / sun_day_period_frames / sun_vertical_gradient / sun_noise_amplitude / sun_noise_frequency / sun_noise_speed`（`src/app/RuntimeEnv.cpp:101-107`），下一帧 `step()` 直接读 `cfg_.sun` 生效。默认参数下曲线即 $0.5+0.5\sin(2\pi\,\text{tick}/628)$。

---

### 2. 蓝细菌场 CyanoField（唯一生产者 + 唯一食物）

实现：`src/environment/CyanoNode.h/.cpp`。该模块**不碰账本**：`step()` 只返回 `CyanoStepReport` 数字，记账全部由组合根完成（`src/environment/CyanoNode.h:6, 148-156`）。

#### 2.1 节点状态变量

`struct CyanoNode { EntityId id; Vec2 pos; double energy; bool alive; double hue; }`（`src/environment/CyanoNode.h:139-145`）

- `id`：实体 id，播种从 `nextId_ = 1` 起递增。
- `pos`：环面坐标，**出生后永不移动**。
- `energy`：储能，初值恒 0。
- `alive`：存活标记；`false` 的节点在当帧末被压缩移除。
- `hue`：遗传色相 $\in[0,360)$。

`CyanoField` 私有状态（`src/environment/CyanoNode.h:272-284`）：`nodes_`（容器序 = 确定性序）、`ballSpawns_`（本帧产球出生点，逐帧重建）、`links_`（最近一次成交的边，两端 `EntityId` 按 (小,大) 存放）、`shade_`（遮蔽量缓存，下标 = `nodes_` 下标）、`worldW_/worldH_`、`linkMoved_/linkEdges_`、`nextId_`、`frameCount_`（连线节流相位，`seed()` 清零）。

#### 2.2 参数表（模块默认值 vs 生产镜像值）

`CyanoParams` 模块默认值（`src/environment/CyanoNode.h:46-137`）与 `CyanoTuning` 生产镜像（`src/core/Config.h:242-303`）逐字段同名。构造期与热改都全量逐字段拷贝（`src/sim/Simulation.cpp:161-199` 构造期；`1144-1186` 热改），生产运行吃后者。**两列不同的行加粗**。

| 符号 | 名称 | 模块默认 | 生产镜像 | 单位/范围 | 源码位置 |
|---|---|---|---|---|---|
| `photosynthesisRate` | 最大光合速率 | 1.0 | 1.0 | 能量/tick | `CyanoNode.h:47`；`Config.h:251` |
| `lightHalfSaturation` | 半饱和光强 | 0.5 | 0.5 | 光强 | `CyanoNode.h:48`；`Config.h:252` |
| `metabolismRate` | 每 tick 基础代谢 | 0.02 | 0.02 | 能量/tick | `CyanoNode.h:49`；`Config.h:253` |
| `energyCap` | 单节点储能上限 | 100.0 | 100.0 | 能量 | `CyanoNode.h:50`；`Config.h:254` |
| `reproductionCost` | 分裂成本（亲代扣） | 40.0 | 40.0 | 能量 | `CyanoNode.h:51`；`Config.h:255` |
| `reproductionProbability` | 分裂概率 | 0.01 | 0.01 | [0,1]/tick | `CyanoNode.h:52`；`Config.h:256` |
| `reproductionOffset` | 子节点最大偏移 | 125.0 | 125.0 | 世界单位 | `CyanoNode.h:53`；`Config.h:260` |
| `minSeparation` | 分裂最小同类间距 | 10.0 | 10.0 | 世界单位 | `CyanoNode.h:54`；`Config.h:261` |
| `ballSpawnCost` | 产球扣款 | 30.0 | 30.0 | 能量 | `CyanoNode.h:55`；`Config.h:262` |
| `ballSpawnProbability` | 产球概率 | 0.02 | 0.02 | [0,1]/tick | `CyanoNode.h:56`；`Config.h:263` |
| `ballRadius` | 球初始能量倍数 | 1.0 | 1.0 | 倍率 | `CyanoNode.h:57`；`Config.h:264` |
| `ballSpawnMinDist` | 产球最小距 | 10.0 | 10.0 | 世界单位 | `CyanoNode.h:58`；`Config.h:265` |
| `ballSpawnMaxDist` | 产球最大距 | 30.0 | 30.0 | 世界单位 | `CyanoNode.h:59`；`Config.h:266` |
| `linkRadius` | 连线最大环距 | 60.0 | 60.0 | 世界单位 | `CyanoNode.h:60`；`Config.h:267` |
| `linkMaxNeighbors` | 每节点最多邻居 | 3 | 3 | 个 | `CyanoNode.h:61`；`Config.h:268` |
| `linkTransferRate` | 富→穷转移比例 | 0.1 | 0.1 | [0,1] | `CyanoNode.h:62`；`Config.h:269` |
| `linkDeadband` | 转移死区 | 1.0 | 1.0 | 能量 | `CyanoNode.h:63`；`Config.h:270` |
| `linkCheckInterval` | 连线结算间隔 | 1 | 1 | 帧 | `CyanoNode.h:71`；`Config.h:271` |
| `maxNodes` | 节点数硬上限 | 256 | **4096**（取自 `SimConfig::cyanoMaxNodes`） | 个 | `CyanoNode.h:77`；`Config.h:272,314`；`Simulation.cpp:155` |
| `nodeSeedCount` | 初始播种节点数 | 32 | **512**（取自 `SimConfig::cyanoNodeCount`） | 个 | `CyanoNode.h:80`；`Config.h:273,310`；`Simulation.cpp:154` |
| `cyanoColonies` | 聚落中心数 | 0（均匀撒点） | **64** | 个； $\le 0$ = 均匀模式 | `CyanoNode.h:89`；`Config.h:286` |
| `clusterSpread` | 聚落撒点半径 | 45.0 | 45.0 | 世界单位 | `CyanoNode.h:90`；`Config.h:287` |
| `shadeK` | 自遮光强度 | 0.05 | **0.20** | — | `CyanoNode.h:123`；`Config.h:296` |
| `shadeRadius` | 遮蔽半径 | 60.0 | 60.0 | 世界单位 | `CyanoNode.h:124`；`Config.h:297` |
| `shadeSigma` | 高斯衰减宽度 | 30.0 | 30.0 | 世界单位 | `CyanoNode.h:125`；`Config.h:298` |
| `grazeRefugeEnergy` | 不可捕食储备 | 0.0（旧语义） | **15.0** | 能量 | `CyanoNode.h:136`；`Config.h:302` |
| `hueJitterSigma` | 分裂色相抖动 $\sigma$ | 8.0 | **20.0** | 度 | `CyanoNode.h:113`；`Config.h:285` |
| `conversionMode` | 转化率公式形态 | 1 | 1 | 0/1/2/3 | `CyanoNode.h:102`；`Config.h:274` |
| `conversionRate` | 转化率基率 | 0.6 | 0.6 | — | `CyanoNode.h:103`；`Config.h:275` |
| `conversionExponent` | 惩罚指数（占位，未消费） | 1.0 | 1.0 | — | `CyanoNode.h:104`；`Config.h:276` |
| `conversionSpeedRef` | 速度参考值 | 40.0 | 40.0 | 速度 | `CyanoNode.h:105`；`Config.h:277` |
| `conversionAccelRef` | 加速度参考值 | 60.0 | 60.0 | 加速度 | `CyanoNode.h:106`；`Config.h:278` |
| `conversionWSpeed` | mode 3 速度权重 | 1.0 | 1.0 | — | `CyanoNode.h:107`；`Config.h:279` |
| `conversionWAccel` | mode 3 加速度权重 | 1.0 | 1.0 | — | `CyanoNode.h:108`；`Config.h:280` |
| `conversionMinRate` | 转化率下限 | 0.05 | 0.05 | — | `CyanoNode.h:109`；`Config.h:281` |
| `conversionMaxRate` | 转化率上限 | 1.0 | 1.0 | — | `CyanoNode.h:110`；`Config.h:282` |

热改键（`src/app/RuntimeEnv.cpp:139-164, 274-275`）：`cyano_photosynthesis_rate`、`cyano_metabolism_rate`、`cyano_reproduction_probability`、`cyano_reproduction_offset`、`cyano_energy_cap`、`cyano_link_distance`、`cyano_max_links`、`cyano_link_check_interval`、`cyano_transfer_rate`、`cyano_transfer_deadband`、`cyano_min_node_separation`、`cyano_conversion_mode/-rate/-exponent/-speed_ref/-accel_ref/-w_speed/-w_accel/-min_rate/-max_rate`、`cyano_hue_jitter`、`cyano_light_half_saturation`、`cyano_ball_spawn_probability`、`cyano_ball_spawn_cost`、`cyano_nodes`。热改经 `applyConfig` 逐字段写回 `CyanoParams`，下一帧生效（`src/sim/Simulation.cpp:1144-1186`）；`nodeSeedCount/maxNodes` 属构造期引导参数，热改不重撒世界（`src/sim/Simulation.h:50-52`）。

三个运行时字段在当前源码中**有写点无读点**（全文检索无任何读取点）：`runtime.cyanoEnabled`（`Config.h:152`；只写于 `RuntimeEnv.cpp:270`）、`runtime.cyanoInitialEnergy`（`Config.h:149`）、`runtime.cyanoColonies`（`Config.h:151`；只写于 `RuntimeEnv.cpp:145`）。节点播种能量恒为 0（`CyanoNode.cpp:139, 153, 626`）。

#### 2.3 播种 seed(count, W, H, rng)

`src/environment/CyanoNode.cpp:105-158`。先记 `worldW_/worldH_`，`frameCount_ = 0`（重置连线节流相位），清 `links_`、清 `nodes_`；`count <= 0` 直接返回（零 RNG 消耗）。

两种分布模式互斥，判据 `cyanoColonies > 0 && clusterSpread > 0`（`CyanoNode.cpp:119-121`）：

**聚落模式**（生产默认，`cyanoColonies = 64`）：

1. 每聚落 2 抽中心： $c_i = \mathrm{wrap}\big(U_1 W,\ U_2 H\big)$（全图均匀）。
2. 每节点 3 抽： $\theta = 2\pi U_3$， $r = \text{clusterSpread}\cdot\sqrt{U_4}$（圆盘内均匀， $\sqrt{}$ 保证面积均匀），
   $$\text{pos} = \mathrm{wrap}\big(c_{i \bmod N_c} + r(\cos\theta, \sin\theta)\big)$$
3. 色相为节点第 3 抽： $\text{hue} = \mathrm{wrapHue}(360\,U_5)$。

**均匀模式**（`cyanoColonies <= 0`，模块默认）：每节点 3 抽 $x = U_1 W$、 $y = U_2 H$、 $\text{hue} = 360\,U_3$。

两模式节点均 `energy = 0.0`、`alive = true`、`id = nextId_++`（`CyanoNode.cpp:132-142, 147-157`）。

组合根调用 `cyano_.seed(cp.nodeSeedCount, W, H, rng_)`（`src/sim/Simulation.cpp:233-234`），默认播种 512 个节点、64 个聚落（`Config.h:310, 286`）。播种顺序固定在 `spawnNuclei()` 之后，以锁定 RNG 流（`src/sim/Simulation.cpp:149-152`）。

#### 2.4 追加播种 appendSeed(count, W, H, rng)

`src/environment/CyanoNode.cpp:616-633`。不清场（`nodes_/links_/frameCount_` 一概不动）；逐节点先查 `nodes_.size() >= maxNodes` 护栏，满了立即停（截断点不消耗 RNG）；每实际追加节点恰 3 抽（ $x, y, \text{hue}$），能量 0。唯一调用方是 `bloom` 事件（`src/sim/Simulation.cpp:1189-1200`；事件参数 `cyanoNodesToAdd = 120`、`cyanoNodesIfDisabled = 150`，`src/app/EnvControl.cpp:184-190`）。追加不记任何账（能量从光合长出时才 `injectPrimary`）。

#### 2.5 每 tick 阶段顺序与 RNG 消耗序

`step(sunLevel, W, H, rng)`（`src/environment/CyanoNode.cpp:160-371`）固定 7 段：

| 阶段 | 动作 | RNG 消耗 | 源码位置 |
|---|---|---|---|
| 1 | 光合（逐存活节点） | 0 | `CyanoNode.cpp:173-198` |
| 2 | 代谢 + 饿死判定 + 上限截断 | 0 | `CyanoNode.cpp:206-221` |
| 2.5 | 连线能量传输（按 `linkCheckInterval` 节流） | 0 | `CyanoNode.cpp:233-236`；`386-548` |
| 3 | 产球判定（每存活节点恰 1 抽） | 1/节点 | `CyanoNode.cpp:242-252` |
| 4 | 产球位置（每产球节点 2 抽：距离、角度） | 2/产球 | `CyanoNode.cpp:263-274` |
| 5 | 分裂判定（每存活节点 1 抽）+ 通过者 2 抽高斯 | 1/节点 + 2/通过者 | `CyanoNode.cpp:284-300` |
| 6 | 分裂位置（每亲代 2 抽：半径、角度）+ 间距校验 + 上限复查 | 2/亲代 | `CyanoNode.cpp:307-358` |
| 7 | 压缩尸体槽位、`++frameCount_` | 0 | `CyanoNode.cpp:365-369` |

死节点在所有判定段一律 `continue`、不消耗随机数；阶段 3/5 的判定集合取帧初快照 `frameStartCount`（`CyanoNode.cpp:283`），本帧新生儿不参与本帧判定。

#### 2.6 光合（含自遮光）

逐存活节点（`src/environment/CyanoNode.cpp:174-198`）：

$$
\text{localSun}_i = \text{sunLevel}\cdot\exp\!\big(-\text{shadeK}\cdot \text{shade}_i\big)
$$

$$
\text{gain}_i = \text{photosynthesisRate}\cdot\frac{\text{localSun}_i}{\text{localSun}_i + \text{lightHalfSaturation}}\quad(\text{Michaelis–Menten 饱和})
$$

$$
\text{room}_i = \max(0,\ \text{energyCap} - E_i),\qquad
\text{credited}_i = \min(\text{gain}_i,\ \text{room}_i)
$$

$$
E_i \mathrel{+}= \text{credited}_i,\qquad \text{report.photosynthesisGain} \mathrel{+}= \text{credited}_i
$$

短路条件：`sunLevel <= 0` 时 gain 取 0；`localSun` 下溢为 0 时同样取 0（防 `lightHalfSaturation = 0` 的 0/0 NaN）（`CyanoNode.cpp:178-185`）。入账口径是 **credited 而非 gain**（`CyanoNode.cpp:189-196`）：已封顶节点入账 0，绝不产生负 gain。

遮蔽量 `shade_[i]` 由上一结算帧的 `linkTransfer()` 缓存（见 2.8）：

$$
\text{shade}_i = \sum_{\substack{j \neq i\\ j\ \text{alive}\\ d_{ij} < \text{shadeRadius}}} \exp\!\left(-\frac{d_{ij}^2}{2\,\text{shadeSigma}^2}\right),
\qquad d_{ij} = \mathrm{torusDistance}(\text{pos}_i, \text{pos}_j)
$$

一帧滞后语义：节点位置永不移动，滞后只来自帧间存活集合变化（`CyanoNode.h:230-234`）。`shade_.size() != nodes_.size()` 时按新尺寸补零（`CyanoNode.cpp:173`）。

数值参考（默认参数、无遮蔽）： $\text{sunLevel}=1$ 时 $\text{gain} = 1.0\times1/(1+0.5) = 0.6667$； $\text{sunLevel}=0.5$ 时 $\text{gain} = 0.5$（单测钉死，`tests/unit/test_cyano.cpp:50-53`）。生产 `shadeK = 0.20` 下，`shade = 1` 时 $\text{localSun} = e^{-0.2} = 0.8187$， $\text{gain} = 0.6208$（降 6.9%）。

#### 2.7 代谢与饿死

`src/environment/CyanoNode.cpp:206-221`。采用**线性常数型**（非能量加权型）：

$$
\text{loss} = \text{metabolismRate}\quad(\text{每 tick 固定}),\qquad E_i \mathrel{-}= \text{loss}
$$

- 若 $E_i \le 0$（饿死）：`report.metabolismLoss += loss + E_i`（ $E_i$ 此时为负，等价于实际耗散 = 死前能量）、 $E_i \leftarrow 0$、`alive = false`、`continue`。
- 否则 `report.metabolismLoss += loss`；再若 $E_i > \text{energyCap}$ 则 $E_i \leftarrow \text{energyCap}$（纯状态归一，不入任何账目字段）。

净增长参考（默认参数、无遮蔽、满光、能量远低于上限）： $0.6667 - 0.02 = 0.6467$ 能量/tick/节点；从 0 涨到 `energyCap = 100` 约需 155 tick。

#### 2.8 连线能量再分配 linkTransfer()

`src/environment/CyanoNode.cpp:386-548`。零 RNG、确定性、零和（内部转移，不改任何账目字段）。每次调用先 `linkMoved_ = 0`、`linkEdges_ = 0`、`links_.clear()`，并把 `shade_.assign(n, 0)` 清零后重算。

**遮蔽与候选同扫**：`wantShade = shadeK > 0 && shadeRadius > 0 && shadeSigma > 0`；`twoSigma2 = 2 shadeSigma^2`（`CyanoNode.cpp:397-400`）。

退化分支（`n < 2`、`linkMaxNeighbors <= 0` 或 `linkRadius <= 0`，`CyanoNode.cpp:401-422`）：不做连线；若 `wantShade && n >= 2` 则跑一趟仅遮蔽的对称对扫描（ $i<j$， $d < \text{shadeRadius}$ 时 $w = \exp(-d^2/\text{twoSigma2})$ 加到两端）。

**候选表**（`CyanoNode.cpp:423-520`）：每个存活节点收集所有 $d_{ij} \le \text{linkRadius}$ 的存活节点，按 `(距离, 对方下标)` 升序排序后截取前 `linkMaxNeighbors` 个（不足照收）。空间加速：`cellSize = max(linkRadius, shadeRadius)`；当 $W \ge 3\,\text{cellSize}$ 且 $H \ge 3\,\text{cellSize}$ 时建环面网格桶、只扫 3×3 邻域（`CyanoNode.cpp:436-438, 460-508`），否则退回全对扫描（`CyanoNode.cpp:441-458`）。遮蔽对先收集、再按 `(i,j)` 字典序回放，保证浮点加法序列与全对扫描一致。

**成边规则**（`CyanoNode.cpp:523-547`）：双向互选才成边（ $i$ 选 $j$ 且 $j$ 选 $i$），只从 $j>i$ 一侧处理一次。设 $a,b$ 为两端：

$$
\text{diff} = |E_a - E_b|,\qquad
\text{amount} =
\begin{cases}
\text{diff}\cdot\text{linkTransferRate}, & \text{diff} > \text{linkDeadband}\\
0\ (\text{跳过该边，防抖}), & \text{否则}
\end{cases}
$$

$$
E_{hi} \mathrel{-}= \text{amount},\qquad E_{lo} \mathrel{+}= \text{amount},\qquad
\text{linkMoved_} \mathrel{+}= \text{amount},\qquad \text{linkEdges_} \mathrel{+}= 1
$$

成交边的两端 `EntityId` 记入 `links_`。`step()` 仅在 `frameCount_ % linkCheckInterval == 0` 的帧调用本函数（`CyanoNode.cpp:233-236`）：`interval = 1`（默认）即每帧结算； $N > 1$ 时跳过帧既不重算拓扑也不结算能量，`links()` 与 `shade_` 保持上一结算帧快照（`CyanoNode.h:217-227`）。

遮蔽缓存另有独立重算入口 `refreshShadeCache()`（`CyanoNode.cpp:646-666`）：同公式、只算遮蔽、不建连线不结算能量，仅在读档后调用以消除"读档第一帧光合分叉"（`src/sim/Simulation.cpp:1691-1694`）。

#### 2.9 产球（自由球生产）

**判定**（`src/environment/CyanoNode.cpp:242-252`）：每存活节点抽 1 次 $U$（无论是否通过都必须抽，参数变化不得改变随机流）：

$$
U < \text{ballSpawnProbability}\ \wedge\ E_i \ge \text{ballSpawnCost}
\;\Longrightarrow\;
E_i \mathrel{-}= \text{ballSpawnCost},\qquad
\text{ballEnergyTotal} \mathrel{+}= \text{ballSpawnCost}\cdot\text{ballRadius}
$$

**位置**（`CyanoNode.cpp:263-274`）：每产球节点 2 抽：

$$
\text{off} = \text{ballSpawnMinDist} + U_1\,(\text{ballSpawnMaxDist}-\text{ballSpawnMinDist}),\qquad
\theta = 2\pi U_2
$$

$$
\text{spawn} = \mathrm{wrap}\big(\text{pos}_{parent} + \text{off}\cdot(\cos\theta, \sin\theta)\big)
$$

出生点经 `lastBallSpawns()` 交给组合根；组合根为每球创建一个自由球实体，初始能量 `per = ballEnergyTotal / ballsProduced`、速度 0（`src/sim/Simulation.cpp:713-739`）。

**账目口径（重要）**：产球时组合根**不调用任何账本函数**——能量只是从节点搬到新球实体，两者都在存活集合内（`src/sim/Simulation.cpp:714-724, 738`）。默认 `ballRadius = 1.0` 时每球恰好带走 `ballSpawnCost \times 1.0 = 30`，与节点扣款相等；若 `ballRadius \neq 1` 则球的初始能量与节点扣款不相等且组合根不做差额入账，审计恒等式会失衡（这是该参数必须保持 1.0 的原因）。

自由球随后每帧按 `take = E \cdot kFreeDriftK` 慢散热，`kFreeDriftK = 0.0002`（`src/sim/Simulation.cpp:624-629`，`dissipate("ballDrift")`）；自由球池上限 `kFreeBallCap = 256`，超限回收最老的蓝细菌源球并全额耗散（`dissipate("cyanoBallExpire")`，`src/sim/Simulation.cpp:69, 741-756`）。

#### 2.10 分裂繁殖

**判定**（`src/environment/CyanoNode.cpp:284-300`）：每存活节点恰 1 抽：

$$
U < \text{reproductionProbability}\ \wedge\ \text{nodes\_.size()} < \text{maxNodes}\ \wedge\ E_i \ge \text{reproductionCost}
$$

`hasRoom` 是对**帧初快照**判定的，且能量检查发生在产球扣款（阶段 3）之后。通过者立即再抽 2 次构造高斯抖动（无论后续间距校验是否放行都恰消耗）：

$$
u_1 = \text{foldCentered}(U_1)\in(0,1],\qquad u_2 = U_2\in[0,1)
$$

$$
r = \sqrt{-2\ln u_1},\qquad g = r\cos(2\pi u_2)
$$

`foldCentered(u) = 2(u<0.5 ? u : 1-u)`（`CyanoNode.cpp:59-63`）。常量 `kGaussEps = 1e-12`（`CyanoNode.cpp:49`）已声明但函数体内无引用点。

**位置**（`CyanoNode.cpp:307-322`）：1 次 $u$ 单抽映射到两段半径（近段概率质量 0.7）：

$$
\text{nearSpan} = 0.5\cdot\text{reproductionOffset},\qquad
\text{radius} =
\begin{cases}
\dfrac{u}{0.7}\,\text{nearSpan}, & u < 0.7\\[4pt]
\text{nearSpan} + \dfrac{u-0.7}{0.3}\big(\text{reproductionOffset}-\text{nearSpan}\big), & u \ge 0.7
\end{cases}
$$

$$
\theta = 2\pi U_2,\qquad
\text{childPos} = \mathrm{wrap}\big(\text{pos}_{parent} + \text{radius}\cdot(\cos\theta, \sin\theta)\big)
$$

常量 `kReproNearFraction = 0.7`、`kReproNearSpan = 0.5`（`CyanoNode.cpp:55-56`）。默认 `reproductionOffset = 125` 下：70% 子代落在 $[0, 62.5)$，30% 落在 $[62.5, 125)$。

**间距校验**（`CyanoNode.cpp:327-337`）：对全部存活节点（亲代自身豁免；本帧已出生的兄弟子代计入）求最小环距，若 $< \text{minSeparation}$ 则放弃：不扣能量、`births` 不加、不记 `reproductionLoss`，但已抽的随机数照常消耗。

**上限复查**（`CyanoNode.cpp:338-343`）：`push_back` 前再查一次 `nodes_.size() >= maxNodes`（阶段 5 的 `hasRoom` 基于帧初快照，同帧多亲代会突破上限）；复查不补抽 RNG。

**真正分裂**（`CyanoNode.cpp:345-357`）：

$$
E_{parent} \mathrel{-}= \text{reproductionCost},\qquad
E_{child} = 0.5\cdot\text{reproductionCost},\qquad
\text{report.reproductionLoss} \mathrel{+}= 0.5\cdot\text{reproductionCost}
$$

即 `reproductionCost = 40` 时：亲代扣 40、子代得 20、另 20 记作耗散。子代 `id = nextId_++`、`alive = true`；子代色相见 2.15。

#### 2.11 死亡与移除条件

| 通道 | 触发条件 | 能量处理 | 源码位置 |
|---|---|---|---|
| 饿死 | 代谢后 $E_i \le 0$ | $E_i \leftarrow 0$，`alive = false`，实际耗散 = 死前能量 | `CyanoNode.cpp:210-215` |
| 被捕食抽干 | `graze()` 后 $E_i \le 0$（仅 `grazeRefugeEnergy <= 0` 可达） | $E_i \leftarrow 0$，`alive = false` | `CyanoNode.cpp:562-569` |
| 移除 | 每帧末压缩 `erase(remove_if(!alive))`，保序、零 RNG | 尸体能量必为 0，不丢能量 | `CyanoNode.cpp:365-367` |

节点被 `graze()` 标记死亡后当帧末即被清除；组合根在核循环中读取 `cyanoNodes()` 时容器仍含本帧尸体标记（`src/sim/Simulation.h:96-98`）。`maxNodes` 按 `nodes_.size()` 计，尸体已先被压缩，不会蚕食出生空间。

#### 2.12 捕食 graze(id, rawEnergy, conversionRate) 与捕食避难所

`src/environment/CyanoNode.cpp:550-573`：

$$
\text{want} = \max(0,\ \text{rawEnergy}\cdot\text{conversionRate}),\qquad
\text{edible} = E_i - \text{grazeRefugeEnergy},\qquad
\text{avail} = \max(0,\ \text{edible})
$$

$$
\text{taken} = \min(\text{want},\ \text{avail}),\qquad E_i \mathrel{-}= \text{taken}
$$

返回 `taken`；若扣后 $E_i \le 0$ 则归零并 `alive = false`（仅 `refuge <= 0` 时可达）。`id` 不存在或已死返回 0。本函数零 RNG 消耗。

`grazeRefugeEnergy > 0`（生产值 15，`Config.h:302`）时节点保留一份不可食储备，被捕食永不被抽死（扣后 $E_i \ge \text{refuge}$）；饿死通道不受影响（`CyanoNode.h:127-136`）。

组合根调用（`src/sim/Simulation.cpp:478-537`）：闸门 `n.alive && genome.cyanoPreference > 0 && n.digestCooldown == 0`；在 `bp_.cyanoPredationRadius` 内做平方距离确定性扫描取最近存活节点（严格 $d^2 < r^2$，贴边不算）。每口量：

$$
\text{want} = \text{cyanoPreference}\cdot\text{grazeBiteFraction}\cdot E_{prey}
$$

（`grazeBiteFraction` 默认 0.15，`src/core/Config.h:193`；与核自身能量无关）。进食后 `digestCooldown = \lceil 1/(0.5 + \text{cyanoDigestion}) \rceil`，每帧减 1（`Simulation.cpp:533-534, 541`）。被吃走的能量经 `AgentBehavior::absorbPrey`（`src/behavior/AgentBehavior.cpp:768-774`）记 `ledger.transfer(preyId, n.id, amount)`（零和搬运，不改注入/耗散总额）。

#### 2.13 捕食转化率 cyanoConversionRate(...)

`src/environment/CyanoNode.cpp:75-103`。设 `base = baseRate`（调用方传 `conversionRate \times genome.cyanoDigestion`）、`okSpeed = maxSpeed > 0 && speedRef > 0`、`okAccel = maxAccel > 0 && accelRef > 0`：

$$
\text{rate} =
\begin{cases}
\text{base}, & \text{mode} = 0\ \text{或未知}\\[4pt]
\text{base}\cdot\dfrac{\text{speedRef}}{\text{speedRef}+\text{maxSpeed}}, & \text{mode} = 1\ (\text{okSpeed 否则 base})\\[8pt]
\text{base}\cdot\dfrac{\text{accelRef}}{\text{accelRef}+\text{maxAccel}}, & \text{mode} = 2\ (\text{okAccel 否则 base})\\[8pt]
\text{base}\cdot p_s\cdot p_a, & \text{mode} = 3
\end{cases}
$$

$$
p_s =
\begin{cases}
w_{speed}\dfrac{\text{speedRef}}{\text{speedRef}+\text{maxSpeed}}, & \text{okSpeed}\\
1, & \text{否则}
\end{cases},
\qquad
p_a =
\begin{cases}
w_{accel}\dfrac{\text{accelRef}}{\text{accelRef}+\text{maxAccel}}, & \text{okAccel}\\
1, & \text{否则}
\end{cases}
$$

`baseRate <= 0` 时直接返回 0；最后若 `minRate < maxRate` 则把 rate 钳到 $[\text{minRate}, \text{maxRate}]$。参数 `exponent` 被 `(void)` 显式弃用（R1-B 占位，`CyanoNode.cpp:80`）。

默认 `mode = 1`、`conversionRate = 0.6`、`conversionSpeedRef = 40`、`conversionMinRate = 0.05`、`conversionMaxRate = 1.0` 时：

$$
\text{rate} = 0.6\cdot\text{cyanoDigestion}\cdot\frac{40}{40+\text{maxSpeed}}
$$

#### 2.14 尸体营养注入 fertilize(nodeId, amount)

`src/environment/CyanoNode.cpp:579-590`：

$$
\text{room} = \max(0,\ \text{energyCap} - E_i),\qquad
\text{accepted} = \min(\text{amount},\ \text{room}),\qquad E_i \mathrel{+}= \text{accepted}
$$

`amount <= 0`、`id` 不存在或已死返回 0；溢出部分由调用方耗散，能量绝不滞留模块内。

尸体包来源：核死亡时 $\text{carrionShare} = 0.3\cdot E_{death}$（`kDeathCarrionFraction = 0.3`，`src/behavior/AgentBehavior.cpp:34, 588-598`），其余 70% 当场 `dissipate("death")`。

组合根分配（`src/sim/Simulation.cpp:653-691`）：每帧把 `carrion_`（核死亡的 30% 遗产包）分配给 `kCarrionSearchRadius = 200`（`Simulation.cpp:57`）内最近的存活节点（按下标序取第一个最近者，平距取小下标，零 RNG）：

- `accepted > 0`：`ledger.transfer(0, best, accepted)`
- `cr.energy > accepted`：溢出部分 `ledger.dissipate("death", overflow)`
- 半径内无节点：全额 `ledger.dissipate("death", cr.energy)`

#### 2.15 遗传色相

- 播种： $\text{hue} = \mathrm{wrapHue}(360\,U)$，区间 $[0,360)$（`CyanoNode.cpp:141, 155, 628`）。
- 分裂： $\text{child.hue} = \mathrm{wrapHue}(\text{parent.hue} + \text{hueJitterSigma}\cdot g)$（`CyanoNode.cpp:354`）， $g$ 为 2.10 的高斯样本。
- `wrapHue(h)`：先 `fmod(h, 360)`；负值 `+360`；再对 $\ge 360$ 容错减一次（`CyanoNode.cpp:65-70`）。
- 色相参与状态哈希（双跑确定性守卫）与存档（`src/sim/Simulation.cpp:1075, 1512`），**不参与能量收支**。

#### 2.16 能量收支总账（组合根接线）

`CyanoStepReport` 字段（`CyanoNode.h:148-156`）与账本动作的对应（`src/sim/Simulation.cpp:704-777`）：

| 报告字段 | 含义 | 账本动作 |
|---|---|---|
| `photosynthesisGain` | 本帧光合实存进节点的总额 | `injectPrimary(kCyanoPoolId = 0, gain)`（`Simulation.cpp:707`） |
| `metabolismLoss` | 本帧代谢实耗（含饿死清算） | `dissipate("cyanoMetabolism", loss)`（`Simulation.cpp:708`） |
| `reproductionLoss` | 本帧分裂耗散（子代没拿到的另一半） | `dissipate("cyanoReproduction", loss)`（`Simulation.cpp:709`） |
| `ballsProduced` / `ballEnergyTotal` | 产球数 / 球带走能量 | **不入账**（节点→球，零和；`Simulation.cpp:714-738`） |
| `births` | 新增节点数 | 不入账（分裂守恒由 `reproductionLoss` 表达） |
| `linkTransferred` | 连线内部转移量（仅观测） | 不入账（零和） |

每帧内部守恒断言：

$$
\Delta E_{cyano} = E_{cyano}^{after} - E_{cyano}^{before}
$$

$$
\big|\Delta E_{cyano} - (\text{photo} - \text{metab} - \text{repro})\big| \le 10^{-6}\cdot\max(1,\ |E^{before}|)
$$

违反则 `auditOk_ = false`（`src/sim/Simulation.cpp:771-777`）。另有尸体流水闭合断言 $|\text{carrionEnergyIn} - \text{carrionEnergyOut}| \le 10^{-9}\max(1, \text{in})$（`Simulation.cpp:785-788`），以及全局零和恒等式 `injected - dissipated = \sum 存活实体能量`（`src/ledger/AgentLedger.h:2-4`，判定 `src/ledger/AgentLedger.cpp:29-32`，容差 $10^{-9}\max(1,\text{totalInjected})$，`Simulation.cpp:791-792`）。

`Simulation::totalEnergy()` 求和口径包含蓝细菌全部存活节点能量（`src/sim/Simulation.cpp:1019`），故节点能量天然在审计恒等式右侧。

#### 2.17 边界与钳制条件汇总

| 条件 | 行为 | 源码位置 |
|---|---|---|
| `sunLevel <= 0` 或 `localSun == 0` | gain = 0（不求值 0/0） | `CyanoNode.cpp:178-185` |
| $E_i$ 已达 `energyCap` | 光合入账 = 0（credited） | `CyanoNode.cpp:193-196` |
| 预置能量 $>$ `energyCap` | 代谢段截断到上限，不记任何 gain | `CyanoNode.cpp:218-220` |
| `count <= 0`（seed） | 空场，零 RNG | `CyanoNode.cpp:112` |
| `nodes_.size() >= maxNodes` | 分裂拒绝（随机数照抽）；`appendSeed` 立即停 | `CyanoNode.cpp:291, 343, 621` |
| 子代距最近同类 $<$ `minSeparation` | 放弃分裂（不扣能量、不记损失） | `CyanoNode.cpp:327-337` |
| 能量 $<$ `ballSpawnCost` | 不产球（判定仍消耗 1 抽） | `CyanoNode.cpp:245-246` |
| 能量 $<$ `reproductionCost` | 不分裂 | `CyanoNode.cpp:292` |
| 能量差 $\le$ `linkDeadband` | 跳过该边（防抖，不计边数） | `CyanoNode.cpp:538` |
| `n < 2` / `linkRadius <= 0` / `linkMaxNeighbors <= 0` | 退化分支：只算遮蔽、无连线 | `CyanoNode.cpp:401-422` |
| `linkCheckInterval <= 0` | `linkTransfer()` 永不调用（连线与遮蔽缓存都不刷新，遮蔽恒为初始 0） | `CyanoNode.cpp:233` |
| `grazeRefugeEnergy <= 0` | 退化为 $\min(\text{want}, E)$，可抽死 | `CyanoNode.cpp:558-569` |
| `rawEnergy \times rate <= 0` | `want = 0`（防负注能） | `CyanoNode.cpp:554-555` |

---

### 3. 温度场 TemperatureField

`src/environment/TemperatureField.h/.cpp`。**温度场没有时间演化**：类中没有 tick 成员，也没有任何 per-tick 更新调用；`generate()` 只在构造期执行一次，此后仅提供只读采样 `sample()` 与 `distortion()`（`TemperatureField.h:14-42`）。因此**不存在扩散项、衰减项或边界松弛方程**——场 = 基线 + 静态高斯源叠加。

#### 3.1 生成

`src/environment/TemperatureField.cpp:30-46`。若 `cfg.sources` 非空则直接采用（零 RNG）；否则撒 `sourceCount` 个源，每源 4 抽：

$$
x = U_1\cdot(W>0\,?\,W:1),\qquad y = U_2\cdot(H>0\,?\,H:1),\qquad
A = -0.4 + 0.8\,U_3,\qquad R = 100 + 200\,U_4
$$

$A \in [-0.4, 0.4)$（正为热源、负为冷源）， $R \in [100, 300)$；若 $R \le 0$ 则置 1（数值容错）。默认 3 个源 → 构造期消耗 12 次 `nextDouble`。构造顺序在核出生之前（`src/sim/Simulation.cpp:144-145`）。

#### 3.2 采样

`src/environment/TemperatureField.cpp:54-60`。单源贡献（高斯，用环面最小环距）：

$$
q_k = \frac{d_{k,x}^2 + d_{k,y}^2}{2R_k^2},\qquad
\text{contribution}_k = A_k\,e^{-q_k},\qquad
(d_{k,x}, d_{k,y}) = \mathrm{torusDelta}(\text{center}_k, \text{pos})
$$

$$
\text{sample}(\text{pos}) = \mathrm{clamp01}\!\left(\text{baseline} + \sum_{k} A_k\,e^{-q_k}\right)
$$

`clamp01`：负值归 0、超过 1 归 1（`TemperatureField.cpp:48-52`）。

#### 3.3 失真度

$$
\text{distortion}(\text{pos}) = \mathrm{clamp01}\Big(\big|\text{sample}(\text{pos}) - \text{preferred}\big|\cdot\text{distortionK}\Big)
$$

（`TemperatureField.cpp:70-72`）`preferred` 为全局常量"最适温度"（`TemperatureField.cpp:68`）。

#### 3.4 与太阳/环境参数的耦合

温度场与阳光**没有函数耦合**：`generate` 不接收 `sunLevel`，`sample` 不读 tick。温度只通过两条路径影响系统：

1. **感知**：`s[16] = distortion`，`s[17] = clamp01(sample + terrain.temperatureOffset)`（`src/perception/AgentSensors.cpp:207-210`）；温度失真放大读噪声（见 5.3）。
2. **呼吸**：行为层乘性温度因子 $1 + \text{respirationTempK}\cdot d$，`respirationTempK = 1.5`（`src/behavior/AgentBehavior.h:158`）；实现在 `src/behavior/AgentBehavior.cpp:385-388`，入参为 `temp_.distortion(n.pos)`（`src/sim/Simulation.cpp:446-447`）。

#### 3.5 参数

| 符号 | 名称 | 默认值 | 单位/范围 | 源码位置 |
|---|---|---|---|---|
| `sourceCount` | 随机源个数 | 3 | 个 | `src/core/Config.h:93` |
| `baseline` | 基线温度 | 0.5 | 归一化 [0,1] | `src/core/Config.h:94` |
| `preferred` | 最适温度 | 0.5 | 归一化 | `src/core/Config.h:95` |
| `distortionK` | 失真放大系数 | 2.0 | — | `src/core/Config.h:96` |
| `sources` | 显式源列表 | 空（= 随机撒） | — | `src/core/Config.h:97` |
| `TemperatureSource::amplitude` | 单源幅度 | $-0.4+0.8U$ | $[-0.4, 0.4)$ | `TemperatureField.cpp:42`；`Config.h:88` |
| `TemperatureSource::radius` | 单源高斯半径 | $100+200U$ | $[100, 300)$ | `TemperatureField.cpp:43`；`Config.h:89` |

温度场**没有运行时热改键**：`RuntimeEnv` 键表（`src/app/RuntimeEnv.cpp:93-276` 全表核对）无温度场条目，`applyConfig` 也不镜像温度场。`distortionK = 2` 时 $|\text{sample} - \text{preferred}| \ge 0.5$ 即达失真上限 1。

---

### 4. 地形 Terrain

`src/environment/Terrain.h/.cpp`。三类圆形 blob：障碍（阻挡移动）、水域（提高移动耗能）、温度区（叠加温度偏移）。所有判定用环面最小环距。

#### 4.1 生成（撒点算法，非噪声场）

`src/environment/Terrain.cpp:29-83`。显式提供的 blob 组直接采用（零 RNG）；某组为空则按 `count` 撒点。 $W \le 0$ 或 $H \le 0$ 时只接受显式 blob（`Terrain.cpp:37`）。

对任一待撒组（半径 $r$ = 组默认半径）：

$$
\text{marginX} = 0.02\,W,\qquad \text{marginY} = 0.02\,H
$$

$$
c = \big(\text{marginX} + U_1(W - 2\,\text{marginX}),\ \text{marginY} + U_2(H - 2\,\text{marginY})\big)
$$

每 blob 最多重试 64 次，接受条件为与同组已放 blob 的环距 $\ge 2r$：

$$
\min_{b \in \text{已放}} \mathrm{torusDistance}(c, b) \ge 2r
$$

64 次全失败则放弃该 blob（该组实际数量少于 `count`，保证终止与确定性）。温度区撒完后统一写入 `b.strength = cfg.thermalStrength`（`Terrain.cpp:81`）。

默认三个 `count` 全 0 → 不撒点、不消耗任何 RNG（`src/core/Config.h:101, 109-111`；`src/sim/Simulation.cpp:139-142`）。构造顺序在一切出生引导之前，以锁定 RNG 流。

#### 4.2 判定与影响公式

| 接口 | 公式 | 源码位置 |
|---|---|---|
| `inside(b, fallbackR, pos)` | $r = b.\text{radius} > 0\ ?\ b.\text{radius} : \text{fallbackR}$； $r \le 0$ 返回 false；判据 $\mathrm{torusDistance}(b, \text{pos}) \le r$（含边界） | `src/environment/Terrain.cpp:85-89` |
| `blocked(pos)` | 任一障碍 blob 命中（fallback 半径 = `obstacleRadius`） | `Terrain.cpp:91-95` |
| `inWater(pos)` | 任一水域 blob 命中（fallback 半径 = `waterRadius`） | `Terrain.cpp:97-101` |
| `moveCostMultiplier(pos)` | $\text{waterCostMultiplier}$（在水域）或 $1.0$ | `Terrain.cpp:103-105` |
| `temperatureOffset(pos)` | $\displaystyle\sum_{b}\big[b.\text{strength}\cdot(1 - d_b/r_b)\big]$，仅对 $d_b < r_b$ 求和（线性衰减圆盘：中心 = strength，边界 = 0） | `Terrain.cpp:107-117` |

水域倍率在行为层的用法： $\text{loco} = \text{locomotionCost}\cdot v^{\beta}\cdot dt\cdot\text{locoMul}$（`src/behavior/AgentBehavior.cpp:366-370`， $\beta$ 默认 2.0）。

#### 4.3 移动解算 resolveMove(from, to)

`src/environment/Terrain.cpp:119-156`。步骤：

1. $\text{target} = \mathrm{wrap}(\text{to})$；若 `blocked(from)`（实体已陷入障碍）则目标合法时返回 target，否则返回 from（`Terrain.cpp:120-122`）。
2. **防隧穿采样**：令 $d = \mathrm{torusDelta}(\text{from}, \text{target})$，依次采样 $t \in \{0.25, 0.5, 0.75\}$ 处的 $\text{mid} = \mathrm{wrap}(\text{from} + t\,d)$，首个被挡的 $t$ 记为 `hi0`（初值 1.0）； $d = (0,0)$ 时跳过（`Terrain.cpp:128-137`）。
3. 若 `!blocked(target)` 且 `hi0 >= 1.0` → 返回 target（与无地形时逐位一致）（`Terrain.cpp:138`）。
4. 若 $d = (0,0)$ → 返回 from（`Terrain.cpp:139`）。
5. 沿 from→target 二分 8 次：`lo = 0`、`hi = hi0`，中点被挡则 `hi = mid`、否则 `lo = mid`（`Terrain.cpp:142-153`）。
6. `lo <= 0` → 返回 from（全程被挡，原地不动）；否则返回 $\mathrm{wrap}(\text{from} + lo\,d)$（`Terrain.cpp:154-155`）。

调用点在行为层位置积分之后：`newPos = wrap(pos + vel*dt)`，terrain 非空时 `newPos = terrain->resolveMove(pos, newPos)`（`src/behavior/AgentBehavior.cpp:356-359`）。

#### 4.4 参数

| 符号 | 名称 | 默认值 | 单位/范围 | 源码位置 |
|---|---|---|---|---|
| `obstacleCount` | 障碍个数 | 0 | 个 | `src/core/Config.h:109` |
| `waterCount` | 水域个数 | 0 | 个 | `src/core/Config.h:110` |
| `thermalZoneCount` | 温度区个数 | 0 | 个 | `src/core/Config.h:111` |
| `obstacleRadius` | 障碍半径 | 24.0 | 世界单位 | `src/core/Config.h:116` |
| `waterRadius` | 水域半径 | 60.0 | 世界单位 | `src/core/Config.h:118` |
| `waterCostMultiplier` | 水域移动耗能倍率 | 1.8 | $\ge 1$ | `src/core/Config.h:119` |
| `thermalRadius` | 温度区半径 | 100.0 | 世界单位 | `src/core/Config.h:121` |
| `thermalStrength` | 温度区中心偏移 | 0.15 | 温度单位 | `src/core/Config.h:122` |
| `TerrainBlob::radius` | 显式 blob 半径 | 100.0 | $\le 0$ 时回退组默认 | `src/core/Config.h:104` |
| `TerrainBlob::strength` | 显式温度区强度 | 1.0 | 温度单位 | `src/core/Config.h:105` |

地形同样**没有运行时热改键**（`RuntimeEnv` 键表无地形条目；`width/height` 在拒绝名单 `kRejectKeys` 中，`src/app/RuntimeEnv.cpp:292`）。

---

### 5. 感知层 AgentSensors

纯函数：只读环境、不写任何状态（`src/perception/AgentSensors.h:56`）。输出 24 维 `SensorVector`（`src/core/SensorTypes.h:37-50`，`kDim = 24`）。

#### 5.1 有效感知半径

$$
R = \text{effectiveSenseRadius}(\text{cfg.senseRadius}, n_{scout}) =
\begin{cases}
0.25\cdot\text{base}, & n_{scout} \le 0\ (\text{"原生盲区"})\\[4pt]
\min\!\big(\text{base}\cdot\sqrt{1+n_{scout}},\ 4\,\text{base}\big), & n_{scout} > 0
\end{cases}
$$

（`src/perception/AgentSensors.cpp:74-79`；基准半径取 `PolicyConfig::senseRadius` ，其值 $= 24.0$，`src/core/Config.h:45`）。侦察球是感知的唯一来源：资源球/护盾球在函数签名中没有入口（`AgentSensors.h:14-21`；`AgentSensors.cpp:13-14`）。组合根按核名下 `BallType::Scout` 逐个计数（`src/sim/Simulation.cpp:346-349`）。 $R$ 同时是所有归一化分母（`AgentSensors.cpp:95, 133`）。

#### 5.2 s[0..9]：蓝细菌候选（唯一食物特征）

候选扫描（`src/perception/AgentSensors.cpp:98-122`）：按 `cyanoNodes` 容器序扫描存活节点， $d = |\mathrm{torusDelta}(\text{self}, \text{pos})|$，只收 $d \le R$（贴边算在内）；插入排序维护**最近 3 个**（距离相等时先扫到者优先，确定性 tie-break）。

对第 $i$ 个候选（ $i = 0,1,2$；不足则槽位保持 0）：

$$
s[2i] = \frac{\Delta x_i}{d_i},\qquad s[2i+1] = \frac{\Delta y_i}{d_i}\qquad(\text{仅 } d_i > 0 \text{ 时写入})
$$

$$
s[6+i] = \mathrm{clamp}_{[0,1]}\!\left(1 - \frac{d_i}{R}\right)\qquad(\text{归一化接近度})
$$

$$
s[9] =
\begin{cases}
\min\!\big(1,\ d_0/R\big), & \text{有候选}\\
0, & \text{无候选}
\end{cases}
$$

（`AgentSensors.cpp:134-156, 175-179`）。与自身重合的节点只贡献 `s[9] = 0`（方向/接近度不写入）。聚合方式 = **最近邻前 3（升序）**，不是求和，也不是加权平均。

#### 5.3 读噪声

$$
\text{amp} = 0.15\cdot\frac{v}{1+v}\cdot\big(1 + 3.0\cdot d\big),\qquad d = \text{temp.distortion(self)}
$$

`kReadNoiseK = 0.15`（`AgentSensors.cpp:40`）、`kTempGain = 3.0`（`AgentSensors.cpp:44`）、`readNoiseAmp(v) = k\cdot v/(1+v)`（`AgentSensors.cpp:47-50`，`v<0` 先钳 0）。 $v \ge 0$ 时 amp 对速度单调递增： $d=0$ 时上界 0.15， $d=1$ 时上界 0.6（温度失真把噪声放大 4 倍）； $v = 0$ 时 amp 恒 0（温度因子不凭空造噪声）。仅当 `rng != nullptr` 时计算 amp（否则 amp 恒 0，`AgentSensors.cpp:129-131`）。

逐候选独立抽 3 个 $U(-\text{amp}, +\text{amp})$ 偏移并叠加：

$$
s[2i] \mathrel{+}= a_x,\qquad s[2i+1] \mathrel{+}= a_y,\qquad
s[6+i] \leftarrow \mathrm{clamp}_{[0,1]}\big(s[6+i] + a_p\big)
$$

其中 $a_\bullet = \text{amp}\cdot(2U-1)$（`perturbCandidate`，`AgentSensors.cpp:59-66`；抽取 `AgentSensors.cpp:146-151`）。

加噪后 $s[0..5]$ 逐一**重新归一化**为单位向量（长度为 0 则写零向量），保证决策层点积语义不被破坏（`AgentSensors.cpp:159-170`）。`s[9]` 有独立噪声通道（P2-8 修复，不再复用候选 0 的偏移），仅在 `candN > 0 && amp > 0` 时抽 1 次并钳回 $[0,1]$（`AgentSensors.cpp:176-179`）。

**RNG 消耗**：每帧至多 $3\times 3 + 1 = 10$ 次 `nextDouble`；`amp == 0`（速度为 0 或 rng 为空）时一次都不抽（`AgentSensors.cpp:9-12`）。

#### 5.4 s[10..17]：自身与空间状态

| 维 | 含义 | 公式 | 源码位置 |
|---|---|---|---|
| `s[10]` | 速度 x（缩放） | $v_x\cdot\dfrac{1}{1+v}$ | `AgentSensors.cpp:182-185` |
| `s[11]` | 速度 y（缩放） | $v_y\cdot\dfrac{1}{1+v}$ | 同上 |
| `s[12]` | 速度大小归一化 | $v\cdot\dfrac{1}{1+v}$（= s[10..11] 的模长） | `AgentSensors.cpp:187` |
| `s[13]` | 距最近边界距离 | $\mathrm{clamp}_{[0,1]}\!\left(\dfrac{\min\big(\min(x,\,W-x),\ \min(y,\,H-y)\big)}{0.5\min(W,H)}\right)$ | `AgentSensors.cpp:191-196` |
| `s[14]` | 本帧读噪声幅度（元信息，非随机数） | amp | `AgentSensors.cpp:201` |
| `s[15]` | 保留维 | 0 | `AgentSensors.cpp:203` |
| `s[16]` | 温度失真度 | $\text{temp.distortion(self)}$ | `AgentSensors.cpp:207` |
| `s[17]` | 归一化温度（含地形偏移） | $\mathrm{clamp01}\big(\text{sample} + \text{terrain.temperatureOffset}\big)$（terrain 非空）或 $\text{sample}$ | `AgentSensors.cpp:208-210` |

`s[13]` 仅在 $W > 0 \wedge H > 0$ 时写入（`AgentSensors.cpp:191`）。

#### 5.5 s[18..23]：社会/施力感知（确定性尾部，零 RNG）

哨兵约定：`s[18] == 1 && s[19] == 0 && s[20] == 0` 等价于"无邻居或在视距外"；`s[23] == 1` 等价于"无可施力球"（`AgentSensors.cpp:213-215`）。

| 维 | 公式 | 无信息时 | 源码位置 |
|---|---|---|---|
| `s[18]` | $\mathrm{clamp}_{[0,1]}\big(\lvert\text{nearest.dist}\rvert/\text{nearest.sightRadius}\big)$，仅当 `found && sightRadius > 1e-12` | 1.0 | `AgentSensors.cpp:216-234` |
| `s[19]` | 邻居方向 x，防御性再归一化（长度 $\le 10^{-12}$ 或非有限 → 0） | 0.0 | `AgentSensors.cpp:219-229` |
| `s[20]` | 邻居方向 y，同上 | 0.0 | 同上 |
| `s[21]` | $\mathrm{clamp}_{[0,1]}(\text{energyRatio})$（NaN/负值归 0） | 0.0 | `AgentSensors.cpp:236` |
| `s[22]` | $\mathrm{clamp}_{[0,8]}(\text{targets.count})/8$，`kMaxForceTargets = 8`（`src/core/SensorTypes.h:18`） | 0.0 | `AgentSensors.cpp:237-243` |
| `s[23]` | $\mathrm{clamp}_{[0,1]}\big(\lvert\text{items}[0].\text{dist}\rvert/R\big)$，分母用有效半径 $R$ | 1.0（`count == 0` 或 $R \le 10^{-12}$） | `AgentSensors.cpp:245-249` |

三个输入由组合根每帧计算后传入（`src/sim/Simulation.cpp:396-421`）：

- `nearest`： $O(n)$ 扫描其它存活核，视距 `sight = genome.nucleusSightRadius`（ $\le 0$ 时取 170），比较 $d^2$（用无 `fmod` 的 `torusDeltaWrapped`），要求 $d^2 > 10^{-24}$ 且 $d^2 < \text{sight}^2$；方向为 self→邻居单位向量（`Simulation.cpp:396-417`）。
- `energyRatio = n.energy / n.cap`（`cap <= 0` 时 0）（`Simulation.cpp:418`）。
- `targets`：`genome.forceRange` 内的自由球（`ownerId == 0`），按环距升序插入前 8（`Simulation.cpp:356-388`）。`TargetList` 只含自由球，不含核自身名下已归属的球。

#### 5.6 聚合方式、方向约定与代价

- **聚合**：蓝细菌维 = 最近 3 个（升序，无加权）；邻居维 = 单个最近邻；施力球维 = 数量密度 + 最近者距离。全部为最近邻/计数，无求和场。
- **方向**：均为 self→目标的**环面最小位移**单位向量（跨接缝正确）； $s[0..5]$ 加噪后再归一化， $s[19..20]$ 由组合根给单位向量、感知层再归一化。
- **归一化分母**：距离维一律除以有效感知半径 $R$（`s[9]`、`s[23]`）或基因视距（`s[18]`）；能量比除以容量；速度维除以 $1+v$。
- **代价**：代码中**没有感知能量代价**，也没有感知的 CPU 计价字段。实付代价只有两项：每帧至多 10 次 `nextDouble` 的 RNG 消耗，以及候选扫描的 $O(N_{cyano})$ 遍历 + 常数 3 槽插入排序。

| 符号 | 名称 | 默认值 | 单位/范围 | 源码位置 |
|---|---|---|---|---|
| `senseRadius` | 基础探测半径 | 24.0 | 世界单位（参数页范围 [1,1000]） | `src/core/Config.h:45` |
| `kReadNoiseK` | 读噪声系数 | 0.15 | 硬编码常量 | `src/perception/AgentSensors.cpp:40` |
| `kTempGain` | 温度失真放大增益 | 3.0 | 硬编码常量 | `src/perception/AgentSensors.cpp:44` |
| `kMaxForceTargets` | 施力目标槽数 | 8 | 个 | `src/core/SensorTypes.h:18` |
| `kDim` | 传感器维数 | 24 | 维 | `src/core/SensorTypes.h:46` |
| `n_{scout}` | 侦察球数 | 0 | 个（ $\le 0$ → 1/4 半径） | `src/sim/Simulation.cpp:346-349` |

热改键：`sense_radius`（`src/app/RuntimeEnv.cpp:95`）。`kReadNoiseK` 与 `kTempGain` 是匿名命名空间硬编码常量，无配置入口（`AgentSensors.cpp:38-44` 注释亦声明此点）。

---

### 6. 环境能量注入与耗散总表

#### 6.1 每 tick 的能量注入

系统的常规能量注入**只有一条通道**：蓝细菌光合，且入账量 = 实际存进节点的量（credited）：

$$
\Phi_{tick} = \sum_{i:\ \text{alive}} \min\!\left(\frac{\text{photosynthesisRate}\cdot L_i}{L_i + \text{lightHalfSaturation}},\ \max(0,\ \text{energyCap} - E_i)\right)
$$

$$
L_i = \text{sunLevel}(t)\cdot e^{-\text{shadeK}\cdot\text{shade}_i}
$$

入账动作 `ledger_.injectPrimary(kCyanoPoolId = 0, \Phi_{tick})`（`src/sim/Simulation.cpp:707`；池 id 定义 `Simulation.cpp:39`）。

- **与太阳强度的关系**： $L_i$ 正比于 `sunLevel`，但分子分母同阶，故 $\Phi$ 对光强**次线性饱和**（ $L \to \infty$ 时每节点趋近 `photosynthesisRate`）。
- **与总面积的关系**：**无直接关系**。注入只与存活节点数及其能量余量有关；节点数由播种/分裂/饿死/捕食决定，与世界面积只有间接联系（聚落半径、遮蔽半径是世界单位）。
- 单节点上界 $\text{gain}_i \le \tfrac{2}{3}$（默认 `photosynthesisRate = 1`，`sunLevel` 被钳在 1 以内、`lightHalfSaturation` 取 0.5；遮蔽只会把 $L_i$ 压得更低）。
- 总上限则至多 $\tfrac{2}{3}N$，默认 $N = 512$ 时约 $341.3$ 能量/tick，实际还受能量帽余量截断（节点涨满后该节点注入降为 0）。
- 全图注入用**同一标量** `sunLevel`（见 1.2）；空间光照差异只出现在前端渲染与感知的 `s[17]` 温度采样中（温度场不含光）。

#### 6.2 一次性 / 事件注入

| 通道 | 量 | 源码位置 |
|---|---|---|
| 构造期蓝细菌库存 | `injectPrimary(kCyanoPoolId, cyano_.totalEnergy())`；但 `seed()` 节点能量恒 0，实际注入 0 | `src/sim/Simulation.cpp:235`；`CyanoNode.cpp:139, 153` |
| 事件 `harvest` | 每存活核 $+8$，单次总额上限 `capPerEvent`（默认 2000）；先 `injectPrimary(0, injected)` 再逐核 `transfer(0, id, 8)` | `src/app/main.cpp:413-433` |
| 事件 `bloom` | 追加节点（默认请求 120 个），新节点能量 0 → 账面零注入 | `src/app/main.cpp:464-469`；`src/sim/Simulation.cpp:1189-1200` |
| 事件 `winter` | 每核 $-5$（能量 $<5$ 跳过），`dissipate("event:winter")` | `src/app/main.cpp:434-448` |
| 事件 `plague` | 能量 $>$ `richThreshold` 的核 $-12$，`dissipate("event:plague")` | `src/app/main.cpp:449-463` |

#### 6.3 与环境层相关的耗散原因

| reason | 来源 | 量 | 源码位置 |
|---|---|---|---|
| `cyanoMetabolism` | 蓝细菌代谢（含饿死清算） | `report.metabolismLoss` | `src/sim/Simulation.cpp:708` |
| `cyanoReproduction` | 分裂耗散（子代没拿的另一半） | $0.5\cdot\text{reproductionCost}$ 每次 | `src/sim/Simulation.cpp:709`；`CyanoNode.cpp:357` |
| `ballDrift` | 自由球慢散热 | $E \times 0.0002$ 每帧 | `src/sim/Simulation.cpp:624-629` |
| `cyanoBallExpire` | 自由球池超 256 时回收最老的蓝细菌源球 | 该球全部能量 | `src/sim/Simulation.cpp:741-756` |
| `death` | 尸体营养溢出 / 半径 200 内无分解者 | 溢出全额 / 全额 | `src/sim/Simulation.cpp:679-688` |

恒等式 $\Phi + \text{harvest} - \sum\text{dissipate} = \sum \text{存活实体能量}$ 必须成立（`src/ledger/AgentLedger.h:2-4`），每帧以 $10^{-9}\max(1, \text{totalInjected})$ 容差断言（`src/sim/Simulation.cpp:790-792`）。

---

### 7. 参数索引与"默认值不一致"说明

#### 7.1 参数页 schema 默认值 ≠ 运行时实际值（本域相关）

`ParamSchema` 的 `dflt` 是参数页"恢复默认"的目标值，与 `Config.h` 的历史实际值允许不同（`src/core/Config.h:137-139` 的既有约定）。本域差异项：

| 键 | schema 默认 | 运行时实际 | 源码位置 |
|---|---|---|---|
| `cyano_predation_radius` | 12.0 | 14.0 | `src/app/ParamSchema.cpp:122`；`src/core/Config.h:184` |
| `cyano_conversion_rate` | 1.0 | 0.6 | `ParamSchema.cpp:124`；`Config.h:275` |
| `cyano_conversion_speed_ref` | 20.0 | 40.0 | `ParamSchema.cpp:126`；`Config.h:277` |
| `cyano_conversion_accel_ref` | 6.0 | 60.0 | `ParamSchema.cpp:127`；`Config.h:278` |
| `cyano_conversion_min_rate` | 0.1 | 0.05 | `ParamSchema.cpp:130`；`Config.h:281` |
| `cyano_hue_jitter` | 8.0 | 20.0 | `ParamSchema.cpp:132`；`Config.h:285` |
| `cyano_colonies` | 4.0 | 64 | `ParamSchema.cpp:113`；`Config.h:286` |
| `sun_base_level` | 1.0 | 0.5 | `ParamSchema.cpp:70`；`Config.h:35` |
| `sun_day_period_frames` | 1000.0 | 628.0 | `ParamSchema.cpp:72`；`Config.h:37` |
| `sun_vertical_gradient` | 0.2 | 0.0 | `ParamSchema.cpp:73`；`Config.h:38` |
| `sun_noise_amplitude` | 0.2 | 0.0 | `ParamSchema.cpp:74`；`Config.h:39` |
| `sun_noise_frequency` | 1.0 | 0.125 | `ParamSchema.cpp:75`；`Config.h:40` |
| `sun_noise_speed` | 0.5 | 1.0 | `ParamSchema.cpp:76`；`Config.h:41` |

#### 7.2 建造顺序对 RNG 流的锁定

构造期顺序固定为：地形撒点 → 温度场撒源 → 核出生 → 蓝细菌播种（`src/sim/Simulation.cpp:139-152, 233-234`）。默认地形三 count 为 0 时不消耗 RNG，故默认配置下地形不改变下游随机流。默认配置消耗：温度场 12 抽（3 源 × 4），蓝细菌聚落播种 $2N_c + 3N$ 抽（默认 $2\times64 + 3\times512 = 1664$）。
