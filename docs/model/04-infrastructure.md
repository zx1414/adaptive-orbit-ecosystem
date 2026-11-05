## 基础设施层：随机数、参数系统、快照/存档格式、时间轴、HTTP、trace 与 CLI

本小节覆盖以下源码模块，全部算式与常数均取自源码并标注 `文件:行号`：

| 模块 | 文件 |
|---|---|
| 伪随机数发生器 | `src/core/PCG32.h`, `src/core/PCG32.cpp` |
| 参数元数据 | `src/app/ParamSchema.h`, `src/app/ParamSchema.cpp` |
| 参数落点映射 | `src/app/RuntimeEnv.h`, `src/app/RuntimeEnv.cpp` |
| 实时调控命令 | `src/app/EnvControl.h`, `src/app/EnvControl.cpp` |
| 材质包 | `src/app/ResourcePack.h`, `src/app/ResourcePack.cpp` |
| 存档 | `src/app/SaveStore.h`, `src/app/SaveStore.cpp` |
| 时间轴 | `src/app/Timeline.h`, `src/app/Timeline.cpp` |
| trace 导出 | `src/app/TraceWriter.h`, `src/app/TraceWriter.cpp` |
| HTTP 服务 | `src/app/HttpServer.h`, `src/app/HttpServer.cpp` |
| 路由与主循环 | `src/app/main.cpp` |
| CLI | `src/app/Cli.h`, `src/app/Cli.cpp` |

---

## 一、随机数与确定性

### 1.1 PCG32：状态转移与输出函数

生成器是两个 64 位无符号整数 `state_` / `inc_`（`src/core/PCG32.h:26-27`）。一次 `nextUint32()` 的完整算式（`src/core/PCG32.cpp:13-20`）：

状态转移（LCG，乘子与增量均为 64 位回绕）：

$$ \text{state}' = \text{old} \cdot 6364136223846793005 + \text{inc} \pmod{2^{64}} $$

输出函数（XSL-RR 64→32）：

$$ \text{xorshifted} = \left(\left(\left(\text{old} \gg 18\right) \oplus \text{old}\right) \gg 27\right) \bmod 2^{32} $$

$$ \text{rot} = \left\lfloor \text{old} / 2^{59} \right\rfloor \in [0, 31] $$

$$ \text{out} = \left(\text{xorshifted} \gg \text{rot}\right) \,\Big|\, \left(\text{xorshifted} \ll \left((32 - \text{rot}) \bmod 32\right)\right) $$

注意输出用的是**转移前**的 `old`（`src/core/PCG32.cpp:14`），不是转移后的新状态。

`nextDouble()` 消耗**两次** `nextUint32()`，拼 53 位有效位（`src/core/PCG32.cpp:22-26`）：

$$ a = \left\lfloor \text{u32}_1 / 2^{5} \right\rfloor \in [0, 2^{27}) ,\quad b = \left\lfloor \text{u32}_2 / 2^{6} \right\rfloor \in [0, 2^{26}) $$

$$ x = \left(2^{26} a + b\right) \cdot \frac{1}{2^{53}} \in [0, 1) $$

### 1.2 种子派生

构造函数签名与默认值（`src/core/PCG32.h:13-14`）：

```cpp
PCG32(std::uint64_t seed = 0x853c49e6748fea9bULL,
      std::uint64_t seq  = 0xda3e39cb94b95bdbULL);
```

初始化序列（`src/core/PCG32.cpp:6-11`），刻意"空转两次"以避免弱种子：

1. `inc_ = (seq << 1) | 1`，强制奇数；默认 `seq` 代入得 `inc_ = 0xb47c73972972b7b7`。
2. `state_ = 0`，调用一次 `nextUint32()`（丢弃输出）→ `state_ = inc_`。
3. `state_ += seed`（mod $2^{64}$）。
4. 再调用一次 `nextUint32()`（丢弃输出）。

构造结束后的状态闭式：

$$ \text{state}_0 = \left(\left(\text{inc} + \text{seed}\right) \cdot 6364136223846793005 + \text{inc}\right) \bmod 2^{64} $$

（自检值：代入 `seed=42` 与默认 `seq`，得 `state_0 = 0x799bb7a22dcaa944`；此值由上式复算而来，不是源码字面量。）

**子系统如何使用种子**：仿真只有一个 `PCG32` 成员 `rng_`，构造时传入世界种子：

```cpp
// src/sim/Simulation.cpp:138
: cfg_(cfg), rng_(cfg.world.seed),
```

即 `seq` 恒为默认值，**没有**按子系统分流派生（没有 stream/splitmix 分派，没有 per-entity RNG）。整个仿真只有一条随机流，所有子系统按固定顺序从同一条流取数。

唯一的例外是感知机策略：它在**构造期**自建一个独立 `PCG32`，种子来自 `cfg.policy.perceptronSeed`（默认 12345，`src/core/Config.h:52`）：

```cpp
// src/decision/PerceptronPolicy.cpp:55-61
PCG32 rng(seed);
b_[i] = (rng.nextDouble() * 2.0 - 1.0) * scale;
w_[i][j] = (rng.nextDouble() * 2.0 - 1.0) * scale;
```

这条流是封闭的：只写权重/偏置，不回流到 `rng_`，也不在 `decide()` 中被使用（`src/decision/PerceptronPolicy.cpp:54`）。

| 符号 | 名称 | 默认值 | 单位/范围 | 源码位置 |
|---|---|---|---|---|
| $M$ | LCG 乘子 | 6364136223846793005 | 无量纲 | `src/core/PCG32.cpp:15` |
| $\text{inc}_0$ | 默认增量（`seq=0xda3e39cb94b95bdb`） | 0xb47c73972972b7b7 | 奇数， $[0,2^{64})$ | `src/core/PCG32.cpp:7` |
| `seed` | 默认种子参数 | 0x853c49e6748fea9b | uint64 | `src/core/PCG32.h:13` |
| `seq` | 默认序列参数 | 0xda3e39cb94b95bdb | uint64 | `src/core/PCG32.h:14` |
| $2^{53}$ | nextDouble 分母 | 9007199254740992 | — | `src/core/PCG32.cpp:25` |

### 1.3 单一随机流的消耗点与固定顺序

因只有一条流，"谁先抽"就是语义。`Simulation::step()` 的消耗时序分三段：**核循环内逐核交错** → **繁殖段** → **蓝细菌段**。段 B 与段 C 之间还夹着 `ballWorldStep()` / `ballOwnership()` / 自由球结算 / 尸体分配，这几步全部零 RNG（`src/sim/Simulation.cpp:572-692`）。

**段 A：核循环内，对每个存活核按下标升序依次执行**

| 步 | 消耗点 | 抽取次数 | 源码位置 |
|---|---|---|---|
| A1 | 感知读噪声 | 有候选且 `amp > 0` 时：每候选 3 次 + s[9] 1 次，最多 10 次；否则 0 次 | `src/perception/AgentSensors.cpp:148-150`、`:177` |
| A2 | 行为写噪声 | `amp > 0` 时 2 次 | `src/behavior/AgentBehavior.cpp:310-311` |
| A3 | T08 触发 roll | 恒 1 次（与是否触发解耦） | `src/behavior/AgentBehavior.cpp:548` |
| A4 | 产球（deploy）轨道角 | 命中时 1 次 | `src/behavior/AgentBehavior.cpp:114` |

A1 属于 `sense()`（`src/sim/Simulation.cpp:419-421`），A2–A4 都在 `AgentBehavior::apply()` 内（`src/sim/Simulation.cpp:446`）。因此两个相邻核之间是"核 i 的 A1–A4，然后核 i+1 的 A1–A4"，不是"全体 A1 然后全体 A2"。

**段 B：繁殖（核循环结束后，按 `reproParents` 收集顺序）**

| 步 | 消耗点 | 抽取次数 | 源码位置 |
|---|---|---|---|
| B1 | `genomeMutate`（25 个 Box–Muller 高斯） | 恰 50 次 | `src/core/Genome.cpp:137-176` |
| B2 | 出生偏移 dx、dy | 恰 2 次（先 dx 后 dy） | `src/behavior/AgentBehavior.cpp:280-281` |

拒绝路径（能量不足/护栏触顶）消耗 0 次（先检查后抽取，`src/behavior/AgentBehavior.cpp:256-262`）。

**段 C：蓝细菌 `CyanoField::step()`（`src/sim/Simulation.cpp:705-706`）**

| 步 | 消耗点 | 抽取次数 | 源码位置 |
|---|---|---|---|
| C1 | 产球判定 | 每个存活节点 1 次 | `src/environment/CyanoNode.cpp:245` |
| C2 | 产球位置（偏移距离、角度） | 每个产球节点 2 次 | `src/environment/CyanoNode.cpp:265,268` |
| C3 | 分裂判定 | 每个存活节点 1 次（**触顶也照抽**） | `src/environment/CyanoNode.cpp:290` |
| C4 | 分裂色相高斯（u1、u2） | 每个判定通过者 2 次 | `src/environment/CyanoNode.cpp:295-296` |
| C5 | 分裂半径、角度 | 每个判定通过者 2 次（**间距校验失败也照抽**） | `src/environment/CyanoNode.cpp:313,319` |

C2.5 的 `linkTransfer()` 位于 C1 之前但**零 RNG**（无 `rng` 参数，`src/environment/CyanoNode.cpp:223-236`）。

构造期（`Simulation::Simulation`）的消耗顺序固定为：地形撒点 → 温度场撒源 → 核出生引导 → 蓝细菌播种（`src/sim/Simulation.cpp:139-150`、`:233-234`）。

| 构造阶段 | 抽取规则 | 源码位置 |
|---|---|---|
| 地形撒点 | 每个候选点 2 次，最多 64 次尝试/blob | `src/environment/Terrain.cpp:48-50` |
| 温度场撒源 | 每个源 4 次（x, y, amplitude, radius） | `src/environment/TemperatureField.cpp:40-43` |
| 核出生位置 | 每次尝试 2 次，最多 200+200 次 | `src/sim/Simulation.cpp:289-306` |
| 核基因组 | 每核恰好 25 次；不育重掷最多 8 轮 × 25 次 | `src/core/Genome.cpp:66-97`、`src/sim/Simulation.cpp:314,322-323` |
| 蓝细菌播种（聚落模式） | 每聚落 2 次 + 每节点 3 次 | `src/environment/CyanoNode.cpp:125-141` |
| 蓝细菌播种（均匀模式） | 每节点 3 次（x, y, hue） | `src/environment/CyanoNode.cpp:151-155` |

**"无论是否生效都固定消耗"是本项目确定性的核心纪律**，源码里三处显式声明：

- 蓝细菌分裂判定触顶时仍抽一次（`src/environment/CyanoNode.cpp:287-291`）。
- 分裂位置抽取后即使间距校验失败也不退还（`src/environment/CyanoNode.cpp:324-326`）。
- T08 触发 roll 每帧恒抽一次，"保证 RNG 流与能量状态解耦"（`src/behavior/AgentBehavior.cpp:546-548`）。

感知噪声则相反：`amp == 0`（速度 0）或 `rng == nullptr` 时**一次都不抽**（`src/perception/AgentSensors.cpp:144-151`）。

### 1.4 零 RNG 的确定性通道

阳光噪声不用 RNG，用 splitmix64 风格整数哈希（`src/sim/Simulation.cpp:83-97`）：

$$ \text{mix}(x):\quad x \mathrel{+}= \text{0x9e3779b97f4a7c15};\; x = (x \oplus (x \gg 30)) \cdot \text{0xbf58476d1ce4e5b9};\; x = (x \oplus (x \gg 27)) \cdot \text{0x94d049bb133111eb};\; x = x \oplus (x \gg 31) $$

$$ h = \text{mix}(a) \oplus \text{mix}(b + \text{0x517cc1b727220a95}) \oplus \text{mix}(c + \text{0x2545f4914f6cdd1d}) $$

$$ \text{hash01}(a,b,c) = \left\lfloor h / 2^{11} \right\rfloor \cdot \frac{1}{2^{53}} \in [0,1) $$

单点阳光（`src/sim/Simulation.cpp:100-127`）：

$$ \text{base} = \text{sun.baseLevel} - \text{sun.verticalGradient} \cdot \frac{y}{H} $$

$$ \text{day} = \text{sun.dayAmplitude} \cdot \sin\!\left(\frac{2\pi \cdot \text{tick}}{\text{sun.dayPeriodFrames}}\right) $$

$$ \text{noise} = \text{sun.noiseAmplitude} \cdot \left(\text{hash01}\left(\left\lfloor \tfrac{\text{tick}}{\text{sun.noiseSpeed}} \right\rfloor, \left\lfloor y \cdot \text{sun.noiseFrequency} \right\rfloor, 0\right) - 0.5\right) $$

$$ \text{sunLevel}(t,y) = \mathrm{clamp}_{[0,1]}(\text{base} + \text{day} + \text{noise}) $$

（`day` 项在 `dayPeriodFrames <= 0` 或 `dayAmplitude == 0` 时短路为 0；`noise` 项在 `noiseAmplitude == 0` 或 `noiseFrequency <= 0` 时短路为 0；`noiseSpeed <= 0` 时时间格恒 0。`src/sim/Simulation.cpp:105-122`）

其余确定性选择：所有"最近邻/最近节点/换主"扫描均为按容器下标序的线性扫描，平距取先扫到者（`src/environment/CyanoNode.cpp:104-122`、`src/sim/Simulation.cpp:487-497`、`:966-975`）。

### 1.5 迭代顺序与容器选择

| 对象 | 容器 | 迭代顺序 | 源码位置 |
|---|---|---|---|
| 核 | `std::vector<Nucleus>` | 下标升序 | `src/sim/Simulation.h:130` |
| 核名下球 | `std::vector<Ball>` | 下标升序 | `src/behavior/AgentBehavior.h:110` |
| 自由球 | `std::vector<Ball>` | 下标升序 | `src/sim/Simulation.h:134` |
| 蓝细菌节点 | `std::vector<CyanoNode>` | 下标升序 | `src/sim/Simulation.h:128` |
| 蓝细菌连线 | `std::vector<std::pair<EntityId,EntityId>>` | 下标升序 | `src/sim/Simulation.h:100-102` |
| 账本按原因耗散 | `std::map<std::string,double>` | 字典序（`std::string` 比较） | `src/ledger/AgentLedger.h:49` |
| 球力累加器 | `std::map<EntityId, Vec2>` | id 升序 | `src/sim/Simulation.cpp:872` |

