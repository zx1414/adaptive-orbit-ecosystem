# Agent 生态 —— 下一步计划（Phase A0：环境 / 探测 / 决策 / 行为 模块化解耦）

> **导读**：本文是「Agent 范式」的第一步实施规格。目标读者是持有工作区源码的贡献者：
> 读完 §1 你能对上现有代码，读完 §3 你能独立领走一个模块的开发任务。
> 每个源码锚点都写成「文件:行号 + 内容特征」双标识，行号漂移时按内容特征定位。
> 配套阅读：`docs/PLAN.md`（现有架构的原始规格）、`src/MOD_SPEC.md`（项目红线，本文 §1.3 全部沿用）。

---

## 1. 背景速览

### 1.1 当前范式（轨道范式）

系统当前是「核-小球」两层结构：**小球（Ball）是被动物理体**——只有位置/速度/类型/归属四个字段
（`src/Ball.h`，BallType = Shield/Worker/Scout），被核的力场吸引环绕；
**核（Nucleus）是唯一决策体**——携带 22 个可遗传参数（`src/NucleusParams.h`），
通过吸收小球获得能量、繁殖、变异，接受自然选择。

每帧由 `World::step()`（`src/World.cpp:207`，特征：函数体第一行是「年龄递增」注释）按固定顺序推进：

```
updateCyanobacteria → buildGrid → updateBallDynamics(:274) → updateOwnership(:393)
→ absorbBalls(:429) → nucleusCombat(:507) → nucleusMovement → reproduce(:677)
→ removeDeadNuclei → replenishBalls → ++frame → tickEnvironment → checkFinish
→ emit FRAME_START → emit FRAME_END
```

### 1.2 五件基础设施（新模块必须接入它们，而不是绕开）

| 设施 | 位置 | 一句话职责 | Agent 模块的接入点 |
|---|---|---|---|
| SpatialGrid | `src/SpatialGrid.h:11-20`（clear/addBall/addNucleus/cellRange/ballsAt/nucleiAt） | 均匀网格空间查询，实测 5000 球/500 核无渲染 ≥60 FPS | 直接**再实例化两份**（agent 网格 + 能量点网格），不改类 |
| EnergySystem | `src/EnergySystem.h`（EnergyReason 枚举 + apply 统一入口） | 能量收支唯一通道：原因科目 + 按科目统计 + 死亡判定 | 账本模块包装它；新科目**只在枚举末尾追加**（见 §3.5） |
| ParamSchema | `src/ParamSchema.h:24,31`（all/applyLine/valueString/writeEnvFile） | ~120 个配置键的定义、解析、参数页渲染、存档兼容 | 新配置键在此注册，自动获得参数页与存档兼容 |
| Sampler | `src/Sampler.cpp` | balls_/nuclei_/trend/survivors CSV 采样 | 后续 A1 加 agents_*.csv（A0 不做） |
| EventBus / ModAPI | `src/EventBus.h`、`src/mod_api.h` | 类型化事件 + mod 唯一接口 | A0 不接 mod；agent 模式的 mod 钩子留给 A2 |

### 1.3 必须沿用的项目红线（全文有效，违反即返工）

1. **基线保护**：不启用新功能时，同种子输出与基线**逐字节一致**（`agent_mode=0` 的实现含义见 §2.2）。
2. **确定性**：随机只用世界 rng 且固定迭代顺序；禁 `rand()/time()`；决策函数本身零随机。
3. **能量零和**：「A 得 B 失」必须等额；注入类（如光合）单独成科目并声明。
4. **枚举只追加**：EnergyReason 只能在末尾加项，`kEnergyReasonCount` 与 `EnergySystem.cpp`
   `printSummary()` 的 `names[]` 三者同步（文件内有 `static_assert` 保护）。
5. **命名空间隔离**：Agent 新类型一律收进 `namespace agentsim`——
   绝不在全局作用域与核心同名（历史教训：mod 里自定义 `SpatialGrid` 撞名 → 7 个编译错误）。

---

## 2. 目标与范式关系

### 2.1 Agent 范式定义：菌落器官模型

**Agent = 菌落（核 + 归属球）**。核是唯一的决策与存储中枢；三类球是菌落的**功能器官**——
不同球功能不同，各自只做一件事：

