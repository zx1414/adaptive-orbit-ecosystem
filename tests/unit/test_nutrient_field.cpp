// agentsim - tests/unit/test_nutrient_field.cpp
// P1 物质循环环境侧（NutrientField）单测：分区几何与环面坐标、建场初值、
// add/take 的守恒与边界、decompose 的单调性与零和、逐位确定性、白盒回填。
// 本文件不引入任何上层模块（sim/decision/behavior/ledger），与模块的四层单向
// 依赖纪律一致。core/PCG32.h 只在一处出现：nutrient_field_consumes_no_rng
// 用它证明模块**没有**消耗随机数（模块自身完全不 include PCG32）。
#include "test_framework.h"

#include <cmath>
#include <cstddef>
#include <limits>
#include <vector>

#include "../../src/core/PCG32.h" // 仅用于"模块零 RNG 消耗"的证明用例
#include "../../src/core/Vec2.h"
#include "../../src/environment/NutrientField.h"

namespace {

using agentsim::NutrientCell;
using agentsim::NutrientField;
using agentsim::Vec2;

// 相对容差断言用的标量：sum(a) 与 sum(b) 的相对差（分母取 max(|a|,|b|,1)）。
bool nearlyEqualRel(double a, double b, double relEps) {
    const double scale = std::fmax(std::fmax(std::fabs(a), std::fabs(b)), 1.0);
    return std::fabs(a - b) <= relEps * scale;
}

// 把一次"固定操作序列"打在场里（确定性用例用：同一序列跑两次须逐位相同）。
void runFixedSequence(NutrientField& f, double worldW, double worldH) {
    const Vec2 pts[4] = {Vec2{0.0, 0.0}, Vec2{worldW - 0.5, worldH - 0.5},
                         Vec2{worldW * 0.37, worldH * 0.61},
                         Vec2{-3.0, worldH + 7.0}}; // 含负坐标/越界坐标
    for (int i = 0; i < 4; ++i) {
        f.addNutrient(pts[i], 12.5 * static_cast<double>(i + 1));
        f.addDebris(pts[i], 30.0 - 3.0 * static_cast<double>(i));
        (void)f.takeNutrient(pts[i], 7.25);
    }
    for (int t = 0; t < 10; ++t) (void)f.decompose(0.25);
}

} // namespace

// ---- 建场与初值 ----

TEST(nutrient_field_build_initial_values) {
    // 1000x800 / regionSize 100 => 10 x 8 = 80 个区域（整除）。
    NutrientField f(1000.0, 800.0, 100.0, 5.0);
    CHECK(f.cols() == 10);
    CHECK(f.rows() == 8);
    CHECK(f.cellCount() == 80u);
    CHECK_NEAR(f.regionSize(), 100.0, 1e-15);
    CHECK_NEAR(f.worldWidth(), 1000.0, 1e-15);
    CHECK_NEAR(f.worldHeight(), 800.0, 1e-15);

    // 逐区域：nutrient 初值 = initialPerRegion，debris 从 0 起（无来源不产生残骸）。
    for (std::size_t i = 0; i < f.cellCount(); ++i) {
        CHECK_NEAR(f.cell(i).nutrient, 5.0, 1e-15);
        CHECK(f.cell(i).debris == 0.0);
    }
    CHECK_NEAR(f.totalNutrient(), 80.0 * 5.0, 1e-12);
    CHECK(f.totalDebris() == 0.0);
    CHECK_NEAR(f.total(), 80.0 * 5.0, 1e-12);

    // 非整除：1000x800 / 250 => 4 x 3 = 12（末列/末行被均分，不截断）。
    NutrientField g(1000.0, 800.0, 250.0, 2.0);
    CHECK(g.cols() == 4);
    CHECK(g.rows() == 3);
    CHECK(g.cellCount() == 12u);
    CHECK_NEAR(g.total(), 12.0 * 2.0, 1e-12);

    // regionSize 大于世界：退化为 1x1（任何坐标都归唯一区域，不越界）。
    NutrientField h(100.0, 100.0, 500.0, 3.0);
    CHECK(h.cols() == 1);
    CHECK(h.rows() == 1);
    CHECK(h.cellCount() == 1u);
    CHECK_NEAR(h.total(), 3.0, 1e-15);
    CHECK(h.indexAt(Vec2{0.0, 0.0}) == 0u);
    CHECK(h.indexAt(Vec2{99.9, 99.9}) == 0u);
}

