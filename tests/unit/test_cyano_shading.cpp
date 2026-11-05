// agentsim - tests/unit/test_cyano_shading.cpp
// R3-A（生态负反馈）单测：自遮光（局部光竞争）。
// 覆盖：遮蔽量高斯核（对称/半径截断/自项为 0）、局部阳光抑制光合、
// 密度负反馈（越密单位产量越低）、孤立节点逐位退回旧公式、
// 零 rng 消耗、关连线时遮蔽仍生效（退化分支）、applyConfig 热调控
// 不冲掉 shade 参数（三处镜像同步的回归锁）。
#include "test_framework.h"

#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "../../src/core/PCG32.h"
#include "../../src/core/Types.h"
#include "../../src/core/Vec2.h"
#include "../../src/environment/CyanoNode.h"
#include "../../src/sim/Simulation.h"

namespace {

using agentsim::CyanoField;
using agentsim::CyanoNode;
using agentsim::CyanoParams;
using agentsim::CyanoStepReport;
using agentsim::PCG32;
using agentsim::Simulation;
using agentsim::SimConfig;
using agentsim::Vec2;

// 测试专用直改辅助（生产代码不得效仿）。
CyanoNode& mut(CyanoField& f, std::size_t i) {
    return const_cast<CyanoNode&>(f.nodes()[i]);
}
CyanoNode& sole(CyanoField& f) {
    return mut(f, 0);
}

} // namespace

// 单节点孤立：shade_=0，exp(-k*0)=1 —— 与旧全局阳光公式逐位一致（回归锁）。
TEST(shade_solitary_node_bitwise_matches_legacy) {
    CyanoParams p;
    p.metabolismRate = 0.0;
    p.ballSpawnProbability = 0.0;
    p.reproductionProbability = 0.0;
    p.linkRadius = 0.0; // 退化分支：无邻居，shade 恒 0
    CyanoField f(p);
    PCG32 rng(701);
    f.seed(1, 500.0, 500.0, rng);
    sole(f).energy = 10.0;

    // sun=0.5, halfSat=0.5, rate=1 -> gain=0.5（与 cyano_photosynthesis_formula 同值）。
    const CyanoStepReport r = f.step(0.5, 500.0, 500.0, rng);
    CHECK_NEAR(r.photosynthesisGain, 0.5, 1e-12);
    CHECK_NEAR(sole(f).energy, 10.5, 1e-12);
    CHECK(f.shadeCache().empty() || f.shadeCache()[0] == 0.0);
}

// 两节点摆位遮蔽：shade = exp(-d²/2σ²)；d>=radius 时互不遮蔽。
TEST(shade_pair_gaussian_weight) {
    CyanoParams p;
    p.metabolismRate = 0.0;
    p.ballSpawnProbability = 0.0;
    p.reproductionProbability = 0.0;
    p.linkRadius = 0.0; // 走退化分支的仅遮蔽扫描（关连线不关遮蔽）
    p.shadeK = 0.05;
    p.shadeRadius = 60.0;
    p.shadeSigma = 30.0;
    CyanoField f(p);
    PCG32 rng(702);
    f.seed(2, 300.0, 300.0, rng);
    mut(f, 0).pos = Vec2{100.0, 150.0};
    mut(f, 1).pos = Vec2{130.0, 150.0}; // 环距 30 < 60
    for (int t = 0; t < 3; ++t) (void)f.step(0.5, 300.0, 300.0, rng);
    // 结算后的缓存（下一帧光合消费的是同值）。
    const double w = std::exp(-(30.0 * 30.0) / (2.0 * 30.0 * 30.0)); // exp(-0.5)
    CHECK_NEAR(f.shadeCache()[0], w, 1e-12);
    CHECK_NEAR(f.shadeCache()[1], w, 1e-12); // 对称

    // 拉开到环距 100 > radius：互不遮蔽。
    mut(f, 1).pos = Vec2{200.0, 150.0};
    (void)f.step(0.5, 300.0, 300.0, rng);
    CHECK(f.shadeCache()[0] == 0.0);
    CHECK(f.shadeCache()[1] == 0.0);
}

