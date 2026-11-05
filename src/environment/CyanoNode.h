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

struct CyanoParams {
    double photosynthesisRate = 1.0;    // 最大光合速率
    double lightHalfSaturation = 0.5;   // 半饱和光强（Michaelis-Menten）
    double metabolismRate = 0.02;       // 每 tick 基础代谢耗散
    double energyCap = 100.0;           // 单节点储能上限
    double reproductionCost = 40.0;     // 分裂成本（亲代扣除）
    double reproductionProbability = 0.01;
    double reproductionOffset = 125.0;  // 子节点与亲代的最大偏移
    double minSeparation = 10.0;        // 分裂时与最近同类的最小间距，不满足则放弃本次分裂
    double ballSpawnCost = 30.0;
    double ballSpawnProbability = 0.02;
    double ballRadius = 1.0;            // 产出的自由球初始能量倍数（见下）
    double ballSpawnMinDist = 10.0;     // R8/P2-1：产球出生点距产者的最小环面距离
    double ballSpawnMaxDist = 30.0;     // R8/P2-1：产球出生点距产者的最大环面距离
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

    // ---- R1-B：捕食转化率公式（v2.3.1 语义：mode 0/1/2/3，最后 clamp 到
    // [conversionMinRate, conversionMaxRate]）----
    //   cyanoConversionRate(mode, maxSpeed, maxAccel, digestion)：
    //     mode 0：常数 rate（maxSpeed/maxAccel 不参与）
    //     mode 1：速度惩罚   rate = base * (speedRef / (speedRef + maxSpeed))
    //     mode 2：加速度惩罚 rate = base * (accelRef / (accelRef + maxAccel))
    //     mode 3：混合       rate = base * wSpeed * speedRef/(speedRef + maxSpeed)
    //                              * wAccel * accelRef/(accelRef + maxAccel)
    //   maxSpeed <= 0（或 maxAccel <= 0）时对应惩罚项取 1（无速度/无机动 = 不受罚）。
    //   base = conversionRate * digestion（基因 cyanoPreference/消化率在此生效）。
    int conversionMode = 1;             // 0 常数 / 1 速度惩罚 / 2 加速度惩罚 / 3 混合
    double conversionRate = 0.6;        // 基础转化率（与旧组合根 kGrazeConversion 同值）
    double conversionExponent = 1.0;    // 惩罚指数（R1-B 占位：预留非线性形状；=1 即 v2.3.1 线性比式）
    double conversionSpeedRef = 40.0;   // 速度参考值（= 行为层默认 maxSpeed 同量级）
    double conversionAccelRef = 60.0;   // 加速度参考值（= 行为层默认 moveK 同量级）
    double conversionWSpeed = 1.0;      // mode 3 的速度惩罚权重
    double conversionWAccel = 1.0;      // mode 3 的加速度惩罚权重
    double conversionMinRate = 0.05;    // 转化率下限（clamp）
    double conversionMaxRate = 1.0;     // 转化率上限（clamp）

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

    // ---- R3-A：捕食避难所（graze refuge）----
    // graze() 只能抽到 (energy - grazeRefugeEnergy) 为止：节点保留一份不可
    // 捕食的储备，被捕食**永远不会**把节点抽死。生态学依据：猎物避难所
    // （refuge）是捕食者-猎物系统稳定共存的标准条件——没有避难所时过度
    // 捕食会把生产者吃绝（实测 seed42 photo=0.7：cyano 27 -> 0 -> 食物链
    // 断裂 -> 全生态崩溃，捕食只在构造期播种，灭绝不可逆）。代谢饿死通道
    // 不受影响（负反馈仍在：遮蔽过密时节点照常饿死）。
    // 模块默认 0.0 = 旧语义逐位（既有单测钉死 graze 抽干致死路径）；标定值
    // 由 Simulation 构造期/applyConfig 从 CyanoTuning 镜像（默认 15）。
    double grazeRefugeEnergy = 0.0;
};

struct CyanoNode {
    EntityId id = 0;
    Vec2 pos{};
    double energy = 0.0;
    bool alive = true;
    double hue = 0.0;   // R1-B：遗传色相 ∈ [0,360)（seed 均匀采样；分裂 = 父 + 高斯抖动）
};

// 一帧的收支报告：本模块不碰账本，只报数字，由组合根记账。
struct CyanoStepReport {
    double photosynthesisGain = 0.0;  // 本帧光合总得（需 injectPrimary）
    double metabolismLoss = 0.0;      // 本帧代谢总耗（需 dissipate）
    double reproductionLoss = 0.0;    // 本帧分裂总耗散（子代没拿到的那部分）
    int births = 0;                   // 新增蓝细菌数
    int ballsProduced = 0;            // 产出的自由球数
    double ballEnergyTotal = 0.0;     // 产出球带走的能量总额
    double linkTransferred = 0.0;     // 本帧连线转移总量（仅观测；零和不影响账本）
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
    CyanoStepReport step(double sunLevel, double worldW, double worldH, PCG32& rng);

    // 被核捕食：taken = min(rawEnergy * conversionRate, energy - grazeRefugeEnergy)
    //（refuge <= 0 时退化为旧式 min(want, node.energy)）。refuge > 0 时被捕食
    // 不会把节点抽死（储备不可食）；仅当节点能量本来就 <= refuge 时 taken=0。
    // want*rate 仍可能耗尽 (energy - refuge) 以下？不会：taken 上限 = 余量，
    // energy 恒 >= refuge（本函数内）；抽干到 refuge 后 alive 恒保持。
    // 例外：refuge <= 0 且节点被抽干到 0 时 alive=false（旧语义，回归逐位）。
    // id 不存在或节点已死返回 0。
    double graze(EntityId id, double rawEnergy, double conversionRate);

    // ---- R3-A（营养循环）：尸体能量真实转移 ----
    // 把 amount 能量给 id 指定的存活节点（受 energyCap 限制，溢出部分返回给
    // 调用方耗散）。返回实际接受量；找不到 / 已死 / amount 非正返回 0。
    // 进账口径与光合的 credited 一致：绝不入负账、绝不越限。组合根按接受量
    // 记 ledger.transfer（零和），溢出量记 dissipate——能量绝不滞留在模块内。
    double fertilize(EntityId nodeId, double amount);

    const std::vector<CyanoNode>& nodes() const { return nodes_; }
    std::size_t aliveCount() const;
    double totalEnergy() const;
    const CyanoParams& params() const { return params_; }

    // ---- R1-B：捕食转化率公式（纯函数，无 rng；公式见 CyanoParams 注释）----
    static double cyanoConversionRate(int mode, double baseRate, double exponent,
                                      double speedRef, double accelRef,
                                      double wSpeed, double wAccel,
                                      double minRate, double maxRate,
                                      double maxSpeed, double maxAccel);

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

    // 最近一次 step() 产出的自由球出生点（环面内坐标；每次 step() 清空重建）。
    // 供组合根实际投放自由球实体；本模块只定坐标，不持有球实体。
    // R8/P2-1：出生点在产者附近随机（距产者 [ballSpawnMinDist, ballSpawnMaxDist]
    // 的环面距离，方向均匀随机后 wrap 回环面内）。
    const std::vector<Vec2>& lastBallSpawns() const { return ballSpawns_; }


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
    std::vector<Vec2> ballSpawns_;
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