| 器官 | 职责（已定稿，勿反复） | 实现阶段 |
|---|---|---|
| **侦察球 Scout** | **探测的唯一来源**：菌落的对外感知（通道/半径/噪声）完全由侦察球持有量决定；无侦察球 = 对外失明 | A0 |
| **资源球 Worker** | **能量电容**：菌落能量容量 = 核原生小容量 + 资源球数 × 单球容量——没有资源球，核存不下多余能量（繁殖门槛 > 原生容量 ⇒ 繁殖必须先囤资源球） | A0 |
| **护盾球 Shield** | **保护与进攻**（拦截/冲撞/承伤，对接战斗重做计划） | 暂不实现，仅占位 |

每个 tick 菌落走四步循环：

```
环境(查询) → 探测(侦察球给出传感向量) → 决策(核产生动作意图) → 行为(执行+扣费)
```

与「每球一个自治 agent」方案（已否决）的差别：球不是自治体而是**器官**——决策仍归核
（保住 22 基因组与进化机器），感知/储能/战斗能力下沉到器官层，由球的**持有结构**决定
菌落的能力面。

### 2.2 平行模式（`agent_mode` 开关）

两套范式**长期共存**，由 `WorldConfig::agent_mode` 切换（默认 0 = 轨道范式）：

- `agent_mode=0`：`World::step()` 走现有管线，**一行行为逻辑都不许变**——这是基线保护的实现含义；
- `agent_mode=1`：`step()` 在 `updateCyanobacteria` 之后、`buildGrid` 之前改走
  `stepAgents()` 管线（§3.7），轨道管线的球/核结构保持空转（渲染兼容）。

> **为什么不做原地替换**：轨道范式是已发布产品（v2.3.1 release + mod 生态 + 基线金标准）。
> 小球一旦升级为 agent，`ball_roles`/`ball_loss_relief` 等按轨道语义写的 mod 全部失效。
> 平行模式让两条线互不污染，代价只是 World 里一个 if。

---

## 3. Phase A0：四模块解耦规格（本次实施范围）

### 3.0 依赖方向与命名空间（先背下来再写代码）

**本节起「agent」一律指菌落整体（核 + 器官球）**；`AgentSimAgent` 是菌落的值对象快照
（含能量与器官计数 scoutCount/workerCount/shieldCount）。

```
                    ┌──────────────────────────────────────────────┐
                    │            AgentEnvironment（环境）           │
                    │  SpatialGrid×2 + EnergyNode[] + tick + rng   │
                    └──────┬───────────────────────────┬───────────┘
              只读查询 ↓                        ↓ 只读查询
        ┌─────────────────┐            ┌──────────────────┐
        │ AgentSensors探测 │            │ AgentBehavior行为 │──写实体──→ Agent
        └────────┬────────┘            └────────┬─────────┘
      传感向量 ↓（值拷贝）                     ↓ 扣费/记账
        ┌─────────────────┐            ┌──────────────────┐
        │ IDecisionPolicy决策│──意图──→ │  AgentLedger账本  │──→ EnergySystem
        └─────────────────┘            └──────────────────┘
```

- **严格单向**：环境→探测→决策→行为→账本。决策不认识 World，探测看不到 Policy，
  **行为是唯一可以写实体状态的模块**，能量只能经账本改（防作弊）。
- 通信全部走**值对象**（传感向量/动作意图），不传可变引用——模块可独立 mock 单测。
- 所有新类型收进 `namespace agentsim`；文件放 `src/` 下扁平 PascalCase（项目惯例）。

### 3.1 环境模块 `AgentEnvironment`（src/AgentEnvironment.h/.cpp）

**职责**：Agent 视角的世界——空间查询、实体注册表、tick、rng、边界。新模块里唯一「知道全局」的类，
因此被设计成**可 mock 的接口**（探测/决策/行为的单测都用假环境）。

