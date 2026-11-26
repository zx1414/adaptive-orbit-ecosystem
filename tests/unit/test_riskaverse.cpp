// agentsim - tests/unit/test_riskaverse.cpp
// RiskAversePolicy（风险规避 + 能量预算策略）单测。
// 红线：测试本身不用 RNG（LCG 常数序列也是纯确定算术），全部手工构造传感器；
// 被测 decide() 禁 RNG、禁读世界，同序列重放必须逐位一致。覆盖：高 EROI 冲刺、
// 低 EROI 巡航、温度折扣、EROI 非负、输出方向单位化或零、确定性、NaN 清洁、
// Conserve 低频转向、name/reset 契约。
//
// P0-4（记忆归个体）：EROI 的 EMA、模式与方向缓存都在 PolicyMemory 里。需要跨帧
// 累积的用例显式传**同一个** memory；状态查询改成 RiskAversePolicy::mode(mem) 等
// 静态接口。保护意图不变（例如"连续上升的接近度把个体推进 Earn 态"仍然被测到）。
#include "test_framework.h"

#include <cmath>
#include <limits>

#include "../../src/core/SensorTypes.h"
#include "../../src/decision/PolicyMemory.h"
#include "../../src/decision/RiskAversePolicy.h"

namespace {

using agentsim::ActionVector;
using agentsim::PolicyMemory;
using agentsim::RiskAversePolicy;
using agentsim::SensorVector;

double nanVal() { return std::numeric_limits<double>::quiet_NaN(); }

// 造一帧传感器：候选 0 方向 (dx,dy) 归一化、接近度 prox、速度幅值 speed（沿 x）、
// 温度失真 d。注意 s[10..11] 的模长恰为 s[12]（感知层契约），策略只读 s[10..11]。
SensorVector frame(double prox, double speed = 0.0, double d = 0.0, double dx = 1.0,
                   double dy = 0.0) {
    SensorVector s{};
    const double len = std::sqrt(dx * dx + dy * dy);
    if (len > 0.0) { dx /= len; dy /= len; }
    s[0] = dx;
    s[1] = dy;
    s[6] = prox;
    s[10] = speed; // vel.x * scale：模长恰为 speed（speed >= 0）
    s[12] = speed / (1.0 + speed);
    s[16] = d;
    return s;
}

} // namespace

// ---- ① 接近度持续上升 -> EROI 冲高 -> Earn 全速冲刺（boost 趋近 1.0）。----
TEST(riskaverse_high_eroi_sprints) {
    RiskAversePolicy pol; PolicyMemory mempol; // 一个个体一份记忆（P0-4）
    // 每帧增益 gain = 10*0.01 = 0.1；v=1.5 -> budget=(1*2.25+0.05)/32=0.0719，
    // obs = 0.1/0.0719 = 1.39 -> EMA 收敛到 1.39 > eroiHigh+h=1.3，约 12 帧入 Earn。
    for (int k = 1; k <= 64; ++k) {
        const ActionVector a = pol.decide(frame(0.05 + 0.01 * k, 1.5), agentsim::TargetList{}, mempol);
        if (k > 24) { // 温身后 EROI 已高
            CHECK(RiskAversePolicy::mode(mempol) == 2);
            CHECK_NEAR(a.boost, 1.0, 1e-12);
            CHECK_NEAR(a.dirX, 1.0, 1e-12);
            CHECK_NEAR(a.dirY, 0.0, 1e-12);
        }
    }
    CHECK(RiskAversePolicy::eroi(mempol) > 1.2);
}

// ---- ② 接近度停滞 -> 无增益 -> EROI 衰减归零 -> Conserve 省能巡航 boost=0.3。----
TEST(riskaverse_low_eroi_cruises) {
    RiskAversePolicy pol; PolicyMemory mempol; // 一个个体一份记忆（P0-4）
    // 预热进 Earn（先构造高 EROI 起点，证明状态机能双向走）。
    for (int k = 1; k <= 40; ++k) (void)pol.decide(frame(0.05 + 0.01 * k, 1.5), agentsim::TargetList{}, mempol);
    CHECK(RiskAversePolicy::mode(mempol) == 2);
    // 100 帧零增益：eroi_k = 1.39*0.875^k，约 10 帧后 < 0.5（Conserve 入带）。
    for (int k = 0; k < 100; ++k) {
        const ActionVector a = pol.decide(frame(0.7, 1.5), agentsim::TargetList{}, mempol);
        if (k > 40) { // 早已穿越 Earn 出带(1.1)与 Conserve 入带(0.5)
            CHECK(RiskAversePolicy::mode(mempol) == 0);
            CHECK(a.boost <= 0.3 + 1e-9);
            CHECK_NEAR(a.boost, 0.3, 1e-12);
        }
    }
    CHECK(RiskAversePolicy::eroi(mempol) < 0.5);
}

