# 用户修正 R1（2026-11-01 01:10）—— 覆盖 V2-SPEC.md 中所有冲突处

本文件优先级**高于** V2-SPEC.md。冲突时以本文件为准。

## 一、删除（整条链路不要）

1. **轨迹拖尾**：前端不得画任何拖尾/残影。球、核、蓝细菌一律不留历史位置。
2. **影响力**：不要影响力预算系统。没有上限、没有开局值、没有每帧恢复、没有扣费、没有"不足则拒绝"。玩家调控**免费且无限**。旧版 EnvControl 的 kInfluence* 常量全部作废。
3. **核战斗**：不要核与核之间的能量转移/攻击/伤害/吸血。旧版 `World::nucleusCombat` 整段丢弃，agentsim 不实现。**战斗相关的 genome 字段（attackRange/attackStrength）也不要有。**

## 二、替代设计

### 1) 对核天然参数（遗传）+ 决策层有限调制

每个核遗传一组"对其他核的天然参数"（这是**社会参数**，不是战斗参数）：

| 字段 | 含义 | Init 范围 | Clamp 范围 |
|---|---|---|---|
| `nucleusSightRadius` | 察觉其他核的距离 | 40 ~ 300 | 10 ~ 600 |
| `nucleusPersonalSpace` | 个人空间半径（小于它施加斥力） | 15 ~ 80 | 5 ~ 200 |
| `nucleusRepelStrength` | 斥力强度 | 0.5 ~ 6.0 | 0.1 ~ 20 |
| `nucleusCohesion` | 向核群聚拢倾向（0 = 独行） | 0 ~ 3.0 | 0 ~ 10 |

物理只做三件事：**斥力 + 聚拢 + 硬分离**。没有任何能量转移。

**决策层可以一定程度改变它**：ActionVector 新增 `mod[4]`，每项 ∈ [-1,1]。行为层算本帧生效值：
```
eff[i] = base[i] * (1 + kModLimit * mod[i]),   kModLimit = 0.4   // ±40% 上限
```
**基因本身不被改写**（表型可塑，不是拉马克式继承），不消耗能量。调制是瞬时的、每帧重算。

### 2) 花能量给指定的球施加加速度

- 核有一个遗传的"控制范围" `forceRange`（Init 30~150，Clamp 10~400）
- 决策层每帧可以**点名单个球** + 给一个加速度矢量
- 行为层校验：(a) 球在 `forceRange` 内 (b) 核能量付得起
- 代价 `cost = forceCostK * |a|^2`（`forceCostK` 遗传，Init 0.005~0.05，Clamp 1e-4~1.0），走 `ledger.dissipate("applyForce")`
- **没有点名 = 不花能量**。现有 `pushFreeBall` 的随机触发**删除**（那是决策层缺失时的占位，现在是死代码候选）
- 可对任何球施力（含 ownerId==0 的自由球）

### 3) 接口契约变更（`src/core/SensorTypes.h`）

```cpp
constexpr int kMaxForceTargets = 8;

struct ForceTarget {
    EntityId id = 0;
    double dx = 0.0, dy = 0.0;  // 相对核的最近像环面位移（不是核→球直线）
    double dist = 0.0;
};
struct TargetList {
    int count = 0;
    ForceTarget items[kMaxForceTargets];
};

struct ActionVector {
    double dirX = 0.0, dirY = 0.0, boost = 0.0;
    int    forceSlot = -1;        // TargetList 下标；-1 = 本帧不施力
    double forceX = 0.0, forceY = 0.0;   // 加速度矢量（未归一化）
    double mod[4] = {0,0,0,0};    // 对核天然参数的调制，每项 [-1,1]，越界钳制
};
```
EntityId 来自 `core/Types.h`（决策层已可 include core/，不算破分层）。

决策接口改为：
```cpp
virtual ActionVector decide(const SensorVector& sensors, const TargetList& targets) = 0;
```
**8 个策略全部要更新签名**。其中**至少 2 个策略必须真的用 `forceSlot/forceX/forceY`**、**至少 2 个必须真的用 `mod[]`**，否则这两个字段就是死参数（门禁会查）。

### 4) 前端相应调整

- 不画拖尾
- 不显示影响力条
- 不显示战斗相关 UI
- 施力时在被施力的**球上**画一个短箭头或脉冲环（**不要**画核到球的连线——用户明确禁止球-核连线）
- 核的颜色仍是遗传 hue；核的聚拢/排斥行为应当肉眼可见（核群会成团又散开）

## 三、受影响的任务卡

| 卡 | 影响 |
|---|---|
| R1-A | **重写**：去掉 attackRange/attackStrength/avoidRange/avoidStrength，换成 4 个 `nucleus*` 参数 + `forceRange`/`forceCostK` |
| R1-B | 无影响，继续 |
| R1-C | **重写 EnvControl 部分**：删掉全部影响力；事件免费；ParamSchema 加上新的 genome 键 |
| R1-D | 前端：删拖尾、删影响力条、删战斗 UI |