// 负反馈形状：同 world、同参数，节点越密单位光合越低。
TEST(shade_density_reduces_per_node_gain) {
    CyanoParams p;
    p.metabolismRate = 0.0;
    p.ballSpawnProbability = 0.0;
    p.reproductionProbability = 0.0;
    p.linkRadius = 0.0;
    p.shadeK = 0.05;
    p.shadeRadius = 60.0;
    p.shadeSigma = 30.0;
    p.energyCap = 1e9; // 不设能量帽，纯看单位产量

    const auto meanGain = [&](int n) {
        CyanoField f(p);
        PCG32 rng(710);
        f.seed(n, 256.0, 256.0, rng);
        // 前两帧只让 linkTransfer 缓存 shade（光合第一帧读的是空缓存）。
        (void)f.step(0.5, 256.0, 256.0, rng);
        const CyanoStepReport r = f.step(0.5, 256.0, 256.0, rng);
        return r.photosynthesisGain / static_cast<double>(n);
    };
    const double g16 = meanGain(16);
    const double g64 = meanGain(64);
    const double g128 = meanGain(128);
    CHECK(g64 < g16);   // 密度升 -> 单位产量降（负反馈方向）
    CHECK(g128 < g64);
    CHECK(g16 > 0.0);
}

// 遮蔽确实压低光合（同一场、同帧，开 vs 关遮蔽对照）。
TEST(shade_suppresses_photosynthesis_vs_unshaded) {
    const auto run = [&](double shadeK) {
        CyanoParams p;
        p.metabolismRate = 0.0;
        p.ballSpawnProbability = 0.0;
        p.reproductionProbability = 0.0;
        p.linkRadius = 0.0;
        p.shadeK = shadeK;
        p.shadeRadius = 60.0;
        p.shadeSigma = 30.0;
        CyanoField f(p);
        PCG32 rng(711);
        f.seed(64, 256.0, 256.0, rng); // 同 seed 同布局
        (void)f.step(0.5, 256.0, 256.0, rng);      // 缓存 shade
        return f.step(0.5, 256.0, 256.0, rng).photosynthesisGain;
    };
    const double off = run(0.0);
    const double on = run(0.05);
    CHECK(on < off);        // 遮蔽开启 -> 总产量下降
    CHECK(off > 0.0);
}

// 遮蔽计算零 rng 消耗（纯几何红线）。
// 判定 roll 本身每节点每帧必抽（判定先于遮蔽的固定消耗序），step 前后状态
// 必然不同；正确测法：两个**仅 shadeK 不同**的同 seed 同布局场跑 N 帧——
// 若遮蔽偷耗 rng，两边消耗总数分叉，rng 状态必不一致；零消耗则逐位一致。
TEST(shade_consumes_no_rng) {
    const auto run = [&](double shadeK) {
        CyanoParams p;
        p.metabolismRate = 0.0;
        p.ballSpawnProbability = 0.3; // 打开随机通道，放大任何消耗差异
        p.reproductionProbability = 0.2;
        p.linkRadius = 0.0;
        p.shadeK = shadeK;
        CyanoField f(p);
        PCG32 rng(712);
        f.seed(4, 200.0, 200.0, rng);
        for (int t = 0; t < 50; ++t) (void)f.step(0.5, 200.0, 200.0, rng);
        std::uint64_t s, i;
        rng.getState(s, i);
        return std::pair<std::uint64_t, std::uint64_t>{s, i};
    };
    const auto off = run(0.0);
    const auto on = run(0.05);
    CHECK(off.first == on.first && off.second == on.second);
    // 退化分支（linkTransfer 直调，仅遮蔽扫描）同样零消耗。
    CyanoParams q;
    q.linkRadius = 0.0;
    CyanoField g(q);
    PCG32 rng2(713);
    g.seed(3, 200.0, 200.0, rng2);
    std::uint64_t s1, i1, s2, i2;
    rng2.getState(s1, i1);
    g.linkTransfer(); // 走退化分支的仅遮蔽扫描
    rng2.getState(s2, i2);
    CHECK(s1 == s2 && i1 == i2);
}

// ---- 组合根级 ----

