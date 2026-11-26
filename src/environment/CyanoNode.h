// agentsim - environment/CyanoNode.h
// 蓝细菌节点场（S22 上半，R1-B 补全）：环境层第一营养级的生产者实体，
// 也是核的唯一食物（R1-B：Food 实体已删除）。数据模型 + 光合 + 代谢 + 分裂繁殖
// + 产球 + 遗传色相 + 转化率公式 + 连线拓扑对外可见。独立可测：不依赖
// AgentEnvironment、账本、行为层；环面几何由 core/Vec2.h 提供（torusDelta/torusDistance）。
// 本模块不碰账本：step() 只产出 CyanoStepReport 数字，由组合根记账。
//
// RNG 纪律（硬要求）：
//   step() 的 rng 消耗顺序严格为
//   [每节点:产球判定] -> [每产球节点:偏移距离,角度] ->
//   [每节点:分裂判定] -> [每分裂节点:偏移半径,角度]（随后做子代间距校验，不耗 rng）。
//   节点按 nodes_ 下标顺序遍历；死节点跳过判定（不消耗 rng）；
//   判定集合取帧初快照（本帧新生儿不再判定）；子代间距校验时，已出生的
//   同帧兄弟子代计入（亲代自身豁免）"最近同类"。
//   seed() 撒点顺序（R7-C 聚落播种，两种模式按 params_.cyanoColonies 二选一）：
//   - 聚落模式（cyanoColonies>0 且 count>0 且 clusterSpread>0）：
//     [每聚落: cx, cy]（2 抽/聚落，中心全图均匀）-> [每节点: 角度, 半径, hue]
//     （3 抽/节点；节点轮转分配 i%colonies，半径 = clusterSpread*sqrt(u) 圆盘
//     均匀——旧版 v2.3.1 同式）。hue 仍是该节点的第 3 抽。
//   - 均匀模式（cyanoColonies<=0，模块默认 = 生态中立）：旧序逐位不变，
//     [每节点: x, y, hue]（3 抽/节点）。无任何装饰性随机数污染主 RNG。
//   分裂色相（R1-B）：分裂判定阶段每存活节点抽 1 次高斯抖动 u（处于该节点
//   "分裂判定 roll" 之后），无论分裂是否放行都恰好消耗一次；放行者用其计算
//   子代 hue = parent.hue + jitterSigma * u（环绕到 [0,360)），放弃者丢弃。
//   消耗序固定：判定 -> (帧初快照全体色相抖动) -> 位置(半径,角度)。
//   S22 下半：step() 在"代谢"后、"产球判定"前调用 linkTransfer()；
//   linkTransfer() 无 rng 参数、内部不抽任何随机数，消耗序因此不受影响。
//   R8/P2-1：产球位置改为产者附近随机（距产者 ballSpawnMinDist~ballSpawnMaxDist），
//   每球仍恰抽两次 nextDouble——rng 消耗量与顺序逐位不变，只改这两次的用途
//   （旧版把两次直接当出生点 x/y，新版当作偏移距离/角度）。
//   R8/P2-2：linkCheckInterval 只控制是否调用 linkTransfer()（rng 红线使其
//   不影响随机流），不影响任何 rng 消耗。
//   R1-B 捕食侧：graze() 本身不消耗 rng（组合根"找最近节点"也是确定性扫描，
//   本段不消耗 rng——消耗点已显式删除，见 Simulation.cpp 的 R1-B 注释）。
#pragma once
#include <cstddef>
#include <utility>
#include <vector>

#include "../core/PCG32.h"
#include "../core/Types.h"
#include "../core/Vec2.h"

