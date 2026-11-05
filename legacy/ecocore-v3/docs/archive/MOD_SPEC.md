# Mod 开发规格书（所有 mod 实现者必读）

本文件是本次「小球生态多样化 mod 开发」的**统一契约**。实现任何一个 mod 前必须完整阅读本文件与
`docs/MOD_DEV_GUIDE.md`。

---

## 0. 项目与目录

- 项目根：`D:\ScuStu\Q\自适应环绕核生态模拟与生存游戏\`
- 头文件目录：`src/`（**本会话沙箱可写区**）
- **新 mod 源码一律写到 `src/mods/`**，不要写 `mods/`（沙箱外）。
  整合阶段会由队长批量移动到 `mods/`。
- 目录 `src/mods/` 可能不存在，请先创建（write 工具会自动创建父目录）。
- 命名规范：文件 `src/mods/<snake_case_id>.cpp`，内部注册函数 `void register<PascalCaseId>(ModAPI&);`

---

## 1. 可用 API 全表（已由队长逐行核实）

```cpp
// src/mod_api.h:14
struct ModAPI {
    World&        world;
    EnergySystem& energy;
    std::mt19937& rng;
    EventBus&     events;
};
```

```cpp
// src/World.h:99-111
const std::vector<Ball>&    balls()  const;   // Ball{pos,vel,type,ownerId}
const std::vector<Nucleus>& nuclei() const;   // Nucleus{pos,vel,energy,alive,id,age,wanderAngle,params}
std::vector<Nucleus>&       nuclei();         // 非 const 版仅供 energy.apply 使用
int frame() const;
const WorldConfig& config() const;            // 见 src/World.h:14-83
int aliveNucleusCount() const;
double averageNucleusEnergy() const;
bool finished() const;
const char* finishReason() const;
```

```cpp
// src/EnergySystem.h:28
double EnergySystem::apply(Nucleus& n, double delta, EnergyReason why);
// 内部：n.energy += delta；energy<=0 时置 n.alive=false 并 emit NUCLEUS_DIED
```

```cpp
// src/EventBus.h:9-17, 33
void subscribe(EventType type, std::function<void(const SimEvent&)> fn);
enum class EventType { FRAME_START, FRAME_END, BALL_ABSORBED,
                       NUCLEUS_BORN, NUCLEUS_DIED, NUCLEUS_REPRODUCED, PLAGUE_TICK };
struct SimEvent { EventType type; int frame; const Nucleus* nucleus;
                  const Ball* ball; double value; const void* extra; };
```

```cpp
// src/Nucleus.h:6-15
struct Nucleus { Vec2 pos; Vec2 vel; double energy; bool alive; int id;
                 int age; double wanderAngle; NucleusParams params; };
