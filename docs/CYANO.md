# 蓝细菌模块设计文档

## 概述

蓝细菌模块在不改变现有核-小球生态核心逻辑的前提下，引入了一个**可选的、零开销的**生产者层级：

- **生产者**：蓝细菌节点通过光合作用产生能量，形成连线网络进行能量传输
- **消费者**：核可选择捕食蓝细菌节点（受 `cyanoPreference` 基因控制）
- **零开销保证**：当 `initialCyanoNodes <= 0` 时，模块完全不参与模拟，RNG 序列与基线**逐字节一致**

---

## 核心架构

### 1. 双 RNG 隔离设计

```
World::rng_      → 主模拟随机流（小球生成、核变异、战斗、移动等）
World::hueRng_   → 仅用于色相突变（可视化专用，不推进主 RNG）
Cyanobacteria::rng_ → 蓝细菌内部随机流（节点生成、连线、光合噪声等）
```

**关键不变量**：
- 关闭蓝细菌（`initialCyanoNodes <= 0`）时，`Cyanobacteria::step()` 直接返回，**零 RNG 调用**
- 开启蓝细菌时，`cyano_.step(rng_, ...)` 使用**主 rng_**，但仅在开启分支内消耗随机数
- `hueRng_` 从种子派生：`seed * 2654435761u + 1013904223u`，与主 RNG 完全独立

### 2. 世界状态机

```
World::step() 每帧顺序：
1. 年龄递增
2. updateCyanobacteria()     ← 蓝细菌光合/生长/产球（仅开启时执行）
3. buildGrid()               ← 含蓝细菌产出的新球
4. updateBallDynamics()
5. updateOwnership()
6. absorbBalls()             ← 含捕食蓝细菌节点（段 1）
7. nucleusCombat()
8. nucleusMovement()
9. reproduce()
10. removeDeadNuclei()
11. replenishBalls()
12. ++frame_ & checkFinish()
```

---

## 参数体系

### WorldConfig 新增键（env_config.txt / 启动器参数页）

| 键 | 类型 | 默认值 | 说明 |
|-----|------|--------|------|
| `initialCyanoNodes` | int | 0 | 初始节点数，**≤0 即关闭模块**（零开销） |
| `cyanoColonies` | int | 4 | 初始聚落数 |
| `cyanoInitialEnergy` | double | 50.0 | 单节点初始能量 |
| `cyanoPhotosynthesisRate` | double | 0.8 | 光合基础速率（能量/帧） |
| `cyanoLightHalfSaturation` | double | 0.5 | 光饱和半常数（Michaelis-Menten） |
| `cyanoMetabolismRate` | double | 0.02 | 维持代谢扣除（能量/帧） |
| `cyanoLinkDistance` | double | 80.0 | 连线最大距离 |
| `cyanoMaxLinks` | int | 3 | 单节点最大连线数 |
| `cyanoLinkCheckInterval` | int | 60 | 连线拓扑重算间隔（帧） |
| `cyanoTransferRate` | double | 0.1 | 连线单帧传输上限比率 |
| `cyanoTransferDeadband` | double | 0.01 | 传输死区阈值 |
| `cyanoBallSpawnCost` | double | 30.0 | 产球能量成本 |
| `cyanoBallSpawnProbability` | double | 0.02 | 产球概率/帧/节点 |
| `cyanoReproductionCost` | double | 40.0 | 分裂繁殖能量成本 |
| `cyanoReproductionProbability` | double | 0.01 | 分裂概率/帧/节点 |
| `cyanoReproductionOffset` | double | 15.0 | 子节点位置偏移 |
| `cyanoMinNodeSeparation` | double | 12.0 | 节点最小间距（防重叠） |
| `cyanoPredationRadius` | double | 30.0 | 核捕食判定半径 |
| `cyanoConversionMode` | int | 0 | 转化率公式：0=基础, 1=速度惩罚, 2=加速度惩罚, 3=混合 |
| `cyanoConversionRate` | double | 0.6 | 基础转化率上限 |
| `cyanoConversionExponent` | double | 1.5 | 转化率非线性指数 |
| `cyanoConversionSpeedRef` | double | 8.0 | 速度归一化基准 |
| `cyanoConversionAccelRef` | double | 6.0 | 加速度归一化基准 |
| `cyanoConversionWSpeed` | double | 0.7 | 速度惩罚权重 |
| `cyanoConversionWAccel` | double | 0.3 | 加速度惩罚权重 |
| `cyanoConversionMinRate` | double | 0.05 | 转化率下限 |
| `cyanoConversionMaxRate` | double | 0.9 | 转化率上限 |
| `cyanoRenderChar` | int | 0 | 控制台渲染字符：0='*', 1='o', 2='+' |
| `cyanoRenderLinks` | bool | true | 是否渲染连线 |
| `cyanoHueJitter` | double | 5.0 | 繁殖色相高斯扰动标准差 |