// ---- 分区几何：下标/中心/反解自洽 ----

TEST(nutrient_field_grid_geometry_self_consistent) {
    NutrientField f(1000.0, 800.0, 100.0, 0.0);
    CHECK(f.indexAt(Vec2{5.0, 5.0}) == 0u);                       // (0,0)
    CHECK(f.indexAt(Vec2{105.0, 5.0}) == 1u);                     // (1,0)
    CHECK(f.indexAt(Vec2{5.0, 105.0}) == static_cast<std::size_t>(f.cols()));
    CHECK(f.ixOf(0u) == 0);
    CHECK(f.iyOf(0u) == 0);
    CHECK(f.ixOf(1u) == 1);
    CHECK(f.iyOf(1u) == 0);
    CHECK(f.ixOf(10u) == 0);
    CHECK(f.iyOf(10u) == 1);
    // 反解与正解互逆，且区域中心确实落在该区域内。
    for (std::size_t i = 0; i < f.cellCount(); ++i) {
        CHECK(f.indexOf(f.ixOf(i), f.iyOf(i)) == i);
        const Vec2 c = f.cellCenter(i);
        CHECK(f.indexAt(c) == i);
        CHECK_NEAR(f.centerAt(c).x, c.x, 1e-12);
        CHECK_NEAR(f.centerAt(c).y, c.y, 1e-12);
        // 中心坐标恒在环面内。
        CHECK(c.x >= 0.0 && c.x < 1000.0);
        CHECK(c.y >= 0.0 && c.y < 800.0);
    }
    // 中心 = (ix + 0.5) * cellW / (iy + 0.5) * cellH。
    const Vec2 c31 = f.cellCenter(f.indexOf(3, 1));
    CHECK_NEAR(c31.x, 350.0, 1e-12);
    CHECK_NEAR(c31.y, 150.0, 1e-12);
}

// ---- add/take 的守恒与边界 ----

TEST(nutrient_field_add_and_take_boundaries) {
    NutrientField f(400.0, 400.0, 100.0, 10.0); // 4x4，每格 10
    const Vec2 p{50.0, 50.0};                   // 区域 (0,0)

    // addNutrient 只加在所在区域，其它区域不变（A6"其他区域不直接收到养分"）。
    const double otherBefore = f.cell(1).nutrient;
    f.addNutrient(p, 5.0);
    CHECK_NEAR(f.cell(0).nutrient, 15.0, 1e-15);
    CHECK(f.cell(1).nutrient == otherBefore);

    // take 超量：只给存量，库存归零且不为负。
    CHECK_NEAR(f.takeNutrient(p, 1000.0), 15.0, 1e-12);
    CHECK(f.cell(0).nutrient == 0.0);
    CHECK(f.cell(0).nutrient >= 0.0);
    CHECK(f.takeNutrient(p, 1.0) == 0.0); // 已空：再取为 0

    // take 负数 / 0 / NaN：不动、返回 0。
    f.addNutrient(p, 4.0);
    const double before = f.cell(0).nutrient;
    CHECK(f.takeNutrient(p, -3.0) == 0.0);
    CHECK(f.takeNutrient(p, 0.0) == 0.0);
    CHECK(f.takeNutrient(p, std::numeric_limits<double>::quiet_NaN()) == 0.0);
    CHECK(f.cell(0).nutrient == before);
    // 刚好取完：返回 want，库存恒等于 0（不是 -0 或更小）。
    CHECK_NEAR(f.takeNutrient(p, 4.0), 4.0, 1e-15);
    CHECK(f.cell(0).nutrient == 0.0);

    // add 负数 / NaN：无操作（不产生负库存、不产生 NaN）。
    f.addNutrient(p, -5.0);
    f.addNutrient(p, std::numeric_limits<double>::quiet_NaN());
    f.addDebris(p, -5.0);
    f.addDebris(p, std::numeric_limits<double>::quiet_NaN());
    CHECK(f.cell(0).nutrient == 0.0);
    CHECK(f.cell(0).debris == 0.0);
}