namespace agentsim {

class NutrientField;   // 规则 v2：物质场（残骸/养分），只读取用养分，不持有能量

struct CyanoParams {
    double photosynthesisRate = 1.0;    // 最大光合速率
    double lightHalfSaturation = 0.5;   // 半饱和光强（Michaelis-Menten）
    double metabolismRate = 0.02;       // 每 tick 基础代谢耗散
    double energyCap = 100.0;           // 单节点储能上限
    double reproductionCost = 40.0;     // 分裂成本（亲代扣除）
    double reproductionProbability = 0.01;
    double reproductionOffset = 125.0;  // 子节点与亲代的最大偏移
    double minSeparation = 10.0;        // 分裂时与最近同类的最小间距，不满足则放弃本次分裂
    // P2：产球参数（ballSpawnCost/Probability/Radius/MinDist/MaxDist）已于规则 v2 删除——
    // 普通生态的器官由核自建，蓝细菌只负责生长与分裂（验收 A11）。
    double linkRadius = 60.0;           // S22 下半：连线的最大环距
    int linkMaxNeighbors = 3;           // 每个节点最多连几个邻居
    double linkTransferRate = 0.1;      // 每帧从高能流向低能的比例
    double linkDeadband = 1.0;          // 能量差 <= 此值不传输（防抖）
    // R8/P2-2（GOAL §2.2.1 ④）：每 N 帧结算一次连线传输。step() 仅在
    // frameCount_ % linkCheckInterval == 0 的帧调用 linkTransfer()；
    // N=1（默认）与"每帧结算"逐位一致（回归安全）。语义选择（有意简化）：
    // 跳过的帧既不重算拓扑、也不结算能量传输——连线能量平衡的结算频率与
    // 拓扑刷新频率被绑定为同一 N。与"拓扑重算但隔 N 帧结算"的备选相比，
    // 这省掉了跳过帧上的 O(n²) 拓扑计算，代价是拓扑新鲜度随 N 下降；
    // 对本模块的用例（连通片内能量拉平）可接受。
    int linkCheckInterval = 1;
    // 存活节点数硬上限（按 nodes_.size() 计）。linkTransfer() 是 O(n²)，
    // 无上限增长会让 20k tick 跑不完。R3-A 起本护栏**只是** O(n²) 的性能护栏，
    // 不再是生态天花板：生态承载力由自遮光（shadeK/shadeRadius/shadeSigma）
    // 决定——节点越密单位光合越低，种群在护栏之下自然收敛
    //（R1-B 起 --cyano-max-nodes 可覆盖）。
    std::size_t maxNodes = 256;
    // ---- R1-B：播种节点数（默认 32）。Simulation 组合根以 cfg 覆盖本字段，
    // CLI --cyano-nodes 是它的用户入口；初始第一营养级库存由它决定。----
    std::size_t nodeSeedCount = 32;

    // ---- R7-C：聚落播种（seed() 的分布形状，消费方 = CyanoField::seed）----
    // cyanoColonies：聚落中心数。0 = 旧均匀撒点（模块默认：生态中立，回归逐位）；
    // 标定值 4 由 CyanoTuning 构造期/applyConfig 镜像覆盖——真实植被的斑块状
    // 分布（旧版 v2.3.1 语义），也是自遮光有意义的前提（R3-A 实测：均匀撒点
    // 下 64 节点自遮光只降 8%，聚簇分布下降 18-67%）。
    // clusterSpread：节点绕聚落中心的最大偏移半径（sqrt 圆盘均匀；仅
    // cyanoColonies>0 时消费）。与白盒镜像契约同源字段：CyanoTuning 侧同名。
    int cyanoColonies = 0;
    double clusterSpread = 45.0;

    // ---- 规则 v2：同化率（**唯一**的摄食效率口径）----
    // 旧版有两类修正，规则 v2 全部删除：
    //   1) 按遗传最高速度/加速度折减每口转化率（conversionMode 0/1/2/3 及
    //      conversionExponent/SpeedRef/AccelRef/WSpeed/WAccel/MinRate/MaxRate）；
    //   2) 按两次进食距离给"迁移回报"。
    // 现在只有一条：同化率 = conversionRate * genome.cyanoDigestion，与速度、
    // 加速度、进食距离、核能量都无关。快核的优势只剩"更早到下一片食物"，
    // 代价是实际移动耗能（验收 A1：同等条件下快慢核当步取食完全一致）。
    // 处理时间仍由消化冷却（digestCooldown）承担，不是转化率折扣。
    double conversionRate = 0.6;        // 同化率基率（× 消化基因 cyanoDigestion）

    // ---- R1-B：遗传色相（seed 随机 0~360，分裂 = 父 + 高斯抖动环绕）----
    double hueJitterSigma = 8.0;        // 分裂色相高斯抖动 σ（度）

