# V2 目标规格：向旧版 v2.3.1 看齐（不兼容，是参考）

本文件是需求来源，但 **`V2-SPEC-AMEND-1.md` 优先级更高**，冲突时以修正文件为准。
参考实现代码已导出到 `D:\ScuStu\Q\_ref-v231`（v2.3.1 tag 的完整源码），
只读参考，**不许复制其架构**。agentsim 必须保持自己的四层单向管道 + Ledger 审计范式。

## 0. 用户拍板的范围（逐条，不可偏离）

必须实现：
1. **核用色相变化**：每个核携带遗传色相 hue∈[0,360)，子代 = 父 + 高斯扰动，独立 RNG 流（不污染主 RNG）。
2. **核的移动**：不再画"球—核连线"（前端不许出现从核到其球的连线）；核在场上移动。
3. **蓝细菌连线 + 繁殖**：连线（网络能量传输）与分裂繁殖都在画面上可见、在模拟中生效。
4. **蓝细菌本身是食物**：取消食物球（Food 实体整体移除），核捕食蓝细菌即进食。
5. **实时调控**：四轴五档 + 事件卡 + 细调参数，运行中可改，改完立即生效。**（修正 1：不要影响力预算，调控免费无限）**
6. **涌现是核心**：可遗传参数 + 变异 + 自然选择必须真的在跑，能被观察到。
7. 原版有的一切（存档/时间轴/世界线分支/参数页/材质包/浏览器启动器/HTTP 接口）。

明确不做：
- 控制台字符渲染（ConsoleRenderer）——用户点名排除。
- mod 系统（玩法包 / mod_api / mods_registry）——用户点名"暂时不要开发"。
- **兼容性**：不读旧存档格式、不兼容旧 trace。旧版是参考，不是兼容目标。
- **轨迹拖尾**（修正 1）：前端不画任何拖尾/残影。
- **影响力系统**（修正 1）：不要预算、不要扣费、不要每帧恢复。
- **核战斗**（修正 1）：不要核间能量转移/伤害/吸血，也不要 attackRange/attackStrength 基因。

## 1. 缺失清单（逐项，全部要落地）

### A. 遗传与进化（旧版 NucleusParams 22 参数 + 3 蓝细菌参数）
字段（按类型数组 + 标量）：
- `affinity[3]` 吸引力权重（归一化和为 1）
- `orbitRadius[3]` 该类型球的期望轨道半径
- `absorbPreference[3]` 吸收该类型球的能量倍率
- `repelStrength[3]` 球过近时的排斥强度
- `attackRange` `attackStrength` `avoidRange` `avoidStrength`
- `maxSpeed` `maxAccel` `energyThreshold` `mutationRate`
- `cyanoPreference`（0 = 完全不捕食蓝细菌）`cyanoDigestion`（消化效率）
- `hue`（遗传色相，独立 hueRng 流）
每参数有 Init[Min,Max]（随机初始化）与 Clamp[Min,Max]（变异裁剪）。变异 = 高斯扰动 × mutationRate × 全局 mutationCoefficient，然后 clamp；affinity 变异后重新归一化。子代继承父代全部参数。

### B. 三类球（Shield / Resource / Scout）真实差异化
- Resource：贡献储能容量（colonyCap = nativeCap + Σ resource.cap）
- Scout：唯一感知来源（已实现，保留）；速度上限最高
- **Shield：必须有真实机制**（旧版只给了速度上限差异，是假的）——护盾球降低核的**代谢损耗**与**能力开销**：代谢 × 1/(1+0.35×shieldCount)，且施力代价可被护盾球分担（有护盾时 forceCost 打折）。不涉及战斗（修正 1 已取消战斗）。
- 三种球各自的 maxSpeed 上限、absorbPreference 倍率生效。

### C. 核与核的相互作用（修正 1：**没有战斗**）
只有三种物理作用，**没有任何能量转移**：
- **斥力**：距离 < `nucleusPersonalSpace` 时沿线施加强斥力（强度 `nucleusRepelStrength`）
- **聚拢**：距离 < `nucleusSightRadius` 且 > personalSpace 时向对方施弱引力（强度 `nucleusCohesion`，0 = 独行）
- **硬分离 pass**：保证核间距 >= `nucleusMinSeparation`（世界参数，非基因）
四个 `nucleus*` 参数**可遗传**，且**可被决策层用 `mod[4]` 调制 ±40%**（表型可塑，基因不被改写）。
旧版 `nucleusCombat` / `attackK` / `avoidK` / `combatRate` / `newbornGraceFrames` 全部**丢弃**。

### D. 球动力学（旧版 updateBallDynamics 力场模型）
替代/增强现有轨道模型：核力场 = 径向弹簧 + 切向环绕驱动 + 近距排斥；跟随耦合（followCoupling）让快核甩不掉球；球间短程排斥 + 同核吸引；按类型限速；阻尼；环面 wrap（现在是 bounce，改成 wrap 或保留设定但要有参数）。
球归属 updateOwnership：detachRadius 内最近核持有；换主时对原主扣 ballLossCost。