```cpp
// src/AgentEnvironment.h
namespace agentsim {

struct EnergyNode {           // 环境模块持有（原架构模块2的最小版）
    Vec2 pos;
    double amount;           // 剩余可采集量
    bool depleted;
};

class IAgentEnvironment {     // 接口：探测/行为只依赖它，不依赖实现
public:
    virtual ~IAgentEnvironment() = default;
    virtual void queryAgents(const Vec2& pos, double radius,
                             std::vector<const AgentSimAgent*>& out) const = 0;
    virtual void queryNodes(const Vec2& pos, double radius,
                            std::vector<EnergyNode*>& out) const = 0;
    virtual double boundaryDist(const Vec2& pos) const = 0;  // 到最近边界的距离
    virtual Vec2 size() const = 0;
    virtual int tick() const = 0;
    virtual std::mt19937& rng() = 0;
};

class SpatialEnvironment : public IAgentEnvironment {  // 唯一生产实现
    /* SpatialGrid agentGrid_ + nodeGrid_（两份新实例，复用 SpatialGrid.h:11-20 API，
       不改该类）；std::vector<AgentSimAgent> colonies_; std::vector<EnergyNode> nodes_;
       WorldConfig 引用；tick 计数 */
};
}  // namespace agentsim
```

实现要点：`queryAgents/queryNodes` 用 `cellRange` 圆形裁剪（照抄 `World.cpp:284-315`
力场查询的写法）；grid cell 尺寸沿用 `grid_cell_size`。

### 3.2 探测模块 `AgentSensors`（src/AgentSensors.h/.cpp）—— **探测只交给 Scout**

**核心设计（已定稿）**：菌落的对外感知**只**来自侦察球。核原生近乎失明（只感知自身能量
与边界距离）；侦察球持有量决定感知半径与质量，并解锁全部对外通道。**资源球/护盾球永不
贡献感知**——囤资源变富但失明、囤侦察看得远但穷，是菌落结构的第一层取舍。

| 菌落构成 | 通道授予 | 感知半径 | 噪声 |
|---|---|---|---|
| 无侦察球 | 仅内部通道（自身能量 + 边界） | 原生盲区 ≈ absorbRadius(30) | — |
| ≥1 侦察球 | 全部 7 个对外通道（食物×3 / 邻近菌落×3 / 密度） | `base + per × √count`，夹到上限 | 随侦察数下降（下限 > 0） |
| 任意资源球/护盾球 | **不影响感知（0 贡献）** | — | — |

```cpp
// src/AgentSensors.h
namespace agentsim {

constexpr int kSensorDim = 9;          // 定长：7 个对外通道 + 2 个内部通道
enum SensorChannel {
    S_NODE_DIR = 0, S_NODE_DIR_Y, S_NODE_DIST,      // 0-2 最近能量点方向xy+距离（对外）
    S_AGENT_DIR, S_AGENT_DIR_Y, S_AGENT_DIST,       // 3-5 最近菌落（对外）
    S_DENSITY,                                       // 6   局部密度（对外）
    S_ENERGY, S_BOUNDARY,                            // 7-8 自身能量/边界距离（内部，恒授予）
    // v2 预留：S_THREAT_*（枚举只追加）
};

constexpr uint16_t kMaskInternal = 0x180;   // S_ENERGY|S_BOUNDARY：核原生即有
constexpr uint16_t kMaskExternal = 0x07F;   // 对外通道：只有侦察球能点亮

struct SensorProfile {                 // 菌落感知档案：由侦察球持有量唯一决定
    uint16_t channelMask;              // 无侦察 = kMaskInternal；有侦察 = 0x1FF
    double radius;                     // scout_sense_base + scout_sense_per × √count
    double noiseSigma;                 // 随侦察数下降
};
SensorProfile colonyProfile(int scoutCount, const ColonySenseParams& p);

// 纯函数：同一输入永远同一输出（决策确定性的前提）。噪声经环境 rng 注入。
void sense(const AgentSimAgent& colony, const IAgentEnvironment& env,
           const SensorProfile& profile, SensorSpan out);
}  // namespace agentsim
```

**契约形式**：传感输出是**定长 9 维向量**——未授予的通道置零并附 `channelMask`，另给
菌落器官构成（scout/worker/shield 计数）。决策器接口不因菌落构成变长：v1 规则表与
v2 网络（9+3 输入定长）无缝互换，这是 §3.3 可替换性的前提。

> 掩码速查：`kMaskInternal = 0x180`（bits7-8）；`kMaskExternal = 0x07F`（bits0-6）。
> 「资源球/护盾球不贡献感知」是规格的一部分——验收 ④ 要断言它。

