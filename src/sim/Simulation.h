// agentsim - sim/Simulation.h
// 组合根：把环境/感知/决策/行为/账本拼成主循环。只负责编排，不改实体规则。
// R1-B（蓝细菌取代食物）：Food 实体整条链路删除——foodCount()/foods() 已移除，
// 核的唯一食物 = 蓝细菌节点（step() 内确定性捕食，见 Simulation.cpp R1-B 注释）。
#pragma once

#include <iosfwd>  // R5-C：序列化流参数
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "core/Config.h"
#include "core/PCG32.h"
#include "core/Types.h"
#include "behavior/AgentBehavior.h"
#include "decision/IDecisionPolicy.h"
#include "environment/AgentEnvironment.h"
#include "environment/CyanoNode.h"
#include "environment/Terrain.h"
#include "environment/TemperatureField.h"
#include "ledger/AgentLedger.h"

namespace agentsim {

class IDecisionPolicy;

class Simulation {
public:
    explicit Simulation(const SimConfig& cfg);
    ~Simulation();
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;

    void step();
    bool runTo(std::uint64_t endTick);
    // ---- R5-C：存档序列化（实现见 Simulation.cpp 尾部）----
    // 全量实体状态（含 genome/rng 流水/账本）小端写读。读回后 stateHash 与
    // 序列化前逐位一致（test_save 的 roundtrip 用例把守）；auditOk 立即成立。
    bool serializeState(std::ostream& os) const;
    bool deserializeState(std::istream& is, std::string& err);
    // 世界线分支（Timeline 调用）：实体与 rng 保留，tick 归零（昼夜相位/统计
    // 节拍从 0 重算；world 几何/seed 由调用方保持不变）。
    void restartClock() { tick_ = 0; }

    // ---- R3-B（实时调控接线）追加：运行时热改入口（只追加，绝不改 step()）----
    // applyConfig(next)：把 next.cfg 融合进 cfg_ 后下一帧生效。融合规则：
    //   - world.width/height/seed/maxTicks 不热改（世界几何/种子是确定性根，
    //     "改几何必须重开局"；maxTicks 归 CLI 批跑契约）；
    //   - colony.nuclei / cyanoNodeCount / cyanoMaxNodes 是**出生/播种引导**参数，
    //     热改不重建世界（初始种群不重撒），故维持原值；cyano 节点补充走
    //     addCyanoNodes 事件路径；
    //   - 其余全部拷贝：sun（下一帧 step() 直接读 cfg_.sun）、policy、terrain 参数、
    //     runtime/nucleus/ball/cyanoTuning。cyanoTuning 与 BehaviorParams 同步：
    //     respirationBase / locomotionCost / cyanoPredationRadius 三个键在 bp_ 有
    //     既有同义字段，逐字段写入（行为层 apply/捕食扫描立即消费）；
    //   - CyanoParams 经白盒镜像逐字段同步进 cyano_（含 genomeRanges 等价物——
    //     无，CyanoParams 无区间概念），光合/代谢/分裂/产球/连线参数下一帧生效；
    //   - genomeRanges：只影响之后出生的核（spawnNuclei/reproduce 每次从
    //     cfg_.colony.genomeRanges 现取），已存在的 genome 不改（基因不能后天改）；
    //   - RNG：本函数零消耗，双跑确定性不受影响。
    void applyConfig(const SimConfig& next);
    // 事件 bloom 用：向蓝细菌场追加 n 个节点（播种消耗 rng，等价一次小规模
    // CyanoField::seed 的追加版；受 cyanoMaxNodes 护栏截断，返回实际追加数）。
    // 能量账：追加节点的能量走 ledger（注入/耗散对由账本自证平衡）。
    int addCyanoNodes(int n);
    // 蓝细菌参数访问器（供测试断言 CyanoParams 白盒同步是否生效）。
    const CyanoParams& cyanoParams() const { return cyano_.params(); }
    // 当前生效配置的只读视图（测试/诊断用：断言 applyConfig 的 merge 结果）。
    const SimConfig& cfgProbe() const { return cfg_; }

    Tick tick() const { return tick_; }
    std::size_t nucleusCount() const;
    double totalEnergy() const;
    std::uint64_t stateHash() const;
    bool auditOk() const { return auditOk_; }
    // 运行时能量审计默认关闭（代码写对自然守恒；每帧全量求和是纯开销）。
    // setAuditEnabled(true) 打开后，step() 每帧校验零和恒等式与两条局部闭合断言，
    // 失败置 auditOk_ = false（runTo 随即返回 false）。单元测试自开。
    void setAuditEnabled(bool on) { auditEnabled_ = on; }
    bool auditEnabled() const { return auditEnabled_; }
    std::uint64_t deathCount() const { return deathCount_; }