`src/` 全目录中 `unordered_map` / `unordered_set` 出现次数为 0（grep 验证），因此不存在"哈希遍历顺序"这一不确定性来源。`std::map` 的有序性同时保证了浮点累加的**求和顺序**恒定（账本按原因耗散表、球力累加器都依赖这一点）。

### 1.6 同种子同输出的成立条件

同 `seed` + 同参数 + 同二进制 → 逐位相同的输出，成立条件（每条均有源码对应）：

1. 单一 PCG32 流，抽取次数与顺序与状态量无关（§1.3）。
2. 所有实体容器为有序序列，遍历顺序 = 下标序（§1.5）。
3. 仿真路径不含 `<random>` / `rand()` / `random_device` / `chrono`（`src/core/Genome.cpp:3` 的注释即为门禁扫描口径；`chrono` 仅出现在 `save` 时间戳与 `--serve` 帧率节流，均不参与仿真状态）。
4. 无墙钟/线程调度进入状态：`step()` 完全同步，主循环限速只影响 `--serve` 的帧间隔（`src/app/main.cpp:1156-1167`）。
5. 浮点为 IEEE-754 double。构建命令行（`tools/build.ps1:49`、`tools/rebuild.bat:11`）为 `/nologo /std:c++17 /W4 /WX /EHsc /permissive- /utf-8`，**未指定** `/fp:fast` 或 `/fp:contract`，即 MSVC 默认 `/fp:precise`；但该默认值本身不在仓库内固化，换编译器/换优化档需自行复核。

可执行的校验手段有两个：`Simulation::stateHash()`（FNV-1a 64）与 `/state` 快照逐字节比较。

状态哈希（`src/sim/Simulation.cpp:1026-1029`、`:1046-1089`）：

$$ h_0 = \text{0xcbf29ce484222325} ;\qquad \text{mix}(h, v):\; h \leftarrow (h \oplus v) \cdot \text{0x100000001b3} $$

入哈希的字段依次为：`tick`；每个核的 `pos.x, pos.y, vel.x, vel.y, energy, speed, alive` 及其每颗球的 `angle, radius, energy, lastOmega`；每个蓝细菌节点的 `id, pos.x, pos.y, energy, alive, hue`；每颗自由球的 `id, pos.x, pos.y, vel.x, vel.y, energy`。`double` 按位入哈希，NaN 归一为 `0x7ff8000000000000`（`src/sim/Simulation.cpp:1030-1038`）。

### 1.7 基因组随机化与变异的抽取契约

`genomeRandomize` 恰好 25 次 `nextDouble()`，顺序为：`affinity[0..2]` → `orbitRadius[0..2]` → `absorbPreference[0..2]` → `repelStrength[0..2]` → 12 个标量（声明序）→ `hue`（`src/core/Genome.cpp:66-97`）。

$$ g_k \sim \text{InitMin} + (\text{InitMax} - \text{InitMin}) \cdot u,\quad u = \text{nextDouble}() $$

`genomeMutate` 恰好 25 个高斯 = 50 次 `nextDouble()`（`src/core/Genome.cpp:137-176`）。高斯用 Box–Muller，每次调用固定消耗 2 次、不缓存配对样本：

$$ u_1 = 1 - \text{nextDouble}() \in (0,1],\quad u_2 = \text{nextDouble}() \in [0,1) $$

$$ z = \sqrt{-2 \ln u_1} \cdot \cos(2\pi u_2) $$

$$ g_k \leftarrow g_k + z \cdot \big(\text{mutationRate} \cdot (\text{ClampMax}_k - \text{ClampMin}_k)\big) \cdot \text{coeff} $$

色相单独处理，不乘 `mutationRate`：

$$ \text{hue} \leftarrow \text{hue} + z \cdot \text{hueJitterSigma} \cdot \text{coeff} \pmod{360} $$

收尾固定执行 `genomeClamp`（钳回 Clamp 区间，hue 环绕）与 `genomeNormalizeAffinity`（`src/core/Genome.cpp:174-175`）。

| 符号 | 名称 | 默认值 | 单位/范围 | 源码位置 |
|---|---|---|---|---|
| `coeff` | 全局变异系数 | 0.25 | 无量纲，0~10 | `src/core/Config.h:79` |
| `hueJitterSigma` | 核色相抖动 σ | 8.0 | 度 | `src/core/Genome.h:74` |
| `kBirthOffset` | 出生偏移半径 | 12.0 | 世界单位 | `src/behavior/AgentBehavior.cpp:31` |
| `kChildEnergyFraction` | 子代能量占比 | 0.5 | 无量纲 | `src/behavior/AgentBehavior.cpp:27` |
| `kDeathCarrionFraction` | 死亡遗产入包占比 | 0.3 | 无量纲 | `src/behavior/AgentBehavior.cpp:34` |

---

## 二、参数系统

### 2.1 三层结构

| 层 | 职责 | 源码 |
|---|---|---|
| `SimConfig`（POD 默认值） | 真实生效值的起点；构造函数拷贝一份到 `cfg_` | `src/core/Config.h:305-328` |
| `ParamSchema`（元数据表） | 113 个键的 key/group/desc/type/min/max/dflt；**不持有 Config 指针，不做 key→字段映射** | `src/app/ParamSchema.cpp:55-188` |
| `RuntimeEnv`（映射唯一权威） | key→`SimConfig` 字段的写入函数表，113 条 | `src/app/RuntimeEnv.cpp:88-280` |

源码对"schema 默认值 ≠ Config 默认值"有明确表态：`ParamSchema` 的 `dflt` 是参数页"恢复默认"的目标值，与 `Config.h` 的历史实际值允许不同（`src/core/Config.h:137-139`、`src/app/ParamSchema.cpp:53-54`）。本节的参数表因此**同时列出两列**：`Config 默认`（真实生效）与 `schema dflt`（参数页恢复值）。

键数核对：`kRows` 共 113 条（`world` 11 / `light` 10 / `food` 12 / `pressure` 9 / `producer` 4 / `cyano` 20 / `genome` 24 / `ball` 15 / `nucleus` 8），源码有编译期下限断言 `>= 110`（`src/app/ParamSchema.cpp:190-191`）。`RuntimeEnv` 的 `entryTable()` 也是 113 条，但集合不同：

$$ 113 = \underbrace{108}_{\text{schema 中已接线}} + \underbrace{2}_{\texttt{migration\_bonus},\ \texttt{migration\_radius}} + \underbrace{1}_{\texttt{cyano\_enabled}} + \underbrace{2}_{\texttt{cyano\_ball\_spawn\_*} \text{ 别名}} $$

`--serve` 启动横幅打印的 `wiredKeyCount()` 即 113（`src/app/main.cpp:1038-1039`）。

### 2.2 覆盖优先级与生效时机

参数进入系统的**全部**通道，按优先级从低到高：

1. **编译期默认值**：`SimConfig` 结构体成员初值（`src/core/Config.h`）。这是唯一"永远存在"的一层。
2. **CLI**：`parseCli` 直接写 `out.config.*`（`src/app/Cli.cpp:62-291`）。它只触及 22 个字段：`world.width/height/maxTicks/seed`、`colony.nuclei`、`cyanoNodeCount`、`cyanoMaxNodes`、`sun.baseLevel`、`sun.dayAmplitude`、`policy.name/usePerceptron/useThermal/mutation/perceptronSeed/perceptronScale/thermalPreferredTemp/thermalWThermal/thermalWCaution`、`terrain.obstacleCount/waterCount/thermalZoneCount`、`agent.agentMode`。**`RuntimeTuning` / `NucleusTuning` / `BallTuning` / `CyanoTuning` 四组无任何 CLI 入口。**
3. **运行时覆盖（HTTP）**：`POST /env` → `EnvControl::apply` → 钳制 → `EnvChange{key,value}` → `RuntimeEnv::applyAll(liveCfg, changes)` → `bridge.markCfgDirty()`（`src/app/main.cpp:360-376`）。下一帧主循环 `takeCfgDirty()` → `sim.applyConfig(liveCfg)`（`src/app/main.cpp:1118-1124`）。
4. **存档/时间轴载入**：`SaveStore::loadHeader` 解析出的 cfg 用于构造 `Simulation`；`Timeline::importFrom` 覆盖 `cfg_`。这是"整份配置替换"，优先级最高。

**生效时机**（`Simulation::applyConfig`，`src/sim/Simulation.cpp:1094-1187`）：

| 参数类 | 生效时机 | 依据 |
|---|---|---|
| `sun.*` | 下一帧 `step()` 直接读 `cfg_.sun` | `src/sim/Simulation.cpp:702` |
| `cyanoTuning.*` | 下一帧 `cyano_.step()`（就地改写 `params_`，**绝不重建 CyanoField**） | `src/sim/Simulation.cpp:1145-1186` |
| `nucleus.*` 中 8 个镜像字段（`respirationBase` / `locomotionCost` / `cyanoPredationRadius` / `migrationBonus` / `migrationRadius` / `speedCostExponent` / `damping` / `reproductionCostRatio`） | 下一帧 `bp_` 被 `AgentBehavior::apply` 消费 | `src/sim/Simulation.cpp:1109-1116` |
| `ball.*`（15 个力场键） | 下一帧 `ballField_` 被 `ballWorldStep` 消费 | `src/sim/Simulation.cpp:1119-1133` |
| `colony.genomeRanges` | **只影响之后出生的核**（已存在 genome 不可改） | `src/app/RuntimeEnv.cpp:29-39` |
| `world.width/height/seed/maxTicks` | **不热改**，保持原值 | `src/sim/Simulation.cpp:1100` |
| `colony.nuclei`、`cyanoNodeCount`、`cyanoMaxNodes` | **不热改**（出生引导参数） | `src/sim/Simulation.cpp:1101-1103` |
| `terrain.waterCostMultiplier` | 本局不生效：`Terrain` 构造期快照，下一局生效 | `src/app/RuntimeEnv.cpp:13-14`、`src/sim/Simulation.cpp:1099-1104` |

### 2.3 校验 vs 钳制：两套刻意的不同语义

| 路径 | 语义 | 越界处理 | 源码 |
|---|---|---|---|
| `ParamSchema::validate` | **校验**（参数页语义） | 越界即拒绝，返回 false + 中文原因 | `src/app/ParamSchema.cpp:330-379` |
| `EnvControl::apply` 细调 | **钳制**（实时滑杆用） | 收敛到 `[min,max]` 再写入 | `src/app/EnvControl.cpp:272-278` |
| `RuntimeEnv::applyOne` | 只做非有限值防御 | `NaN/Inf` 拒绝（不进 unwired，不算未知键） | `src/app/RuntimeEnv.cpp:331` |

`clampToSchema` 的完整算式（`src/app/EnvControl.cpp:272-278`）：

$$ v' = \min(\max(v, \text{min}), \text{max}) ;\qquad \text{Int}: v' = \mathrm{round}(v) ;\qquad \text{Bool}: v' = \begin{cases}1 & v \ge 0.5 \\ 0 & v < 0.5\end{cases} $$

整数族写入器另有一层收窄（`src/app/RuntimeEnv.cpp:74-83`）：

$$ \text{toSizeT}(v) = \begin{cases} 0 & \lnot(v>0) \\ \min(\lfloor v + 0.5 \rfloor, 9.2\times10^{18}) & \text{否则} \end{cases} $$

$$ \text{toNonNegInt}(v) = \begin{cases} 0 & \lnot(v>0) \\ \min(\lfloor v + 0.5 \rfloor, 2.1\times10^{9}) & \text{否则} \end{cases} $$

布尔键一律按阈值折算：`wrap_boundary` → `v >= 0.5`（`src/app/RuntimeEnv.cpp:257`），`cyano_enabled` → `v >= 0.5`（`:270`）。

`ParamSchema::validate` 在 `src/` 内**没有任何调用点**（全仓 grep 只在 `tests/unit/test_envcontrol.cpp` 中出现）。实际生效的 `POST /env` 路径走的是 `EnvControl::apply` 的钳制语义；`validate` 目前只有单测与外部工具会用到。

### 2.4 拒绝名单与未接线诊断

五个键在 `RuntimeEnv` 中被**设计性拒绝**（静默、不告警、不进 unwired）：`width`、`height`、`seed`、`ticks`、`regrow_food`（`src/app/RuntimeEnv.cpp:292`）。

未知键的处理（`src/app/RuntimeEnv.cpp:341-357`）：记入 `unwired` 集合（去重，按首次出现序），每个 key 在进程生命周期内只打一次 stderr 警告；`POST /env` 响应体的 `unwired` 字段会带上该集合（`src/app/main.cpp:387-395`）。

### 2.5 资源包（ResourcePack）不参与任何数理参数

重要事实：材质包**只能改颜色与渲染标量**，对 §2.4 的 113 个参数没有任何写入口。

包结构（`src/app/ResourcePack.h:6-9`）：

```cpp
struct ResourcePack {
    std::string name, desc, version, author;
    std::map<std::string, std::string> colors;   // key -> "r,g,b"
};
```

- `pack.txt`：只解析 `name` / `desc` / `version` / `author` 四个键，未知键忽略（`src/app/ResourcePack.cpp:152-168`）。
- `colors.txt`：`mode`、7 个标量键（`size_ball_* / size_free_ball / size_nucleus / cyano_node_size / cyano_link_alpha`）、1 个字符串键（`cyano_color_mode`）原样存储；其余键必须是 `r,g,b` 三元组（段数 ≥ 3 时取前 3 段、每段 trim 后严格整数、非法整行丢弃），归一化为 `"r,g,b"`（`src/app/ResourcePack.cpp:82-135`）。
- 合并语义：以 `builtInDefaults()` 打底，按 `enabled` 顺序叠加，后者覆盖前者（`src/app/ResourcePack.cpp:268-280`）。
- 选项持久化：`resourcepacks.list` 单行 `enabled=a,b,c`（`src/app/ResourcePack.cpp:291-339`）；文件不存在 = 空列表且不报错。

内置调色板默认值（`src/app/ResourcePack.cpp:252-266`）：

| 键 | 默认值 | 源码位置 |
|---|---|---|
| `mode` | `type_color` | `src/app/ResourcePack.cpp:258` |
| `background` | `10,14,26` | `:259` |
| `ball_shield` | `255,207,94` | `:260` |
| `ball_worker` | `69,224,216` | `:261` |
| `ball_scout` | `183,139,255` | `:262` |
| `free_ball` | `200,200,210` | `:263` |
| `nucleus` | `255,255,255` | `:264` |

