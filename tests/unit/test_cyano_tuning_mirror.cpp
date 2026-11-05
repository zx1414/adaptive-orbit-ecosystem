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
    t.ballSpawnCost        = sent(9);
    t.ballSpawnProbability = sent(10);
    t.ballRadius           = sent(11);
    t.ballSpawnMinDist     = sent(12);
    t.ballSpawnMaxDist     = sent(13);
    t.linkRadius           = sent(14);
    t.linkMaxNeighbors     = 14;               // int 字段：序号值（非默认 3）
    t.linkTransferRate     = sent(15);
    t.linkDeadband         = sent(16);
    t.linkCheckInterval    = 17;               // int 字段：非默认 1
    t.conversionMode       = 18;               // int 字段：非默认 1
    t.conversionRate       = sent(19);
    t.conversionExponent   = sent(20);
    t.conversionSpeedRef   = sent(21);
    t.conversionAccelRef   = sent(22);
    t.conversionWSpeed     = sent(23);
    t.conversionWAccel     = sent(24);
    t.conversionMinRate    = sent(25);
    t.conversionMaxRate    = sent(26);
    t.hueJitterSigma       = sent(27);
    t.cyanoColonies        = 28;               // int 字段：非默认 4（R7-C）
    t.clusterSpread        = sent(29);         // R7-C
    t.shadeK               = sent(30);
    t.shadeRadius          = sent(31);
    t.shadeSigma           = sent(32);
    t.grazeRefugeEnergy    = sent(33);
    return t;
}

// 断言链：CyanoParams 每个镜像字段 == Tuning 侧哨兵。逐字段手写（防回归本体）。
void checkMirror(const CyanoParams& p, const CyanoTuning& t) {
    CHECK_NEAR(p.photosynthesisRate,      t.photosynthesisRate, 1e-9);
    CHECK_NEAR(p.lightHalfSaturation,     t.lightHalfSaturation, 1e-9);
    CHECK_NEAR(p.metabolismRate,          t.metabolismRate, 1e-9);
    CHECK_NEAR(p.energyCap,               t.energyCap, 1e-9);
    CHECK_NEAR(p.reproductionCost,        t.reproductionCost, 1e-9);
    CHECK_NEAR(p.reproductionProbability, t.reproductionProbability, 1e-9);
    CHECK_NEAR(p.reproductionOffset,      t.reproductionOffset, 1e-9);
    CHECK_NEAR(p.minSeparation,           t.minSeparation, 1e-9);
    CHECK_NEAR(p.ballSpawnCost,           t.ballSpawnCost, 1e-9);
    CHECK_NEAR(p.ballSpawnProbability,    t.ballSpawnProbability, 1e-9);
    CHECK_NEAR(p.ballRadius,              t.ballRadius, 1e-9);
    CHECK_NEAR(p.ballSpawnMinDist,        t.ballSpawnMinDist, 1e-9);
    CHECK_NEAR(p.ballSpawnMaxDist,        t.ballSpawnMaxDist, 1e-9);
    CHECK_NEAR(p.linkRadius,              t.linkRadius, 1e-9);
    CHECK(p.linkMaxNeighbors == t.linkMaxNeighbors);
    CHECK_NEAR(p.linkTransferRate,        t.linkTransferRate, 1e-9);
    CHECK_NEAR(p.linkDeadband,            t.linkDeadband, 1e-9);
    CHECK(p.linkCheckInterval == t.linkCheckInterval);
    CHECK(p.conversionMode == t.conversionMode);
    CHECK_NEAR(p.conversionRate,          t.conversionRate, 1e-9);
    CHECK_NEAR(p.conversionExponent,      t.conversionExponent, 1e-9);
    CHECK_NEAR(p.conversionSpeedRef,      t.conversionSpeedRef, 1e-9);
    CHECK_NEAR(p.conversionAccelRef,      t.conversionAccelRef, 1e-9);
    CHECK_NEAR(p.conversionWSpeed,        t.conversionWSpeed, 1e-9);
    CHECK_NEAR(p.conversionWAccel,        t.conversionWAccel, 1e-9);
    CHECK_NEAR(p.conversionMinRate,       t.conversionMinRate, 1e-9);
    CHECK_NEAR(p.conversionMaxRate,       t.conversionMaxRate, 1e-9);
    CHECK_NEAR(p.hueJitterSigma,          t.hueJitterSigma, 1e-9);
    CHECK(p.cyanoColonies == t.cyanoColonies);   // R7-C
    CHECK_NEAR(p.clusterSpread,           t.clusterSpread, 1e-9); // R7-C
    CHECK_NEAR(p.shadeK,                  t.shadeK, 1e-9);
    CHECK_NEAR(p.shadeRadius,             t.shadeRadius, 1e-9);
    CHECK_NEAR(p.shadeSigma,              t.shadeSigma, 1e-9);
    CHECK_NEAR(p.grazeRefugeEnergy,       t.grazeRefugeEnergy, 1e-9);
}

} // namespace

// 构造期镜像：哨兵 Tuning 构造 Simulation，CyanoParams 每个字段必须等于哨兵。
TEST(cyano_tuning_mirror_completeness_ctor) {
    SimConfig cfg;
    cfg.world.seed = 42;
    cfg.cyanoTuning = makeSentinel();
    Simulation sim(cfg);
    checkMirror(sim.cyanoParams(), cfg.cyanoTuning);
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
    next.ballSpawnCost        = sent(109);
    next.ballSpawnProbability = sent(110);
    next.ballRadius           = sent(111);
    next.ballSpawnMinDist     = sent(112);
    next.ballSpawnMaxDist     = sent(113);
    next.linkRadius           = sent(114);
    next.linkMaxNeighbors     = 115;          // int：非默认非第一组
    next.linkTransferRate     = sent(116);
    next.linkDeadband         = sent(117);
    next.linkCheckInterval    = 118;
    next.conversionMode       = 119;
    next.conversionRate       = sent(120);
    next.conversionExponent   = sent(121);
    next.conversionSpeedRef   = sent(122);
    next.conversionAccelRef   = sent(123);
    next.conversionWSpeed     = sent(124);
    next.conversionWAccel     = sent(125);
    next.conversionMinRate    = sent(126);
    next.conversionMaxRate    = sent(127);
    next.hueJitterSigma       = sent(128);
    next.cyanoColonies        = 130;          // R7-C：非默认 4 非第一组 28
    next.clusterSpread        = sent(131);    // R7-C
    next.shadeK               = sent(132);
    next.shadeRadius          = sent(133);
    next.shadeSigma           = sent(134);
    next.grazeRefugeEnergy    = sent(135);
    // applyConfig 只替换 cyanoTuning（其余保持构造期配置，重点隔离被测面）。
    SimConfig nextCfg = cfg;
    nextCfg.cyanoTuning = next;
    sim.applyConfig(nextCfg);
    checkMirror(sim.cyanoParams(), next);
}