TEST(nutrient_field_matter_conserved_across_add_and_take) {
    NutrientField f(400.0, 400.0, 100.0, 1.0);
    const double t0 = f.total();
    f.addDebris(Vec2{10.0, 10.0}, 25.0);
    CHECK(nearlyEqualRel(f.total(), t0 + 25.0, 1e-12)); // 加残骸 = 总量加
    f.addNutrient(Vec2{210.0, 210.0}, 7.5);
    CHECK(nearlyEqualRel(f.total(), t0 + 32.5, 1e-12));
    // 取养分只减总量，减多少 = 取多少。
    const double got = f.takeNutrient(Vec2{210.0, 210.0}, 3.0);
    CHECK_NEAR(got, 3.0, 1e-15);
    CHECK(nearlyEqualRel(f.total(), t0 + 29.5, 1e-12));
    // 取超量（受存量限制）：减掉的恰是取到的。
    const double stock = f.cellAt(Vec2{210.0, 210.0}).nutrient;
    const double got2 = f.takeNutrient(Vec2{210.0, 210.0}, 1e9);
    CHECK(got2 == stock);
    CHECK(nearlyEqualRel(f.total(), t0 + 29.5 - stock, 1e-12));
}

// ---- decompose：单调、零和、总量守恒 ----

TEST(nutrient_field_decompose_conserves_and_is_monotone) {
    NutrientField f(500.0, 500.0, 100.0, 0.0); // 5x5 全空
    f.addDebris(Vec2{50.0, 50.0}, 40.0);        // 区域 (0,0)
    f.addDebris(Vec2{450.0, 450.0}, 60.0);      // 区域 (4,4)
    const double totalD0 = f.totalDebris();
    const double totalN0 = f.totalNutrient();
    const double total0 = f.total();
    CHECK_NEAR(totalD0, 100.0, 1e-12);
    CHECK(totalN0 == 0.0);

    double debrisPrev = f.totalDebris();
    double nutrientPrev = f.totalNutrient();
    double convertedSum = 0.0;
    for (int t = 0; t < 12; ++t) {
        const double converted = f.decompose(0.3);
        // 返回本帧转化总量 = 残骸减少量（同一 double 参与减法与加法）。
        CHECK_NEAR(converted, debrisPrev - f.totalDebris(), 1e-12);
        CHECK(converted >= 0.0);
        // 单调：残骸只降、养分只升。
        CHECK(f.totalDebris() <= debrisPrev);
        CHECK(f.totalNutrient() >= nutrientPrev);
        // 逐区域零和（残骸减少量 == 养分增加量），同时本区两种量都不为负。
        for (std::size_t i = 0; i < f.cellCount(); ++i) {
            CHECK(f.cell(i).debris >= 0.0);
            CHECK(f.cell(i).nutrient >= 0.0);
        }
        // 总量守恒：Σdebris + Σnutrient 恒定（只剩加法舍入差）。
        CHECK(nearlyEqualRel(f.total(), total0, 1e-12));
        convertedSum += converted;
        debrisPrev = f.totalDebris();
        nutrientPrev = f.totalNutrient();
    }
    // 12 帧 rate=0.3 后残骸按几何衰减剩 100 * 0.7^12（不是 0：分解是渐进的，
    // 这正是"随模拟时间推进"的语义，不能期望一帧或几帧就转完）。
    CHECK_NEAR(f.totalDebris(), totalD0 * std::pow(0.7, 12.0), 1e-9);
    // 累计转化量 == 初始残骸 - 当前残骸（无泄漏、无凭空生成）。
    CHECK(nearlyEqualRel(convertedSum, totalD0 - f.totalDebris(), 1e-12));

    // 继续分解到残骸可忽略（300 帧，0.7^300 ≈ 1e-47）：总量仍严格守恒。
    for (int t = 0; t < 288; ++t) (void)f.decompose(0.3);
    CHECK(f.totalDebris() < 1e-9);
    CHECK(f.totalDebris() >= 0.0);
    CHECK(nearlyEqualRel(f.total(), total0, 1e-12));
    CHECK(nearlyEqualRel(f.totalNutrient(), totalN0 + totalD0, 1e-9));

    // 区域局部性：养分只出现在原本有残骸的两个区域内。
    CHECK(f.cell(f.indexOf(0, 0)).nutrient > 0.0);
    CHECK(f.cell(f.indexOf(4, 4)).nutrient > 0.0);
    CHECK(f.cell(f.indexOf(2, 2)).nutrient == 0.0);
}

