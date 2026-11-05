# M5 实施计划：恢复 v2.3.1 生态模型

> 依据：Git 标签 `legacy-v2.3.1` 是语义基准，移植时逐函数对照，不得凭记忆重写。
> 决策：放弃 v2 的 mod 兼容与逐字节基线，不做 `agent_mode` 双模式。

## 1. 目标模型

```
阳光 ──→ 蓝细菌（环境层，唯一光合者）
              │ 节点能量
              ▼
           核（消费者，唯一决策体）
              │ 探测 → 决策 → 行为
              ▼
        小球（器官，绕核旋转，能量存放在资源球内）
```

| 概念 | 角色 | 关键性质 |
| --- | --- | --- |
| `CyanoNode` | 环境生产者 | 光合注入、代谢耗散、繁殖、产球；节点间传输零和 |
| `Nucleus` | 消费者/决策体 | 携带遗传参数；捕食节点、回收小球、繁殖变异 |
| `Ball` | 器官 | 无决策能力；Resource/Scout/Shield 三型；绕核旋转 |
| 阳光 | 环境输入 | 昼夜 + 垂直梯度 + 噪声；核与球都不能光合 |

**能量流向**：阳光 → 蓝细菌节点 → 核 → 资源球 → 核。小球本身不产生能量。

## 2. 阶段与验收

### 阶段 1 文档纠正（无代码）
ARCH 删除"不包含 Ball/Nucleus"，加入 v2.3.1 对应关系与偏离记录，修正探测层 RNG 红线；
ROADMAP 插入 M4.5 与 M5/M6/M7；本文件；同步 HANDOFF、BALANCE、M2.5/M3/M4-PLAN、README。

验收：`tools/check.ps1` 通过；文档之间无矛盾。

### 阶段 2 环境层 `src/environment`

```cpp
struct CyanoNode {
    core::Vec2 pos{};
    double energy = 0.0;
    core::EntityId id = core::kInvalidEntityId;
    bool alive = true;
    core::Tick age = 0;
};
```

每 tick 顺序（对照 `Cyanobacteria::step`）：
1. 光合：`gain = photo * light * (1 - energy/cap)`，走 `ledger.injectPrimary`。
2. 代谢：`ledger.dissipate(..., CyanoMetabolism)`；能量耗尽则死亡。
3. 死亡清理：`ledger.kill(..., CyanoDeath)`。
4. 繁殖：能量 ≥ 阈值时按概率在 `offset` 距离内生成子节点，能量零和转移。

接口：`nodesNear(center, radius, out)` 复用 `SpatialIndex`，零分配；
`consumeNode(id)` 返回节点剩余能量并标记死亡，**不立即移除**（避免索引失效）。

验收：光合是唯一 `injectPrimary` 调用点；节点能量守恒；每 tick 审计通过。

### 阶段 3 核与小球 `src/colony`

删除 `src/agents` 与 `src/producers`。

```cpp
enum class BallType : std::uint8_t { Resource = 0, Scout = 1, Shield = 2 };

struct Ball {
    core::Vec2 pos{}, vel{};
    BallType type = BallType::Resource;
    core::EntityId ownerId = core::kInvalidEntityId;  // 0 = 自由球
    double energy = 0.0;   // 能量真实存放在球里
    double cap = 0.0;      // Resource 球贡献的储能上限
};

struct NucleusParams {     // M6 精简 8 项；M7 扩展到完整基因组
    double orbitRadius = 100.0;     // 单一轨道半径，所有球共用
    double affinity = 0.5;
    double maxSpeed = 60.0;
    double maxAccel = 6.0;
    double energyThreshold = 120.0;
    double mutationRate = 0.05;
    double harvestRate = 1.0;
    double spotRadius = 200.0;
};

struct Nucleus {
    core::Vec2 pos{}, vel{};
    double energy = 0.0;
    double nativeCap = 30.0;   // 不含资源球的基础容量
    core::EntityId id = core::kInvalidEntityId;
    core::Tick age = 0;
    NucleusParams params{};
    double wanderAngle = 0.0;
};
```

轨道物理（写在 `Behavior`，唯一写实体处）：

```
dir      = normalize(ball.pos - nucleus.pos)
dist     = |ball.pos - nucleus.pos|
tangent  = perpendicular(dir)
acc  = dir * (-affinity * (dist - orbitRadius) * radialK)   // 径向弹簧
acc += tangent * (affinity * tangentialK)                    // 切向驱动
acc += dir * ballRepel * (1 - dist / repelRadius)            // 球间排斥，同环散开
acc += nucleusRepel                                          // 近核排斥，避免塌陷
vel  = limit(vel * damping + acc * dt, maxBallSpeed)
```