### 3.3 决策模块 `IDecisionPolicy`（src/AgentDecision.h/.cpp）

**职责**：传感向量 → 动作意图。这是**可替换件**——v1 规则表、v2 小网络、测试用
StubPolicy 全部实现同一接口，探测/行为对它们无感知。决策主体 = **核**（菌落唯一大脑），
器官球不参与决策。

```cpp
// src/AgentDecision.h
namespace agentsim {

struct ActionIntent {                  // 纯值对象：决策与行为之间唯一的通信物
    enum class Kind { MOVE, REST, HARVEST };
    Kind kind = Kind::REST;
    Vec2 dir;                          // MOVE/HARVEST 的方向（单位向量）
    double magnitude = 0.0;            // 0~1 强度（速度档位/采集力度）
};

class IDecisionPolicy {
public:
    virtual ~IDecisionPolicy() = default;
    virtual ActionIntent decide(const float* sensors, int dim,
                                const AgentSimAgent& self) = 0;
};

class RulePolicy : public IDecisionPolicy {   // v1：~8 参数规则表（见下表）
    /* 参数即未来基因组（A1 接 ParamRanges 式变异/裁剪） */
};
}  // namespace agentsim
```

RulePolicy v1 参数表（候选，实现时可微调但**不得增删接口**）：

| 参数 | 含义 | 界 |
|---|---|---|
| `turnGain` | 朝目标方向转向上限 | [0, 3] |
| `restEnergyBelow` | 能量低于该比例 → REST | [0, 0.5] |
| `resumeEnergyAbove` | 能量高于该比例 → 恢复 FORAGE | [0.3, 1] |
| `harvestDistPref` | 距能量点多近开始 HARVEST | [0, 1] |
| `wanderTurn` | 无目标时随机游走转角 | [0, 0.5] |
| `speedPref` | 速度档位偏好 | [0.2, 1] |
| `crowdAvoid` | 密度规避权重 | [0, 2] |
| `targetBias` | 能量点 vs 同类聚集的权重 | [0, 1] |

**决策函数零随机**：游走项的随机数在**传感阶段**由环境 rng 注入（噪声通道），
decide 本身是纯函数——这是「同输入同输出」可测试性的保证。

### 3.4 行为模块 `AgentBehavior`（src/AgentBehavior.h/.cpp）

**职责**：把核的意图落到世界上——**唯一可写实体状态的模块**。A0 中菌落移动 = 核移动
（器官在 A1 才接入真实归属球与跟随物理，A0 的器官只是菌落值对象里的计数）。

```cpp
// src/AgentBehavior.h
namespace agentsim {
class AgentBehavior {
public:
    // 执行意图：移动（复用 World.cpp:15 bounce() 的边界反弹语义——把它提为
    // agentsim 可见的小工具，不改原函数）、休整、采集（经账本扣节点能量）。
    // 密度排斥 A0 只留接口占位（注释标明 A1 实现）。
    void execute(AgentSimAgent& self, const ActionIntent& intent,
                 IAgentEnvironment& env, IEnergyLedger& ledger);
};
}  // namespace agentsim
```

实现要点：移动代价 `move_cost = k × v²`（对齐核的 `speedCostK` 语义，`WorldConfig` 已有先例）；
采集 = 从目标 EnergyNode 扣 amount、给 agent 记账，**一次 intent 至多采一个节点**（防 O(N²)）；
REST 档代谢减半（规则表的休整才有意义）。

### 3.5 能量账本 `AgentLedger`（src/AgentLedger.h/.cpp）

**职责**：包装 EnergySystem 的纪律，让行为层**只能**通过它改能量。

```cpp
// src/AgentLedger.h
namespace agentsim {
class IEnergyLedger {
public:
    virtual ~IEnergyLedger() = default;
    virtual void charge(AgentSimAgent& a, double amount, EnergyReason why) = 0;
    virtual void credit(AgentSimAgent& a, double amount, EnergyReason why) = 0;
    virtual bool isDead(const AgentSimAgent& a) const = 0;
};
}  // namespace agentsim
```

