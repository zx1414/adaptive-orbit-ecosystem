// agentsim - tests/unit/test_cyano_tuning_mirror.cpp
// R7-C（applyConfig 镜像完整性审计）：CyanoTuning -> CyanoParams 白盒镜像
// 防漏回归锁。上轮教训：applyConfig 逐字段镜像漏字段 = "热调参冲掉值"的
// 静默 bug（shade 三参数最先暴露）。
//
// 防漏手段（编译器不能反射，逐字段手写断言是防回归的最实际手段）：
//   1. 把 CyanoTuning 的每个字段设成唯一哨兵值（1.234 + 字段序号*0.001，
//      int 字段用序号+7），构造 Simulation——构造期镜像必须把每个哨兵带进
//      CyanoParams 对应字段；
//   2. applyConfig 热调控：改哨兵重跑，断言热路径镜像同样逐字段生效。
//   未来加字段忘了两侧同步，本文件对应断言先编译失败（缺字段行不存在），
//   或运行时红（镜像行缺失）——两条路都拦得住。
//
// 注意镜像契约的两类"有意不同步"（不算漏镜像，哨兵断言显式绕开）：
//   - nodeSeedCount / maxNodes：出生引导量，构造期由 SimConfig.cyanoNodeCount /
//     cyanoMaxNodes 覆盖（applyConfig 显式不回写）——本测试只验证它们保持
//     构造期值不变（热调控不冲掉）。
//   - conversionRate 等注释声明"实际值非提案值"的字段：镜像就是同名字段直通，
//     哨兵从 Tuning 侧注入即可，无特殊处理。
//
// ---- 规则 v2（P0–P2）清单迁移（本文件的哨兵清单必须与 CyanoTuning 字段表一一对应）----
// 删除（对应生产字段已从两处结构里消失，哨兵行同步删除）：
//   ballSpawnCost / ballSpawnProbability / ballRadius / ballSpawnMinDist /
//   ballSpawnMaxDist                 —— 蓝细菌不再产球（验收 A11）
//   conversionMode / conversionExponent / conversionSpeedRef / conversionAccelRef /
//   conversionWSpeed / conversionWAccel / conversionMinRate / conversionMaxRate
//                                    —— 按速度折减转化率的整条通道（验收 A1）
//   grazeRefugeEnergy                —— 每节点不可食保底（验收 A2/A3）
// 新增（哨兵行同步补上，保持"每个字段都被覆盖"这条防漏契约完整）：
//   minStructure / maxMaterial / initialMaterial / growthPerTick /
//   nutrientPerMaterial / growthEnergyCost / assimilation
//   —— 组织（material）与养分口径（设计 §3.1–§3.2 / 验收 A6–A8）
// 新增（2026-11-25，随规则 v2 的"维持开销"一起进来的字段）：
//   maintenancePerMaterial           —— 每单位组织的每帧维持开销（规则 v2 的主要
//                                       能量出口，撤掉产球后的负反馈来源）
// 说明：nutrientRegionSize / nutrientInitialPerRegion / decomposeRate 是**养分场**
// 的参数（NutrientField / Simulation::step 直接从 cfg 读，不经 CyanoParams 镜像），
// 因此没有对应镜像字段，少一条断言是语义正确的，不是覆盖缺口。
// seedEnergy 是本类"有意不镜像"的第三个字段（入口是 cfg.runtime.cyanoInitialEnergy），
// 由 cyano_seed_energy_comes_from_runtime_not_tuning 单独钉住。
#include "test_framework.h"

#include <cstddef>

#include "../../src/core/Config.h"
#include "../../src/sim/Simulation.h"