### 2.6 参数总表

图例：**Config 默认** = `Config.h` 中的真实初始值；**schema** = `ParamSchema` 的 `dflt`（参数页"恢复默认"目标值），两栏不同时以 Config 为准。位置列格式为 `ParamSchema:行 / RuntimeEnv:行`。

#### world（世界与运行，11 键）

| 键 | 类型 | 范围 | Config 默认 | schema | 落点字段 | 位置 |
|---|---|---|---|---|---|---|
| `width` | Double | 100~10000 | 1024.0 | 1024.0 | —（拒绝，不可热改） | `ParamSchema.cpp:57` |
| `height` | Double | 100~10000 | 1024.0 | 1024.0 | —（拒绝） | `ParamSchema.cpp:58` |
| `seed` | Int | 0~4294967295 | 42 | 42 | —（拒绝） | `ParamSchema.cpp:59` |
| `ticks` | Int | 1~1e9 | 1000 | 1000 | —（拒绝） | `ParamSchema.cpp:60` |
| `nuclei` | Int | 0~100000 | 12 | 12 | `colony.nuclei`（不热改） | `ParamSchema.cpp:61 / RuntimeEnv.cpp:92` |
| `balls` | Int | 0~100000 | 0 | 64 | `runtime.initialFreeBalls`（无消费者） | `ParamSchema.cpp:62 / RuntimeEnv.cpp:93` |
| `cyano_nodes` | Int | 0~100000 | 512 | 512 | `cyanoNodeCount`（不热改） | `ParamSchema.cpp:63 / RuntimeEnv.cpp:94` |
| `sense_radius` | Double | 1~1000 | 24.0 | 24.0 | `policy.senseRadius` | `ParamSchema.cpp:64 / RuntimeEnv.cpp:95` |
| `max_fps` | Double | 0~1000 | 0.0 | 0.0 | `runtime.maxFps` | `ParamSchema.cpp:65 / RuntimeEnv.cpp:96` |
| `render_interval` | Int | 0~10000 | 1 | 1 | `runtime.renderInterval` | `ParamSchema.cpp:66 / RuntimeEnv.cpp:97` |
| `autosave_interval` | Int | 0~1000000 | 200 | 200 | `runtime.autosaveInterval`（无消费者） | `ParamSchema.cpp:67 / RuntimeEnv.cpp:98` |

#### light（光照，10 键）

| 键 | 类型 | 范围 | Config 默认 | schema | 落点字段 | 位置 |
|---|---|---|---|---|---|---|
| `sun_base_level` | Double | 0~5 | 0.5 | 1.0 | `sun.baseLevel` | `ParamSchema.cpp:70 / RuntimeEnv.cpp:101` |
| `sun_day_amplitude` | Double | 0~5 | 0.5 | 0.5 | `sun.dayAmplitude` | `ParamSchema.cpp:71 / RuntimeEnv.cpp:102` |
| `sun_day_period_frames` | Double | 0~1e6 | 628.0 | 1000.0 | `sun.dayPeriodFrames` | `ParamSchema.cpp:72 / RuntimeEnv.cpp:103` |
| `sun_vertical_gradient` | Double | 0~1 | 0.0 | 0.2 | `sun.verticalGradient` | `ParamSchema.cpp:73 / RuntimeEnv.cpp:104` |
| `sun_noise_amplitude` | Double | 0~5 | 0.0 | 0.2 | `sun.noiseAmplitude` | `ParamSchema.cpp:74 / RuntimeEnv.cpp:105` |
| `sun_noise_frequency` | Double | 0~10 | 0.125 | 1.0 | `sun.noiseFrequency` | `ParamSchema.cpp:75 / RuntimeEnv.cpp:106` |
| `sun_noise_speed` | Double | 0~10 | 1.0 | 0.5 | `sun.noiseSpeed` | `ParamSchema.cpp:76 / RuntimeEnv.cpp:107` |
| `cyano_light_half_saturation` | Double | 0~10 | 0.5 | 0.5 | `cyanoTuning.lightHalfSaturation` | `ParamSchema.cpp:77 / RuntimeEnv.cpp:108` |
| `light_warmth` | Double | 0~1 | 0.5 | 0.5 | `runtime.lightWarmth`（无消费者） | `ParamSchema.cpp:78 / RuntimeEnv.cpp:109` |
| `light_sample_grid` | Int | 4~64 | 8 | 8 | `runtime.lightSampleGrid`（无消费者） | `ParamSchema.cpp:79 / RuntimeEnv.cpp:110` |

#### food（自由球能量经济学，12 键）

| 键 | 类型 | 范围 | Config 默认 | schema | 落点字段 | 位置 |
|---|---|---|---|---|---|---|
| `ball_energy` | Double | 0.01~1000 | 10.0 | 10.0 | `ball.energy`（无运行时读取点） | `ParamSchema.cpp:82 / RuntimeEnv.cpp:113` |
| `ball_radius` | Double | 0.1~100 | 1.0 | 1.0 | `ball.radius`（无消费者） | `ParamSchema.cpp:83 / RuntimeEnv.cpp:114` |
| `ball_vel_range` | Double | 0~50 | 5.0 | 5.0 | `ball.velRange`（无消费者） | `ParamSchema.cpp:84 / RuntimeEnv.cpp:115` |
| `ball_initial_energy` | Double | 0~1000 | 10.0 | 10.0 | `ball.initialEnergy`（无消费者） | `ParamSchema.cpp:85 / RuntimeEnv.cpp:116` |
| `ball_energy_cap` | Double | 0~10000 | 500.0 | 500.0 | `ball.energyCap`（无消费者） | `ParamSchema.cpp:86 / RuntimeEnv.cpp:117` |
| `ball_spawn_probability` | Double | 0~1 | 0.02 | 0.0 | `cyanoTuning.ballSpawnProbability` | `ParamSchema.cpp:87 / RuntimeEnv.cpp:118` |
| `ball_spawn_cost` | Double | 0~1000 | 30.0 | 30.0 | `cyanoTuning.ballSpawnCost` | `ParamSchema.cpp:88 / RuntimeEnv.cpp:119` |
| `ball_spawn_min_dist` | Double | 0~1000 | 10.0 | 10.0 | `cyanoTuning.ballSpawnMinDist` | `ParamSchema.cpp:89 / RuntimeEnv.cpp:120` |
| `ball_spawn_max_dist` | Double | 1~2000 | 30.0 | 30.0 | `cyanoTuning.ballSpawnMaxDist` | `ParamSchema.cpp:90 / RuntimeEnv.cpp:121` |
| `ball_loss_cost` | Double | 0~1000 | 0.2 | 5.0 | `ball.lossCost` | `ParamSchema.cpp:91 / RuntimeEnv.cpp:122` |
| `detach_radius` | Double | 10~2000 | 60.0 | 60.0 | `ball.detachRadius` | `ParamSchema.cpp:92 / RuntimeEnv.cpp:123` |
| `regrow_food` | Bool | 0~1 | —（Food 实体已删） | 0.0 | —（拒绝） | `ParamSchema.cpp:93` |

#### pressure（生存压力，9 键 + 2 个 schema 外键）

| 键 | 类型 | 范围 | Config 默认 | schema | 落点字段 | 位置 |
|---|---|---|---|---|---|---|
| `basal_cost` | Double | 0~10 | 0.03 | 0.01 | `nucleus.respirationBase` | `ParamSchema.cpp:96 / RuntimeEnv.cpp:126` |
| `respiration_base` | Double | 0~10 | 0.03 | 0.008 | `nucleus.respirationBase`（与上行**同字段**） | `ParamSchema.cpp:97 / RuntimeEnv.cpp:127` |
| `speed_cost_k` | Double | 0~1 | 0.0002 | 0.005 | `nucleus.locomotionCost` | `ParamSchema.cpp:98 / RuntimeEnv.cpp:128` |
| `speed_cost_exponent` | Double | 1~4 | 2.0 | 1.5 | `nucleus.speedCostExponent` | `ParamSchema.cpp:99 / RuntimeEnv.cpp:131` |
| `reproduction_cost_ratio` | Double | 0~2 | 1.0 | 0.4 | `nucleus.reproductionCostRatio` | `ParamSchema.cpp:100 / RuntimeEnv.cpp:132` |
| `child_energy_ratio` | Double | 0~1 | 0.5 | 0.5 | `nucleus.childEnergyRatio`（无消费者） | `ParamSchema.cpp:101 / RuntimeEnv.cpp:133` |
| `reproduction_offset` | Double | 0~1000 | 25.0 | 25.0 | `nucleus.reproductionOffset`（无消费者） | `ParamSchema.cpp:102 / RuntimeEnv.cpp:134` |
| `mutation_coefficient` | Double | 0~10 | 0.25 | 1.0 | `colony.mutationCoefficient` | `ParamSchema.cpp:103 / RuntimeEnv.cpp:135` |
| `water_cost_multiplier` | Double | 1~10 | 1.8 | 1.8 | `terrain.waterCostMultiplier`（下一局生效） | `ParamSchema.cpp:104 / RuntimeEnv.cpp:136` |
| `migration_bonus` | Double | 无 schema 条目 | 0.0 | — | `nucleus.migrationBonus` | `RuntimeEnv.cpp:129` |
| `migration_radius` | Double | 无 schema 条目 | 120.0 | — | `nucleus.migrationRadius` | `RuntimeEnv.cpp:130` |

`migration_bonus` / `migration_radius` 是 R9-A 新增键，只在 `RuntimeEnv` 表中、不在 `ParamSchema` 表中，因此 `GET /schema` 看不到它们，但 `POST /env` 可用。

#### producer（生产者光合，4 键）

| 键 | 类型 | 范围 | Config 默认 | schema | 落点字段 | 位置 |
|---|---|---|---|---|---|---|
| `cyano_photosynthesis_rate` | Double | 0~10 | 1.0 | 1.0 | `cyanoTuning.photosynthesisRate` | `ParamSchema.cpp:107 / RuntimeEnv.cpp:139` |
| `cyano_metabolism_rate` | Double | 0~10 | 0.02 | 0.02 | `cyanoTuning.metabolismRate` | `ParamSchema.cpp:108 / RuntimeEnv.cpp:140` |
| `cyano_reproduction_probability` | Double | 0~1 | 0.01 | 0.01 | `cyanoTuning.reproductionProbability` | `ParamSchema.cpp:109 / RuntimeEnv.cpp:141` |
| `cyano_reproduction_offset` | Double | 0~1000 | 125.0 | 125.0 | `cyanoTuning.reproductionOffset` | `ParamSchema.cpp:110 / RuntimeEnv.cpp:142` |

#### cyano（蓝细菌网络，20 键）

| 键 | 类型 | 范围 | Config 默认 | schema | 落点字段 | 位置 |
|---|---|---|---|---|---|---|
| `cyano_colonies` | Int | 0~1000 | 64 | 4.0 | `runtime.cyanoColonies`（无消费者；播种实际读 `cyanoTuning.cyanoColonies`） | `ParamSchema.cpp:113 / RuntimeEnv.cpp:145` |
| `cyano_initial_energy` | Double | 0~1000 | 40.0 | 40.0 | `runtime.cyanoInitialEnergy`（无消费者；播种恒 0 能） | `ParamSchema.cpp:114 / RuntimeEnv.cpp:146` |
| `cyano_energy_cap` | Double | 0~10000 | 100.0 | 100.0 | `cyanoTuning.energyCap` | `ParamSchema.cpp:115 / RuntimeEnv.cpp:147` |
| `cyano_link_distance` | Double | 1~2000 | 60.0 | 60.0 | `cyanoTuning.linkRadius` | `ParamSchema.cpp:116 / RuntimeEnv.cpp:148` |
| `cyano_max_links` | Int | 0~64 | 3 | 3.0 | `cyanoTuning.linkMaxNeighbors` | `ParamSchema.cpp:117 / RuntimeEnv.cpp:149` |
| `cyano_link_check_interval` | Int | 1~1000 | 1 | 1.0 | `cyanoTuning.linkCheckInterval` | `ParamSchema.cpp:118 / RuntimeEnv.cpp:150` |
| `cyano_transfer_rate` | Double | 0~1 | 0.1 | 0.1 | `cyanoTuning.linkTransferRate` | `ParamSchema.cpp:119 / RuntimeEnv.cpp:151` |
| `cyano_transfer_deadband` | Double | 0~100 | 1.0 | 1.0 | `cyanoTuning.linkDeadband` | `ParamSchema.cpp:120 / RuntimeEnv.cpp:152` |
| `cyano_min_node_separation` | Double | 0~1000 | 10.0 | 10.0 | `cyanoTuning.minSeparation` | `ParamSchema.cpp:121 / RuntimeEnv.cpp:153` |
| `cyano_predation_radius` | Double | 0~1000 | 14.0 | 12.0 | `nucleus.cyanoPredationRadius` | `ParamSchema.cpp:122 / RuntimeEnv.cpp:154` |
| `cyano_conversion_mode` | Int | 0~3 | 1 | 1.0 | `cyanoTuning.conversionMode` | `ParamSchema.cpp:123 / RuntimeEnv.cpp:155` |
| `cyano_conversion_rate` | Double | 0~100 | 0.6 | 1.0 | `cyanoTuning.conversionRate` | `ParamSchema.cpp:124 / RuntimeEnv.cpp:156` |
| `cyano_conversion_exponent` | Double | 0~5 | 1.0 | 1.0 | `cyanoTuning.conversionExponent`（占位，函数内 `(void)exponent` 未使用） | `ParamSchema.cpp:125 / RuntimeEnv.cpp:157` |
| `cyano_conversion_speed_ref` | Double | 1~10000 | 40.0 | 20.0 | `cyanoTuning.conversionSpeedRef` | `ParamSchema.cpp:126 / RuntimeEnv.cpp:158` |
| `cyano_conversion_accel_ref` | Double | 0.01~10000 | 60.0 | 6.0 | `cyanoTuning.conversionAccelRef` | `ParamSchema.cpp:127 / RuntimeEnv.cpp:159` |
| `cyano_conversion_w_speed` | Double | 0~10 | 1.0 | 1.0 | `cyanoTuning.conversionWSpeed` | `ParamSchema.cpp:128 / RuntimeEnv.cpp:160` |
| `cyano_conversion_w_accel` | Double | 0~10 | 1.0 | 1.0 | `cyanoTuning.conversionWAccel` | `ParamSchema.cpp:129 / RuntimeEnv.cpp:161` |
| `cyano_conversion_min_rate` | Double | 0~100 | 0.05 | 0.1 | `cyanoTuning.conversionMinRate` | `ParamSchema.cpp:130 / RuntimeEnv.cpp:162` |
| `cyano_conversion_max_rate` | Double | 0~1000 | 1.0 | 1.0 | `cyanoTuning.conversionMaxRate` | `ParamSchema.cpp:131 / RuntimeEnv.cpp:163` |
| `cyano_hue_jitter` | Double | 0~180 | 20.0 | 8.0 | `cyanoTuning.hueJitterSigma` | `ParamSchema.cpp:132 / RuntimeEnv.cpp:164` |