    // ---- 只读视图（组合根接线用，S15 轨迹导出消费）----
    // 只暴露 const 引用，外部无法写入，不破坏"行为层是唯一写点"红线。
    // 顺序即内部容器下标顺序，确定性渲染/序列化依赖它。
    const std::vector<Nucleus>& nuclei() const { return nuclei_; }
    const AgentLedger& ledger() const { return ledger_; }
    // R3-B（实时调控接线）：事件执行通道的可变访问器（main.cpp --serve 的
    // harvest/winter/plague 事件必须走 ledger 记账）。组合根main 是账本的合法
    // 记账方（与行为层并列的两处调用点之一）；实体能量写入仍在行为层 apply
    // 与组合根事件通道（守卫 1c 豁免清单已覆盖 spawnNuclei，事件写点在报告中声明）。
    AgentLedger& ledger() { return ledger_; }
    // S19/S20：已脱离的自由球（ownerId == 0）。前端与轨迹导出必须能看到它们，
    // 否则"球脱离了"这件事在数据上不可见（曾经 TraceWriter 只遍历 n.balls，
    // 导致自由球计数恒为 0，误判成 S19 未实现）。
    const std::vector<Ball>& freeBalls() const { return freeBalls_; }
    // S22 接线：蓝细菌场只读视图（trace/前端观测光合-代谢-分裂与产球状态）。
    const CyanoField& cyano() const { return cyano_; }
    // T29（契约 v2）：直取蓝细菌节点容器的便捷视图。与 cyano().nodes() 等价，
    // 顺序即 nodes_ 下标（trace 双跑一致性依赖它）；饿死节点在当帧末被压缩，
    // 被捕食节点由 graze() 标记后于同一帧末清除——容器只含存活节点。
    const std::vector<CyanoNode>& cyanoNodes() const { return cyano_.nodes(); }
    // R1-B：连线拓扑只读视图（前端画连线网络；cyano().links() 的转发）。
    const std::vector<std::pair<EntityId, EntityId>>& cyanoLinks() const {
        return cyano_.links();
    }

private:
    void spawnNuclei();

    // ---- R7-B：球世界积分（本卡实现）----
    // ballWorldStep：全部球（nuclei_[i].balls + freeBalls_）的力场求和 + 积分。
    // 在行为层 apply 之后调用（球能看到本帧新核/新球/新能量，无一帧旧位残差）。
    // 无自由球且全菌落无球时 O(nuclei) 早退（成本一次 bool 扫描），逐位回归安全。
    void ballWorldStep();
    // ballOwnership：归属结算（v231 updateOwnership 语义：detachRadius 内最近核）。
    // 复用 R2-A targets 扫描（freeBalls 拾取已做，不重复）；消耗 rng 的核逐球扫描，
    // 无球核跳过。换主/失主经 AgentBehavior::reassignBall（账目在行为层）。
    void ballOwnership();

    SimConfig cfg_{};
    PCG32 rng_;
    // TERRAIN 接线：只读地形（障碍/水域/温度区）。默认三 count 全 0 时不生成、
    // 不消耗 rng、零影响（回归安全）。声明顺序须与构造初始化顺序一致。
    Terrain terrain_;
    AgentEnvironment env_;
    // GOAL 温度接线：组合根持有温度场（构造期用 rng_ 撒源，step() 只读传入感知层）。
    TemperatureField temp_;
    // S22 接线：环境层蓝细菌场（生产者，R1-B 起兼作唯一食物）。step() 在
    // env_.tick() 之前推进；账目由组合根按 CyanoStepReport 记入 ledger_
    //（模块本身不碰账本）。
    CyanoField cyano_;
    AgentLedger ledger_;
    std::vector<Nucleus> nuclei_;
    // R7-B：球世界积分（本卡实现）
    // S19/S20：自然脱离后成为自由球的容器。每帧把各核 detached_ 取走转存于此，
    // 做位置积分与环面 wrap；能量始终留在球内（不耗散），因此必须计入 totalEnergy。
    std::vector<Ball> freeBalls_;
    // R7-B：球力场参数（行为层 BallFieldParams 的运行时生效副本；构造期从
    // cfg_.ball 逐字段镜像，applyConfig 同步热改。默认值 = v2.3.1 env_config 标定；
    // cfg_.ball 的 R6-C 字段覆盖同名语义，near_repel_radius/detach_radius/
    // ball_ball_repel_radius/same_owner_attract_radius 按 R6-C Config.h 注释取
    //（8.0/60.0/6.0/40.0）——行为层结构默认保留 v231 原值供独立单测）。
    BallFieldParams ballField_{};   // R7-B（behavior 命名空间域结构，类外）
    std::unique_ptr<IDecisionPolicy> policy_;
    BehaviorParams bp_{};
    // R2-A §8：待分配尸体营养包（核死亡 30% 遗产，apply 填充 → 本容器中转 →
    // 当帧分配给最近的蓝细菌节点）。能量始终在账：totalEnergy() 计入 carrion_。
    std::vector<Carrion> carrion_;
    // R3-A（营养循环）：carrion 能量流水（逐包累加）。in = 死核装包入队的
    // 能量；out = 分配时实际出账的能量（fertilize 接受量 + 溢出/无分解者的
    // dissipate 量）。carrion_ 每帧清空 => 流水每帧闭合：|in - out| 只剩浮点
    // 尾数（逐包 ≤1 ulp），step() 审计段断言 1e-9 相对容差内相等——"包被取走
    // 却既没转移也没耗散"（能量滞留/蒸发）当场置 auditOk_=false。
    double carrionEnergyIn_ = 0.0;
    double carrionEnergyOut_ = 0.0;
    Tick tick_ = 0;
    bool auditOk_ = true;
    bool auditEnabled_ = false; // 运行时审计开关：默认关（opt-in），见 setAuditEnabled
    std::size_t aliveCount_ = 0;
    std::uint64_t deathCount_ = 0;
    EntityId nextNucleusId_ = 1;
    // S22：蓝细菌产出的自由球 id 空间（0x40000000 起，严格递增）。
    // 避开核（1 起递增）与蓝细菌池（0）的小 id 空间，防 id 混淆。
    EntityId nextFreeBallId_ = 0x40000000ULL;
};

} // namespace agentsim