    // ---- R3-A：自遮光（生态负反馈 / 局部光竞争）----
    // shade(node_i) = Σ_{j≠i, alive, d(i,j) < shadeRadius} exp(-d² / (2·shadeSigma²))
    // localSun(node_i) = sunLevel * exp(-shadeK * shade(node_i))
    // gain = photosynthesisRate * localSun / (localSun + lightHalfSaturation)
    // 节点越密 → 互相遮光 → 单位产量越低（自疏 / 负反馈）。遮蔽量在
    // linkTransfer() 的 O(n²) 邻居扫描里顺带累计（热路径零新增扫描）；
    // shadeK=0 或 shadeRadius=0 时逐位退回旧的全局阳光公式（回归安全）。
    // 纯几何，零 rng 消耗。
    double shadeK = 0.05;        // 局部光竞争强度
    double shadeRadius = 60.0;   // 遮蔽半径
    double shadeSigma = 30.0;    // 高斯衰减宽度

    // ---- 规则 v2：组织（material）与养分（nutrient）----
    // 旧版"每节点固定 15 单位不可食保底"已删除（验收 A2/A3：一片菌落必须能
    // 被吃空）。取而代之的是**可解释的最小存活结构量**：
    //   可食量 edible = max(0, material - minStructure)
    //   material <= minStructure 时节点死亡，剩余结构成为残骸（P1）。
    // 也就是说保底不再是"永远吃不动的能量"，而是"吃掉活组织本身会让它死"——
    // 吃空 = 杀死生产者，而不是把它冻结在一个不可食余额上。
    double minStructure = 8.0;          // 最小存活结构量（= 不可食的活组织核心）
    double maxMaterial = 120.0;         // 单节点组织上限（自遮光之外的第二重密度约束）
    double initialMaterial = 45.0;      // 播种/分裂时节点的初始组织量
    // 播种/分裂时节点的初始可用能量。
    // 规则 v2 起必须非零：播种能量恒 0 时，第一帧"光合 − 维持"只要不为正
    // 就会立刻饿死——默认光强(0.5)与满组织维持开销恰好收支相抵，于是整片
    // 菌落在第 1 帧团灭（实测 512 → 2）。这不是调参问题，是"新个体没有
    // 启动储备"的建模错误：任何细胞都有起始储备。
    // 值由 cfg.runtime.cyanoInitialEnergy 经组合根镜像（默认 40）。
    double seedEnergy = 40.0;
    // 形成新组织：必须有养分，也必须付出能量。
    //   n = min(growthPerTick, 区域可用养分/nutrientPerMaterial,
    //           maxMaterial - material, energy/growthEnergyCost)
    // 所以无光无储备的节点即使被施肥也长不出新组织（验收 A8）。
    double growthPerTick = 0.6;         // 每帧最多新增的组织量
    double nutrientPerMaterial = 1.0;   // 每单位新组织需要的可吸收养分
    double growthEnergyCost = 0.6;      // 每单位新组织消耗的节点可用能量
    // 摄食同化率：咬下的组织里 assimilated 部分进入核体（物质）与核能量，
    // 其余回到环境（残骸/养分，P1 接线）。A2 的"取走量"与"吸收量"分开结算。
    double assimilation = 0.7;
};

struct CyanoNode {
    EntityId id = 0;
    Vec2 pos{};
    // energy = 可支配的光合产物（"活动储备"）：核吃不到它，它只在节点内部驱动
    // 代谢与生长；被细胞分裂带走一半。
    double energy = 0.0;
    // material = 构成身体的物质（"可食组织"）。核摄食咬下的是它，不是 energy。
    // 规则 v2 起这是唯一决定"这个节点还能被吃多少"的量：edible = material - minStructure。
    // 物质来源只有养分（P1 接线），不是光照——所以一具尸体无法替代阳光。
    // 默认值非 0：一个存在的节点就是一具身体（构造即有组织），"没有物质"必须
    // 显式写 0 才是异常态。这样直接把 CyanoNode 摆进夹具的既有单测不会因为
    // 新字段默认 0 而批量变成"不可食"。
    double material = 120.0;
    bool alive = true;
    double hue = 0.0;   // R1-B：遗传色相 ∈ [0,360)（seed 均匀采样；分裂 = 父 + 高斯抖动）
};

// 一帧的收支报告：本模块不碰账本，只报数字，由组合根记账。
struct CyanoStepReport {
    double capacityLoss = 0.0; // 降低储能上限后排出的超额储备
    double photosynthesisGain = 0.0;  // 本帧光合总得（需 injectPrimary）
    double metabolismLoss = 0.0;      // 本帧代谢总耗（需 dissipate）
    double reproductionLoss = 0.0;    // 本帧分裂总耗散（子代没拿到的那部分）
    int births = 0;                   // 新增蓝细菌数
    double linkTransferred = 0.0;     // 本帧连线转移总量（仅观测；零和不影响账本）
    // ---- 规则 v2：物质流（P1）。本模块不碰账本，只报数字 ----
    double tissueGrowth = 0.0;        // 本帧光合能量转成的新组织量（material 增加）
    double nutrientConsumed = 0.0;    // 本帧生长消耗的可吸收养分量
    double growthEnergySpent = 0.0;   // 本帧生长花掉的节点能量（需 dissipate 或留账）
    double deadMaterial = 0.0;        // 本帧饿死节点留下的组织量（→ 残骸）
    double deadEnergy = 0.0;          // 本帧饿死节点残留的可用能量（→ 耗散）
};

// ---- 规则 v2：摄食请求 / 结算（一个模拟步内统一分配，验收 A2）----
struct GrazeRequest {
    EntityId nucleusId = 0;
    EntityId nodeId = 0;
    double wantMaterial = 0.0;   // 想咬下的组织量（受摄食能力与实际储存余量限制）
    double efficiency = 1.0;     // 同化率（= conversionRate * cyanoDigestion），仅回传给调用方
};

struct GrazeGrant {
    EntityId nucleusId = 0;
    EntityId nodeId = 0;
    double material = 0.0;           // 实际咬下的组织量（从节点扣掉的量）
    double energyTransferred = 0.0;  // 随该组织一起从节点转移出的可用能量
    double efficiency = 1.0;
};

class CyanoField {
public:
    CyanoField() = default;
    explicit CyanoField(const CyanoParams& p);