#### genome（24 键；min/max = Clamp 区间，schema 默认 = Init 区间中点）

写入语义与其他组不同：每个键同时改 `GenomeRanges` 的 **Init 收敛 + Clamp 扩宽**（`src/app/RuntimeEnv.cpp:63-71`）：

$$ \text{InitMin} \leftarrow v,\quad \text{InitMax} \leftarrow v,\quad \text{ClampMin} \leftarrow \min(\text{ClampMin}, v),\quad \text{ClampMax} \leftarrow \max(\text{ClampMax}, v) $$

源码给出的理由：Clamp 若一并收敛成点，则 `mutationRate * (clampMax - clampMin)` 的变异步长恒 0，该基因进化死亡（`src/app/RuntimeEnv.cpp:34-38`）。

同族三类型的键写同一区间（`affinity_resource/_scout/_shield` 共享 `affinityInitMin/Max` 等），因为 `GenomeRanges` 对同族三类型只有一个区间。

| 键 | Clamp 范围 | Init 区间（Config.h 默认） | schema 默认 | GenomeRanges 字段 | 位置 |
|---|---|---|---|---|---|
| `affinity_resource` / `_scout` / `_shield` | 1e-4~10 | [0.1, 1.0] | 0.55 | `affinityInitMin/Max/ClampMin/ClampMax` | `ParamSchema.cpp:137-139 / RuntimeEnv.cpp:169-177` |
| `orbit_radius_resource` / `_scout` / `_shield` | 10~300 | [30, 200] | 115.0 | `orbitRadius*` | `ParamSchema.cpp:140-142 / RuntimeEnv.cpp:178-186` |
| `absorb_preference_resource` / `_scout` / `_shield` | 0.1~3 | [0.5, 1.5] | 1.0 | `absorbPreference*` | `ParamSchema.cpp:143-145 / RuntimeEnv.cpp:187-195` |
| `repel_strength_resource` / `_scout` / `_shield` | 0.1~6 | [0.5, 3.0] | 1.75 | `repelStrength*` | `ParamSchema.cpp:146-148 / RuntimeEnv.cpp:196-204` |
| `nucleus_sight_radius` | 10~600 | [40, 300] | 170.0 | `nucleusSightRadius*` | `ParamSchema.cpp:149 / RuntimeEnv.cpp:205` |
| `nucleus_personal_space` | 5~200 | [15, 80] | 47.5 | `nucleusPersonalSpace*` | `ParamSchema.cpp:150 / RuntimeEnv.cpp:208` |
| `nucleus_repel_strength` | 0.1~20 | [0.5, 6.0] | 3.25 | `nucleusRepelStrength*` | `ParamSchema.cpp:151 / RuntimeEnv.cpp:211` |
| `nucleus_cohesion` | 0~10 | [0, 3.0] | 1.5 | `nucleusCohesion*` | `ParamSchema.cpp:152 / RuntimeEnv.cpp:214` |
| `force_range` | 10~400 | [30, 150] | 90.0 | `forceRange*` | `ParamSchema.cpp:153 / RuntimeEnv.cpp:217` |
| `force_cost_k` | 1e-4~1 | [0.005, 0.05] | 0.0275 | `forceCostK*` | `ParamSchema.cpp:154 / RuntimeEnv.cpp:220` |
| `max_speed` | 5~300 | [30, 100] | 65.0 | `maxSpeed*` | `ParamSchema.cpp:155 / RuntimeEnv.cpp:223` |
| `max_accel` | 0.5~50 | [1, 20] | 10.5 | `maxAccel*` | `ParamSchema.cpp:156 / RuntimeEnv.cpp:226` |
| `energy_threshold` | 20~1000 | [80, 200] | 140.0 | `energyThreshold*` | `ParamSchema.cpp:157 / RuntimeEnv.cpp:229` |
| `mutation_rate` | 0.001~0.5 | [0.01, 0.2] | 0.105 | `mutationRate*` | `ParamSchema.cpp:158 / RuntimeEnv.cpp:232` |
| `cyano_preference` | 0~1 | [0, 1.0] | 0.5 | `cyanoPreference*` | `ParamSchema.cpp:159 / RuntimeEnv.cpp:235` |
| `cyano_digestion` | 0.05~1 | [0.1, 1.0] | 0.55 | `cyanoDigestion*` | `ParamSchema.cpp:160 / RuntimeEnv.cpp:238` |

#### ball（球动力学，15 键）

| 键 | 类型 | 范围 | Config 默认 | schema | 落点字段 | 位置 |
|---|---|---|---|---|---|---|
| `ball_damping` | Double | 0.5~1 | 0.92 | 0.92 | `ball.damping` | `ParamSchema.cpp:163 / RuntimeEnv.cpp:243` |
| `radial_k` | Double | 0~1 | 0.05 | 0.05 | `ball.radialK` | `ParamSchema.cpp:164 / RuntimeEnv.cpp:244` |
| `tangential_k` | Double | 0~10 | 1.0 | 1.0 | `ball.tangentialK` | `ParamSchema.cpp:165 / RuntimeEnv.cpp:245` |
| `near_repel_radius` | Double | 1~200 | 8.0 | 8.0 | `ball.nearRepelRadius` | `ParamSchema.cpp:166 / RuntimeEnv.cpp:246` |
| `ball_ball_repel_radius` | Double | 1~100 | 6.0 | 6.0 | `ball.ballRepelRadius` | `ParamSchema.cpp:167 / RuntimeEnv.cpp:247` |
| `ball_ball_repel_k` | Double | 0~10 | 0.5 | 0.5 | `ball.ballRepelK` | `ParamSchema.cpp:168 / RuntimeEnv.cpp:248` |
| `same_owner_attract_radius` | Double | 0~200 | 0.0 | 0.0 | `ball.sameOwnerAttractRadius` | `ParamSchema.cpp:169 / RuntimeEnv.cpp:249` |
| `same_owner_attract_k` | Double | 0~1 | 0.05 | 0.05 | `ball.sameOwnerAttractK` | `ParamSchema.cpp:170 / RuntimeEnv.cpp:250` |
| `ball_max_speed_shield` | Double | 1~500 | 15.0 | 15.0 | `ball.maxSpeedShield` | `ParamSchema.cpp:171 / RuntimeEnv.cpp:251` |
| `ball_max_speed_resource` | Double | 1~500 | 20.0 | 20.0 | `ball.maxSpeedResource` | `ParamSchema.cpp:172 / RuntimeEnv.cpp:252` |
| `ball_max_speed_scout` | Double | 1~500 | 30.0 | 30.0 | `ball.maxSpeedScout` | `ParamSchema.cpp:173 / RuntimeEnv.cpp:253` |
| `follow_coupling` | Double | 0~100 | 4.0 | 4.0 | `ball.followCoupling` | `ParamSchema.cpp:174 / RuntimeEnv.cpp:254` |
| `follow_boost` | Double | 0~10 | 1.5 | 1.5 | `ball.followBoost` | `ParamSchema.cpp:175 / RuntimeEnv.cpp:255` |
| `bounce_restitution` | Double | 0~1 | 1.0 | 1.0 | `ball.bounceRestitution`（无消费者，恒 wrap） | `ParamSchema.cpp:176 / RuntimeEnv.cpp:256` |
| `wrap_boundary` | Bool | 0~1 | true | 1.0 | `ball.wrapBoundary`（无消费者，恒 wrap） | `ParamSchema.cpp:177 / RuntimeEnv.cpp:257` |

#### nucleus（核动力学，8 键）

| 键 | 类型 | 范围 | Config 默认 | schema | 落点字段 | 位置 |
|---|---|---|---|---|---|---|
| `nucleus_damping` | Double | 0.5~1 | 1.0 | 0.9 | `nucleus.damping` | `ParamSchema.cpp:180 / RuntimeEnv.cpp:260` |
| `nucleus_min_separation` | Double | 0~200 | 12.0 | 12.0 | `nucleus.minSeparation`（无读取点） | `ParamSchema.cpp:181 / RuntimeEnv.cpp:261` |
| `nucleus_repel_k` | Double | 0~100 | 2.0 | 2.0 | `nucleus.repelK`（无读取点） | `ParamSchema.cpp:182 / RuntimeEnv.cpp:262` |
| `forage_k` | Double | 0~10 | 1.0 | 1.0 | `nucleus.forageK`（无读取点） | `ParamSchema.cpp:183 / RuntimeEnv.cpp:263` |
| `forage_radius` | Double | 10~2000 | 100.0 | 100.0 | `nucleus.forageRadius`（无读取点） | `ParamSchema.cpp:184 / RuntimeEnv.cpp:264` |
| `wander_k` | Double | 0~10 | 0.5 | 0.5 | `nucleus.wanderK`（无读取点） | `ParamSchema.cpp:185 / RuntimeEnv.cpp:265` |
| `wander_turn_rate` | Double | 0~3 | 0.5 | 0.5 | `nucleus.wanderTurnRate`（无读取点） | `ParamSchema.cpp:186 / RuntimeEnv.cpp:266` |
| `nucleus_init_energy_ratio` | Double | 0~2 | 1.0 | 1.0 | `nucleus.nucleusInitEnergyRatio`（无读取点，出生恒 60） | `ParamSchema.cpp:187 / RuntimeEnv.cpp:267` |

#### 非 schema 键（仅 RuntimeEnv 存在）

| 键 | 类型 | 默认 | 落点字段 | 位置 |
|---|---|---|---|---|
| `cyano_enabled` | Bool（`v>=0.5`） | true | `runtime.cyanoEnabled`（无读取点） | `src/app/RuntimeEnv.cpp:270` |
| `cyano_ball_spawn_probability` | Double | 0.02 | `cyanoTuning.ballSpawnProbability`（EnvControl food 轴别名） | `src/app/RuntimeEnv.cpp:274` |
| `cyano_ball_spawn_cost` | Double | 30.0 | `cyanoTuning.ballSpawnCost`（同上） | `src/app/RuntimeEnv.cpp:275` |

### 2.7 无条件消费的参数（诚实清单）

以下键**在 `src/` 中除 `RuntimeEnv` 的写入外没有任何读取点**，改动它们对外部可观测行为无影响（`Config.h` 的注释多以"R3-A/R4 消费"标注为预留）：

`width`, `height`, `seed`, `ticks`, `regrow_food`（设计性拒绝）；
`balls`, `autosave_interval`, `light_warmth`, `light_sample_grid`, `cyano_initial_energy`, `cyano_colonies`（runtime 侧）；
`ball_energy`, `ball_radius`, `ball_vel_range`, `ball_initial_energy`, `ball_energy_cap`, `bounce_restitution`, `wrap_boundary`；
`child_energy_ratio`, `reproduction_offset`, `nucleus_init_energy_ratio`, `nucleus_min_separation`, `nucleus_repel_k`, `forage_k`, `forage_radius`, `wander_k`, `wander_turn_rate`；
`cyano_conversion_exponent`（函数内显式丢弃，`src/environment/CyanoNode.cpp:80`）；
`cyano_enabled`。

反之，以下键有真实消费点，调参**立即改变**生态轨迹：`sun_*`（`src/sim/Simulation.cpp:702-706`）、`nucleus.respirationBase/locomotionCost/speedCostExponent/reproductionCostRatio/damping/cyanoPredationRadius/migrationBonus/migrationRadius`（`src/sim/Simulation.cpp:1109-1116`、`:505-526`）、`cyanoTuning` 中所有有键的字段（`src/environment/CyanoNode.cpp:160-350`）、`ball` 力场 15 键中除 `bounce_restitution`/`wrap_boundary` 外的 13 键（`src/sim/Simulation.cpp:1119-1133`）、`colony.mutationCoefficient` 与 `colony.genomeRanges`（`src/behavior/AgentBehavior.cpp:273`、`src/sim/Simulation.cpp:314`）、`policy.senseRadius`（`src/perception/AgentSensors.cpp:95`）、`runtime.maxFps` 与 `runtime.renderInterval`（`src/app/main.cpp:1046,1157`）。

### 2.8 无法通过任何键到达的字段

有些 `Config`/`CyanoParams` 字段**既没有 CLI 入口、也没有 `RuntimeEnv` 键**，只能取 `Config.h` 的默认值（或由构造期镜像从默认值代入）。这决定了若干生态机制的内部常数是硬编码的：

| 字段 | 默认值 | 消费点 | 说明 |
|---|---|---|---|
| `NucleusTuning.grazeBiteFraction` | 0.15 | `src/sim/Simulation.cpp:525` | 捕食口粮系数，决定承载力 $N^* \propto 1/f$；运行时/CLI 均不可改 |
| `CyanoTuning.reproductionCost` | 40.0 | `src/environment/CyanoNode.cpp:292` | 分裂成本，无对应键 |
| `CyanoTuning.ballRadius` | 1.0 | `src/environment/CyanoNode.cpp:249` | 产球能量倍数（`ballEnergyTotal = ballSpawnCost × ballRadius`），无对应键 |
| `CyanoTuning.cyanoColonies` | 64 | `src/environment/CyanoNode.cpp:119` | 播种聚落数；schema 的 `cyano_colonies` 落到 `runtime.cyanoColonies`，**不是**这里 |
| `CyanoTuning.clusterSpread` | 45.0 | `src/environment/CyanoNode.cpp:135` | 聚落撒点半径，无对应键 |
| `CyanoTuning.shadeK` | 0.20 | `src/environment/CyanoNode.cpp:180,397` | 自遮光强度（承载力负反馈主旋钮），无对应键 |
| `CyanoTuning.shadeRadius` | 60.0 | `src/environment/CyanoNode.cpp:413,448,498` | 遮蔽半径，无对应键 |
| `CyanoTuning.shadeSigma` | 30.0 | `src/environment/CyanoNode.cpp:399` | 高斯衰减宽度，无对应键 |
| `CyanoTuning.grazeRefugeEnergy` | 15.0 | `src/environment/CyanoNode.cpp:558` | 捕食避难所（节点保留不可食部分），无对应键 |
| `CyanoTuning.maxNodes` / `nodeSeedCount` | 4096 / 128（CyanoParams 默认） | `src/sim/Simulation.cpp:154-155` | 构造期被 `cyanoMaxNodes` / `cyanoNodeCount` 覆盖 |
| `RuntimeTuning.cyanoInitialEnergy` | 40.0 | 无 | 播种恒 0 能（`CyanoNode.cpp:139,153`），字段本身无读取点 |
| `BallTuning.bounceRestitution` / `wrapBoundary` | 1.0 / true | 无 | 世界恒为环面 wrap，两字段仅存档语义 |

