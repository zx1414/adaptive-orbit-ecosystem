# 下一版生态机制 · 实施契约（Lead 维护，2026-11-25）

本文件是并行改造的**唯一接口契约**。规则来源：`docs/下一版生态机制设计.md` +
`docs/生态机制改造执行提示词.md`。契约由 Lead 维护；同事实现时只读，需要改动先在
汇报里提。

## 0. 规则版本

- 新增常量 `kRulesVersion = 2`（`src/core/Config.h`），语义 = "P0–P2 生态规则"。
- 存档头写入规则版本；旧档（version < 2）在读取时**提示版本并拒绝**，绝不静默转换、
  绝不改写原件。新档 root 名加后缀以便人眼辨别。

## 1. P0-1/2/3 食物竞争（**Lead 负责**：environment + sim + behavior + core）

删除（连同 CLI/运行时入口）：
- `Config::nucleus.migrationBonus` / `migrationRadius`（迁移回报）
- `CyanoParams.grazeRefugeEnergy`（每节点不可食保底）
- `CyanoParams.conversionMode/Exponent/SpeedRef/AccelRef/WSpeed/WAccel`（按最高速度折减
  转化率的入口）→ 生产路径改用 `rate = clamp(conversionRate * genome.cyanoDigestion,
  conversionMinRate, conversionMaxRate)`，与速度/加速度无关。

新增（`CyanoParams`）：
| 字段 | 默认 | 语义 |
|---|---|---|
| `minStructure` | 8.0 | 节点最小存活结构量（material 低于它 → 死亡，残骸留在原地） |
| `maxMaterial` | 120.0 | 单节点组织上限（自遮光之外的第二重密度约束） |
| `energyPerMaterial` | 0.6 | 形成 1 单位组织消耗的能量 |
| `nutrientPerMaterial` | 1.0 | 形成 1 单位组织需要的养分 |
| `growthPerTick` | 0.6 | 每帧最多吸收/形成的组织量 |

`CyanoNode` 加字段 `double material`（组织，可食量口径）。`energy` 保留为可支配光合产物。

### 摄食批结算（A2 的落点）
```cpp
struct GrazeRequest { EntityId nucleusId; EntityId nodeId; double wantMaterial; double efficiency; };
struct GrazeGrant   { EntityId nucleusId; EntityId nodeId; double material;
                      double energyTransferred; double energyAbsorbed; };
std::vector<GrazeGrant> CyanoField::grazeBatch(std::vector<GrazeRequest> reqs);
```
- 先按 `(nodeId, nucleusId)` 升序排序（结果与请求遍历顺序无关 → A2 对齐后一致）。
- 每节点 `available = max(0, material)`；`totalWant = Σ want`。
- `totalWant <= available` → 全部满足；否则 `share_i = available * want_i / totalWant`。
- 从节点扣的 material 之和 = 实际分配之和；节点的 energy 按 `share/material` 比例扣减。

## 2. P0-4 个体记忆（**Teammate A 负责**：src/decision/**）

```cpp
// src/decision/PolicyMemory.h —— **必须 POD**：只含 double/int/固定数组，
// 无指针、无 vector/string，可直接 memcpy 语义序列化（存档按字段写读）。
struct PolicyMemory { ... };
```
- `IDecisionPolicy` 主接口改为
  `virtual ActionVector decide(const SensorVector&, const TargetList&, PolicyMemory&) = 0;`
  具体策略**不得**再有跨 decide 存活的实例可变成员；不可变参数可在构造期持有。
- 保留两参兼容重载 `decide(sv, targets)`：每次调用传入函数内新建的临时
  `PolicyMemory{}`（一次性无状态语义，注释写明），两参`decide(sv)` 同理。
- 组合根调用点：`policy_->decide(sv, targets, n.memory)` —— `Nucleus.memory` 由 Lead 接线；
  A 不要改 AgentBehavior.h / Simulation.cpp。
- 新生核记忆默认构造（不继承亲代）；存档恢复每个核**自己的** memory。

## 3. P0-5 玩家干预（**Teammate B 负责**：src/app/** + frontend/**）

- 普通玩法撤下：`harvest`（全体 +8）、`plague`（富核 -12）、`bloom`（直接追加节点）、
  `winter` 的"扣能量"语义。
- `winter` 改为**有持续时间的环境效果**：降低光照基线（`sun.base`）与/或温度，
  到期恢复；期间玩家改的基础环境设置仍然有效（实现要点：把"玩家基础值"与
  "事件临时偏移"分开存，到期只撤偏移）。
- `bloom` 不再由玩家直接追加节点；玩家播种是独立显式输入（保留 `/env cyano seed`
  路径若已存在，或明确标注为"外部输入"）。
- 过期/未知命令必须有明确反馈（HTTP 4xx/JSON error 或命令回执）。
- 前端源码改动后必须 `node frontend\tools\bundle.mjs` 重新生成 `frontend/agentsim.html`，
  并跑 `node frontend\tools\smoke.mjs`。

## 4. P1 物质循环（**Lead 负责**）

```cpp
// src/environment/NutrientField.h —— 环境层，纯确定性、零 RNG、不碰能量账本
struct NutrientCell { double debris = 0.0; double nutrient = 0.0; };
class NutrientField {
    NutrientField(double worldW, double worldH, double regionSize, double initialPerRegion);
    const NutrientCell& cellAt(Vec2 p) const;
    double takeNutrient(Vec2 p, double want);   // 返回实际取得
    void addNutrient(Vec2 p, double amount);
    void addDebris(Vec2 p, double amount);
    double decompose(double rate);              // debris → nutrient，返回本帧转化量
    double totalDebris() const; double totalNutrient() const; double total() const;
};
```
物质守恒（A7）：`Σ node.material + Σ nucleus.bodyMass + Σ debris + Σ nutrient` 恒定，
仅显式施肥/播种/开局注入改变。能量账本独立闭合（残骸/养分不注入能量）。

## 5. P2 器官（**Lead 负责**）

- 撤去蓝细菌产球（`CyanoParams.ballSpawn*`、`CyanoStepReport.ballsProduced`）。
- 侦察球：按**球的实际世界坐标**在 `scoutSenseRadius` 内采样可食节点，经
  `scoutControlRadius`（有效控制距离）回传，环面最短距离，合并重复目标；
  资源球不提供视野。
- 资源球：核超容富余能量转入球（容量 `Ball.cap`、存量 `Ball.energy` 分开），缺粮时按
  `kResourceBallWithdrawRate` 取回；脱离/拾取/死亡/回收有明确去向；删掉换主罚款
  （`BallFieldParams.lossCost` 的使用点）。
- 造球：核以自身能量+物质建造，占用可繁殖资源；新生代第一批器官由核自建。

## 6. 并行纪律

- **构建隔离**：默认 `build/` 归 Lead。其他人用
  `powershell -File %TEMP%\agentsim-tools\pbuild.ps1 -Tag <你的名字>`，
  产物落在 `build\<tag>\`，互不覆盖。
- 单测同理：`build\<tag>\unit_tests.exe`。
- 不要 `git commit / checkout / reset / clean`；不要动 `release/`、`legacy/`。
- 报告与临时产出写 `docs/`（报告）或 `%TEMP%`（探针），不要在仓库根堆积。
- 每次汇报给出：改了哪些文件、跑过什么命令、结果（原始关键行）、未验证项。