**EnergySystem.h 改动（唯一的核心枚举改动，仅末尾追加）**：
当前枚举尾是 `BALL_ROLES_SHIELD`（`src/EnergySystem.h:33`），其后追加：

```cpp
    // ---- Agent 范式（docs/PLAN_AGENTS.md Phase A0）----
    AGENT_METABOLISM,  // 基础代谢（REST 减半）
    AGENT_MOVE,        // 移动代价 k×v²
    AGENT_HARVEST,     // 采集能量点所得（节点侧扣减另行记账，零和）
```

`kEnergyReasonCount`（当前 19，`src/EnergySystem.h:39`）→ 22；
`EnergySystem.cpp` `printSummary()` 的 `names[]` 同步追加三项（文件内 `static_assert` 会兜底）。
账本实现内部持 EnergySystem 引用；`AgentSimAgent` 的能量字段由账本独占写入。

**能量电容模型（资源球决定当前能量容量）——账本模块的核心新纪律**：

```
colonyEnergyCap = colony_native_cap + workerCount × worker_cap_each
```

- 核原生容量很小（默认 30）；能量**只能充到 cap**——采集/收获在 cap 处截断；
- **繁殖门槛（energyThreshold 80~200）> 原生容量 ⇒ 繁殖必须先囤资源球**
  （「有资源球核才有多余能量」的直接实现，不需要任何额外规则）；
- 资源球失去（被抢/脱离/被自家核吸收）→ cap 下降，**超出部分立即裁剪销毁**
  （v1 设计选择：简单且后果可观测；后续如需账目可见性，按「枚举只追加」补 `CAP_OVERFLOW` 科目）；
- 自家资源球贴核被吸收 = 「吃掉电池换零食」——orbitRadius 基因因此成为负载基因
  （好基因组会把资源球停在核外轨道上），进食与储能的张力由此涌现。

### 3.6 器官占位：护盾球（A0/A1 不实现，仅立契约）

护盾球的定位是**保护与进攻**——拦截来袭（未来的子弹系统）、承受/发起冲撞、战斗
减伤/增伤。这些属于战斗重做（路线图 A2，对接既有「计划2：子弹 + 护盾撞击」可行性结论：
需要核心 Projectile 实体与否决式钩子），**本阶段一律不做**。

A0 唯一要做的：`AgentSimAgent` 的器官计数里保留 `shieldCount` 字段（传感与电容都明确
**不**消费它），代码注释标明「预留：保护与进攻」。A2 接入战斗时器官模型不用返工。

### 3.7 管线编排 `WorldAgents.cpp`（新翻译单元）

`World` 增加：`bool agentMode_`、`std::vector<agentsim::AgentSimAgent> colonies_`、
`agentsim::SpatialEnvironment agentEnv_`、策略工厂。`step()` 开头分发：

```cpp
void World::step() {
    if (agentMode_) { stepAgents(); return; }   // agent 范式管线
    // ……现有轨道管线一字不动……
}
```

`stepAgents()` 固定顺序（对齐总架构的每 tick 数据流）：

```
1. 能量点生成/再生（速率 G、上限 max_nodes）
2. buildGrid（agent 网格 + 节点网格）
3. for colony in colonies_（固定下标序）:
     a. colonyProfile(scoutCount, params)   # 探测能力只由侦察球决定
     b. sense(...)            → 传感向量（对外通道无侦察球时全零）
     c. policy_->decide(...)  → ActionIntent（决策主体 = 核）
     d. behavior.execute(...) → 移动/采集 + 账本扣费（含电容截断与死亡判定）
4. 清理死 agent（swap-pop，id 永不回收——照抄 Nucleus::id 纪律）
5. ++frame → emit FRAME_START/FRAME_END（沿用 EventBus，mod 时序不变）
```

策略实例：`World` 持一个 `std::unique_ptr<IDecisionPolicy>`，由
`agent_policy` 配置键选择（A0 只实现 `rule` 与 `stub` 两档——stub 用于可替换性验收）。

### 3.8 配置键清单（ParamSchema 追加，`src/ParamSchema.cpp`）