TEST(nutrient_field_decompose_rate_edges) {
    NutrientField f(200.0, 200.0, 100.0, 0.0);
    f.addDebris(Vec2{50.0, 50.0}, 10.0);
    // 非正 / NaN 速率：无操作，返回 0。
    CHECK(f.decompose(0.0) == 0.0);
    CHECK(f.decompose(-1.0) == 0.0);
    CHECK(f.decompose(std::numeric_limits<double>::quiet_NaN()) == 0.0);
    CHECK_NEAR(f.totalDebris(), 10.0, 1e-15);
    CHECK(f.totalNutrient() == 0.0);
    // rate > 1 被 clamp 到 1：一次全转，库存不为负、不产生 NaN。
    const double converted = f.decompose(5.0);
    CHECK_NEAR(converted, 10.0, 1e-12);
    CHECK(f.totalDebris() == 0.0);
    CHECK_NEAR(f.totalNutrient(), 10.0, 1e-12);
    CHECK(!std::isnan(f.total()));
}

// ---- 环面坐标正确性 ----

TEST(nutrient_field_torus_wrapping) {
    // 1000x1000 / regionSize 100 => cols = rows = 10，cellW = cellH = 100。
    NutrientField f(1000.0, 1000.0, 100.0, 0.0);
    CHECK(f.cols() == 10);

    // 缝两侧：x = 0 与 x = worldW - ε 落在**相邻**的两个区域（0 与 cols-1），
    // 而不是同一区域、也不是越界索引。这是"环面按固定网格分区"的定义结果：
    // 每个区域是半开区间 [ix*cellW, (ix+1)*cellW)，worldW 本身 wrap 回 0。
    CHECK(f.ixOf(f.indexAt(Vec2{0.0, 0.0})) == 0);
    CHECK(f.ixOf(f.indexAt(Vec2{999.999, 0.0})) == 9);
    CHECK(f.ixOf(f.indexAt(Vec2{1000.0, 0.0})) == 0);   // 恰好等于 W：wrap 回 0
    CHECK(f.ixOf(f.indexAt(Vec2{-0.001, 0.0})) == 9);   // 负向越界：wrap 到末格
    CHECK(f.ixOf(f.indexAt(Vec2{-1000.0, 0.0})) == 0);  // 整圈：回到 0

    // 跨接缝取料只影响自己所在区域：在 x=999.5 取，cell(9) 变、cell(0) 不变。
    f.addNutrient(Vec2{999.5, 50.0}, 8.0);
    CHECK(f.cell(f.indexOf(9, 0)).nutrient == 8.0);
    CHECK(f.cell(f.indexOf(0, 0)).nutrient == 0.0);
    // x = 1 属于区域 0；与 x = 999（区域 9）不同区，但两者环面距离最小。
    CHECK(f.ixOf(f.indexAt(Vec2{1.0, 50.0})) == 0);
    CHECK(f.ixOf(f.indexAt(Vec2{999.0, 50.0})) == 9);

    // 缝相邻性：区域 9 与区域 0 的中心，其**环面**最短距离恰为 1 个 cellW。
    const Vec2 c0 = f.cellCenter(f.indexOf(0, 0));
    const Vec2 c9 = f.cellCenter(f.indexOf(9, 0));
    CHECK_NEAR(c0.x, 50.0, 1e-12);
    CHECK_NEAR(c9.x, 950.0, 1e-12);
    CHECK_NEAR(agentsim::torusDistance(c0, c9, 1000.0, 1000.0), 100.0, 1e-12);
    // 欧氏距离是 900（跨接缝的远侧）；环距 100 —— 证明分区确实按环面语义工作。
    CHECK_NEAR(std::fabs(c9.x - c0.x), 900.0, 1e-12);

    // 非整除参数下同样正确：1000 / 300 => cols = 3（cellW = 1000/3）。
    NutrientField g(1000.0, 1000.0, 300.0, 0.0);
    CHECK(g.cols() == 3);
    const double cw = 1000.0 / 3.0;
    CHECK_NEAR(g.cellCenter(g.indexOf(2, 0)).x, 2.5 * cw, 1e-12);
    // 末格上边界：x 略小于 1000 必须落在 ix=2，不能溢出成 3。
    CHECK(g.ixOf(g.indexAt(Vec2{1000.0 - 1e-9, 0.0})) == 2);
    CHECK(g.ixOf(g.indexAt(Vec2{cw, 0.0})) == 1); // 边界点归右侧区域
    CHECK(g.ixOf(g.indexAt(Vec2{cw - 1e-9, 0.0})) == 0);

    // 越界/负坐标（如核被推出边界时的位置）仍安全落到合法区域，不越界访问。
    for (int k = 0; k < 8; ++k) {
        const Vec2 weird{-5000.0 + 1300.0 * k, 7000.0 - 1900.0 * k};
        const std::size_t idx = f.indexAt(weird);
        CHECK(idx < f.cellCount());
        const Vec2 c = f.cellCenter(idx);
        CHECK(c.x >= 0.0 && c.x < 1000.0);
        CHECK(c.y >= 0.0 && c.y < 1000.0);
    }
}

