// agentsim - tests/unit/test_foragebank.cpp
// ForageBankPolicy（觅食-蓄能两态策略）单测：低能觅食、高能蓄能、阈值间滞回、
// 输出范数约束、无 RNG 双跑一致。红线：测试本身不用 RNG，全部手工构造传感器。
#include "test_framework.h"

#include <cmath>

#include "../../src/core/SensorTypes.h"
#include "../../src/decision/ForageBankPolicy.h"

namespace {

using agentsim::ActionVector;
using agentsim::ForageBankPolicy;
using agentsim::SensorVector;

// 构造传感器：食物方向 (fx,fy)、边界距离 s[13]=margin、速度 (vx,vy)。
// s[12] 按 speed/(1+speed) 契约填；s[9] 与 s[6] 填成一致（有目标即可）。
SensorVector mk(double fx, double fy, double margin, double vx, double vy) {
    SensorVector s{};
    const double fl = std::sqrt(fx * fx + fy * fy);
    if (fl > 0.0) { s[0] = fx / fl; s[1] = fy / fl; s[6] = 0.7; s[9] = 0.3; }
    s[13] = margin;
    s[10] = vx;
    s[11] = vy;
    const double sp = std::sqrt(vx * vx + vy * vy);
    s[12] = sp / (1.0 + sp);
    return s;
}

} // namespace

// ---- ① 低能量代理：方向指向最近食物、boost=1.0（全力觅食）。----
TEST(foragebank_low_energy_forages) {
    ForageBankPolicy pol;
    CHECK(!pol.foraging()); // 默认蓄能态，先压低代理触发换态
    // 代理 = 0.8*0.1 + 0.2*(1-0) = 0.28 < forageBelow 0.3 -> 觅食态。
    const ActionVector a = pol.decide(mk(3.0, -4.0, 0.1, 0.0, 0.0));
    CHECK(pol.foraging());
    CHECK_NEAR(a.dirX, 0.6, 1e-12);
    CHECK_NEAR(a.dirY, -0.8, 1e-12);
    CHECK_NEAR(a.boost, 1.0, 1e-12);
}

// ---- ② 高能量代理：蓄能态，boost=0，方向不再指向食物。----
TEST(foragebank_high_energy_banks) {
    ForageBankPolicy pol;
    // 代理 = 0.8*1.0 + 0.2*(1-0) = 1.0 > bankAbove 0.7 -> 蓄能态。
    const ActionVector a = pol.decide(mk(0.0, 1.0, 1.0, 0.0, 0.0));
    // P0 修复（2026-10-31）：蓄能态 + 速度为零原会输出零向量 → 永久静止死锁
    // （实测 seed42 的 12 个核出生时全部落在中间带并保持默认蓄能态，
    //   导致 foragebank 与 StubPolicy 逐帧完全相同、策略形同不存在）。
    // 现在速度近零时强制转入觅食态自举，因此此处**应当**处于觅食态。
    CHECK(pol.foraging());              // 自举：不允许静止死锁
    CHECK_NEAR(a.boost, 1.0, 1e-12);    // 自举即全力觅食
    // 有速度时 -> 逆速度方向（停止扩张），仍不指向食物。
    // （先让策略进入蓄能态：本用例的代理 1.0 > bankAbove，且此时速度非零，
    //   不会触发自举分支。）
    const ActionVector b = pol.decide(mk(0.0, 1.0, 1.0, 5.0, 0.0));
    CHECK_NEAR(b.dirX, -1.0, 1e-12);
    CHECK_NEAR(b.dirY, 0.0, 1e-12);
    CHECK_NEAR(b.boost, 0.0, 1e-12);
}