// 构造期配置 shade 生效 + applyConfig 热调控不冲掉（三处镜像同步的回归锁）。
TEST(shading_survives_hot_reload) {
    SimConfig cfg;
    cfg.world.seed = 42;
    cfg.cyanoTuning.shadeK = 0.09;          // 设一个非默认值
    Simulation sim(cfg);
    CHECK_NEAR(sim.cyanoParams().shadeK, 0.09, 1e-12);
    CHECK_NEAR(sim.cyanoParams().shadeRadius, 60.0, 1e-12);
    CHECK_NEAR(sim.cyanoParams().shadeSigma, 30.0, 1e-12);

    SimConfig next = cfg;
    next.sun.baseLevel = 2.0;               // 模拟"用户改了光照"
    sim.applyConfig(next);                  // 热调控

    CHECK_NEAR(sim.cyanoParams().shadeK, 0.09, 1e-12);   // 必须还在！
    CHECK_NEAR(sim.cyanoParams().shadeRadius, 60.0, 1e-12);
    CHECK_NEAR(sim.cyanoParams().shadeSigma, 30.0, 1e-12);
    CHECK(sim.runTo(100));                  // 新参数下推进，审计闭合
    CHECK(sim.auditOk());
}

// RuntimeEnv 合法路径热改 shade 参数（与 RuntimeEnv 键名契约的接线面）。
TEST(shading_hot_tune_via_runtime_env) {
    SimConfig cfg;
    cfg.world.seed = 42;
    Simulation sim(cfg);
    // R3-A 标定默认 0.20（见 Config.h 注释）；构造期镜像必须先生效。
    CHECK_NEAR(sim.cyanoParams().shadeK, 0.20, 1e-12);
    SimConfig next = cfg;
    // 键是否存在由 RuntimeEnv 决定；本卡只保证 CyanoTuning 落点存在。
    // （RuntimeEnv 未注册 shade 键时此测试仍有效：值不变。）
    next.cyanoTuning.shadeK = 0.07;
    sim.applyConfig(next);
    CHECK_NEAR(sim.cyanoParams().shadeK, 0.07, 1e-12);
}

// 默认参数快照。注意白盒镜像契约是"两边同名同义"，不是"两边默认值恒等"：
// CyanoParams.shadeK=0.05 是模块层默认（生态中立），CyanoTuning.shadeK=0.20 是
// R3-A 标定（经构造期镜像覆盖模块默认）；refuge 同理（模块 0 / 调控层 15）。
TEST(shade_default_params_match) {
    CyanoParams p;
    CHECK(p.shadeK == 0.05);
    CHECK(p.shadeRadius == 60.0);
    CHECK(p.shadeSigma == 30.0);
    CHECK(p.grazeRefugeEnergy == 0.0); // 模块层默认：旧 graze 语义逐位
    CHECK(p.maxNodes == 256u); // R3-A：性能护栏升级
    // R7-C：聚落播种——模块层默认 0 聚落（旧均匀撒点，生态中立逐位回归），
    // CyanoTuning 侧标定 4 聚落（经构造期镜像覆盖模块默认）；refuge 同理。
    CHECK(p.cyanoColonies == 0);
    CHECK(p.clusterSpread == 45.0);
    SimConfig cfg;
    CHECK(cfg.cyanoTuning.shadeK == 0.20);        // R3-A 标定（Config.h 注释）
    CHECK(cfg.cyanoTuning.shadeRadius == p.shadeRadius);
    CHECK(cfg.cyanoTuning.shadeSigma == p.shadeSigma);
    CHECK(cfg.cyanoTuning.grazeRefugeEnergy == 15.0);
    CHECK(cfg.nucleus.grazeBiteFraction == 0.15);
    CHECK(cfg.cyanoMaxNodes == 4096u);   // R10 世界 1024
    CHECK(cfg.cyanoTuning.cyanoColonies == 64);   // R10 世界 1024：16→64
    CHECK(cfg.cyanoTuning.clusterSpread == 45.0);
}

// ---- R7-C：聚落播种 ----

