// agentsim - tests/unit/test_cyano_wiring.cpp
// S22 接线单测：CyanoField 进入 Simulation 组合根后的行为与账本恒等式。
// 覆盖：播种、光照净增长、20k tick 审计闭合、totalEnergy 包含蓝细菌、
// 同种子逐位确定性、产球实体投放。
#include "test_framework.h"

#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include "../../src/sim/Simulation.h"

namespace {

using agentsim::Simulation;
using agentsim::SimConfig;

// 统一构造：默认配置 + 指定种子（其余字段用默认，贴近 CLI 缺省运行）。
Simulation makeSim(std::uint64_t seed) {
    SimConfig cfg;
    cfg.world.seed = seed;
    cfg.world.width = 256.0; cfg.world.height = 256.0;  // R8-T53：钉 256（长跑审计/增长测试测机制不测默认缩放）
    return Simulation(cfg);
}

} // namespace

TEST(wiring_cyano_seeded) {
    Simulation sim = makeSim(42);
    // 构造即播种：cfg.cyanoNodeCount（默认 128，R8-T53 世界 4 倍同步）个节点，全部存活。
    CHECK(sim.cyano().aliveCount() > 0);
    CHECK(sim.cyano().nodes().size() == 512);  // R10 世界 1024
    CHECK(sim.auditOk()); // 构造期注入（初始能量入账）后审计即应成立
}

TEST(wiring_cyano_grows_with_sun) {
    Simulation sim = makeSim(42);
    const std::size_t initial = sim.cyano().aliveCount();
    CHECK(initial > 0);
    CHECK(sim.runTo(2000));
    // 【断言修正 2026-10-31】原断言是 aliveCount() >= initial（"种群只增不减"），
    // 那是在**捕食还没接线**时写的假设。现在核会吃蓝细菌（S22 捕食接线），
    // 蓝细菌数量是被"光合增长"和"被吃掉"两个力共同决定的，不再单调递增。
    // 正确的不变量是：**不被吃绝**（第一营养级必须一直存在，否则食物链断裂）。
    CHECK(sim.cyano().aliveCount() > 0);
    // 同时守住节点数上限（性能护栏）：超过 maxNodes(64) 就违背设计。
    // 这里写字面值而不是 CyanoParams{}.maxNodes，是为了不让测试文件
    // 额外 include environment/CyanoNode.h（保持测试只依赖 Simulation 接口）。
    CHECK(sim.cyano().nodes().size() <= 256);
    CHECK(sim.auditOk());
}

TEST(wiring_cyano_total_energy_in_sum) {
    Simulation sim = makeSim(42);
    CHECK(sim.runTo(1000));
    // 总能量口径必须覆盖蓝细菌（否则 sumLiving 漏一块，审计恒等式即破）。
    CHECK(sim.totalEnergy() >= sim.cyano().totalEnergy());
    CHECK(sim.auditOk());
}

TEST(wiring_deterministic) {
    Simulation a = makeSim(20260704);
    Simulation b = makeSim(20260704);
    CHECK(a.runTo(500));
    CHECK(b.runTo(500));
    CHECK(a.stateHash() == b.stateHash());
}

// 产球链路：跑足够多 tick，累计 ballsProduced 必为正，且每个产球 tick
// freeBalls 容器都承接了对应数量的新实体（id 从 0x40000000 起分配）。
TEST(wiring_free_balls_from_cyano) {
    Simulation sim = makeSim(42);
    bool sawBalls = false;
    for (std::uint64_t t = 0; t < 6000 && !sawBalls; ++t) {
        sim.step();
        // 模块报告无法从外部逐帧取（step 内部消化），改从自由球容器观测：
        // cyano 产的球 id >= 0x40000000，核脱离球 id 则来自行为层小 id 空间。
        for (const agentsim::Ball& b : sim.freeBalls()) {
            if (b.id >= 0x40000000ULL) { sawBalls = true; break; }
        }
        if (sim.cyano().aliveCount() == 0) break; // 极端灭绝则放弃触发
    }
    // 断言：不崩溃、审计全程通过，并且确实观测到过蓝细菌产出的自由球。
    CHECK(sim.auditOk());
    CHECK(sawBalls);
}

// 审计闭合（最关键一条）：整个推进过程中 injected-dissipated
// 与全部存活实体（核+球+蓝细菌+自由球）能量总和逐 tick 相等。
//
// 【2026-11-01 拆分说明】原来这里直接跑 20000 tick。R2-A 合并后种群从 24 涨到
// 最多 300，对核物理是 O(n²)（每核扫全种群），实测 20000 tick 要 6+ 分钟，
// 门禁不可接受。拆成两档：
//   · 快档 8000 tick（本用例）——日常门禁用，覆盖"长时间不破账"的核心意图；
//   · 慢档 50000 tick（下面 _slow 用例）——需显式置 AGENTSIM_SLOW_TESTS=1。
// 注意：这条测试的价值是"账本在长时间运行后仍闭合"，不是"必须恰好 20000"。
// 8000 tick 已经跨过了种群从 12 涨到数百、经历出生/死亡/捕食/脱离的完整阶段。
TEST(wiring_audit_holds_long_run) {
    Simulation sim = makeSim(42);
    CHECK(sim.runTo(8000));
    CHECK(sim.auditOk());
}

// 慢档：深度审计。默认跳过（读环境变量），CI/手工按需开。
// 为什么保留：快档覆盖不了"超大种群 + 极长时间"的组合，而审计失效往往
// 是低频事件（某个罕见的死亡/脱离路径漏记账），需要长跑才能暴露。
TEST(wiring_audit_holds_50000_slow) {
    // MSVC 把 std::getenv 标记为不安全（C4996），/WX 下是错误。用两种办法绕开：
    // 1) 优先用 _dupenv_s（MSVC 安全版本，malloc 出来的串，用完 free）；
    // 2) 非 MSVC 走 std::getenv。
    // 这里的目的是"读一个可选的环境变量"，不该为了它引入跨平台抽象层。
#if defined(_MSC_VER)
    char* slowRaw = nullptr;
    std::size_t slowLen = 0;
    const bool enabled = (_dupenv_s(&slowRaw, &slowLen, "AGENTSIM_SLOW_TESTS") == 0) &&
                         slowRaw != nullptr && slowRaw[0] == '1';
    std::free(slowRaw);
    if (!enabled) return;
#else
    const char* slow = std::getenv("AGENTSIM_SLOW_TESTS");
    if (slow == nullptr || slow[0] != '1') return;
#endif
    Simulation sim = makeSim(42);
    CHECK(sim.runTo(50000));
    CHECK(sim.auditOk());
}

// R1-B：库存覆盖真生效——cyanoNodeCount / cyanoMaxNodes 改变构造期播种与护栏。
TEST(wiring_cyano_stock_overrides) {
    SimConfig cfg;
    cfg.world.seed = 42;
    cfg.cyanoNodeCount = 5;
    Simulation sim(cfg);
    CHECK(sim.cyanoNodes().size() == 5);
    CHECK(sim.auditOk());
    // 上限覆盖：maxNodes 更大时种群上限随之放大（分裂不再被 64 截停）。
    SimConfig big;
    big.world.seed = 42;
    big.cyanoNodeCount = 8;
    big.cyanoMaxNodes = 128;
    Simulation simBig(big);
    CHECK(simBig.cyanoNodes().size() == 8);
    CHECK(simBig.runTo(500));
    CHECK(simBig.cyanoNodes().size() <= 128);
    CHECK(simBig.auditOk());
}