同理，`TerrainConfig.obstacleRadius/waterRadius/thermalRadius/thermalStrength` 与 `TemperatureConfig.baseline/preferred/distortionK/sourceCount` 无任何键，只有 `terrain.waterCostMultiplier` 落到 `TerrainConfig`（且要下一局才生效）。

---

## 三、`GET /state` 二进制快照格式

### 3.1 总布局

构造代码：`buildStateSnapshot`（`src/app/main.cpp:490-659`）。全部小端，无对齐填充，`f32` 为 IEEE-754 单精度，`i32/u32` 4 字节，`u8` 1 字节。

> 注意：函数头注释（`src/app/main.cpp:481-489`）仍写着 v1 的旧步长（球 24 B、核 28 B）。**以下各表以实际写入代码为准**（球 25 B、核 128 B），前端解析同样按实际步长（`frontend/js/live-client.js:153`、`:174-175`、`:192`）。

```
偏移   0            80                   80+25·nB              80+25·nB+128·nN
       +------------+---------------------+---------------------+------------------+-------------+
       | 头部 80 B  | 球记录 25 B × nB    | 核记录 128 B × nN   | 蓝细菌 20 B × nC | 连线 8 B×nL |
       +------------+---------------------+---------------------+------------------+-------------+
```

总长度：

$$ \text{总字节} = 80 + 25 \, n_B + 128 \, n_N + 20 \, n_C + 8 \, n_L $$

前端在解析时按此式做 stride 自检（`frontend/js/live-client.js:153-157`）。

记录书写顺序（**不是**按头部字段顺序）：① 逐核遍历存活核，先写该核名下 `n.balls`，再写该核 `n.detached_` 交接球（`src/app/main.cpp:514-540`）；② 全部核之后写 `freeBalls_`（`:541-552`）；③ 写核记录（`:567-598`）；④ 写蓝细菌节点（`:603-611`）；⑤ 写连线（`:615-619`）。因为记录区先写、头部后前插（`src/app/main.cpp:621-657`），头部计数来自实际写入条数，与字节流恒自洽。

### 3.2 头部（80 字节）

| 偏移 | 类型 | 字节 | 字段 | 含义 | 源码 |
|---|---|---|---|---|---|
| 0 | u8 | 1 | `magic` | 恒 `0x41`（ASCII `'A'`） | `src/app/main.cpp:627` |
| 1 | u8 | 1 | `version` | 恒 `2`（v1 核记录 32 B 无基因，v2 = 32 B 头 + 24 基因） | `src/app/main.cpp:628` |
| 2 | u8 | 1 | `phase` | `0`=运行，`1`=已结束，`2`=暂停 | `src/app/main.cpp:630-632` |
| 3 | u8 | 1 | `reserved` | 恒 0 | `src/app/main.cpp:633` |
| 4 | i32 | 4 | `tick` | 当前帧号 | `src/app/main.cpp:634` |
| 8 | f32 | 4 | `worldWidth` | 世界宽 | `src/app/main.cpp:635` |
| 12 | f32 | 4 | `worldHeight` | 世界高 | `src/app/main.cpp:636` |
| 16 | i32 | 4 | `ballCount` | 球记录总数（含归属球、detached 球、自由球） | `src/app/main.cpp:637` |
| 20 | i32 | 4 | `nucleusCount` | 核记录数（只含存活核） | `src/app/main.cpp:638` |
| 24 | i32 | 4 | `aliveNucleusCount` | 存活核数（与 20 同值，快照只含存活核） | `src/app/main.cpp:639` |
| 28 | i32 | 4 | `freeBallCount` | 自由球数（`detached_` + `freeBalls_`） | `src/app/main.cpp:640` |
| 32 | i32 | 4 | `cyanoCount` | 蓝细菌节点数 | `src/app/main.cpp:641` |
| 36 | i32 | 4 | `linkCount` | 蓝细菌连线数 | `src/app/main.cpp:642` |
| 40 | f32 | 4 | `cyanoAvgEnergy` | 节点平均能量（`cyanoCount == 0` 时为 0） | `src/app/main.cpp:643-645` |
| 44 | f32 | 4 | `sunLevel` | 全局阳光估算值 | `src/app/main.cpp:646` |
| 48 | i32 | 4 | `reserved` | 恒 0 | `src/app/main.cpp:647` |
| 52 | char[28] | 28 | `reason` | 结束原因 ASCII（≤27 字符 + NUL），如 `running`/`extinct`/`max_ticks`/`paused` | `src/app/main.cpp:649-655` |

`phase` 的判定式（`src/app/main.cpp:630-632`）：

$$ \text{phase} = \begin{cases} 2 & \text{paused} = \text{true} \\ 1 & \text{reason} \in \{\text{"extinct"},\ \text{"max_ticks"}\} \\ 0 & \text{否则} \end{cases} $$

`sunLevel` 由 serve 侧估算函数给出（`serveSunEstimate`，`src/app/main.cpp:664-674`），**只用于显示**，不参与任何账目：

$$ \text{sun} = \mathrm{clamp}_{[0,1]}\!\left(\text{sun.baseLevel} + \text{sun.dayAmplitude}\cdot\sin\!\left(\frac{6.283185307179586 \cdot \text{tick}}{\text{sun.dayPeriodFrames}}\right)\right) $$

调用点以世界最暗点 `y = H` 取值（`src/app/main.cpp:1112-1113`、`:1146`）。

### 3.3 球记录（25 字节）

| 记录内偏移 | 类型 | 字节 | 字段 | 含义 | 源码 |
|---|---|---|---|---|---|
| +0 | u32 | 4 | `id & 0xFFFFFFFF` | 球 id | `src/app/main.cpp:518` |
| +4 | u32 | 4 | `owner` | 宿主核 id；`0` = 自由球（含 `detached_` 交接球） | `src/app/main.cpp:519,531,544` |
| +8 | f32 | 4 | `x` | 世界 x（已 wrap 到 `[0,W)`） | `src/app/main.cpp:520` |
| +12 | f32 | 4 | `y` | 世界 y（已 wrap 到 `[0,H)`） | `src/app/main.cpp:521` |
| +16 | f32 | 4 | `energy` | 球储能 | `src/app/main.cpp:522` |
| +20 | u8 | 1 | `type` | `0`=Resource，`1`=Scout，`2`=Shield（写侧 `& 0x3`） | `src/app/main.cpp:523` |
| +21 | f32 | 4 | `radius` | 球半径 | `src/app/main.cpp:524` |

### 3.4 核记录（128 字节 = 32 B 头 + 24×f32 基因）

| 记录内偏移 | 类型 | 字节 | 字段 | 含义 | 源码 |
|---|---|---|---|---|---|
| +0 | u32 | 4 | `id` | 核 id | `src/app/main.cpp:570` |
| +4 | f32 | 4 | `x` | 世界 x（wrap 后） | `src/app/main.cpp:571` |
| +8 | f32 | 4 | `y` | 世界 y（wrap 后） | `src/app/main.cpp:572` |
| +12 | f32 | 4 | `energy` | 核能量 | `src/app/main.cpp:573` |
| +16 | f32 | 4 | `cap` | 菌落容量上限 | `src/app/main.cpp:574` |
| +20 | f32 | 4 | `speed` | 上一帧实际速度 | `src/app/main.cpp:575` |
| +24 | f32 | 4 | `hue` | 遗传色相，`[0,360)` | `src/app/main.cpp:576` |
| +28 | u8 | 1 | `alive` | 恒 `1`（快照只含存活核） | `src/app/main.cpp:577` |
| +29 | u8[3] | 3 | `pad` | 恒 0（对齐到 4 字节边界） | `src/app/main.cpp:578` |
| +32 | f32[24] | 96 | `genes[24]` | 基因组，顺序见下表 | `src/app/main.cpp:581-596` |

基因组 24 值的顺序（**契约**，前端按此解析：`frontend/js/live-client.js:213-228`）：

| 基因下标 | 记录内偏移 | 字段 | 源码 |
|---|---|---|---|
| 0,1,2 | +32, +36, +40 | `affinity[Resource, Scout, Shield]` | `src/app/main.cpp:581` |
| 3,4,5 | +44, +48, +52 | `orbitRadius[Resource, Scout, Shield]` | `src/app/main.cpp:582` |
| 6,7,8 | +56, +60, +64 | `absorbPreference[Resource, Scout, Shield]` | `src/app/main.cpp:583` |
| 9,10,11 | +68, +72, +76 | `repelStrength[Resource, Scout, Shield]` | `src/app/main.cpp:584` |
| 12 | +80 | `maxSpeed` | `src/app/main.cpp:585` |
| 13 | +84 | `maxAccel` | `src/app/main.cpp:586` |
| 14 | +88 | `energyThreshold` | `src/app/main.cpp:587` |
| 15 | +92 | `mutationRate` | `src/app/main.cpp:588` |
| 16 | +96 | `cyanoPreference` | `src/app/main.cpp:589` |
| 17 | +100 | `cyanoDigestion` | `src/app/main.cpp:590` |
| 18 | +104 | `nucleusSightRadius` | `src/app/main.cpp:591` |
| 19 | +108 | `nucleusPersonalSpace` | `src/app/main.cpp:592` |
| 20 | +112 | `nucleusRepelStrength` | `src/app/main.cpp:593` |
| 21 | +116 | `nucleusCohesion` | `src/app/main.cpp:594` |
| 22 | +120 | `forceRange` | `src/app/main.cpp:595` |
| 23 | +124 | `forceCostK` | `src/app/main.cpp:596` |

注意 `hue` 在 28 字节头内（+24），不在基因区重复。

### 3.5 蓝细菌节点（20 字节）与连线（8 字节）

| 记录 | 偏移 | 类型 | 字段 | 源码 |
|---|---|---|---|---|
| 节点 | +0 | u32 | `id` | `src/app/main.cpp:604` |
| 节点 | +4 | f32 | `x`（`CyanoField` 内部已 wrap） | `src/app/main.cpp:605` |
| 节点 | +8 | f32 | `y` | `src/app/main.cpp:606` |
| 节点 | +12 | f32 | `energy` | `src/app/main.cpp:607` |
| 节点 | +16 | f32 | `hue` | `src/app/main.cpp:608` |
| 连线 | +0 | u32 | `idA`（有序对的小端） | `src/app/main.cpp:616` |
| 连线 | +4 | u32 | `idB` | `src/app/main.cpp:617` |

### 3.6 边界与一致性约束

- 死核不写入快照（`src/app/main.cpp:515`、`:568`）；核能量已通过 `dissipate/carrion` 入账，不展示。
- 容器只含存活节点：饿死/被捕食节点帧末压缩，因此节点记录无 `alive` 字段。
- 计数与实体一致性由"先写记录、后写头部、计数来自实际写入"保证（`src/app/main.cpp:487-489`），无需二次遍历。
- `freeBallCount` 的构成：`detached_` 交接球（每颗算一颗，`src/app/main.cpp:538`）+ `freeBalls_`（`:551`）；归属球不计入。
- 前端对越界/截断的容错策略（不崩、按可读前缀解析）：`frontend/js/live-client.js:136-172`。

---

## 四、存档格式（SaveStore）

### 4.1 目录与文件

`saves/<name>/` 下三个文件（`src/app/SaveStore.h:2-7`）：

| 文件 | 内容 | 写入位置 |
|---|---|---|
| `save.bin` | 版本头 + cfg 块 + `Simulation::serializeState` 全量实体态 | `src/app/SaveStore.cpp:380-385` |
| `meta.txt` | `name/frame/seed/aliveNuclei/savedAt` 的 key=value 文本 | `src/app/SaveStore.cpp:388-397` |
| `timeline.bin` | `Timeline` 检查点序列（由 `Timeline::exportTo` 写） | `src/app/Timeline.cpp:156-182` |

目录根为相对路径 `"saves"`（`src/app/SaveStore.cpp:33`），即相对进程工作目录。

### 4.2 `save.bin` 布局

```
+0      u32 magic   = 0x41534153 ("ASAV")
+4      u32 version = 1
+8      u8  seedIsDefault   (seed == 42 时 1，否则 0)
+9      u8[3] pad = 0
+12     cfg 块（长度可变，见 §4.3）
+12+L   Simulation::serializeState 负载（见 §4.4）
```

| 偏移 | 类型 | 字节 | 字段 | 源码 |
|---|---|---|---|---|
| 0 | u32 | 4 | `magic = 0x41534153` | `src/app/SaveStore.cpp:28,364` |
| 4 | u32 | 4 | `version = 1` | `src/app/SaveStore.cpp:29,365` |
| 8 | u8 | 1 | `seedIsDefault`：`cfg.world.seed == 42 ? 1 : 0` | `src/app/SaveStore.cpp:367` |
| 9 | u8[3] | 3 | 保留，全 0 | `src/app/SaveStore.cpp:369` |

实体的 `serializeState` 自身也带一份 `ASAV` 魔数（版本 3），所以 `save.bin` 内出现两个 `ASAV`：外层（version=1，存档封装）与内层（version=3，实体态）。两者是**不同的版本号空间**，不得混用。

### 4.3 cfg 块（`savedetail::writeCfgBlock`）

`save.bin` 与 `timeline.bin` 共用同一节（`src/app/SaveStore.h:61-68`、`src/app/SaveStore.cpp:134-200`）。字段顺序即写入顺序：