    // 播种。RNG 消耗顺序（R7-C 两种模式，见文件头）：
    //   聚落模式（params.cyanoColonies>0 且 count>0 且 clusterSpread>0）：
    //     [每聚落: cx, cy] -> [每节点: 角度, 半径, hue]。
    //   均匀模式（cyanoColonies<=0，模块默认）：[每节点: x, y, hue]。
    // hue = 360.0 * 节点第 3 抽（遗传色相，[0,360) 均匀）。count<=0 时不消耗 rng。
    // 同时把帧计数清零（重新播种即重新起算 linkCheckInterval 的相位）。
    void seed(int count, double worldW, double worldH, PCG32& rng);

    // ---- R3-B（实时调控接线）追加：追加式播种（事件 bloom 用）----
    // 语义 = seed() 的"不清场"版：在现有场**末尾追加** count 个节点（id 取
    // nextId_ 续号，位置/色相的 rng 消耗序与 seed() 逐节点相同：x, y, hue 各一抽），
    // 已有节点/连线/节流相位一概不动。受 params_.maxNodes 护栏截断（与分裂的
    // 上限检查同源），返回实际追加数。新节点能量 0（与 seed() 同口径：能量
    // 从光合长出来，账面零操作——由 step() 的 injectPrimary 入账）。
    // 为什么必须在场内追加而不是重建：节点是生态连续体（遗传色相谱系/连线拓扑/
    // 节点能量），bloom 事件"爆发"语义 = 增量，不是格式化重来。
    // 本方法零改既有行（纯追加声明 + 纯追加实现），rng 消耗序只在本方法被调用
    // 时生效——不调用则与旧版本逐位一致。
    int appendSeed(int count, double worldW, double worldH, PCG32& rng);

