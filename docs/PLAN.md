# 自适应生态系统 —— 实施计划（Phase 0：轻量 Mod 基础架构）

> 本文档是给编码 agent 的【可执行规格】：按第 3 节「实施步骤」顺序完成后，
> 运行第 5 节「验收清单」全部通过，即 Phase 0 完成。
> 本阶段只做基础架构，不实现任何生态机制；后续阶段见第 8 节路线图（仅预告，本次不做）。

---

## 1. 目标

搭建轻量 Mod 基础架构，为后续「生态机制 Mod 化」与「可视化扩展」留口：

1. EventBus：类型化事件发布/订阅。
2. EnergySystem：核能量收支的唯一通道（原因标签 + 按原因统计 + 统一死亡判定）。
3. ModAPI + registerMod：轻量 Mod 接口（无基类、无配置解析器），开发者写一个函数即可做 mod。
4. 示例 mod：periodic_drain（周期性扣能量），默认不注册（保证默认行为不变）。
5. IRenderer 接口：现有 Renderer 改造为 ConsoleRenderer : IRenderer，为未来 GUI/Web 渲染留口。
6. README 新增「Mod 开发者指南」。
7. 回归保障：不注册任何 mod 时，同种子重构前后模拟输出完全一致。

---

## 2. 现状与约束（动手前先读代码）