### NucleusParams 新增遗传字段

| 字段 | 类型 | 默认值 | 范围 | 含义 |
|------|------|--------|------|------|
| `cyanoPreference` | double | 0.0 | [0.0, 1.0] | 捕食意愿，**0 = 完全不捕食** |
| `cyanoDigestion` | double | 0.5 | [0.0, 1.0] | 消化效率，影响转化率公式 |
| `maxAccel` | double | 6.0 | [1.0, 20.0] | 最大加速度，参与转化率惩罚计算 |

**重要**：`cyanoPreference = 0.0` 时，核**完全不参与捕食判定**，与关闭蓝细菌模块效果等价，保证向后兼容。

---

## 关键算法

### 光合作用（每帧每节点）

```
energy_gain = photosynthesisRate * (sunLevel / (sunLevel + lightHalfSaturation))
energy_loss = metabolismRate
net = energy_gain - energy_loss
```

- `sunLevel` 来自 `SunModel`（昼夜周期 + 垂直梯度 + 噪声）
- 节点能量下限 0，上限无硬性限制（但受产球/分裂消耗自然约束）

### 能量传输（连线网络）

- 每 `linkCheckInterval` 帧重算拓扑：半径 `linkDistance` 内最近 `maxLinks` 个邻居
- 每帧：能量从高向低传输，单向上限 `transferRate * (E_high - E_low)`，死区 `transferDeadband`
- 传输不产生/消耗能量，仅重新分配

### 产球机制

```
每帧每存活节点：
  if (rng < ballSpawnProbability && energy >= ballSpawnCost):
      energy -= ballSpawnCost
      在节点附近随机生成自由小球（类型均匀随机）
```

### 分裂繁殖

```
每帧每存活节点：
  if (rng < reproductionProbability && energy >= reproductionCost):
      energy -= reproductionCost
      在 offset 范围内生成子节点（能量平分，最小间距检查）
```

### 捕食转化（核吃蓝细菌）

```
每帧每存活核（cyanoPreference > 0）：
  在 predationRadius 内找最近节点
  rawEnergy = 节点当前能量（consumeNode 标记死亡，能量归零）
  rate = cyanoConversionRate(
           mode, maxSpeed, maxAccel, cyanoDigestion)  // 见下文
  gain = rawEnergy * rate * cyanoPreference
  energy_.apply(n, gain, EnergyReason::CYANO_GAIN)
```

**转化率公式**（`CyanoConversion`）：

```
mode 0 (基础):        rate = baseRate
mode 1 (速度惩罚):    rate = baseRate * (1 - (maxSpeed/speedRef)^exponent * wSpeed)
mode 2 (加速度惩罚):  rate = baseRate * (1 - (maxAccel/accelRef)^exponent * wAccel)
mode 3 (混合):        rate = baseRate * (1 - [wSpeed*(maxSpeed/speedRef)^exp + wAccel*(maxAccel/accelRef)^exp])

最后钳制到 [minRate, maxRate]
```

> 设计意图：快速/高加速度核捕食效率更低，诱导出"缓慢但强力消化"与"快速但低效捕食"的策略分化。

---

## RNG 确定性契约

### 关闭态（`initialCyanoNodes <= 0`）
- `World::updateCyanobacteria()` 首行即 `return`
- `Cyanobacteria::step()` **从未被调用**
- 主 `rng_` 序列与无蓝细菌版本**逐字节一致**
- `hueRng_` 仅用于核色相，不受影响