// src/Ball.h:6-11
struct Ball   { Vec2 pos; Vec2 vel; BallType type; int ownerId; };
// src/BallType.h:4-11
enum class BallType { Shield=0, Worker=1, Scout=2 };
constexpr int BALL_TYPE_COUNT = 3;
// src/NucleusParams.h:38-50
struct NucleusParams {
    std::array<double,3> affinity;         // 和归一化为 1
    std::array<double,3> orbitRadius;      // 各类型环绕半径
    std::array<double,3> absorbPreference; // 吸收各类型的能量倍率
    std::array<double,3> repelStrength;
    double attackRange, attackStrength, avoidRange, avoidStrength;
    double maxSpeed, energyThreshold, mutationRate;
};
```

### 关键 WorldConfig 字段（`src/World.h:14-83`）
`width/height`、`seed`、`initialBalls=1000`、`initialNuclei=20`、`maxFrames=100000`、
`maxNuclei`（<=0 时按 `width*height/territoryPerNucleus=4000` 自动算，默认 2000×2000 ⇒ **1000**）、
`absorbRadius=30`、`ballSpawnProbability=0.01`、`baseBallEnergy=1.0`、
`ballLossCost=5`、`basalCost=0.01`、`speedCostK=0.0001`、`nucleusMinSeparation=40`、
`newbornGraceFrames=30`、`reproductionCostRatio=0.8`、`childEnergyRatio=0.5`、
`nucleusInitEnergyRatio=0.7`、`forageRadius=300`、`detachRadius=400`。

---

## 2. 事件精确时机（队长已核实 `src/World.cpp:80-118`）

`World::step()` 的执行顺序：

| # | 行号 | 动作 |
|---|------|------|
| 1 | World.cpp:82-84 | 所有 alive 核 `++age` |
| 2 | :85 | `buildGrid()` |
| 3 | :86 | `updateBallDynamics()` — 球受力、积分、限速、边界反弹 |
| 4 | :87 | `updateOwnership()` — 归属重算；换主时对**旧主**扣 `ballLossCost` |
| 5 | :88 | `absorbBalls()` — swap-pop 删除被吸收的球 |
| 6 | :90-97 | emit `BALL_ABSORBED`（`value`=本帧吸收总能量，`extra`→`int*` 球数）。**仅当本帧有吸收才发** |
| 7 | :98 | `nucleusCombat()` — 能量转移 |
| 8 | :99 | `nucleusMovement()` — 两阶段：算加速度 → 积分+代谢扣能量 → 硬分离 |
| 9 | :100 | `reproduce()` — push_back 子核，emit `NUCLEUS_BORN`（`nucleus` 指向 `nuclei_.back()`） |
| 10 | :101 | `removeDeadNuclei()` — **swap-pop**，索引会失效 |
| 11 | :102 | `replenishBalls()` — **push_back 小球，会使 balls() 的引用失效** |
| 12 | :103 | `++frame_` |
| 13 | :104 | `checkFinish()` |
| 14 | :106-111 | emit `FRAME_START`（`e.frame = frame_`，即**新帧号**） |
| 15 | :112-117 | emit `FRAME_END`（`e.frame = frame_`） |

**重要推论：**
- `FRAME_START`/`FRAME_END` 都发生在**所有物理与死亡移除之后**。回调里 `nuclei_` 已无死亡残留。
- `NUCLEUS_DIED` 由 `EnergySystem::apply` 内部 emit（`src/EnergySystem.cpp:12-19`），`e.frame` 恒为 0。
- `NUCLEUS_BORN` 的 `e.nucleus` 指向子核，`age==0`，`params` 已变异完。
- `NUCLEUS_REPRODUCED` / `PLAGUE_TICK` **当前代码中从未被 emit**（已 grep 确认），可安全用作内部标记语义。

---

## 3. 红线（违反即判定失败）

1. **不改核心**：`src/World.*`、`src/NucleusParams.*`、`src/EnergySystem.*`（除队长指定的枚举追加）、
   `src/BallType.h`、`src/ParamSchema.*` 一律不得修改。
2. **不改 22 个遗传参数**的语义与裁剪范围。mod **可以读** `params`，
   **绝对不可以写** `n.params.xxx`。
3. **确定性**：随机只允许 `api.rng`（`std::mt19937&`）。禁止 `rand()`/`random_device`/`time()` 种子/
   `<chrono>`。禁止 `static` 初始化里的随机。禁止依赖 map 迭代顺序做随机抽样。
4. **只经 `energy.apply` 改能量**。不得写 `n.energy = x`、`n.energy += x`。
5. **不得写 world 实体字段**：`n.pos/vel/alive/id/age/wanderAngle/params`、`b.pos/vel/type/ownerId`
   全部只读。**唯一允许的写入是 `energy.apply` 造成的 `n.energy` 变化与随之而来的 `n.alive=false`。**
6. **跨帧追踪用 `Nucleus::id`**，绝不用数组下标。`removeDeadNuclei()` 用 swap-pop，
   下标每帧都可能变；`id` 由 `nextNucleusId_++` 单调分配、**永不回收**。
7. **性能**：帧级回调里禁止对核做 O(N²) 配对。需要邻近查询时自建轻量空间分桶
   （`cellSize` 建议 ≥ 200，因为核间距被 `nucleusMinSeparation=40` 与 `forageRadius=300` 量级约束）。
8. **零警告**：`-Wall -Wextra` 下不得有任何警告。注意：
   - 有符号/无符号混比（`int` vs `size_t`）——循环用 `size_t`，比较前显式 `static_cast`。
   - 未使用的参数——用 `(void)param;` 或省略参数名。
   - 浮点比较——不要用 `==` 判浮点。
9. **零和原则（最重要）**：凡是「A 得、B 失」的机制，总能量必须守恒。
   转移用 `apply(a, -x, R); apply(b, +x, R);` 两次调用，**x 固定**，
   不要用 `apply` 的返回值做二次传递（返回的是扣减后的值，会引入不对称）。
10. 每个 mod 必须**可单独关闭**：不启用任何 mod 时模拟输出必须与基线逐字节一致
    （mod 代码在未注册时完全不执行，所以只要不写 `static` 全局可变状态就不会污染）。

---

## 4. 队长的枚举扩展（**必须使用下列已分配名字，不要自己造**）

`EnergyReason` 将在 `src/EnergySystem.h` 末尾追加以下枚举项（由队长统一改，你只管用）：

| 枚举名 | 分配给 |
|--------|--------|
| `SUBSIDY`  | M1 稀有球型保育补贴 |
| `GUILD`    | M2 生态位行会 |
| `SYMBIOSIS`| M4 核间共生 |
| `SEASON`   | M5 季节轮回 |
| `TERRITORY`| M6 失球缓冲 |
| `SPILL`    | M7 能量溢出再分配 |

可直接复用、**不要重新定义**的既有枚举：
`ABSORB, COMBAT, METABOLISM, REPRODUCTION, AGING, CROWDING, PLAGUE, BALL_LOSS`

> 你在本地写代码时 `EnergyReason::SUBSIDY` 之类**暂时编译不过**是正常的，
> 队长整合阶段会补上枚举。你**不要**自己去改 `src/EnergySystem.h`。
> 为了能在本地自检，你可以在文件顶部写：
> ```cpp
> // TODO(整合时删除): 本地单文件自检用的占位，等待队长补入 src/EnergySystem.h
> ```
> 并且**不要**在文件里重新定义枚举（会造成与队长的定义冲突）。

---

## 5. 登记（**你不要做，队长统一做**）

你只交付 `src/mods/<id>.cpp` 一个文件 + 文件头注释块。
文件头注释块必须包含以下小节（这是启动器 Mod 页要给用户看的说明）：

```
// ============================================================
// mod id:        <id>
// 名称:          <中文名>
// 机制:          <2~4 行说明，含关键参数与默认值>
// 平衡作用:      <它修正了生态系统的哪个失衡>
// touches:       <逗号分隔的 EnergyReason 名>
// incompatible:  <逗号分隔的 mod id，没有就写空>
// 依赖:          <无 / 依赖哪些 mod>
// ============================================================
```

---

## 6. 验收标准

- [ ] 只产出 1 个（或指定的 2 个）`.cpp` 文件，位于 `src/mods/`
- [ ] 零第三方依赖，只 `#include "mod_api.h"` 与 `"World.h"`（需要时加 `<cmath>` `<algorithm>` `<vector>` `<map>` `<unordered_map>`）
- [ ] 不修改 `src/` 下任何既有文件
- [ ] 文件头注释块 7 个小节齐全
- [ ] 内部状态用 `static` 结构体持有，且**提供**（不需要调用）`saveXxx(std::ostream&)` /
      `loadXxx(std::istream&)` 两个自由函数（内部状态若为空可省略并说明）
- [ ] 代码中所有随机都走 `api.rng`，并有注释标注用途
- [ ] 零和机制的两次 `apply` 调用使用**同一个** x
- [ ] 跨帧状态用 `Nucleus::id` 做 key，定期清理已死亡 id（避免 map 无限增长）
- [ ] 文件末尾说明该 mod 单独启用时的**预期可观测效果**（跑 `--energy-summary` 应该看到哪个枚举的 in/out 非零）

**你可以做**：`write` 工具写文件、`read`/`grep` 工具读源码。
**你不可以**：运行 g++ 编译（PowerShell 在本沙箱被禁）、修改 `src/` 下既有文件、改 `mods_registry.cpp`。