    // 一帧：光合 -> 代谢 -> 连线传输(零和,不耗rng,按 linkCheckInterval 节流)
    // -> 产球判定 -> 产球位置（产者附近随机，GOAL §2.2.1 ③）
    // -> 分裂判定 -> 分裂色相抖动 -> 分裂位置
    //（rng 细序与放弃语义见文件头与 CyanoNode.cpp 注释）。sunLevel ∈ [0,1] 全局光强。
    // 规则 v2：nutrients 为可吸收养分来源（nullptr = 无养分场，生长被完全限制）。
    // 生长从所在区域取养分、从自身能量付出代价——所以"只施肥不给光"长不出新组织。
    CyanoStepReport step(double sunLevel, double worldW, double worldH, PCG32& rng,
                         NutrientField* nutrients = nullptr);

    // ---- 规则 v2：可食量（感知、选目标、请求、扣库存共用这一份定义）----
    // edible = max(0, material - minStructure)。这是**唯一**的可食量口径：
    // 感知层用它判断"这个节点还值不值得去"，行为层用它算请求量，本模块用它
    // 扣库存。三处若各算一套，"盯着一片吃不动的节点"的旧 bug 就会回来。
    static double edibleOf(const CyanoNode& n, const CyanoParams& p);
    double edibleOf(EntityId id) const;

    // ---- 规则 v2：一个模拟步内的摄食批结算（验收 A2）----
    // 调用方（组合根）先把**全部**核的摄食请求收集完，再一次性交给本方法：
    // 先按 (nodeId, nucleusId) 排序（与请求遍历顺序无关 ⇒ 颠倒核遍历序结果
    // 相同），再按节点结算。每节点：
    //   available = Σ edible；totalWant = Σ want
    //   totalWant <= available → 全给；否则 share_i = available * want_i / totalWant
    // 扣的是 material（不是 energy）。返回每个请求实际拿到的组织量，调用方
    // 据此再决定吸收/损失去向（行为的"唯一写点"仍在行为层）。
    std::vector<GrazeGrant> grazeBatch(const std::vector<GrazeRequest>& requests);
    // 本帧被吃空的节点（material <= minStructure）在这里统一死亡：material 归零、
    // alive=false。调用方取走 deadMaterial 作为残骸（P1）。
    // 由 grazeBatch 内部的 pendingDeath_ 记录；本方法消费并清空它们。
    // 单独成方法是为了让"被吃空 → 死亡 → 残骸"成为显式的、可审计的一步。
    struct DeadNode {
        EntityId id = 0;
        Vec2 pos{};
        double material = 0.0;   // 留下的组织（→ 残骸）
        double energy = 0.0;     // 残留可用能量（→ 耗散）
        double hue = 0.0;
    };
    std::vector<DeadNode> takeDeadNodes();

    // 旧的单节点摄食接口（规则 v2 语义，保留给既有单测/独立使用）：
    // 直接咬下 want 的组织，返回实际取走量；不涉及同化/能量（那是行为层）。
    // 兼容旧签名：第二参仍叫 rawEnergy，语义改为"想咬下的组织量"。
    double graze(EntityId id, double rawEnergy, double conversionRate);

    const std::vector<CyanoNode>& nodes() const { return nodes_; }
    std::size_t aliveCount() const;
    double totalEnergy() const;
    // 规则 v2：全图存活节点的组织总量（物质账审计用；与能量账完全独立）。
    double totalMaterial() const;
    const CyanoParams& params() const { return params_; }

    // 连线能量再分配（S22 下半，v2.3.1 语义）：双向互选成边 + 高能向低能传输
    //（(hi-lo)*linkTransferRate，差 <= linkDeadband 跳过）。零和、确定性、
    // 绝不消耗 rng；成交边同步写入 links_（R1-B：拓扑对外可见）。step() 按
    // linkCheckInterval 节流调用（语义见 CyanoParams 注释）。
    void linkTransfer();

    // ---- R1-B：连线拓扑对外可见（渲染/审计用）----
    // 最近一次 linkTransfer() 重算拓扑时实际成交的边（两端点为 nodes() 下标，
    // 恒按 (小,大) 有序对存储，按下标升序排序）。step() 按 linkCheckInterval
    // 节流后，非结算帧拓扑保持上一次结算的快照（与能量口径一致）。
    const std::vector<std::pair<EntityId, EntityId>>& links() const { return links_; }