### 开启态
- `cyano_.step(rng_, frame, sun, spawned)` 消耗主 `rng_`
- 消耗顺序固定：光合噪声 → 连线拓扑（间隔帧） → 产球判定 → 分裂判定 → 产球位置/类型 → 分裂位置
- 存档 `writeState`/`readState` 完整序列化 `rng_`，载入后**逐帧一致**

---

## 存档兼容性

### 版本 v2（v2.2.0 起）
- 存档格式 `NSMS` version=2：在原有配置/RNG/球/核之后追加色相 RNG、蓝细菌节点/连接/nextNodeId、EnvState（影响力/旧冷却字段/干预计数/峰值）。
- 读档后蓝细菌节点/连线/能量、影响力全部恢复；旧冷却字段保留在格式里但读档清零（手动事件已无冷却）。世界尺寸/网格/阳光/蓝细菌参数按存档配置重建。
- v1 旧存档仍可读取；旧档没有蓝细菌段，载入后蓝细菌按关闭处理（不伪造节点、不消耗 RNG），玩家可用 `cyano=on` 重新激活。

### 版本 v1（旧）
- 配置段：`ParamSchema` 自动包含所有新键
- 核数据段：在 22 个原有遗传参数后**追加** 3 个蓝细菌字段
  - `cyanoPreference`, `cyanoDigestion`, `maxAccel`
- 读取端：按顺序读取，旧存档（无这 3 字段）会读到 0/0/0 → 等价于 `cyanoPreference=0`，**自动兼容为不捕食**

### 迁移建议
- 旧存档载入后，所有核 `cyanoPreference=0`，不会捕食
- 如需启用捕食，需通过突变或手动编辑存档引入非零值

---

## 性能特征

| 场景 | 每帧开销 | 备注 |
|------|----------|------|
| 关闭（初始节点 ≤ 0） | **0** | 短路返回，无分支预测开销 |
| 开启，空群落 | O(1) | 仅 `empty()` 检查 |
| 开启，N 节点 | O(N + L) | N=节点数，L=连线数（通常 L ≈ 1.5N） |
| 网格查询 | O(1) 期望 | 复用世界网格，`nodesNear` 定半径查询 |

**内存**：`Cyanobacteria` 仅在开启时分配节点/连线向量，关闭时为空（零内存）。

---

## 扩展点

### Mod 订阅事件
- `EventType::CYANO_NODE_BORN`：节点出生（分裂/初始化）
- `EventType::CYANO_NODE_DIED`：节点死亡（能量耗尽/被捕食）
- `EventType::CYANO_LINK_FORMED` / `CYANO_LINK_BROKEN`：连线变更
- `EventType::CYANO_BALL_SPAWNED`：产出自由小球

### 自定义转化率
Mod 可在 `absorbBalls()` 段 1 后通过事件修改 `n.params.cyanoDigestion` 或注入额外能量。

### 渲染材质包
`resourcepacks/<包名>/colors.txt` 支持：
```
cyano_node = r,g,b
cyano_link = r,g,b
```

---

## 验收清单（对应 PLAN.md Redline 验收项）

- [x] **#9**：`cyanoPreference = 0` 时核完全不捕食，行为与关闭模块一致
- [x] **#13**：关闭蓝细菌时，主 RNG 序列与基线逐字节一致
- [x] **#14**：开启蓝细菌时，存档载入后逐帧完全一致（含 RNG 状态）
- [x] **#15**：控制台渲染关闭态输出与基线逐字节一致
- [x] **#16**：`initialCyanoNodes <= 0` 时零内存、零 CPU、零 RNG 调用

---

## 调试建议

1. **开启能量汇总**：`--energy-summary` 可看到 `CYANO_GAIN` 统计
2. **趋势 CSV**：`--trend-csv trend.csv --trend-interval 100` 含 3 个蓝细菌遗传列
3. **控制台统计行**：开启时显示 `蓝细菌节点 X | 蓝细菌连接 Y | 节点平均能量 Z | 阳光 W`
4. **单步模式**：启动器暂停/单步可观察节点能量流动

---

## 变更日志

| 版本 | 日期 | 变更 |
|------|------|------|
| v1 | 2025 | 初始实现：光合、连线、产球、分裂、捕食、双 RNG 隔离、存档兼容 |