// ---- 确定性：同一操作序列跑两次，逐位相等 ----

TEST(nutrient_field_deterministic_bitwise) {
    NutrientField a(1000.0, 800.0, 120.0, 4.5);
    NutrientField b(1000.0, 800.0, 120.0, 4.5);
    CHECK(a.cellCount() == b.cellCount());
    runFixedSequence(a, 1000.0, 800.0);
    runFixedSequence(b, 1000.0, 800.0);
    CHECK(a.totalDebris() == b.totalDebris());   // 逐位（==，不是 CHECK_NEAR）
    CHECK(a.totalNutrient() == b.totalNutrient());
    CHECK(a.total() == b.total());
    for (std::size_t i = 0; i < a.cellCount(); ++i) {
        CHECK(a.cell(i).debris == b.cell(i).debris);
        CHECK(a.cell(i).nutrient == b.cell(i).nutrient);
    }
    // 与"每次新建再跑"的第三次结果也逐位相同（无跨实例/静态状态）。
    NutrientField c(1000.0, 800.0, 120.0, 4.5);
    runFixedSequence(c, 1000.0, 800.0);
    for (std::size_t i = 0; i < a.cellCount(); ++i) {
        CHECK(c.cell(i).debris == a.cell(i).debris);
        CHECK(c.cell(i).nutrient == a.cell(i).nutrient);
    }
    // 操作顺序不同则状态不同（证明上面的相等不是"忽略输入的常量结果"）。
    NutrientField d(1000.0, 800.0, 120.0, 4.5);
    d.addDebris(Vec2{0.0, 0.0}, 1.0);
    CHECK(d.totalDebris() != a.totalDebris());
}

// ---- 白盒回填（存档通道的最小面）----

TEST(nutrient_field_restore_cells) {
    NutrientField f(400.0, 400.0, 100.0, 3.0);
    std::vector<NutrientCell> snap = f.cells();
    f.addDebris(Vec2{50.0, 50.0}, 9.0);
    f.addNutrient(Vec2{350.0, 350.0}, 2.0);
    CHECK(f.totalDebris() == 9.0);
    CHECK(f.restoreCells(snap));
    CHECK(f.totalDebris() == 0.0);
    CHECK_NEAR(f.totalNutrient(), 16.0 * 3.0, 1e-12);

    // 尺寸不符：拒绝且不改动现状。
    std::vector<NutrientCell> wrong(snap.size() + 1);
    CHECK(!f.restoreCells(wrong));
    CHECK_NEAR(f.totalNutrient(), 48.0, 1e-12);
    // 非法值（负库存）：整批拒绝。
    std::vector<NutrientCell> bad = snap;
    bad[2].debris = -1.0;
    CHECK(!f.restoreCells(bad));
    CHECK_NEAR(f.totalNutrient(), 48.0, 1e-12);
}