**球没有任何个体力学参数**；`type` 只决定功能，不改变上式。

验收：单核单球下半径收敛到 `orbitRadius` ±20%、切向速度非零；
`--trace-json` 中球相对核的半径标准差 < 轨道半径的 20%。

### 阶段 4 球内能量 + 侦察采样

**审计恒等式扩展**：

```
injected - dissipated == Σ(核能量) + Σ(球能量) + Σ(节点能量)
```

新增 `EnergyReason`（末尾追加）：`BallCharge`、`BallDischarge`、`BallRecycle`、`BallDeath`、`CapOverflow`。

规则：
- 核 → 资源球：零和转账（`BallCharge`），受球 `cap` 限制。
- 资源球 → 核：零和转账（`BallDischarge`）。
- 回收球：少量转账入核（`BallRecycle`）+ 余量 `dissipate`。
- **球脱离归属只改 `ownerId`，能量不动**；带能量的球留在场上，可被任意核重新归属。
- `colonyCap = nativeCap + Σ(resourceBall.cap)`；超出立即裁剪并记 `CapOverflow`。

**侦察**：
- 每个侦察球独立采样：对候选实体算 `strength = max(0, 1 - d / spotRadius)`。
- 读数 = `value * strength + noise`；噪声来自 `Simulation` 的 PCG32，按固定球顺序消费。
- 核汇总所有侦察球报告，取最显著的 2 个节点与 2 个其他核写入 Observation。
- **无侦察球 ⇒ 外部通道全零**，核只能看到自身能量与边界。

验收：拆卸资源球后容量下降、超出部分被销毁且审计平衡；带能量的球脱离与被另一核拾取时总能量不变；
无侦察球时外部通道为零；同 seed 双跑哈希一致。

### 阶段 5 决策与繁殖

`Observation` 升到 v2（约 24 维）：自身能量比、储能比、三型球计数、最近节点 2 槽、最近他核 2 槽、
四向边界距离、昼夜相位。旧 v1 策略必须拒绝 v2 观测。

`Action` v2：`Move` / `Rest` / `Harvest`（捕食节点或回收球）/ `DeployBall`（消耗能量造球）。

`RulePolicy` 阈值全部取自 `NucleusParams`，保持纯函数、零随机（随机只在探测阶段注入）。

繁殖：能量 ≥ `energyThreshold` → 子核继承参数，按 `mutationRate` 扰动，按 `ParamRanges` 裁剪。

验收：5000 tick 后参数均值出现可测漂移；不同 seed 产生不同轨道特征；无球的核无法长期存活。

### 阶段 6 持久化、轨迹、门禁

- 快照升 **version 2**：核（含参数）、球（含能量与归属）、蓝细菌节点、阳光相位。v1 文件明确拒绝（退出码 5）。
- `trace::Body` 增 `ownerId`、`ballType`；新增 `c` 数组（蓝细菌）。前端契约只增不改。
- `stateHash` 覆盖：核参数与能量、球位置/能量/归属/类型、节点能量、账本全部科目。
- 重写 `tools/check.ps1` 依赖守卫（新增 `environment`、`colony` 层规则）。
- 重写受影响单测；新增对抗测试覆盖球脱离、拾取、超容量裁剪、无侦察、审计。
- 更新 `acceptance.ps1`、`m3_acceptance.ps1`、`m4_acceptance.ps1`。

验收：全套门禁通过；存档续跑逐位一致。

## 3. 变更清单

| 项 | 变更 |
| --- | --- |
| 新增目录 | `src/environment/`、`src/colony/` |
| 删除目录 | `src/agents/`、`src/producers/` |
| `Observation` | 28 维 v1 → 约 24 维 v2 |
| `Action` | 新增 `DeployBall` |
| `EnergyReason` | 末尾追加 5 项 |
| `EnergyLedger` | 审计覆盖核/球/节点三方 |
| 快照 | version 2 |

## 4. 风险与应对

| 风险 | 应对 |
| --- | --- |
| 单一 `orbitRadius` 使所有球挤在同一环 | 靠 `ballRepel` 散开；若视觉仍挤，按类型乘固定系数（仍由核参数决定） |
| 现有 35 个单测基于错误模型 | 就地重写，不保留双模型 |
| 球内能量使审计复杂 | 阶段 4 专项单测覆盖脱离/拾取/超容量 |
| 移植语义走样 | 逐函数对照 `legacy-v2.3.1` |
| 性能 | 节点自带网格；球只在行为层循环；阶段 4 后实测 500 核 + 5000 球 tick 耗时 |

## 5. 执行顺序

阶段 1 先落文档。阶段 2–5 每完成一个跑 `tools/check.ps1` 并报告门禁数据，通过后再进下一阶段。
阶段 6 收尾并提交。