namespace {

using agentsim::CyanoParams;
using agentsim::CyanoTuning;
using agentsim::Simulation;
using agentsim::SimConfig;

// 双精度哨兵：1.234 + k*0.001，k = 字段序号（唯一性 eps 1e-9 下足够）。
double sent(int k) { return 1.234 + 0.001 * static_cast<double>(k); }

// 哨兵注入：每个 CyanoTuning 字段一个唯一值。新增字段时在此补一行 +
// 在下方 CHECK 链补对应断言——漏补会导致对应字段保持默认值而断言失败
//（默认值不等于任何哨兵，除了 setter 与断言都漏的极端情况——那不是镜像
// bug，是本测试的覆盖缺口，报告里写明）。
//
// 序号 1..N 连续使用（不重用已删除字段的号），便于人眼核对"当前字段数 == N"。
CyanoTuning makeSentinel() {
    CyanoTuning t;
    t.photosynthesisRate   = sent(1);
    t.lightHalfSaturation  = sent(2);
    t.metabolismRate       = sent(3);
    t.energyCap            = sent(4);
    t.reproductionCost     = sent(5);
    t.reproductionProbability = sent(6);
    t.reproductionOffset   = sent(7);
    t.minSeparation        = sent(8);
    t.linkRadius           = sent(9);
    t.linkMaxNeighbors     = 10;               // int 字段：非默认 3
    t.linkTransferRate     = sent(11);
    t.linkDeadband         = sent(12);
    t.linkCheckInterval    = 13;               // int 字段：非默认 1
    t.conversionRate       = sent(14);         // 规则 v2：唯一的同化率基率
    t.hueJitterSigma       = sent(15);
    t.cyanoColonies        = 16;               // int 字段：非默认 64（R10）
    t.clusterSpread        = sent(17);         // R7-C
    t.shadeK               = sent(18);
    t.shadeRadius          = sent(19);
    t.shadeSigma           = sent(20);
    // ---- 规则 v2：组织 / 养分参数 ----
    t.minStructure         = sent(21);
    t.maxMaterial          = sent(22);
    t.initialMaterial      = sent(23);
    t.growthPerTick        = sent(24);
    t.nutrientPerMaterial  = sent(25);
    t.growthEnergyCost     = sent(26);
    t.assimilation         = sent(28);
    return t;
}

// 断言链：CyanoParams 每个镜像字段 == Tuning 侧哨兵。逐字段手写（防回归本体）。
// 顺序与 makeSentinel() 一致，人眼可双向核对。
void checkMirror(const CyanoParams& p, const CyanoTuning& t) {
    CHECK_NEAR(p.photosynthesisRate,      t.photosynthesisRate, 1e-9);
    CHECK_NEAR(p.lightHalfSaturation,     t.lightHalfSaturation, 1e-9);
    CHECK_NEAR(p.metabolismRate,          t.metabolismRate, 1e-9);
    CHECK_NEAR(p.energyCap,               t.energyCap, 1e-9);
    CHECK_NEAR(p.reproductionCost,        t.reproductionCost, 1e-9);
    CHECK_NEAR(p.reproductionProbability, t.reproductionProbability, 1e-9);
    CHECK_NEAR(p.reproductionOffset,      t.reproductionOffset, 1e-9);
    CHECK_NEAR(p.minSeparation,           t.minSeparation, 1e-9);
    CHECK_NEAR(p.linkRadius,              t.linkRadius, 1e-9);
    CHECK(p.linkMaxNeighbors == t.linkMaxNeighbors);
    CHECK_NEAR(p.linkTransferRate,        t.linkTransferRate, 1e-9);
    CHECK_NEAR(p.linkDeadband,            t.linkDeadband, 1e-9);
    CHECK(p.linkCheckInterval == t.linkCheckInterval);
    CHECK_NEAR(p.conversionRate,          t.conversionRate, 1e-9);
    CHECK_NEAR(p.hueJitterSigma,          t.hueJitterSigma, 1e-9);
    CHECK(p.cyanoColonies == t.cyanoColonies);   // R7-C
    CHECK_NEAR(p.clusterSpread,           t.clusterSpread, 1e-9); // R7-C
    CHECK_NEAR(p.shadeK,                  t.shadeK, 1e-9);
    CHECK_NEAR(p.shadeRadius,             t.shadeRadius, 1e-9);
    CHECK_NEAR(p.shadeSigma,              t.shadeSigma, 1e-9);
    // ---- 规则 v2：组织 / 养分参数 ----
    CHECK_NEAR(p.minStructure,            t.minStructure, 1e-9);
    CHECK_NEAR(p.maxMaterial,             t.maxMaterial, 1e-9);
    CHECK_NEAR(p.initialMaterial,         t.initialMaterial, 1e-9);
    CHECK_NEAR(p.growthPerTick,           t.growthPerTick, 1e-9);
    CHECK_NEAR(p.nutrientPerMaterial,     t.nutrientPerMaterial, 1e-9);
    CHECK_NEAR(p.growthEnergyCost,        t.growthEnergyCost, 1e-9);
    CHECK_NEAR(p.assimilation,            t.assimilation, 1e-9);
}

} // namespace

// 构造期镜像：哨兵 Tuning 构造 Simulation，CyanoParams 每个字段必须等于哨兵。
TEST(cyano_tuning_mirror_completeness_ctor) {
    SimConfig cfg;
    cfg.world.seed = 42;
    cfg.cyanoTuning = makeSentinel();
    Simulation sim(cfg);
    checkMirror(sim.cyanoParams(), cfg.cyanoTuning);
    // 哨兵必须与默认值不同（否则"镜像漏了一行"会被默认值巧合掩盖）。
    const CyanoTuning dflt;
    CHECK(sim.cyanoParams().minStructure != dflt.minStructure);
    CHECK(sim.cyanoParams().assimilation != dflt.assimilation);
    CHECK(sim.cyanoParams().growthPerTick != dflt.growthPerTick);
}