| 键 | 默认 | 说明 |
|---|---|---|
| `agent_mode` | 0 | 0=轨道范式 1=菌落范式 |
| `agent_policy` | rule | rule / stub |
| `colony_init_pop` | 12 | 开局菌落（核）数 |
| `colony_native_cap` | 30 | 核原生能量容量（繁殖门槛 > 它 ⇒ 必须先囤资源球） |
| `worker_cap_each` | 8.0 | 每颗资源球的电容 |
| `scout_sense_base` | 40 | 无侦察球时菌落感知半径（≈原生盲区） |
| `scout_sense_per` | 180 | 每侦察球半径增量基数（半径 = base + per × √count） |
| `scout_sense_max` | 800 | 感知半径上限（对齐 forage_radius 语义） |
| `node_target_count` | 300 | 环境能量点稳态数量 |
| `node_spawn_rate` | 0.06 | 每 tick 生成期望 |
| `node_amount` | 8.0 | 单点能量 |
| `colony_metabolism` | 0.02 | 菌落基础代谢/帧 |
| `colony_move_cost_k` | 0.0002 | 移动代价系数（v²） |

（感知噪声 σ 与 √ 收敛指数做成 `SensorProfile` 内常数 + 注释，A1 再升级为配置键——A0 控制键数量。）

---

## 4. 实施步骤（每步完成后先编译再进下一步）

| Step | 内容 | 自验 |
|---|---|---|
| 1 | `AgentDefs.h`（菌落值对象：能量 + 器官计数 scout/worker/shield）+ `AgentLedger`（含电容钳制）+ EnergyReason 追加 | 编译零警告；`--energy-summary` 空跑不打印新科目（零值跳过逻辑不变） |
| 2 | `AgentEnvironment`（SpatialEnvironment + EnergyNode 生成/再生） | 单测：查询半径内返回正确实体集；mock 环境可实例化 |
| 3 | `AgentSensors`（colonyProfile + sense 纯函数） | 单测：同输入同输出；无侦察球→对外通道全零且半径=原生盲区；worker/shield 计数不影响感知 |
| 4 | `IDecisionPolicy` + `RulePolicy` + `StubPolicy`（固定转圈的第二实现，可替换性验收用） | 单测：决策零随机（同传感两次调用同意图） |
| 5 | `AgentBehavior`（移动/采集 + bounce 复用 + 代价记账 + 电容截断） | 单测：移动后边界反弹正确；HARVEST 后节点量减、账本科目对；能量停在 colonyEnergyCap；workerCount 减少时超额被裁剪 |
| 6 | `WorldAgents.cpp` 管线 + `agent_mode` 分发 + ParamSchema 键 | **回归门槛：agent_mode=0 跑基线逐字节一致**（命令见 §7） |
| 7 | 冒烟：`--console --agent-mode 1 --seed 42` 跑 1000 tick | 双跑 stdout 逐字节一致；能量摘要出现 AGENT_*；agents 全程不越界不 NaN |

## 5. 验收清单（全部打勾 = Phase A0 完成）

- [ ] `agent_mode=0`：`tests/README.md` 基线配方 off/on 全部 CSV 哈希与 `tests/baseline/v2.3.1-baseline.txt` 一致；
- [ ] `agent_mode=1` 同种子双跑 stdout 逐字节一致（确定性）；
- [ ] **可替换性**：`agent_policy=stub` 注入跑通，探测/行为文件零改动（diff 证明）；
- [ ] **器官模型生效**：①无侦察球菌落对外通道全零（半径 = 原生盲区）；②加侦察球后半径按 `base + per×√count` 增长；③断言 worker/shield 计数不影响感知；④电容：无资源球菌落能量恒 ≤ colony_native_cap；有资源球时 cap = native + worker×each，失球超额被裁剪；
- [ ] 账目：`--energy-summary` 出现 `AGENT_METABOLISM/AGENT_MOVE/AGENT_HARVEST` 三行且数值合理（HARVEST in = Σ节点扣减）；
- [ ] g++ `-Wall -Wextra` 与 MSVC `/W4` 双口径零警告；
- [ ] 性能抽查：200 agents + 300 节点无渲染跑 2000 tick 无明显卡顿（A1 再提 60fps 硬指标）。

## 6. 路线图与关键决策记录