| 块内偏移（`policyName = "rule"`） | 类型 | 字节 | 字段 | 源码 |
|---|---|---|---|---|
| +0 | f64 | 8 | `world.width` | `SaveStore.cpp:145` |
| +8 | f64 | 8 | `world.height` | `:146` |
| +16 | u64 | 8 | `world.seed` | `:147` |
| +24 | u64 | 8 | `world.maxTicks` | `:148` |
| +32 | u64 | 8 | `colony.nuclei` | `:149` |
| +40 | u64 | 8 | `colony.maxNuclei` | `:150` |
| +48 | f64 | 8 | `colony.mutationCoefficient` | `:151` |
| +56 | u64 | 8 | `cyanoNodeCount` | `:152` |
| +64 | u64 | 8 | `cyanoMaxNodes` | `:153` |
| +72 | f64 | 8 | `sun.baseLevel` | `:155` |
| +80 | f64 | 8 | `sun.dayAmplitude` | `:156` |
| +88 | f64 | 8 | `sun.dayPeriodFrames` | `:157` |
| +96 | f64 | 8 | `sun.verticalGradient` | `:158` |
| +104 | f64 | 8 | `sun.noiseAmplitude` | `:159` |
| +112 | f64 | 8 | `sun.noiseFrequency` | `:160` |
| +120 | f64 | 8 | `sun.noiseSpeed` | `:161` |
| +128 | f64 | 8 | `policy.senseRadius` | `:163` |
| +136 | f64 | 8 | `policy.damping` | `:164` |
| +144 | f64 | 8 | `policy.mutation` | `:165` |
| +152 | u8 | 1 | `pnLen` = `policyName.length()`（≤64） | `:166-167` |
| +153 | u8[pnLen] | pnLen | `policyName` 原始字节（无 NUL） | `:168` |
| +153+pnLen | u64 | 8 | `policy.perceptronSeed` | `:169` |
| +161+pnLen | f64 | 8 | `policy.perceptronScale` | `:170` |
| +169+pnLen | f64 | 8 | `policy.thermalPreferredTemp` | `:171` |
| +177+pnLen | f64 | 8 | `policy.thermalWThermal` | `:172` |
| +185+pnLen | f64 | 8 | `policy.thermalWCaution` | `:173` |
| +193+pnLen | f64[67] | 536 | `GenomeRanges` 全部 67 个标量（字段序 = `Genome.h` 声明序，见下） | `:175-194` |
| +729+pnLen | u8 | 1 | `pnLen`（重复，存档面权威） | `:196-197` |
| +730+pnLen | u8[pnLen] | pnLen | `policyName` 重复 | `:198` |

块总长：

$$ L = 730 + 2 \cdot \text{pnLen} \;\text{字节} \;\Rightarrow\; \text{pnLen}=4 \Rightarrow L = 738 $$

`GenomeRanges` 的 67 个 f64 顺序（`src/app/SaveStore.cpp:177-193`）：16 组 × 4（InitMin, InitMax, ClampMin, ClampMax）+ 3 个（`hueInitMin`, `hueInitMax`, `hueJitterSigma`）：

```
affinity, orbitRadius, absorbPreference, repelStrength, maxSpeed, maxAccel,
energyThreshold, mutationRate, cyanoPreference, cyanoDigestion,
nucleusSightRadius, nucleusPersonalSpace, nucleusRepelStrength, nucleusCohesion,
forceRange, forceCostK          (每组 4 个：InitMin, InitMax, ClampMin, ClampMax)
hueInitMin, hueInitMax, hueJitterSigma
```

**cfg 块只覆盖上述字段**，`RuntimeTuning` / `NucleusTuning` / `BallTuning` / `CyanoTuning` / `TerrainConfig` / `TemperatureConfig` 全部不落档。因此读档后这些参数一律回到 `Config.h` 默认值（这是格式的真实边界）。

策略名白名单（8 项，读档时校验，未知即报错）：`rule`、`stub`、`perceptron`、`thermal`、`hysteresis`、`foragebank`、`bandit`、`riskaverse`（`src/app/SaveStore.cpp:114-120`）。读档时按名字回填策略装配标志与 `policy.mutation`（`src/app/SaveStore.cpp:122-127`）：

$$ \text{policy.mutation} = \begin{cases} 0.05 & \text{name} = \text{"rule"} \\ 0 & \text{否则}\end{cases} $$

### 4.4 实体态段（`Simulation::serializeState`，版本 3）

实现：`serializeState`（`src/sim/Simulation.cpp:1423-1517`）与读写原语（`:1203-1421`）；读侧 `deserializeState`（`:1519-1708`）。全部小端，`f64` 按 IEEE-754 位原样（`memcpy` 到 u64 再写），无对齐填充。

```
+0    u32 magic   = 0x41534153 ("ASAV")
+4    u32 version = 3
+8    u64 tick
+16   u8  auditOk
+17   u8[7] pad = 0
+24   u64 rng.state
+32   u64 rng.inc
+40   u64 nextNucleusId
+48   u64 nextFreeBallId
+56   u64 deathCount
+64   u64 aliveCount
+72   f64 ledger.totalInjected
+80   f64 ledger.totalDissipated
+88   f64 ledger.totalTransferred
+96   u64 reasonCount
+104  reasonCount × { u8 len, u8[len] reason, f64 amount }
...   u64 nucleiCount
...   nucleiCount × 核记录
...   u64 freeBallsCount
...   freeBallsCount × 球记录
...   u64 carrionCount
...   carrionCount × { f64 x, f64 y, f64 energy }
...   u64 cyanoNodeCount
...   cyanoNodeCount × 节点记录
...   u64 cyano.nextNodeId
...   i64 cyano.frameIndex
```

**核记录**（`src/sim/Simulation.cpp:1457-1489`）：

| 字段 | 类型 | 字节 | 源码 |
|---|---|---|---|
| `id` | u64 | 8 | `:1458` |
| `pos.x, pos.y` | f64×2 | 16 | `:1459-1460` |
| `vel.x, vel.y` | f64×2 | 16 | `:1461-1462` |
| `energy` | f64 | 8 | `:1463` |
| `cap` | f64 | 8 | `:1464` |
| `speed` | f64 | 8 | `:1465` |
| `alive` | u8 | 1 | `:1466` |
| pad | u8[7] | 7 | `:1467` |
| `genome` | f64×25 | 200 | `:1468`（顺序见下） |
| `effSight, effSpace, effRepel, effCohesion` | f64×4 | 32 | `:1469-1472` |
| `digestCooldown` | i32 | 4 | `:1473` |
| `balls.size()` + 球记录 | u64 + 93·n | 8 + 93n | `:1474-1477` |
| `detached_.size()` + 球记录 | u64 + 93·m | 8 + 93m | `:1478-1481` |
| `carrion_.pos.x, pos.y, energy` | f64×3 | 24 | `:1482-1484` |
| `lastMealPos.x, lastMealPos.y` | f64×2 | 16 | `:1486-1487` |
| `hasMealPos` | u8 | 1 | `:1488` |

**球记录（93 字节）**（`src/sim/Simulation.cpp:1334-1349`）：`id`(u64,8) → `angle`(f64,8) → `radius`(f64,8) → `energy`(f64,8) → `lastOmega`(f64,8) → `type`(i32,4) → `cap`(f64,8) → `pos.x`(8) → `pos.y`(8) → `vel.x`(8) → `vel.y`(8) → `ownerId`(u64,8) → `r7Lane`(u8,1) = 93 字节。`type` 读回时校验 `0 ≤ type ≤ 2`（`:1365`）。

**基因组 25 个 f64 顺序**（`src/sim/Simulation.cpp:1372-1391`）：与 `/state` 的 24 个基因顺序相同，但在末尾多了 `hue`：

```
affinity[0..2], orbitRadius[0..2], absorbPreference[0..2], repelStrength[0..2],
maxSpeed, maxAccel, energyThreshold, mutationRate, cyanoPreference, cyanoDigestion,
nucleusSightRadius, nucleusPersonalSpace, nucleusRepelStrength, nucleusCohesion,
forceRange, forceCostK, hue
```

**蓝细菌节点记录**（`src/sim/Simulation.cpp:1505-1513`）：`id`(u64,8) → `pos.x`(f64,8) → `pos.y`(f64,8) → `energy`(f64,8) → `alive`(u8,1) → pad(u8[7]) → `hue`(f64,8) = 48 字节。

**全量守恒信息**：账本三总额（injected / dissipated / transferred）与"按原因耗散表"随档走，因此读档后审计恒等式立即成立（`src/app/SaveStore.h:8-10`、测试 `tests/unit/test_save.cpp:76-89`）。

读档后的派生量重算：`cyano_.refreshShadeCache()` 是纯几何重算、零 RNG，不刷新会导致第一帧光合分叉（`src/sim/Simulation.cpp:1691-1694`）。

### 4.5 版本与兼容性策略

| 层 | 版本常量 | 不匹配时的行为 | 源码 |
|---|---|---|---|
| `save.bin` 封装 | `kVersion = 1` | 报 `unsupported save version: N (expected 1)` | `src/app/SaveStore.cpp:29,412-416` |
| 实体态 | `kSerVersion = 3` | 报 `unsupported save version: N (expected 3)` | `src/sim/Simulation.cpp:1209,1531-1535` |
| `timeline.bin` | `kVersion = 1` | 报 `unsupported timeline version` | `src/app/Timeline.cpp:21,192-196` |

**兼容性 = 严格拒绝，不做迁移**。理由（源码注释）：格式校验从严，v1 档在 ball 车道字节处必然失败（`src/sim/Simulation.cpp:1207-1209`）。实体态版本演进记录：

- v1：无 `Ball.r7Lane`。
- v2：加 `Ball.r7Lane` 尾部字段。
- v3：加核迁移状态 `lastMealPos` / `hasMealPos`（`src/sim/Simulation.cpp:1209`、`:1485-1488`）。

### 4.6 读档校验与失败路径

| 校验 | 规则 | 源码 |
|---|---|---|
| `magic` | 必须 `0x41534153` | `src/app/SaveStore.cpp:411`、`src/sim/Simulation.cpp:1530` |
| 世界尺寸 | `width > 0 && height > 0` | `src/app/SaveStore.cpp:215-217` |
| 种群规模 | `nuclei ≤ 2^{20}`，`maxNuclei ≤ 2^{20}` | `src/app/SaveStore.cpp:218-220` |
| policy 名长度 | `≤ 64` | `src/app/SaveStore.cpp:244-245,289-290` |
| policy 名白名单 | 必须 8 个已知名之一 | `src/app/SaveStore.cpp:297-300` |
| 填充字节 | 必须存在且为 0 | `src/sim/Simulation.cpp:1313-1320` |
| 计数护栏 | 所有 `u64` 计数 `≤ 2^{31}` | `src/sim/Simulation.cpp:1210,1322-1332` |
| ledger reason 长度 | `1 ≤ len ≤ 64`（0 长度报错） | `src/sim/Simulation.cpp:1211,1566-1567` |
| `digestCooldown` | 必须 `≥ 0` | `src/sim/Simulation.cpp:1598` |
| 球类型 | `0 ≤ type ≤ 2` | `src/sim/Simulation.cpp:1365` |
| 存活核计数 | 头部 `aliveCount` 必须等于逐核统计 | `src/sim/Simulation.cpp:1674-1678` |
| 尾随字节 | 负载结束后必须 EOF | `src/sim/Simulation.cpp:1670-1672` |
| 原子提交 | 全量读入局部变量，末尾一次性写成员 | `src/sim/Simulation.cpp:1522-1524,1679-1698` |

`save` 的失败路径零副作用：cfg 块与实体态先序列化到内存，两者都成功才 `create_directories` + 落盘（`src/app/SaveStore.cpp:358-385`）。

存档名合法性（`src/app/SaveStore.cpp:308-350`）：非空、≤64 字符、无控制字符（`< 0x20` 或 `0x7F`）、不含 `/ \ : * ? " < > |`、不含 `..`、不以空格开头/结尾、不以点结尾、不是 Windows 保留设备名（`CON/PRN/AUX/NUL/COM1-9/LPT1-9`，大小写不敏感）。

### 4.7 `meta.txt`