// ---- ③ 中间带滞回：从觅食态进入中间带保持觅食；从蓄能态进入保持蓄能。----
TEST(foragebank_hysteresis_between_thresholds) {
    ForageBankPolicy pol;
    // 先进入觅食态（代理 0.28 < 0.3）。
    (void)pol.decide(mk(1.0, 0.0, 0.1, 0.0, 0.0));
    CHECK(pol.foraging());
    // 代理 = 0.8*0.25 + 0.2*(1-0.5) = 0.3：不小于 forageBelow(0.3)，
    // 也不大于 bankAbove(0.7) -> 落入中间带，按滞回规则保持觅食态。
    const ActionVector mid = pol.decide(mk(1.0, 0.0, 0.25, 1.0, 0.0));
    CHECK(pol.foraging());              // 保持觅食态（滞回）
    CHECK_NEAR(mid.boost, 1.0, 1e-12);  // 仍全力觅食
    CHECK_NEAR(mid.dirX, 1.0, 1e-12);   // 仍指向食物

    // 另开一个策略：先进入蓄能态（代理 1.0 > 0.7），再进同一中间带。
    // 另开一个策略：先在**速度非零**的条件下进入蓄能态（代理 1.0 > 0.7），
    // 再进同一中间带，验证滞回保持蓄能态。
    // （不能用 speed=0 的输入进蓄能态——那会触发 P0 修复引入的自举分支，
    //   强制转入觅食态，测不到滞回。）
    ForageBankPolicy pol2;
    (void)pol2.decide(mk(1.0, 0.0, 1.0, 3.0, 0.0)); // 有速度 -> 正常进蓄能态
    CHECK(!pol2.foraging());
    const ActionVector mid2 = pol2.decide(mk(1.0, 0.0, 0.25, 1.0, 0.0));
    CHECK(!pol2.foraging());            // 保持蓄能态（滞回）
    CHECK_NEAR(mid2.boost, 0.0, 1e-12); // 不冲刺
}

// ---- ④ 输出方向要么是单位向量要么是零向量（长度绝不超过 1.001）。----
TEST(foragebank_output_unit_or_zero) {
    ForageBankPolicy pol;
    // 觅食态：食物方向带非单位噪声输入。
    const ActionVector a = pol.decide(mk(3.0, 4.0, 0.0, 0.0, 0.0));
    const double na = std::sqrt(a.dirX * a.dirX + a.dirY * a.dirY);
    CHECK(na <= 1.001);
    CHECK(na >= 0.999); // 此场景应恰为单位向量
    // 蓄能态：逆速度方向，也是单位向量。
    const ActionVector b = pol.decide(mk(0.0, 0.0, 1.0, -2.0, 2.0));
    const double nb = std::sqrt(b.dirX * b.dirX + b.dirY * b.dirY);
    CHECK(nb <= 1.001);
    CHECK(nb >= 0.999);
    // 无速度蓄能：零向量。
    const ActionVector c = pol.decide(mk(0.0, 0.0, 1.0, 0.0, 0.0));
    CHECK_NEAR(c.dirX, 0.0, 1e-12);
    CHECK_NEAR(c.dirY, 0.0, 1e-12);
    // 全 NaN 输入：无 NaN 传播，输出有限。
    SensorVector bad{};
    for (int i = 0; i < SensorVector::kDim; ++i) bad[i] = std::nan("");
    const ActionVector d = pol.decide(bad);
    CHECK(std::isfinite(d.dirX) && std::isfinite(d.dirY) && std::isfinite(d.boost));
}

// ---- ⑤ 无 RNG：同一策略实例同一输入连调两次，输出逐位相同。----
TEST(foragebank_deterministic) {
    ForageBankPolicy pol;
    (void)pol.decide(mk(1.0, 0.0, 0.1, 0.0, 0.0)); // 预热进入觅食态
    const SensorVector s = mk(2.0, -1.0, 0.1, 0.0, 0.0);
    const ActionVector a1 = pol.decide(s);
    const ActionVector a2 = pol.decide(s);
    CHECK(a1.dirX == a2.dirX);
    CHECK(a1.dirY == a2.dirY);
    CHECK(a1.boost == a2.boost);
}