| 阶段 | 范围 | 验收标准 |
|---|---|---|
| **A0（本文）** | 四模块解耦骨架 + 冒烟 | §5 清单 |
| **A1 最小闭环** | AgentGenome（规则表基因化 + ParamRanges 式变异裁剪）、繁殖分裂（电容门槛天然生效）、**器官层从计数升级为真实归属球**（复用 updateOwnership 语义）、种群容量、agents_*.csv 指标、快照 v3 + web 渲染、8 种子平衡标定（能量流 `G < A < M`） | 不灭绝不爆炸；基因方差非零；账目守恒审计通过；基线仍逐字节 |
| **A1.5 蜂后式 CORE** | 核 = 菌落基因组载体 + 繁殖中心：核达阈值分裂出子菌落，器官球按邻近重新归属（与 ballLossCost 形成传承代价博弈）；侦察扩视已随 A0 器官模型落地 | 两层选择可观测（菌落色相聚类）；基线仍绿 |
| **A2 分化与网络** | 空间异质性斑块 + 状态机扩展（EVADE/STORE）+ NNetPolicy（9+3→6→5 小网络，同接口接入）+ 攻击/避让 + **护盾球功能落地**（拦截/冲撞——战斗重做，对接「计划2」）+ 突变体隔离区 | 行为聚类 ≥2 簇；方差维持非零 |
| **A3 生态位构建** | 标记/信息素、建造/巢穴、能量分享、频率依赖、有性重组 | 生态位构建行为可观测；无早熟收敛 |

**关键决策记录（已定，勿反复）**：

1. **决策器 v1 用规则表**——零依赖、确定性、可解释，且参数表直接就是 A1 的基因组；v2 换网络时探测/行为零改动（IDecisionPolicy 接缝）。
2. **中心核保留**——「环绕核」是项目身份；A1.5 起核 = 菌落载体，形成个体 + 菌落两层选择。
3. **器官模型（用户定稿，勿反复）**——侦察球 = 唯一探测来源（核原生近乎失明）；资源球 = 能量电容（核原生容量小，繁殖必须先囤资源球）；护盾球 = 保护与进攻（占位，随 A2 战斗重做实现）。「每球一个自治 agent」方案已否决：决策留在核，器官只提供能力；全局感知会让决策退化成查表，故感知受限于侦察球持有量。
4. **平行模式而非替换**——保护已发布产品、mod 生态与基线金标准。

## 7. 附录

### 7.1 源码导读（按阅读顺序）

| 文件 | 一句话 | 关键锚点 |
|---|---|---|
| `src/Ball.h` / `src/NucleusParams.h` | 两个现存实体长什么样 | — |
| `src/World.cpp:207` | step() 十阶段管线（轨道范式的全部） | 各阶段函数 274/393/429/507/677 |
| `src/EnergySystem.h` | 能量纪律 + 原因科目（枚举尾 :33，计数 :39） | `static_assert` 在 EnergySystem.cpp |
| `src/SpatialGrid.h:11-20` | 空间查询 API | cellRange/ballsAt/nucleiAt |
| `src/ParamSchema.h:24,31` | 配置键体系 | all/applyLine |
| `src/MOD_SPEC.md` | 项目红线全文 | §3 |
| `tests/README.md` | 基线回归配方 | v2.3.1-baseline.txt |

### 7.2 贡献自验命令

```bash
# 编译（双口径）
g++  -std=c++17 -O2 -Wall -Wextra -Isrc -o simulator src/*.cpp mods/*.cpp -lws2_32 -lshell32
# MSVC：cmd 下先跑 vcvars64，再
cl /std:c++17 /utf-8 /MT /EHsc /W4 /Isrc /Fo:build\ /Fe:build\simulator.exe src\*.cpp mods\*.cpp ws2_32.lib shell32.lib

# 基线回归（agent_mode=0 必须逐字节一致，对照 tests/baseline/v2.3.1-baseline.txt）
./simulator --console --no-render --env env_off.txt --seed 42 --balls 200 --nuclei 12 \
            --frames 5000 --sample-interval 5000 --max-fps 0
sha256sum balls_005000.csv nuclei_005000.csv survivors.csv

# Agent 冒烟（Step 7）
./simulator --console --no-render --agent-mode 1 --agent-policy rule --seed 42 --frames 1000 --energy-summary
```