固定 5 行 key=value（`src/app/SaveStore.cpp:391-395`），值中的 `\`、换行、回车前加反斜杠转义（`src/app/SaveStore.cpp:89-96`）：

```
name=<名字>
frame=<tick>
seed=<world.seed>
aliveNuclei=<存活核数>
savedAt=<YYYY-MM-DD HH:MM:SS 本地时间>
```

`list()` 以文件内 `name=` 优先于目录名（`src/app/SaveStore.cpp:484-488`），排序规则为 `savedAt` **倒序**、同刻按名字**升序**（`:491-494`）。`rename` 会同步改写 `meta.txt` 的 `name=` 行（`:527-543`）。

---

## 五、时间轴（Timeline）

### 5.1 数据结构与检查点捕获

```cpp
// src/app/Timeline.h:28-32
struct Checkpoint {
    std::uint64_t frame = 0;              // 捕获时的世界 tick
    std::uint64_t hash = 0;               // 捕获时 sim.stateHash()
    std::vector<unsigned char> payload;   // serializeState 原始字节（含 rng）
};
```

容量硬顶 `kMaxCheckpoints = 2000`（`src/app/Timeline.h:35`），默认间隔 `interval_ = 200` 帧（`src/app/Timeline.h:92`）。

捕获条件（`src/app/Timeline.cpp:96-116`）：

$$ \text{捕获} \iff \text{interval} \ne 0 \ \land\ \text{tick} \bmod \text{interval} = 0 $$

捕获内容是 `sim.serializeState()` 的**全量**字节（含 `rng_` 的 `state/inc`），这是 seek 后能逐位续跑的前提（`src/app/Timeline.h:7-9`）。快照失败时跳过本检查点、不打断主循环（`src/app/Timeline.cpp:110-112`）。

主循环的捕获触发点（`src/app/main.cpp:1151-1153`）：

```cpp
if (simRaw->tick() % timelinePtr->checkpointInterval() == 0) {
    timelinePtr->maybeCheckpoint(*simRaw, simRaw->tick(), simRaw->stateHash());
}
```

注意：`interval_` 由构造函数默认值 200 固定，`setCheckpointInterval` 在 `src/` 内无调用点；`runtime.autosaveInterval` 与 `ParamSchema` 的 `autosave_interval` **不驱动** `Timeline` 的间隔。

### 5.2 抽稀算法（超出 2000 条时）

两档策略（`src/app/Timeline.cpp:59-94`）：

**第一档**（若 `size/2 + 1 ≤ 2000`）：保留下标 0, 2, 4, …（隔一去一）：

$$ \text{kept} = \{c_{2i}\}_{i=0}^{\lfloor n/2 \rfloor} $$

**第二档**（一档后仍超限）：在"首尾之外"的 $n-2$ 个候选里均匀丢弃 $\text{excess} = n - 2000$ 个：

$$ \text{dropIdx}(k) = \left\lfloor \frac{k \cdot (n-2)}{\text{excess}} \right\rfloor,\quad k = 0,1,\dots,\text{excess}-1 $$

首元素与尾元素**永不进入丢弃集**（"保首尾"）。

### 5.3 `seek`：最近检查点恢复 + 重放补帧

`seek(frame)` 是**重放式**，不是精确快照：

1. 线性扫描取满足 `cp.frame ≤ frame` 的最后一个检查点（容器有序，遇大即停）（`src/app/Timeline.cpp:122-125`）。
2. 用该 payload 调 `sim.deserializeState` 恢复世界（含 rng）（`:131-136`）。
3. 校验 `sim.stateHash() == cp.hash`；不等即判定 payload 损坏或格式漂移，返回 false（`:138-143`）。
4. **补帧由调用方完成**：主循环在 seek 成功后连续 `step()` 直到 `tick ≥ target`（`src/app/main.cpp:1082-1085`）。

$$ \text{恢复帧} = \max\{\, \text{cp.frame} \;\big|\; \text{cp.frame} \le \text{target} \,\} ,\qquad \text{补帧数} = \text{target} - \text{恢复帧} $$

因此 seek 的最坏代价 = 一个检查点间隔的重放。前置条件：必须处于暂停态（HTTP 层返回 409，`src/app/main.cpp:959-962`）。

### 5.4 `branch`：世界线分支

`branch(newCfg)` 只换运行参数，**不触碰 sim**（`src/app/Timeline.cpp:146-154`）：

$$ \text{cfg}_{\text{new}} = \text{newCfg} \quad\text{但}\quad \text{cfg}_{\text{new}}.\text{world} = \text{cfg}_{\text{old}}.\text{world} $$

- `world.width/height/seed/maxTicks` 强制保留旧值（世界几何与种子是确定性根）。
- `checkpoints_` 清空（新世界线的过去不可用旧时间轴回看）。
- `branched_ = true`。
- 调用方若需要"从 0 计帧"应显式调 `sim.restartClock()`（`src/sim/Simulation.h:44`，仅把 `tick_` 置 0）。
- 地形/温度 blob 是构造期实体，本通道不重建（`src/app/Timeline.h:62-64`）。

`branch` 在 `src/` 内无调用点（`Timeline::branch` 仅有定义）。

### 5.5 `timeline.bin` 布局

`src/app/Timeline.cpp:156-235`，与 `Timeline.h:82-84` 的契约注释一致：

| 偏移 | 类型 | 字节 | 字段 | 源码 |
|---|---|---|---|---|
| 0 | u32 | 4 | `magic = 0x41544D4C`（"ATML"） | `Timeline.cpp:20,160` |
| 4 | u32 | 4 | `version = 1` | `Timeline.cpp:21,161` |
| 8 | u64 | 8 | `interval_` | `Timeline.cpp:162` |
| 16 | u8 | 1 | `branched_`（0/1） | `Timeline.cpp:163-164` |
| 17 | u8[7] | 7 | pad = 0 | `Timeline.cpp:165` |
| 24 | — | $L$ | cfg 块（同 §4.3，`policyName = cfg_.policy.name`） | `Timeline.cpp:166-170` |
| 24 + $L$ | u64 | 8 | `checkpointCount` | `Timeline.cpp:171` |
| … | — | — | count × `{u64 frame, u64 hash, u64 len, u8[len] payload}` | `Timeline.cpp:172-178` |

读侧校验：magic、version、`count ≤ 2^{20}`、每条 `len ≤ 2^{30}`（`Timeline.cpp:211-222`）。cfg 块的 `policyName` 非空时回写 `cfg_.policy.name`（`:208-209`）。

---

## 六、HTTP 路由

### 6.1 服务器行为

| 项 | 值 | 源码 |
|---|---|---|
| 绑定地址 | `127.0.0.1`（`INADDR_LOOPBACK`），不对外 | `src/app/HttpServer.cpp:175` |
| 端口冲突 | 从 `port` 起 `+1` 重试，最多 20 次 | `src/app/HttpServer.cpp:168-193` |
| 协议 | HTTP/1.1 + `Connection: close`（无 keep-alive） | `src/app/HttpServer.cpp:366-372` |
| 并发模型 | 单线程 `accept` + 每连接同步处理 | `src/app/HttpServer.cpp:212-236` |
| 请求头上限 | 64 KiB | `src/app/HttpServer.cpp:46` |
| 请求体上限 | 1 MiB（`Content-Length` 声明超限时**不等 body** 直接 413） | `src/app/HttpServer.h:40`、`src/app/HttpServer.cpp:312-321` |
| 目录穿越 | 解码后路径任意位置含 `..` 一律拒绝（400） | `src/app/HttpServer.cpp:85-95`、`:386-389` |
| 静态根 | 根路径 `/` 与以 `/` 结尾的路径映射到 `agentsim.html`（无 `index.html` 回落；文件缺失即 404） | `src/app/HttpServer.cpp:85-95`、`:391-395` |
| webRoot 定位 | 依次尝试「exe 上级 `frontend`」→「exe 同级 `frontend`」→ 相对 CWD 的 `frontend` | `src/app/main.cpp:771-785` |
| 响应头 | `Content-Type` / `Content-Length` / `Connection: close` / `Cache-Control: no-store` / `Server: agentsim` | `src/app/HttpServer.cpp:366-372` |

状态码文本仅覆盖 200/400/403/404/405/413/500，其余回落 200（`src/app/HttpServer.cpp:49-60`）。路由优先于静态文件（`src/app/HttpServer.cpp:331-348`）。

### 6.2 路由表

共 **15 个不同路径、16 条 method-path 路由**（`/env` 有 GET 与 POST 两条）。全部定义在 `src/app/main.cpp:786-999` 的 router lambda 中。未命中的路径回落静态文件（`:998`）。

| # | 方法 | 路径 | 请求体 | 响应体 | 副作用 | 源码 |
|---|---|---|---|---|---|---|
| 1 | GET | `/status` | — | `{"ok":true,"phase":"running","tick":N}` | 无（只读 bridge 快照） | `main.cpp:791-798` |
| 2 | POST | `/control` | `cmd=pause\|resume\|step` | `{"ok":true,"paused":bool}`；未知 cmd → 400 + `{"ok":false,"error":"unknown cmd"}` | 置暂停/继续/单步标志 | `main.cpp:801-819` |
| 3 | GET | `/env` | — | 主循环每帧发布的 `EnvControl::toJson` 结果 | 无 | `main.cpp:820-825` |
| 4 | GET | `/schema` | — | `ParamSchema::toJson()`（`{"count":113,"groups":[…]}`） | 无（静态表） | `main.cpp:826-833` |
| 5 | POST | `/env` | 行式 `key=value`，每行一条命令 | `{"ok":bool,"applied":N,"paramChanges":N,"queuedEvents":N,"rejected":N[,"error":…][,"unwired":[…]]}` | 写 `liveCfg`、入队事件、置 `cfgDirty` | `main.cpp:834-839`、`:335-398` |
| 6 | GET | `/state` | — | `application/octet-stream`，即 §3 二进制快照 | 无 | `main.cpp:840-846` |
| 7 | POST | `/run` | `seed=`、`nuclei=`、`cyano_nodes=`、`policy=`、`max_fps=`（均可选） | `{"ok":true,"phase":"running","seed":N}` | 入队换局请求，主循环下帧原子替换 `Simulation` 与 `Timeline` | `main.cpp:848-874`、`:1050-1061` |
| 8 | GET | `/saves` | — | `{"ok":true,"saves":[{"name","frame","seed","aliveNuclei","savedAt"},…]}` | 无 | `main.cpp:875-889` |
| 9 | POST | `/save` | `name=` | 成功 `{"ok":true,"name":…}`；名字非法 400；超时 504；写失败 500 | 主循环当帧执行 `SaveStore::save`（磁盘 IO），HTTP 线程最多等 3000 ms | `main.cpp:890-909` |
| 10 | POST | `/load` | `name=` | 成功 `{"ok":true,"name":…}`；头解析失败 400 + `{"ok":false,"error":…}` | 入队载入请求：主循环用存档 cfg 新建 `Simulation` 再 `deserializeState` | `main.cpp:910-921`、`:1062-1076` |
| 11 | POST | `/rename` | `old=`、`new=` | 成功 `{"ok":true,"name":new}`；失败 400 | 目录改名 + 改写 `meta.txt` | `main.cpp:922-933` |
| 12 | POST | `/delete-save` | `name=` | 成功 `{"ok":true,"deleted":name}`；失败 400 | `remove_all` 存档目录 | `main.cpp:934-943` |
| 13 | GET | `/timeline` | — | `{"ok":true,"checkpoints":[frame,…],"current":N}` | 无 | `main.cpp:944-956` |
| 14 | POST | `/seek` | `frame=` | 成功 `{"ok":true,"frame":N}`；未暂停 409；缺 frame 400 | 入队 seek，主循环恢复检查点并补帧 | `main.cpp:957-972`、`:1077-1090` |
| 15 | GET | `/packs` | — | `{"ok":true,"packs":[…],"active":{颜色键:值},"err":…}` | 无 | `main.cpp:975-980`、`:695-725` |
| 16 | POST | `/set-packs` | `enabled=a,b,c`（空 = 全停用） | 同 `/packs`；写盘失败回落 500 | 改写 `resourcepacks.list` 并重扫包目录 | `main.cpp:981-997` |

线程归属（`src/app/main.cpp:727-736`）：HTTP 线程独占 `envSt` / `liveCfg` / `packState`；主循环独占 `sim`；两者只通过 `ServeBridge`（`std::mutex` 保护）交换事件队列、`cfgDirty` 标志与每帧发布的只读快照。

### 6.3 `POST /env` 行式协议

每行一条命令，形式 `key=value`（`src/app/main.cpp:343-377`）。五种命令形态（`src/app/EnvControl.h:124-133`）：

| 形态 | 示例 | 语义 | 幂等行为 | 源码 |
|---|---|---|---|---|
| `axis.<id>=<0..4>` | `axis.light=3` | 四轴快调，一次产出该档位的全部键值 | 同档返回 true 且**不计数**、不产出变更 | `EnvControl.cpp:429-457` |
| `fine.<key>=<value>` | `fine.sun_base_level=1.35` | 细调单条，值按 schema 钳制 | 无幂等（每次计 1 次干预） | `EnvControl.cpp:459-479` |
| `event.<id>=1` | `event.harvest=1` | 事件，只产出效果描述并入队 | 无冷却、无条件成功 | `EnvControl.cpp:365-393` |
| `preset.<id>=1` | `preset.balance=1` | 四轴档位 + 蓝细菌开关 | 无 | `EnvControl.cpp:395-427` |
| `cyano=on\|off` | `cyano=off` | 蓝细菌总开关 | 同态返回 true 且不计数 | `EnvControl.cpp:342-363` |

四轴档位表（`src/app/EnvControl.cpp:45-150`）：

| 轴 | 档位 0..4 | 每档设置的键值 | 源码 |
|---|---|---|---|
| `light` | 极夜 / 阴天 / 标准 / 晴朗 / 灼热 | `sun_base_level` ∈ {0.05, 0.4, 1.0, 1.8, 3.0}；`sun_day_amplitude` ∈ {0.05, 0.2, 0.5, 0.7, 0.9} | `EnvControl.cpp:55-61` |
| `food` | 贫瘠 / 稀少 / 标准 / 丰饶 / 泛滥 | `ball_energy` ∈ {3, 6, 10, 18, 30}；`cyano_ball_spawn_probability` ∈ {0.005, 0.01, 0.02, 0.05, 0.1} | `EnvControl.cpp:80-86` |
| `pressure` | 安逸 / 温和 / 标准 / 严酷 / 炼狱 | `basal_cost` ∈ {0.002, 0.006, 0.01, 0.02, 0.04} | `EnvControl.cpp:105-111` |
| `producer` | 停滞 / 稀疏 / 标准 / 繁盛 / 爆发 | `cyano_photosynthesis_rate` ∈ {0.15, 0.4, 1.0, 2.0, 4.0} | `EnvControl.cpp:130-136` |

细调白名单 = 四轴 `fine` 列表的并集，共 25 个键（`src/app/EnvControl.cpp:62-69, 87-94, 112-119, 137-145`），未登记键返回 `"该参数不支持实时细调"`（`:462-465`）。

四个事件与效果（`src/app/EnvControl.cpp:152-194`，执行器在 `src/app/main.cpp:408-479`）：

| 事件 | 效果字段 | 执行语义 | 账目通道 |
|---|---|---|---|
| `harvest` | `perNucleus=8.0`，`capPerEvent=2000.0` | 每个存活核 +8，累计达 2000 即停 | 先 `transfer(0, id, per)`，末尾 `injectPrimary(0, injected)` |
| `winter` | `perNucleus=-5.0`，`skipBelow=5.0` | 能量 `< 5` 的核跳过；扣不起则不扣 | `dissipate("event:winter", take)` |
| `plague` | `perNucleus=-12.0`，`onlyRich=true`，`richThreshold=100.0` | 仅能量 `> 100` 的核扣 12 | `dissipate("event:plague", take)` |
| `bloom` | `cyanoNodesToAdd=120`，`cyanoNodesIfDisabled=150` | 蓝细菌关闭时加 150，开启时加 120（受 `cyanoMaxNodes` 截断） | 0 能播种，账面零操作 |

事件后审计不成立即 `exit 3`（`src/app/main.cpp:471-477`）。

四个预设（`src/app/EnvControl.cpp:196-207`）：

| id | 名称 | levels[Light, Food, Pressure, Producer] | cyanoOn | cyanoTarget |
|---|---|---|---|---|
| `balance` | 平衡 | {2, 2, 2, 2} | true | 150 |
| `bounty` | 丰饶 | {3, 3, 1, 3} | true | 150 |
| `harsh` | 严酷 | {1, 1, 3, 1} | true | 150 |
| `bloom` | 爆发 | {4, 2, 3, 4} | true | 400 |

`cyanoTarget` 只出现在 `GET /env` 的 preset 描述里，执行器不消费（`src/app/EnvControl.cpp:420-426` 只按 `levels` 与 `cyanoOn` 产出变更）。

`EnvState` 的字段（`src/app/EnvControl.h:31-37`）：`interventions`（每次非幂等成功的 `apply` 加 1，见 `src/app/EnvControl.cpp:360,379,425,452,476`）、`peakAlive`（**在 `src/` 内无写入点**，恒 0）、`frames`（由 `tickFrame` 累加，但 `tickFrame` **在 `src/` 内无调用点**，恒 0）、`axisLevels[4]`（初始全 2，`apply` 时更新）、`cyanoOn`（初始 true）。`GET /env` 直接输出这些字段（`src/app/EnvControl.cpp:498-511`），因此 `peakAlive` 与 `frames` 在响应里恒为 0。

---

## 七、trace 与 stats 输出

### 7.1 trace 顶层结构（`--trace <path>`）

契约版本 3（`docs/TRACE-CONTRACT.md` v3）；`schemaVersion` 与 `meta.contractVersion` 两处必须同步写入（`src/app/TraceWriter.h:3-6`）。顶层键序固定（`src/app/TraceWriter.cpp:203-218`）：

```json
{"schemaVersion":3,
 "meta":{"seed":…,"ticks":…,"world":{"width":…,"height":…},"policy":"…",
         "generator":"agentsim","contractVersion":3,
         "framesCaptured":N,"framesTruncated":bool},
 "bounds":{"minX":…,"minY":…,"maxX":…,"maxY":…},
 "series":[…],
 "frames":[…]}