// 热路径镜像：applyConfig 换一组哨兵，CyanoParams 必须逐字段跟着走——
// "热调参冲掉值"/"热调参不生效"两个方向的静默 bug 都在本断言暴露。
TEST(cyano_tuning_mirror_completeness_hot) {
    SimConfig cfg;
    cfg.world.seed = 42;
    cfg.cyanoTuning = makeSentinel();
    Simulation sim(cfg);

    // 第二组哨兵：第一组 + 偏移（保证与第一组不同且非默认）。
    CyanoTuning next = makeSentinel();
    next.photosynthesisRate   = sent(101);
    next.lightHalfSaturation  = sent(102);
    next.metabolismRate       = sent(103);
    next.energyCap            = sent(104);
    next.reproductionCost     = sent(105);
    next.reproductionProbability = sent(106);
    next.reproductionOffset   = sent(107);
    next.minSeparation        = sent(108);
    next.linkRadius           = sent(109);
    next.linkMaxNeighbors     = 110;          // int：非默认非第一组
    next.linkTransferRate     = sent(111);
    next.linkDeadband         = sent(112);
    next.linkCheckInterval    = 113;
    next.conversionRate       = sent(114);
    next.hueJitterSigma       = sent(115);
    next.cyanoColonies        = 116;          // R7-C：非默认 64 非第一组 16
    next.clusterSpread        = sent(117);    // R7-C
    next.shadeK               = sent(118);
    next.shadeRadius          = sent(119);
    next.shadeSigma           = sent(120);
    next.minStructure         = sent(121);
    next.maxMaterial          = sent(122);
    next.initialMaterial      = sent(123);
    next.growthPerTick        = sent(124);
    next.nutrientPerMaterial  = sent(125);
    next.growthEnergyCost     = sent(126);
    next.assimilation         = sent(128);
    // applyConfig 只替换 cyanoTuning（其余保持构造期配置，重点隔离被测面）。
    SimConfig nextCfg = cfg;
    nextCfg.cyanoTuning = next;
    sim.applyConfig(nextCfg);
    checkMirror(sim.cyanoParams(), next);
    // 反向验证：热改确实离开了第一组哨兵（不是"两次相同所以看起来都对"）。
    CHECK(sim.cyanoParams().minStructure != cfg.cyanoTuning.minStructure);
    CHECK(sim.cyanoParams().assimilation != cfg.cyanoTuning.assimilation);
}

// ---- seedEnergy：**有意不镜像**的一个字段，必须被显式钉住 ----
// CyanoTuning.seedEnergy 存在，但组合根**不**读它——播种能量取自
// cfg.runtime.cyanoInitialEnergy（与 nodeSeedCount/maxNodes 同类的"构造期引导量"）。
// 这是典型的"两个配置默认值"风险点：字段在参数页里能改，运行时不生效。
// 本用例把**实际生效的那个入口**钉死，并显式记录另一个入口的不生效，
// 让任何未来的"顺手接通"或"顺手删掉"都必须先改这条断言。
// （已向 Lead 报告该字段当前是死字段，是否接线由产品决定。）
TEST(cyano_seed_energy_comes_from_runtime_not_tuning) {
    SimConfig cfg;
    cfg.world.seed = 42;
    cfg.cyanoTuning.seedEnergy = 7.0;      // 参数页入口：当前**不**生效
    cfg.runtime.cyanoInitialEnergy = 33.0; // 组合根真实读取的入口
    Simulation sim(cfg);
    CHECK_NEAR(sim.cyanoParams().seedEnergy, 33.0, 1e-12);
    // 播种出来的节点能量确实等于这个值（不是巧合：seed() 直接赋 params_.seedEnergy）。
    CHECK(sim.cyanoNodes().size() > 0);
    if (!sim.cyanoNodes().empty()) {
        CHECK_NEAR(sim.cyanoNodes().front().energy, 33.0, 1e-12);
    }
    // 热改两个入口：仍然只有 runtime 那个生效（行为逐位可预测）。
    SimConfig next = cfg;
    next.cyanoTuning.seedEnergy = 9.0;
    next.runtime.cyanoInitialEnergy = 44.0;
    sim.applyConfig(next);
    CHECK_NEAR(sim.cyanoParams().seedEnergy, 44.0, 1e-12);
}