// 聚落模式：节点必须落在某个聚落中心的 clusterSpread 半径内（带环面 wrap 容差），
// 且与旧均匀模式消耗**不同**的 rng 流；分布确实成簇（聚落内平均环距远小于全图均匀）。
TEST(cyano_seed_colony_clusters_nodes_around_centers) {
    const double W = 256.0, H = 256.0;
    CyanoParams p;
    p.cyanoColonies = 4;
    p.clusterSpread = 45.0;
    CyanoField f(p);
    PCG32 rng(1);
    f.seed(32, W, H, rng);
    CHECK(f.aliveCount() == 32);
    // 重放聚落中心的消耗序（前 2*4 抽 = cx, cy），得到与 seed() 相同的中心集合。
    PCG32 replay(1);
    std::vector<Vec2> centers;
    for (int i = 0; i < 4; ++i) {
        centers.push_back(Vec2{replay.nextDouble() * W, replay.nextDouble() * H});
    }
    // 每个节点必须属于某个中心的 [0, clusterSpread] 邻域。用环面距离：
    // 中心/节点越界 wrap 后直距会翻倍膨胀，只有环距才是真实拓扑距离。
    for (const CyanoNode& n : f.nodes()) {
        bool near = false;
        for (const Vec2& c : centers) {
            if (torusDistance(n.pos, c, W, H) <= p.clusterSpread + 1e-9) {
                near = true;
                break;
            }
        }
        CHECK(near);
    }
    // 成簇性：到最近中心的平均环距必须显著小于全图均匀分布的期望（~W/4）。
    double sum = 0.0;
    for (const CyanoNode& n : f.nodes()) {
        double best = 1e18;
        for (const Vec2& c : centers) {
            const double d = torusDistance(n.pos, c, W, H);
            if (d < best) best = d;
        }
        sum += best;
    }
    const double meanNearest = sum / static_cast<double>(f.nodes().size());
    CHECK(meanNearest < W * 0.25 * 0.5); // 斑块状：均值 < 全图均匀期望的一半
}

// 均匀模式（cyanoColonies=0）：模块默认逐位回到旧 [每节点: x, y, hue] 序——
// 消耗量恰 3 抽/节点，且第一个节点的 (x, y) 直接等于 rng 流前两抽（旧语义回归锁）。
TEST(cyano_seed_uniform_mode_bitwise_legacy) {
    CyanoParams p; // cyanoColonies 默认 0
    CyanoField f(p);
    PCG32 probe(77), ref(77);
    f.seed(3, 500.0, 500.0, probe);
    for (int i = 0; i < 9; ++i) (void)ref.nextDouble(); // 3 节点 x 3 抽
    std::uint64_t pa, ia, pb, ib;
    probe.getState(pa, ia);
    ref.getState(pb, ib);
    CHECK(pa == pb && ia == ib); // 与旧消耗序逐位一致
    // 首节点坐标 = 前两抽的旧公式（wrap 前），色相 = 第三抽。
    PCG32 re(77);
    const double x = re.nextDouble() * 500.0;
    const double y = re.nextDouble() * 500.0;
    const double hue = 360.0 * re.nextDouble();
    CHECK(f.nodes()[0].pos.x == x);
    CHECK(f.nodes()[0].pos.y == y);
    CHECK(f.nodes()[0].hue == hue);
}

// rng 消耗序（聚落模式）：2*colonies + 3*count 抽，逐段可重放。
TEST(cyano_seed_colony_rng_budget) {
    const int colonies = 4;
    const int count = 7; // 非整除：最后一聚落分到 1 个节点（轮转分配覆盖）
    CyanoParams p;
    p.cyanoColonies = colonies;
    p.clusterSpread = 45.0;
    CyanoField f(p);
    PCG32 probe(1234), ref(1234);
    f.seed(count, 256.0, 256.0, probe);
    const int expected = 2 * colonies + 3 * count;
    for (int i = 0; i < expected; ++i) (void)ref.nextDouble();
    std::uint64_t pa, ia, pb, ib;
    probe.getState(pa, ia);
    ref.getState(pb, ib);
    CHECK(pa == pb && ia == ib); // 恰好 2C+3N 抽，无装饰性消耗
    // count<=0：零消耗（含聚落中心也不撒）。
    CyanoField g(p);
    PCG32 r2(99);
    g.seed(0, 256.0, 256.0, r2);
    std::uint64_t s1, i1, s2, i2;
    r2.getState(s1, i1);
    CHECK(g.aliveCount() == 0);
    r2.getState(s2, i2);
    CHECK(s1 == s2 && i1 == i2);
}

// 组合根：默认运行下种群不撞护栏（自遮光接管承载力）。
TEST(shade_default_run_stays_below_guard) {
    SimConfig cfg;
    cfg.world.seed = 42;
    cfg.world.width = 256.0; cfg.world.height = 256.0;  // R10：钉 256（机制测试，长跑需快）
    Simulation sim(cfg);
    CHECK(sim.runTo(1000));
    CHECK(sim.auditOk());
    CHECK(sim.cyano().nodes().size() <= 4096u); // 性能护栏（R10 世界 1024 同步）
    CHECK(sim.cyano().aliveCount() > 0);       // 第一营养级不断
}