// ---- ③ 相同增益序列、s[16]=0.9 vs 0：失真大时 EROI 显著打折、boost 更低。----
TEST(riskaverse_temp_distortion_reduces_eroi) {
    // 两条独立策略跑同一"上升"序列，只有 s[16] 不同。
    // 折扣 = 移动/呼吸代价上浮 (2.39/2.30) * 乘性折扣 (1-0.45) -> 约 0.53 倍。
    RiskAversePolicy cleanPol; PolicyMemory memcleanPol; // 一个个体一份记忆（P0-4）
    RiskAversePolicy dirty; PolicyMemory memdirty; // 一个个体一份记忆（P0-4）
    ActionVector lastClean{}, lastDirty{};
    for (int k = 1; k <= 64; ++k) {
        lastClean = cleanPol.decide(frame(0.05 + 0.01 * k, 1.5, 0.0), agentsim::TargetList{}, memcleanPol);
        lastDirty = dirty.decide(frame(0.05 + 0.01 * k, 1.5, 0.9), agentsim::TargetList{}, memdirty);
    }
    CHECK(RiskAversePolicy::eroi(memcleanPol) > 1.2);            // 无失真 -> EROI 冲高入 Earn
    CHECK(RiskAversePolicy::mode(memcleanPol) == 2);
    CHECK(RiskAversePolicy::eroi(memdirty) >= 0.0);
    CHECK(RiskAversePolicy::eroi(memdirty) < RiskAversePolicy::eroi(memcleanPol));   // 温度失真压低 EROI
    CHECK(RiskAversePolicy::eroi(memdirty) <= 0.7 * RiskAversePolicy::eroi(memcleanPol)); // 且幅度显著（约腰斩）
    CHECK(RiskAversePolicy::mode(memdirty) != 2);                // 打折后够不着 Earn
    CHECK(lastDirty.boost < lastClean.boost); // 同帧 boost 更低
}

// ---- ④ 任意输入 1000 帧（含 NaN/Inf/负值/超界），eroi_ 恒有限且 >= 0。----
TEST(riskaverse_eroi_stays_positive) {
    RiskAversePolicy pol; PolicyMemory mempol; // 一个个体一份记忆（P0-4）
    for (int k = 0; k < 1000; ++k) {
        SensorVector s{};
        // 纯确定性 LCG 常数序列（测试构造用，不涉及被测代码 RNG 红线）。
        const double r1 = ((k * 1103515245 + 12345) % 1000) / 999.0 - 0.5;
        const double r2 = ((k * 69069 + 7) % 1000) / 999.0 - 0.5;
        s[0] = r1;
        s[1] = r2;
        s[6] = (k % 3 == 0) ? -0.7 : 0.1 + 0.8 * ((k % 17) / 17.0);
        s[10] = 2.0 * r2;
        s[11] = -1.0 * r1;
        s[12] = 0.5;
        s[16] = (k % 7 == 0) ? 1.5 : (k % 5 == 0) ? -0.3 : (k % 11) / 11.0;
        if (k % 97 == 0) s[6] = nanVal();
        if (k % 89 == 0) s[16] = nanVal();
        if (k % 83 == 0) s[10] = 1e308; // v^2 溢出为 Inf，必须被有界化
        const ActionVector a = pol.decide(s, agentsim::TargetList{}, mempol);
        CHECK(RiskAversePolicy::eroi(mempol) >= 0.0);
        CHECK(std::isfinite(RiskAversePolicy::eroi(mempol)));
        const double b = a.boost;
        CHECK(b == b); // 无 NaN
        CHECK(b >= 0.0 && b <= 1.0);
        CHECK(std::isfinite(a.dirX) && std::isfinite(a.dirY));
    }
}

// ---- ⑤ 方向长度 ∈ {0} ∪ [0.999, 1.001]，boost ∈ [0,1]（三种模式全覆盖）。----
TEST(riskaverse_output_unit_or_zero) {
    RiskAversePolicy pol; PolicyMemory mempol; // 一个个体一份记忆（P0-4）
    for (int k = 0; k < 240; ++k) {
        // 相位轮转：上升(入 Earn) -> 停滞(衰减) -> 下降(入 Conserve)，覆盖全部模式。
        const double prox = (k < 80) ? 0.1 + 0.01 * (k % 80)
                                     : (k < 160) ? 0.9
                                                 : 0.9 - 0.01 * (k - 160);
        SensorVector s = frame(prox, 1.0, 0.2 * (k % 3));
        if (k % 5 == 0) { s[0] = 0.0; s[1] = 0.0; } // 偶发无方向帧
        const ActionVector a = pol.decide(s, agentsim::TargetList{}, mempol);
        const double len = std::sqrt(a.dirX * a.dirX + a.dirY * a.dirY);
        CHECK(len == 0.0 || (len >= 0.999 && len <= 1.001));
        CHECK(a.boost >= 0.0 && a.boost <= 1.0);
    }
}