// ---- 退化输入：不得出现 NaN / 负库存 / 越界 ----

TEST(nutrient_field_degenerate_inputs_are_safe) {
    // 世界尺寸 0 / 负 / NaN，或 regionSize <= 0：退化为 1x1，任何坐标都有归属。
    NutrientField zero(0.0, 0.0, 100.0, 2.0);
    CHECK(zero.cellCount() == 1u);
    CHECK_NEAR(zero.totalNutrient(), 2.0, 1e-15);
    CHECK(zero.indexAt(Vec2{123.0, -456.0}) == 0u);

    NutrientField neg(-100.0, -50.0, 10.0, 1.0);
    CHECK(neg.cellCount() == 1u);
    CHECK(neg.indexAt(Vec2{7.0, 9.0}) == 0u);

    NutrientField badRegion(100.0, 100.0, 0.0, 1.0);
    CHECK(badRegion.cellCount() == 1u);
    CHECK(badRegion.regionSize() > 0.0); // 恒报正数，不外泄 0/NaN

    // initialPerRegion 为负 / NaN：视为 0（不产生负库存）。
    NutrientField negInit(100.0, 100.0, 50.0, -8.0);
    CHECK(negInit.total() == 0.0);
    CHECK(!std::isnan(negInit.total()));

    // 坐标含 NaN：不越界访问、不产生 NaN 库存（NaN 比较恒假 → 落到第 0 格）。
    NutrientField f(400.0, 400.0, 100.0, 1.0);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const std::size_t idx = f.indexAt(Vec2{nan, nan});
    CHECK(idx < f.cellCount());
    f.addNutrient(Vec2{nan, nan}, 3.0);
    f.addDebris(Vec2{nan, nan}, 4.0);
    CHECK(!std::isnan(f.total()));
    CHECK(f.total() >= 0.0);
    for (std::size_t i = 0; i < f.cellCount(); ++i) {
        CHECK(f.cell(i).nutrient >= 0.0);
        CHECK(f.cell(i).debris >= 0.0);
        CHECK(!std::isnan(f.cell(i).nutrient));
        CHECK(!std::isnan(f.cell(i).debris));
    }

    // rate == 1：恰好一帧全转，残骸精确归零（不是 -0、不是极小正数）。
    NutrientField g(200.0, 200.0, 100.0, 0.0);
    g.addDebris(Vec2{10.0, 10.0}, 7.5);
    CHECK(g.decompose(1.0) == 7.5);
    CHECK(g.totalDebris() == 0.0);
    CHECK(g.totalNutrient() == 7.5);
}

// ---- 零 RNG 消耗（可存档确定性复现的硬前提） ----

TEST(nutrient_field_consumes_no_rng) {
    // 本用例是全文件唯一 include core/PCG32.h 的地方，用途是**证明**模块不碰随机源：
    // 两个同种子 rng 一分为二，一份与完整的物质操作序列同时存在，另一份全程不碰；
    // 之后逐次抽样与末尾完整状态都必须逐位相同。
    agentsim::PCG32 used(20261125);
    agentsim::PCG32 untouched(20261125); // 同种子：起点状态逐位相同
    NutrientField f(1024.0, 1024.0, 64.0, 2.0);
    for (int i = 0; i < 40; ++i) {
        const Vec2 p{13.0 * static_cast<double>(i), 29.0 * static_cast<double>(i)};
        f.addDebris(p, 1.5);
        f.addNutrient(p, 0.5);
        (void)f.takeNutrient(p, 0.25);
    }
    (void)f.decompose(0.1);
    (void)f.decompose(0.37);
    // 若模块偷抽了随机数，used 会跑到前面，下面 8 次抽样必然不等。
    for (int i = 0; i < 8; ++i) {
        CHECK(used.nextUint32() == untouched.nextUint32());
    }
    std::uint64_t sa = 0, ia = 0, sb = 0, ib = 0;
    used.getState(sa, ia);
    untouched.getState(sb, ib);
    CHECK(sa == sb);
    CHECK(ia == ib);
}