### E. 蓝细菌完整化
- 光合（Michaelis-Menten 光饱和）+ 代谢 + 能量上限
- 连线拓扑（linkDistance / maxLinks / linkCheckInterval）+ 能量从高向低传输（transferRate / deadband）
- 分裂繁殖（reproductionCost/Probability/Offset/minNodeSeparation）
- 产球（ballSpawnCost/Probability）— 产出的球加入世界
- 被核捕食：转化率公式 mode 0/1/2/3（速度惩罚/加速度惩罚/混合），clamp 到 [minRate,maxRate]，gain = raw × rate × cyanoPreference
- 节点色相（遗传，繁殖时抖动）
- **取消 Food 实体**：删除 FoodConfig/AgentEnvironment 食物全链路，核唯一食物 = 蓝细菌。初始库存改用"初始蓝细菌节点数"。

### F. 阳光模型
sunLevel = base + dayAmplitude × sin(2π·frame/period) + 垂直梯度 + 噪声，全部参数可调。现有硬编码 `0.5+0.5*sin(tick*0.01)` 必须参数化。

### G. 实时调控（修正 1：**无影响力**）
- 四轴五档：light / food / pressure / producer，每档 = 一组参数键值；换档**免费、无限次**
- 细调：白名单参数键精确改值，**免费**
- 事件卡：harvest / winter / plague / bloom，**免费、无冷却**
- 预设 preset：一套四轴档位 + 蓝细菌开关，**免费**
- 蓝细菌开关 cyano=on/off，**免费**
- 干预能量效果全部走 ledger（注入走 injectPrimary，其余零和）——审计仍必须成立
- 旧版 `kInfluenceMax/kInfluenceStart/kInfluenceRegenPerFrame/kQuickCost/kFineCost/kPresetCost/kCyanoToggleCost` **全部作废**

### H. HTTP 实时服务 + 浏览器启动器
- 极简 HTTP/1.1 服务器（WinSock，绑 127.0.0.1，端口占用自动 +1），零第三方依赖
- 接口：GET /status /state /env /schema /saves /packs /timeline，POST /run /control /env /seek /branch /save /load /delete-save /rename-save /save-config /set-packs
- 实时：/state 返回二进制快照（逐球 + 核 + 蓝细菌节点 + 连线 + 统计）
- 前端点开即玩：菜单页（seed/balls/nuclei/frames/蓝细菌/自动存档）→ 运行页实时渲染

### I. 存档 + 时间轴 + 世界线分支
- 二进制存档 save.bin（配置 + RNG 状态 + 帧号 + 球 + 核含全部基因参数 + 蓝细菌节点/连线/nextId + 色相 RNG + EnvState）
- 时间轴：每 200 帧一个完整检查点，内存上限 2000（超出抽稀）；timeline.bin
- 拖动/输入帧号回看历史帧（从最近检查点快进）
- 变动世界线 branch：保留当前实体状态，只换运行参数，从当前帧开新局
- 存档目录 saves/<名字>/：save.bin + timeline.bin + meta.txt + thumb.png

### J. 参数页 + 材质包
- 参数页：全部 ~120 个参数按 5 类分组，带范围校验、恢复默认、保存为默认（写 env_config.txt）
- 材质包：resourcepacks/<包名>/ 下的 pack.txt（name/desc/version/author）+ colors.txt（type_color + 各类型 RGB）；堆叠顺序后者覆盖前者；内置一个示例包
- **不做玩法 mod**

### K. 前端渲染（用户明确要求）
- 核：圆形，**颜色 = 遗传色相**（hue → HSL），不画球—核连线，**不画拖尾**
- 球：按类型区分形状/颜色（Resource 实心圆、Scout 三角/菱形、Shield 六边形/方），**不画拖尾**
- 蓝细菌：节点 + **连线网络**（必须可见）+ 繁殖时色相抖动
- 昼夜背景、缩放平移、悬停查看核的全部基因参数、右侧可折叠状态栏、统计曲线
- 时间轴控件、存档页、参数页、菜单页

## 2. 硬约束（每一轮都适用）
- C++17 / MSVC /W4 /WX /permissive- /utf-8，**零警告**，零第三方依赖
- 四层单向管道不可破：环境 → 感知 → 决策 → 行为（唯一写入者）→ Ledger
- 决策层不得 include environment/sim/ledger，不得用 RNG
- 行为层是唯一写入者（守卫脚本强制）
- Ledger 恒等式 `injected - dissipated == sumLiving`（含蓝细菌、球、自由球）
- 双跑确定性：同 seed 两次 stdout 字节相同（含新增随机源必须先定 RNG 消耗序）
- 错误码：2 参数错，3 审计失败，4 I/O，5 灭绝
- 测试框架零第三方（tests/unit/test_framework.h），新功能必须带单测
- 门禁 tools/check.ps1 必须全绿才算完成

## 3. 分层归属（谁写哪一层）
- 环境层 src/environment/：阳光模型、蓝细菌全功能、地形
- 感知层 src/perception/：SensorVector 维度扩展（邻近核威胁/护盾/蓝细菌密度）+ 新增 TargetList（范围内可施力的球）
- 决策层 src/decision/：策略消费新维度；策略不动实体
- 行为层 src/behavior/：全部实体写入（球动力学、核对核斥力/聚拢、点名施力、繁殖+变异、捕食）
- 应用层 src/app/ + src/sim/：组合根、CLI、trace、HTTP、存档
- 前端 frontend/：渲染与交互