// ---- ⑥ 同序列重放逐位一致（禁 RNG 的可测推论），reset 后与全新实例逐位一致。----
TEST(riskaverse_deterministic) {
    RiskAversePolicy pol1; PolicyMemory mempol1; // 一个个体一份记忆（P0-4）
    RiskAversePolicy pol2; PolicyMemory mempol2; // 一个个体一份记忆（P0-4）
    for (int k = 1; k <= 120; ++k) {
        const double prox = (k <= 60) ? 0.05 + 0.015 * k : 0.95 - 0.02 * (k - 60);
        const SensorVector s = frame(prox, 1.2, 0.3 * (k % 2));
        const ActionVector a1 = pol1.decide(s, agentsim::TargetList{}, mempol1);
        const ActionVector a2 = pol2.decide(s, agentsim::TargetList{}, mempol2);
        CHECK(a1.dirX == a2.dirX);
        CHECK(a1.dirY == a2.dirY);
        CHECK(a1.boost == a2.boost);
        CHECK(RiskAversePolicy::eroi(mempol1) == RiskAversePolicy::eroi(mempol2));
        CHECK(RiskAversePolicy::mode(mempol1) == RiskAversePolicy::mode(mempol2));
    }
    // reset 后重放：与全新实例逐位一致（无隐藏状态泄漏）。
    RiskAversePolicy::reset(mempol1);
    RiskAversePolicy pol3; PolicyMemory mempol3; // 一个个体一份记忆（P0-4）
    for (int k = 1; k <= 120; ++k) {
        const double prox = (k <= 60) ? 0.05 + 0.015 * k : 0.95 - 0.02 * (k - 60);
        const SensorVector s = frame(prox, 1.2, 0.3 * (k % 2));
        const ActionVector a1 = pol1.decide(s, agentsim::TargetList{}, mempol1);
        const ActionVector a3 = pol3.decide(s, agentsim::TargetList{}, mempol3);
        CHECK(a1.dirX == a3.dirX);
        CHECK(a1.dirY == a3.dirY);
        CHECK(a1.boost == a3.boost);
    }
}

// ---- ⑦ 全 NaN 输入：输出有限（零向量/有界 boost），eroi_ 有限且 >= 0。----
TEST(riskaverse_nan_clean) {
    RiskAversePolicy pol; PolicyMemory mempol; // 一个个体一份记忆（P0-4）
    for (int k = 0; k < 64; ++k) {
        SensorVector s{};
        for (int i = 0; i < SensorVector::kDim; ++i) s[i] = nanVal();
        const ActionVector a = pol.decide(s, agentsim::TargetList{}, mempol);
        CHECK(std::isfinite(a.dirX));
        CHECK(std::isfinite(a.dirY));
        CHECK(std::isfinite(a.boost));
        CHECK(std::isfinite(RiskAversePolicy::eroi(mempol)));
        CHECK(RiskAversePolicy::eroi(mempol) >= 0.0);
        CHECK(a.boost >= 0.0 && a.boost <= 1.0);
    }
    // NaN 后接正常帧：状态未被污染，仍能正常进 Earn。
    for (int k = 1; k <= 64; ++k) {
        const ActionVector a = pol.decide(frame(0.05 + 0.01 * k, 1.5), agentsim::TargetList{}, mempol);
        CHECK(std::isfinite(a.boost));
    }
    CHECK(RiskAversePolicy::mode(mempol) == 2);
}

// ---- ⑧ Conserve 态方向每 3 帧才更新一次（低频转向，减少转向损耗）。----
TEST(riskaverse_conserve_refreshes_direction_every_3_frames) {
    RiskAversePolicy pol; PolicyMemory mempol; // 一个个体一份记忆（P0-4）
    // 零增益长序列压进 Conserve（食物方向 +x）。
    for (int k = 0; k < 80; ++k) (void)pol.decide(frame(0.7, 1.5, 0.0, 1.0, 0.0), agentsim::TargetList{}, mempol);
    CHECK(RiskAversePolicy::mode(mempol) == 0);
    // 进入 Conserve 后：食物方向突变为 +y。只有 refresh 帧才允许方向跳变。
    int switched = 0;
    double prevY = 0.0;
    for (int k = 0; k < 9; ++k) {
        const ActionVector a = pol.decide(frame(0.7, 1.5, 0.0, 0.0, 1.0), agentsim::TargetList{}, mempol);
        if (a.dirY != prevY) ++switched;
        prevY = a.dirY;
    }
    // 9 帧内最多一次真实跳变（(1,0)->(0,1) 之后的 refresh 都是同值覆写）。
    CHECK(switched <= 2);
    // 且最终稳定在 +y（缓存已被某个 refresh 帧覆盖）。
    const ActionVector last = pol.decide(frame(0.7, 1.5, 0.0, 0.0, 1.0), agentsim::TargetList{}, mempol);
    CHECK_NEAR(last.dirY, 1.0, 1e-12);
}

// ---- ⑨ name() / reset() 契约（组合根装配用）。----
TEST(riskaverse_name_and_reset) {
    RiskAversePolicy pol; PolicyMemory mempol; // 一个个体一份记忆（P0-4）
    CHECK(pol.name() != nullptr);
    CHECK(pol.name()[0] != 0);
    (void)pol.decide(frame(0.5, 1.0), agentsim::TargetList{}, mempol);
    RiskAversePolicy::reset(mempol);
    CHECK(RiskAversePolicy::eroi(mempol) == 0.0);
    CHECK(RiskAversePolicy::mode(mempol) == 1);
    CHECK(RiskAversePolicy::frameInMode(mempol) == 0);
}