    // ---- R3-A：遮蔽量缓存只读视图（测试/审计用）----
    // 最近一次 linkTransfer() 结算缓存的逐节点遮蔽量（高斯核求和，公式见
    // CyanoParams 注释；下标 = nodes() 下标）。一帧滞后语义：step() 的光合
    // 读取的是上一结算帧的快照——蓝细菌节点位置永不移动，滞后仅来自帧间
    // 存活集合变化（新生儿/死亡的一帧内不计入遮蔽），量级可忽略。
    const std::vector<double>& shadeCache() const { return shade_; }

    // R5-C（存档续跑确定性）：restoreForSaveLoad 之后 shade_ 归零，而 step() 的
    // 光合读的是**上一结算帧**的 shade_ 快照——不刷新则读档后第一帧光合与
    // 原世界分叉（save_resume_identical 用例的失败根因）。本函数按当前 nodes_
    // 重算遮蔽缓存：与 linkTransfer 的遮蔽扫描同公式（高斯核、对称对记两端），
    // 但**不建连线、不结算能量、不消耗 rng**。位置永不移动 ⇒ 与"上一帧快照"
    // 的唯一差异来自存活集合变化，量级 ≤1 ulp 级（与一帧滞后同量级）。
    void refreshShadeCache();

    // 最近一次 linkTransfer() 转移的能量总量（观测用；每次调用先重置为 0 再累计）。
    // 注意：step() 按 linkCheckInterval 节流后，非结算帧该值保持上一次结算的残留
    // （step() 内仅在真正结算时写入 report.linkTransferred）。
    double lastLinkTransferred() const { return linkMoved_; }

    // 最近一次 linkTransfer() 实际成交的边数（观测/调试用；防抖跳过的不计）。
    int lastLinkEdgeCount() const { return linkEdges_; }

    // 注：规则 v2（P2）起本模块**不再产出自由球**。旧 lastBallSpawns()/
    // report.ballsProduced 整条通道已删除——普通生态的器官由核自建（验收 A11）。


    // ---- R5-C（存档系统）追加：白盒存取通道（唯一消费者：Simulation 序列化）----
    // 为什么必须存在：nextId_ / frameCount_ 是确定性的组成部分——id 计数不复位则
    // 读档后新生儿 id 撞已有节点；连线节流相位不恢复则 linkCheckInterval 结算帧
    // 错位，读档后轨迹与原世界分叉（确定性红线）。两者都没有任何既有公开通道，
    // 只能按 applyConfig/appendSeed 的既有先例以纯追加方法补最小白盒面。
    EntityId nextNodeId() const { return nextId_; }
    long long frameIndex() const { return frameCount_; }
    // 反序列化恢复：nodes 容器原样回填（容器序即确定性序）、id 计数与连线节流相位
    // 原样恢复；links_ 清空——拓扑由下一次 linkTransfer 结算帧确定性重建，
    // 恢复期绝不能主动调 linkTransfer()（那会多结算一次能量转移，
    // 破坏"读档后逐位续跑"）。params_ 不在此恢复：它经 cfg/applyConfig 通道对齐。
    void restoreForSaveLoad(std::vector<CyanoNode> nodes, EntityId nextId,
                            long long frameCount);
    private:
    CyanoParams params_{};
    std::vector<CyanoNode> nodes_;
    std::vector<DeadNode> pendingDeath_; // 规则 v2：本帧被吃空、待组合根取走的死亡节点
    std::vector<std::pair<EntityId, EntityId>> links_; // R1-B：最近一次成交拓扑
    std::vector<double> shade_;  // R3-A：最近一次结算的遮蔽量缓存（下标 = nodes_ 下标）
    double worldW_ = 0.0;    // 最近一次 seed()/step() 的环面尺寸（linkTransfer 用）
    double worldH_ = 0.0;
    double linkMoved_ = 0.0; // 最近一次 linkTransfer() 的转移总量
    int linkEdges_ = 0;      // 最近一次 linkTransfer() 结算的边数（观测）
    EntityId nextId_ = 1;
    long long frameCount_ = 0; // 已跑帧数（linkCheckInterval 的节流相位）
};

} // namespace agentsim