- 语言/约束：C++17，仅 STL，无第三方依赖；控制台程序，无图形库。
- 实体：Ball（Shield/Worker/Scout 三类，无决策）；Nucleus（22 个可遗传参数 + energy + alive + 稳定 id）。
- 已有能力：物理核心（小球力场/核移动）、演化（遗传/变异/选择）、SpatialGrid（5000球/500核无渲染 ≥60 FPS）、确定性（mt19937 + 帧内固定迭代顺序）、Renderer（80×40 网格 + 清屏/窗口锁定）、CSV 采样（balls_*/nuclei_*/trend/survivors）、env_config.txt 自动加载、scripts/ 批量聚类。
- 文件结构（src/ 下）：Vec2、BallType、Ball、NucleusParams、Nucleus、World、SpatialGrid、Renderer、Sampler、ConfigLoader、EnvConfig、main（.h/.cpp 成对）。构建用 CMakeLists.txt；也可 g++ -std=c++17 -O2 src/*.cpp。
- World::step() 现有顺序：buildGrid → updateBallDynamics → updateOwnership → absorbBalls → nucleusCombat → nucleusMovement → reproduce → removeDeadNuclei → replenishBalls → ++frame → checkFinish。
- 必须保持：22 遗传参数含义与 CSV 列顺序不变；物理系数/CLI 语义/env_config 自动加载逻辑不变；确定性；性能目标（5000球/500核无渲染 ≥60 FPS）。

---

## 3. 实施步骤（按顺序执行，每步完成后先编译再进下一步）

### Step 1：EventBus

新建 src/EventBus.h / src/EventBus.cpp。

    enum class EventType {
        FRAME_START, FRAME_END,           // 帧级事件
        BALL_ABSORBED,                    // 吸收（按帧聚合发射）
        NUCLEUS_BORN, NUCLEUS_DIED,       // 子核诞生 / 死亡
        NUCLEUS_REPRODUCED, PLAGUE_TICK   // 本阶段定义，暂不发射（预留给后续 mod）
    };

    struct SimEvent {
        EventType type;
        int frame = 0;
        const Nucleus* nucleus = nullptr;
        const Ball* ball = nullptr;
        double value = 0.0;       // 如吸收总量、扣减量
        const void* extra = nullptr;
    };

    class EventBus {
    public:
        void subscribe(EventType, std::function<void(const SimEvent&)>);
        void emit(const SimEvent&);
    };

实现要求：每事件类型一个 listener 向量；subscribe 追加（保持插入顺序）；emit 按插入顺序遍历。
加入 CMakeLists.txt。

### Step 2：EnergySystem

新建 src/EnergySystem.h / src/EnergySystem.cpp。

    enum class EnergyReason {
        ABSORB, COMBAT, METABOLISM, REPRODUCTION, AGING, CROWDING, PLAGUE
    };

    class EnergySystem {
    public:
        explicit EnergySystem(EventBus& bus);
        double apply(Nucleus& n, double delta, EnergyReason why);  // 唯一入口
        void printSummary(std::ostream& os) const;                 // 调试：按 reason 汇总
    };

apply 行为：
1. n.energy += delta。
2. 按 reason 累计统计：delta>0 记 in，delta<0 记 out（每帧清零，累计保留）。
3. 若 n.alive 且 n.energy <= 0：n.alive = false，并 emit NUCLEUS_DIED（nucleus=&n）。

### Step 3：World 能量迁移（风险最高，先做基线再动）

迁移前：用固定种子（如 --seed 42）跑相同命令，保存基线输出（survivors.csv 与某几个 nuclei_*.csv 的哈希或内容），供回归对比。

给 World 增加成员：EventBus events_; EnergySystem energy_{events_};
并添加公开访问器：EnergySystem& energy(); EventBus& events();（rng() 已存在）。

把 World.cpp 中所有直接修改 n.energy 的代码改为 energy_.apply(...)：

1. absorbBalls：n.energy += gain → energy_.apply(n, gain, EnergyReason::ABSORB)。
2. nucleusCombat：a.energy += delta 与 b.energy -= delta → apply(a, +delta, COMBAT) 与 apply(b, -delta, COMBAT)；删除原有两处 alive=false 判定（apply 已统一处理）。
3. nucleusMovement 代谢块：n.energy -= loss → apply(n, -loss, METABOLISM)；删除原有 alive=false 判定。
4. reproduce：父核 n.energy -= threshold*cost → apply(n, -cost, REPRODUCTION)；删除原有 alive=false 判定；子核 child.energy = threshold*childRatio 保持直接赋值（新核初始能量，不会触发死亡）。

注意：apply 在每次调用后立即判死，与旧代码「同帧末尾判死」对同一核的值完全相同，行为必须一致（用基线对比验证）。

### Step 4：ModAPI + 注册表 + 示例 mod

新建 src/mod_api.h：

    #pragma once
    #include <random>
    #include "EventBus.h"
    #include "EnergySystem.h"

    class World;

    struct ModAPI {
        World&        world;    // 读状态（约定：只读；要改能量必须走 energy.apply）
        EnergySystem& energy;   // 写能量（唯一通道）
        std::mt19937& rng;      // 随机（种子驱动，保证可复现）
        EventBus&     events;   // 事件（订阅即回调）
    };

    using ModFactory = void (*)(ModAPI&);

新建 src/mods_registry.h / src/mods_registry.cpp：

    // mods_registry.cpp —— 启用/添加 mod 的唯一地方
    #include "mod_api.h"
    void registerPeriodicDrain(ModAPI&);   // 示例 mod 声明

    void registerAllMods(ModAPI& api) {
        // registerPeriodicDrain(api);   // 默认注释掉；启用某 mod 就取消注释
    }

新建 mods/example_periodic_drain.cpp（示例，默认不注册）：

    #include "mod_api.h"
    #include "World.h"

    void registerPeriodicDrain(ModAPI& api) {
        api.events.subscribe(EventType::FRAME_END, [&](const SimEvent& e) {
            if (e.frame > 0 && e.frame % 100 == 0) {
                for (auto& n : api.world.nuclei()) {
                    if (n.alive) api.energy.apply(n, -10.0, EnergyReason::PLAGUE);
                }
            }
        });
    }

main.cpp 接线：World world(cfg) 创建并 initialize() 后，构造 ModAPI api{world, world.energy(), world.rng(), world.events()}，调用 registerAllMods(api)。

构建集成：把 EventBus.cpp、EnergySystem.cpp、mods_registry.cpp 加入 CMakeLists.txt；mods/*.cpp 用显式文件列表或 file(GLOB)（二选一并在 CMakeLists 注明）。

### Step 5：IRenderer 接口化

新建 src/IRenderer.h：

    class IRenderer {
    public:
        virtual ~IRenderer() = default;
        virtual void render(const World&) = 0;
    };

把 Renderer 改为 ConsoleRenderer : IRenderer（文件可改名为 ConsoleRenderer.h/.cpp 或在原文件内改名类），逻辑不变：80×40 网格、setClear、窗口锁定、底部统计。
main 改为持有 std::unique_ptr<IRenderer>，默认构造 ConsoleRenderer(cfg.gridCols, cfg.gridRows) 并 setClear(cfg.clearScreen)。
约束：World 不得 include 任何 Renderer；渲染器只通过 World 的 const 访问器读数据。

### Step 6：事件发射时机（在 World::step() 中，保持主循环可读）

固定顺序：
1. ++frame_ 之后：emit FRAME_START。
2. absorbBalls 结束后：若本帧有吸收，聚合 emit 一次 BALL_ABSORBED（value=本帧总吸收能量，extra 可放计数）。
3. NUCLEUS_DIED 由 EnergySystem::apply 内部发射（无需在 World 里额外写）。
4. reproduce 中每次 push_back 子核后：emit NUCLEUS_BORN（nucleus=&子核，在 push_back 之后取指针）。
5. step() 末尾：emit FRAME_END。

### Step 7：README 文档

README.md 新增「Mod 开发者指南」章节，包含：
- ModAPI 四个字段的说明（world 只读约定、energy 唯一写通道、rng 确定性要求、events 订阅）。
- 如何写一个 mod：一个 registerMod 函数 + 完整示例代码。
- 如何启用：在 mods_registry.cpp 取消对应行的注释；如何新建：新建 mods/xxx.cpp + 声明 + 注册一行。
- 确定性注意事项：mod 内禁止使用未种子随机，一律用 api.rng。
- 强调：不注册任何 mod 时行为与旧版完全一致。

---

## 4. 边界与依赖方向（写进代码注释）

- 物理核心（World 的运动/物理代码）：不订阅事件、不直接产生能量效果，只算力与运动。
- Mod：只通过 EventBus 读事件、通过 EnergySystem 改能量、通过 world 读状态（约定不直接改 n.energy 等）。
- 依赖方向：Mod → EventBus/EnergySystem → World 核心。禁止反向依赖（核心不 include mod、不 include 渲染器）。
- 新机制 = 新 mod 文件 + 新 EventType（如需）+ 新 EnergyReason（如需）+ mods_registry 一行；不修改物理核心。

---

## 5. 验收清单（每条给出验证方法）

- [ ] g++ -std=c++17 -O2 -Wall -Wextra 零警告；cmake 可构建（给出构建命令与结果）。
- [ ] 默认（不注册任何 mod）同种子重构前后输出一致：用 Step 3 的基线对比（survivors.csv 哈希一致）。
- [ ] 示例 mod：在 mods_registry.cpp 取消注册注释后，核周期性扣能量（用 EnergySystem 摘要或采样对比验证）；恢复注释后行为复原。
- [ ] EnergySystem::printSummary 能输出各原因收支摘要（提供触发方式：临时打印或加调试开关）。
- [ ] IRenderer 抽象成立，ConsoleRenderer 渲染/清屏/窗口锁定不回归（跑渲染模式肉眼/截图对比）。
- [ ] README「Mod 开发者指南」存在，且按示例可写出并启用一个 mod。
- [ ] 5000球/500核/无渲染帧率 ≥60 FPS（与重构前对比，给出前后数字）。

---

## 6. 明确不做（防范围蔓延）

- 不实现任何生态机制（衰老/拥挤/瘟疫效果/抗病/繁殖倾向/适应度/血脉等）。
- 不做 rules.txt 配置解析、脚本引擎（Lua 等）、热重载、网络/UI/存档/编辑器。
- 不改物理核心行为、22 遗传参数含义、CLI 参数语义、env_config.txt 自动加载逻辑。
- 不引入任何第三方库。

---

## 7. 扩展点清单（后续阶段使用）

- 新生态机制 = 新 mod 文件 + 新 EventType（如需）+ 新 EnergyReason（如需）+ mods_registry 一行注册。
- 新事件类型 = EventType 枚举加一项（不破坏既有订阅）。
- 新渲染器 = 实现 IRenderer 并在 main 按配置选型。
- 可视化扩展 = 用 FRAME_END 等事件订阅（与 mod 同通道）。
- 未来若需纯配置 DIY = 可选加一层「rules.txt → 生成/组合 mod」，不影响本接口。

---

## 8. 后续阶段路线图（仅预告，本次不做）

| Phase | 内容 | 验证标准（预告） |
|-------|------|-----------------|
| 0.5 核-球群动力学与物理修复 | 跟随加速 + 失球惩罚（失去球扣能量）+ 贴脸/新生修复 | 快核不甩球；贴脸稳定；新核有保护期；详见第 10 节 |
| 1 演化参数 | mutationCoeff / reproductionTendency / fitness + bloodline + age；CSV/脚本同步 | 聚类多样性（角色数 > 1） |
| 2 生命史 | 衰老（随 age）+ 环境安静指数（密度→补充概率） | 总能量不再无限涨；密度自适应资源 |
| 3 多样性维持 | 拥挤度惩罚（策略参数分箱，O(N)） | 冷门策略存活率上升 |
| 4 时间扰动 | 瘟疫（血脉段打击）+ 抗病性（∝速度） | 快/慢核随瘟疫周期轮替占优 |
| 5 规则化 | （可选）rules.txt 纯配置 DIY 层 | 玩家可在 rules.txt 开关/调参机制 |

---

## 9. 风险与注意事项

- 重构回归（最高风险）：Step 3 能量迁移后必须同种子对比基线；先迁移、验证、再进 Step 4。
- 性能：BALL_ABSORBED 必须按帧聚合发射（一个事件），禁止每球一个事件；帧事件每帧仅 2~3 个。
- 确定性：所有新增随机一律走 World 的 mt19937；事件发射顺序固定。
- CSV：本阶段不新增列；后续阶段列只能追加在末尾，不得重排。

---

## 10. 核-球群动力学与物理修复（设计定稿，Phase 0.5）

> 背景：核 maxSpeed 遗传上限可达 300，而球速上限仅 25/50/80，核一加速就会甩掉自己的球群；
> 另有两个运动 bug：核近距离重叠/抖动、新核出生即被父核攻击。

### 10.1 跟随加速（方案 A：球跟核，不限制核速）

- 归属球的限速 = max(类型上限, 其核当前速度 × follow_boost)。
- 效果：核照常跑快，归属球总能跟上，不被甩掉。
- 配置：follow_boost = 1.5（0 = 关闭，恢复纯按类型限速）。
- 实现位置：updateBallDynamics 的球限速处（查归属核 vel）。

### 10.2 失球惩罚（方案 C：失去球会扣能量）

- 归属球距其核超过 detach_radius → 变为自由球（脱附）。
- 每当核「失去」一个归属球（脱附 / 被其他核抢走），核扣除 ball_loss_cost 能量（EnergyReason 新增 BALL_LOSS）。
- 效果：核可以跑快，但把球甩出 detach_radius 就要付能量代价 —— 球是「累赘」，甩掉 = 亏能量；快核要么少带球、要么别甩球。
- 配置：detach_radius = 400，ball_loss_cost = 5.0。
- 实现：维护每球「上一帧归属」数组（随球增删同步）；在 updateOwnership 检测归属变化，对失去方 apply(-ball_loss_cost, BALL_LOSS)。

### 10.3 修复：核近距离运动异常

- 根因：两核可完全重叠，攻击力在 dist→0 时最大 → 无限对冲、剧烈抖动。
- 方案：nucleus_min_separation = 40（dist < 40 时施加强斥力推开，并跳过攻击能量转移）；nucleus_repel_k = 4.0。
- 结果：两核在约 40 距离对峙战斗（能量转移照常），不再重叠/抖动。

### 10.4 修复：刚繁殖时异常（父-子出生即互打）

- 根因：子核出生在父核 ±20 内，天然处于攻击范围。
- 方案一（间距）：出生时若距父核 < nucleus_min_separation，把子核外推到该距离。
- 方案二（保护期）：newborn_grace_frames = 30 —— 新核出生后 30 帧内不主动攻击、也不被攻击（可逃跑/站稳）。
- Nucleus 增加 age 字段（每帧 +1，供保护期判定；也为后续「衰老」阶段铺路）。

### 10.5 新增配置键汇总

follow_boost、ball_loss_cost、detach_radius、nucleus_min_separation、nucleus_repel_k、newborn_grace_frames

### 10.6 验证

- 快核(200) + 护盾球(25)：归属球与核的最大距离有界（跟随生效）。
- 两核贴脸：稳定对峙、不重叠、不抖动。
- 新核：30 帧保护期内不被攻击；之后正常。
- 默认配置下与旧行为一致（除修复项外）；确定性保持（新增随机一律走 rng_）。