```

`bounds` 取 `{0, 0, world.width, world.height}`（`src/app/main.cpp:1340-1341`）。

### 7.2 `series` 列（每 tick 一行，永不降采样）

| 列名 | 类型 | 含义 | 来源 | 源码 |
|---|---|---|---|---|
| `tick` | int | 帧号 | `sim.tick()` | `TraceWriter.cpp:68` |
| `nuclei` | int | 存活核数 | `sim.nucleusCount()` | `TraceWriter.cpp:69` |
| `cyanoAlive` | int | 存活蓝细菌节点数 | `sim.cyano().aliveCount()` | `TraceWriter.cpp:70` |
| `cyanoEnergy` | float(%.6f) | 节点能量总和 | `sim.cyano().totalEnergy()` | `TraceWriter.cpp:71` |
| `totalEnergy` | float | 生态总能量（含核/球/自由球/尸体包/蓝细菌） | `sim.totalEnergy()` | `TraceWriter.cpp:72` |
| `injected` | float | 账本累计一级注入 | `ledger().totalInjected()` | `TraceWriter.cpp:73` |
| `dissipated` | float | 账本累计耗散 | `ledger().totalDissipated()` | `TraceWriter.cpp:74` |
| `auditOk` | bool | 当帧守恒审计结果 | `sim.auditOk()` | `TraceWriter.cpp:75` |

`totalEnergy()` 的求和口径（`src/sim/Simulation.cpp:1001-1021`）：

$$ E_{\text{total}} = \sum_{\text{alive } i}\Big(n_i.E + \sum_{b \in n_i.\text{balls}} b.E + \sum_{b \in n_i.\text{detached}} b.E\Big) + \sum_{b \in \text{freeBalls}} b.E + \sum_{c \in \text{carrion}} c.E + E_{\text{cyano}} $$

### 7.3 `frames` 列（空间快照，按 `--trace-interval` 降采样）

每帧对象键序固定为 `tick, n, b, f, c`（`src/app/TraceWriter.cpp:158-166`），`--trace-genome` 时在 `c` 之后追加 `g`（`:165`）。

| 键 | 数组元素 | 下标含义 | 源码 |
|---|---|---|---|
| `n` | `[id, x, y, energy, cap, speed, alive]` | 7 项；`alive` 为 JSON bool | `TraceWriter.cpp:125-126` |
| `b` | `[id, owner, x, y, energy, radius, type]` | 7 项；`owner` 为宿主核 id，自由球写 0 | `TraceWriter.cpp:134-135` |
| `f` | `[idA, idB]` | 2 项，蓝细菌连线有序对（键名沿用 v2 的 "f"，载荷已改为拓扑） | `TraceWriter.cpp:143` |
| `c` | `[id, x, y, energy, alive, hue]` | 6 项，`hue` ∈ [0,360) | `TraceWriter.cpp:152-153` |
| `g` | `[id, gene0…gene23, hue]` | 25 项，仅 `--trace-genome` 时出现；整键不出现（不是空数组） | `TraceWriter.cpp:87-93`、`main.cpp:1427-1454` |

遍历顺序：核 → 核名下球 → 自由球 → 连线 → 蓝细菌节点 → 基因组，全部按容器下标序，**不排序**（双跑一致性红线，`src/app/main.cpp:1382-1452`）。

### 7.4 数值格式与截断

- 浮点一律 `std::fixed << std::setprecision(6)`，即 `%.6f`；每个可能含浮点的流都单独设置（`src/app/TraceWriter.cpp:8-18`）。
- `NaN/Inf` 一律折叠为 `0.000000`（`src/app/TraceWriter.cpp:21`）。
- 帧采集上限两个（Defect-C）：帧数上限 `kDefaultMaxFrames = 10000`，帧流字节上限 `kDefaultMaxFrameBytes = 64 MiB`（`src/app/TraceWriter.h:28-29`）。任一触顶即**永久停止采集帧**，`series` 不受限（`src/app/TraceWriter.cpp:47-58`）。
- 触顶后 `meta.framesTruncated = true`、`meta.framesCaptured = N`（`src/app/TraceWriter.cpp:211-212`）。

### 7.5 `stats.csv`（`--stats [path]`）

表头固定 7 列（`src/app/main.cpp:1317`）：

```
tick,nuclei,cyano,total_energy,audit_ok,deaths,state_hash
```

写入条件 `tick % statsInterval == 0`（默认 `statsInterval = 100`），`audit_ok` 写 1/0，`state_hash` 写 `stateHash()` 的十进制（`src/app/main.cpp:1356-1364`）。默认路径 `build/stats.csv`（`:1306-1307`）。

---

## 八、命令行参数全表

### 8.1 参数表

解析实现：`src/app/Cli.cpp:62-291`；serve 相关的三项在 `main` 中**预提取**（`src/app/main.cpp:1186-1227`）。

| 参数 | 取值 | 落点 / 含义 | 默认 | 源码 |
|---|---|---|---|---|
| `--help` | — | 打印帮助并 exit 0 | — | `Cli.cpp:65`、`main.cpp:1235` |
| `--version` | — | 打印 `agentsim 3.0.0` 并 exit 0 | — | `Cli.cpp:66`、`main.cpp:1236` |
| `--audit` | — | 仅置标志（advisory） | false | `Cli.cpp:67` |
| `--render` | — | 置渲染标志（无消费者） | false | `Cli.cpp:68` |
| `--no-render` | — | 清渲染标志（无消费者） | false | `Cli.cpp:69` |
| `--width W` | double > 0 | `world.width` | 1024.0 | `Cli.cpp:70-77` |
| `--height H` | double > 0 | `world.height` | 1024.0 | `Cli.cpp:70-77` |
| `--ticks N` | u64 | `world.maxTicks` | 1000 | `Cli.cpp:78-85` |
| `--seed S` | u64 | `world.seed` | 42 | `Cli.cpp:78-85` |
| `--nuclei N` | int 0~10000 | `colony.nuclei` | 12 | `Cli.cpp:86-99` |
| `--world-width W` | double 32~100000 | `world.width` | 1024.0 | `Cli.cpp:101-111` |
| `--world-height H` | double 32~100000 | `world.height` | 1024.0 | `Cli.cpp:101-111` |
| `--cyano-nodes N` | int 0~100000 | `cyanoNodeCount`（初始节点数） | 512 | `Cli.cpp:112-128` |
| `--cyano-max-nodes N` | int 0~100000 | `cyanoMaxNodes`（硬上限） | 4096 | `Cli.cpp:112-128` |
| `--sun-base F` | double | `sun.baseLevel` | 0.5 | `Cli.cpp:131-142` |
| `--sun-amplitude F` | double | `sun.dayAmplitude` | 0.5 | `Cli.cpp:131-142` |
| `--policy NAME` | `rule\|stub\|perceptron\|thermal\|hysteresis\|foragebank\|bandit\|riskaverse` | `policy.name`，并置 `usePerceptron`/`useThermal` 与 `policy.mutation` | rule | `Cli.cpp:143-163` |
| `--perceptron-seed S` | u64 | `policy.perceptronSeed` | 12345 | `Cli.cpp:164-177` |
| `--perceptron-scale F` | double | `policy.perceptronScale` | 0.1 | `Cli.cpp:164-177` |
| `--thermal-pref F` | double | `policy.thermalPreferredTemp` | 0.5 | `Cli.cpp:178-193` |
| `--thermal-w-thermal F` | double | `policy.thermalWThermal` | 1.2 | `Cli.cpp:178-193` |
| `--thermal-w-caution F` | double | `policy.thermalWCaution` | 0.8 | `Cli.cpp:178-193` |
| `--terrain-obstacles N` | int ≥ 0 | `terrain.obstacleCount` | 0 | `Cli.cpp:194-208` |
| `--terrain-waters N` | int ≥ 0 | `terrain.waterCount` | 0 | `Cli.cpp:194-208` |
| `--terrain-thermal N` | int ≥ 0 | `terrain.thermalZoneCount` | 0 | `Cli.cpp:194-208` |
| `--agent-mode N` | 仅 `1` | `agent.agentMode`；`0` 解析期即拒绝 | 1 | `Cli.cpp:209-229` |
| `--trace PATH` | 路径 | 打开 JSON trace 输出 | 关闭 | `Cli.cpp:230-256` |
| `--trace-genome` | — | 每帧额外导出 `g`（每核 25 数） | 关闭 | `Cli.cpp:257` |
| `--trace-interval N` | int > 0 | 帧降采样步长 | 1 | `Cli.cpp:258-266` |
| `--stats [PATH]` | 可选路径 | 打开 CSV 统计 | 关闭；路径默认 `build/stats.csv` | `Cli.cpp:267-276`、`main.cpp:1306-1307` |
| `--stats-interval N` | int > 0 | 采样间隔帧 | 100 | `Cli.cpp:277-285` |
| `--serve [PORT]` | 可选端口 | 进入实时服务模式 | 关闭；端口默认 8080 | `main.cpp:1196-1208` |
| `--serve=PORT` | 端口 | 同上（等号形态） | — | `main.cpp:1213-1222` |
| `--no-open` | — | 抑制自动拉起浏览器 | 关闭（默认会拉起） | `main.cpp:1209-1212` |

`printHelp` 的文案与实际默认值有一处不一致：帮助里写 `--cyano-max-nodes ... (default 1024)`（`src/app/main.cpp:73`），而 `SimConfig::cyanoMaxNodes` 的实际默认值是 4096（`src/core/Config.h:314`）。`--cyano-nodes` 的 "default 512"（`src/app/main.cpp:72`）与实际一致。

### 8.2 解析规则与拒绝条件

数值解析统一三关（`src/app/Cli.cpp:15-32`）：`errno == ERANGE` 拒绝、非有限值拒绝、**首字符为 `-` 一律拒绝**（避免 `strtoull("-1")` 回绕成 `ULLONG_MAX`）。整数另有范围校验；`--nuclei` 上限 10000，`--cyano-*` 上限 100000（`src/app/Cli.cpp:18-19`）。

三类显式拒绝（均返回 false，主程序 exit 2）：

1. `--trace` 的值以 `-` 开头或为**纯数字** → 报错并提示"是不是想写 `--trace-interval`"（`src/app/Cli.cpp:233-252`）。
2. `--agent-mode 0` → "orbital paradigm not implemented yet"（`src/app/Cli.cpp:218-222`）。
3. 未知选项 / 多余位置参数（`src/app/Cli.cpp:286-288`）。

`--trace-interval` 在无 `--trace` 时给非默认值只打 stderr 警告，不报错（`src/app/main.cpp:1324-1327`）。

### 8.3 `--serve` 的预提取

`--serve` / `--serve=PORT` / `--no-open` 在 `parseCli` **之前**从 argv 中被摘掉，剩余参数交给 `parseCli`（`src/app/main.cpp:1190-1227`）。端口跟随规则：仅当下一 token 是纯数字且不以 `-` 开头才消费为端口（`:1199-1206`）。

无任何参数启动（`argc == 1`）等价于 `--serve 8080`（`src/app/main.cpp:1295-1296`），且先做单实例探活：对 `127.0.0.1:8080..8090` 逐个非阻塞 connect（`select` 300 ms 超时），对活着的端口发 `GET /state HTTP/1.0` 并检查响应第 10-12 字节为 `"200"`；命中则拉起浏览器指向该端口并 exit 0（`src/app/main.cpp:1242-1294`）。

### 8.4 退出码

| 码 | 含义 | 源码 |
|---|---|---|
| 0 | 正常结束 | `src/app/main.cpp:1489` |
| 2 | 参数错误（解析失败 / 未知选项 / 非法 serve 端口） | `src/app/main.cpp:1218,1231-1234` |
| 3 | 守恒审计失败（批跑或 `--serve` 事件后） | `src/app/main.cpp:1479`、`:1136-1138`、`:476` |
| 4 | I/O 失败（stats 打不开 / trace 打不开或写失败） | `src/app/main.cpp:1314,1333,1470` |
| 5 | 种群灭绝（`nucleusCount() == 0`） | `src/app/main.cpp:1484-1488` |

---

## 附：本领域涉及但未展开的边界

- `/state` 与 `save.bin` 是两套独立格式：前者为前端渲染优化（f32、无 rng、无账本），后者为逐位续跑（f64、含 rng 与账本）。二者不可互相替代，也不共享版本号空间。
- `ParamSchema` 与 `RuntimeEnv` 的键集合**不相等**（schema 有 `width/height/seed/ticks/regrow_food` 等 5 个拒绝键；RuntimeEnv 有 `migration_bonus/migration_radius/cyano_enabled/cyano_ball_spawn_*` 等 5 个 schema 外键），因此 `GET /schema` 不是"可用参数"的完备清单。
- cfg 块不落 `RuntimeTuning/NucleusTuning/BallTuning/CyanoTuning/TerrainConfig/TemperatureConfig`，读档后这些一律回到 `Config.h` 默认值。
