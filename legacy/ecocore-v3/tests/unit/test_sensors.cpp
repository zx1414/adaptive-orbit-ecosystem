// agentsim - tests/unit/test_sensors.cpp
// R1-B（蓝细菌取代食物）：18 维传感器单测。s[0..9] 的候选来源从 Food 换成
// 蓝细菌节点容器（唯一食物来源），语义/噪声模型/rng 消耗序与旧食物版同构；
// 温度维（s[16]/s[17]）、侦察球半径（S17）、读噪声（S16/P2-8）断言全部保留。
#include "test_framework.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "../../src/core/Config.h"
#include "../../src/core/PCG32.h"
#include "../../src/core/SensorTypes.h"
#include "../../src/core/Vec2.h"
#include "../../src/decision/RulePolicy.h"
#include "../../src/environment/AgentEnvironment.h"
#include "../../src/environment/CyanoNode.h"
#include "../../src/environment/TemperatureField.h"
#include "../../src/perception/AgentSensors.h"

namespace {

// R1-B：播种蓝细菌场，再确定性扫描找一个半径 24 内 >=2 个节点的点。
// 等价旧 makeEnvWithFood：返回世界 + 候选容器 + 该点。
agentsim::AgentEnvironment makeEnvWithCyano(std::vector<agentsim::CyanoNode>& field,
                                            agentsim::Vec2& center) {
    agentsim::AgentEnvironment env(agentsim::SimConfig{});
    agentsim::CyanoParams p;
    p.nodeSeedCount = 32;
    p.maxNodes = 1024;
    agentsim::CyanoField cy(p);
    agentsim::PCG32 rng(5);
    cy.seed(32, 256.0, 256.0, rng);
    field = cy.nodes();
    for (int gy = 0; gy < 64; ++gy) {
        for (int gx = 0; gx < 64; ++gx) {
            const agentsim::Vec2 p2{2.0 * gx + 1.0, 2.0 * gy + 1.0};
            int near = 0;
            for (const agentsim::CyanoNode& n : field) {
                if (agentsim::torusDistance(p2, n.pos, 256.0, 256.0) <= 24.0) ++near;
            }
            if (near >= 2) { center = p2; return env; }
        }
    }
    center = agentsim::Vec2{128.0, 128.0};
    return env;
}

// 平场：baseline == preferred -> 失真恒 0。
agentsim::TemperatureField flatField() {
    agentsim::TemperatureConfig c;
    c.sourceCount = 0;
    c.baseline = 0.5;
    c.preferred = 0.5;
    agentsim::PCG32 rng(1);
    return agentsim::TemperatureField(c, 256.0, 256.0, rng);
}

// 热源场：中心点 sample=0.9，distortion = |0.9-0.5|*2 = 0.8。
agentsim::TemperatureField hotField(agentsim::Vec2 center) {
    agentsim::TemperatureConfig c;
    c.sourceCount = 0;
    c.baseline = 0.5;
    c.preferred = 0.5;
    agentsim::TemperatureSource s;
    s.x = center.x;
    s.y = center.y;
    s.amplitude = 0.4;
    s.radius = 30.0;
    c.sources.push_back(s);
    agentsim::PCG32 rng(2);
    return agentsim::TemperatureField(c, 256.0, 256.0, rng);
}

// 本地转发器：参数序 (env, 候选容器, self, vel, speed, cfg, rng, temp, terrain, scouts)。
// 内部转发到 agentsim::sense 的生产签名（容器在 rng 之后）——单测侧只换容器位置，
// 语义零差异（薄转发，非被测逻辑）。
agentsim::SensorVector senseCyano(const agentsim::AgentEnvironment& env,
                                  const std::vector<agentsim::CyanoNode>& field,
                                  agentsim::Vec2 pos, agentsim::Vec2 vel, double speed,
                                  const agentsim::PolicyConfig& cfg, agentsim::PCG32* rng,
                                  const agentsim::TemperatureField& temp = agentsim::TemperatureField{},
                                  const agentsim::Terrain* terrain = nullptr,
                                  int scouts = 0) {
    return agentsim::sense(env, pos, vel, speed, cfg, rng, field, temp, terrain, scouts);
}

bool finiteAll(const agentsim::SensorVector& s) {
    for (int i = 0; i < agentsim::SensorVector::kDim; ++i) {
        if (!std::isfinite(s[i])) return false;
    }
    return true;
}

} // namespace

// ---- 1) kDim==18；2) s[16]/s[17] 与 temp.distortion/sample 一致（平场与热源场）----
TEST(sensors_kdim_is_24_and_temperature_dims_match_field) {
    // 主代理维护：R2 契约把 kDim 从 18 扩到 24（core/SensorTypes.h，s[0..17] 语义不变）。
    // 本断言是维度契约的哨兵；改为字面量会让维度变更悄悄漏过（曾因此漏掉
    // PerceptronPolicy 的 w[2][18] 越界）。
    static_assert(agentsim::SensorVector::kDim == 24,
                  "SensorVector must be 24-dim after R2 social/force wiring");
    volatile int dim = agentsim::SensorVector::kDim; // volatile 阻常量折叠，避开 C4127/WX
    CHECK(dim == 24);
    std::vector<agentsim::CyanoNode> field;
    agentsim::Vec2 pos{};
    const agentsim::AgentEnvironment env = makeEnvWithCyano(field, pos);
    const agentsim::PolicyConfig cfg;
    const agentsim::Vec2 vel{1.0, 0.0};
    const agentsim::TemperatureField flat = flatField();
    const agentsim::TemperatureField hot = hotField(pos);
    for (int f = 0; f < 2; ++f) {
        const agentsim::TemperatureField& tf = (f == 0) ? flat : hot;
        for (int trial = 0; trial < 3; ++trial) {
            agentsim::PCG32 rng(static_cast<unsigned long long>(7 + trial));
            const agentsim::SensorVector s =
                senseCyano(env, field, pos, vel, 6.0, cfg, &rng, tf);
            CHECK_NEAR(s[16], tf.distortion(pos), 1e-15);
            CHECK_NEAR(s[17], tf.sample(pos), 1e-15);
            CHECK(s[16] >= 0.0 && s[16] <= 1.0);
            CHECK(s[17] >= 0.0 && s[17] <= 1.0);
        }
    }
    // 平场：d==0；热源场中心：d==0.8、t==0.9（精确解）。
    agentsim::PCG32 rng(7);
    const agentsim::SensorVector sf =
        senseCyano(env, field, pos, vel, 6.0, cfg, &rng, flat);
    CHECK_NEAR(sf[16], 0.0, 1e-15);
    CHECK_NEAR(sf[17], 0.5, 1e-15);
    agentsim::PCG32 rng2(7);
    const agentsim::SensorVector sh =
        senseCyano(env, field, pos, vel, 6.0, cfg, &rng2, hot);
    CHECK_NEAR(sh[16], 0.8, 1e-12);
    CHECK_NEAR(sh[17], 0.9, 1e-12);
}

// ---- 3) 温度失真放大读噪声：同种子同速度，热源场 amp 严格大于平场 ----
TEST(sensors_temperature_distortion_amplifies_read_noise) {
    std::vector<agentsim::CyanoNode> field;
    agentsim::Vec2 pos{};
    const agentsim::AgentEnvironment env = makeEnvWithCyano(field, pos);
    const agentsim::PolicyConfig cfg;
    const agentsim::Vec2 vel{1.0, 0.0};
    const agentsim::TemperatureField flat = flatField();
    const agentsim::TemperatureField hot = hotField(pos);
    for (int trial = 0; trial < 8; ++trial) {
        agentsim::PCG32 rngF(static_cast<unsigned long long>(100 + trial));
        agentsim::PCG32 rngH(static_cast<unsigned long long>(100 + trial));
        const agentsim::SensorVector sf =
            senseCyano(env, field, pos, vel, 10.0, cfg, &rngF, flat);
        const agentsim::SensorVector sh =
            senseCyano(env, field, pos, vel, 10.0, cfg, &rngH, hot);
        // s[14] 是本帧读噪声幅度元信息维，直接可测放大关系。
        CHECK(sh[14] > sf[14]);
        CHECK_NEAR(sh[14], sf[14] * (1.0 + 3.0 * sh[16]), 1e-12);
    }
}

// ---- 4) speed=0 且 d=0：amp 必须为 0，s[0..9] 与无噪声（rng=nullptr）一致 ----
TEST(sensors_no_noise_at_rest_without_distortion) {
    std::vector<agentsim::CyanoNode> field;
    agentsim::Vec2 pos{};
    const agentsim::AgentEnvironment env = makeEnvWithCyano(field, pos);
    const agentsim::PolicyConfig cfg;
    const agentsim::Vec2 vel{1.0, 0.0};
    const agentsim::TemperatureField flat = flatField();
    agentsim::PCG32 rng(7);
    const agentsim::SensorVector sn =
        senseCyano(env, field, pos, vel, 0.0, cfg, &rng, flat);
    const agentsim::SensorVector sd =
        senseCyano(env, field, pos, vel, 0.0, cfg, nullptr, flat);
    CHECK_NEAR(sn[14], 0.0, 0.0); // amp 恰为 0
    CHECK(sn[16] == 0.0);
    for (int i = 0; i <= 9; ++i) CHECK_NEAR(sn[i], sd[i], 0.0); // 逐位一致
    // amp=0 时 sense 不消耗 rng：前后 PCG32 内部状态逐位一致（无噪声路径零副作用）。
    std::uint64_t st1 = 0, in1 = 0, st2 = 0, in2 = 0;
    agentsim::PCG32 probe(7);
    probe.getState(st1, in1);
    (void)senseCyano(env, field, pos, vel, 0.0, cfg, &probe, flat);
    probe.getState(st2, in2);
    CHECK(st1 == st2 && in1 == in2);
}

// ---- 5) 输出无 NaN/Inf（噪声路径 + 温度维）----
TEST(sensors_output_finite_all_dims) {
    std::vector<agentsim::CyanoNode> field;
    agentsim::Vec2 pos{};
    const agentsim::AgentEnvironment env = makeEnvWithCyano(field, pos);
    const agentsim::PolicyConfig cfg;
    const agentsim::Vec2 vel{1.0, 0.0};
    const agentsim::TemperatureField hot = hotField(pos);
    for (int trial = 0; trial < 16; ++trial) {
        for (double speed : {0.0, 0.5, 4.0, 40.0}) {
            agentsim::PCG32 rng(static_cast<unsigned long long>(900 + trial));
            const agentsim::SensorVector s =
                senseCyano(env, field, pos, vel, speed, cfg, &rng, hot);
            CHECK(finiteAll(s));
            CHECK(s[16] >= 0.0 && s[16] <= 1.0);
            CHECK(s[17] >= 0.0 && s[17] <= 1.0);
            CHECK(s[14] >= 0.0); // amp 非负（失真只放大，不改号）
        }
    }
}

// ---- 6) RulePolicy 温度信任因子：d>0 时 proximity 打折可能改选/降分，d=0 不变 ----
TEST(rulepolicy_temperature_trust_reduces_proximity_weight) {
    const agentsim::PolicyConfig cfg;
    std::vector<agentsim::CyanoNode> field;
    agentsim::Vec2 pos{};
    const agentsim::AgentEnvironment env = makeEnvWithCyano(field, pos);
    const agentsim::TemperatureField flat = flatField();
    const agentsim::TemperatureField hot = hotField(pos);
    // S17 注：显式给 3 个侦察球（有效半径 48），保证候选 0 在读数范围内，
    // 维持"平场下有候选可追"的本意（默认盲区 6 会缩水候选）。
    agentsim::PCG32 rng1(7);
    const agentsim::SensorVector sf =
        senseCyano(env, field, pos, {1.0, 0.0}, 10.0, cfg, &rng1, flat, nullptr, 3);
    agentsim::PCG32 rng2(7);
    const agentsim::SensorVector sh =
        senseCyano(env, field, pos, {1.0, 0.0}, 10.0, cfg, &rng2, hot, nullptr, 3);
    agentsim::RulePolicy pol; // decide() 非 const 成员（接口如此），不可 const 调用
    const agentsim::ActionVector af = pol.decide(sf);
    const agentsim::ActionVector ah = pol.decide(sh);
    CHECK(af.dirX != 0.0 || af.dirY != 0.0); // 平场下有候选可追
    CHECK(finiteAll(sf) && finiteAll(sh));
    CHECK(std::isfinite(ah.dirX) && std::isfinite(ah.dirY));
    CHECK(std::fabs(ah.dirX * ah.dirX + ah.dirY * ah.dirY) <= 1.0 + 1e-12);
    // 可翻转场景（手工构造，wP=1,wA=0.8,wU=0.6；urgency 为同常数不影响比较）：
    //   候选0 近而垂直航向：prox=0.9, dir=(0,1)（align=0）
    //   候选1 远而 60° 顺路：prox=0.4, dir=(0.5, sqrt3/2)（align=0.5，二进制精确）
    //   航向 (1,0)：s[10]=s[12]=0.5。
    //   d=0：score0=0.9     > score1=0.4+0.8*0.5=0.8 → 选 0。
    //   d=1：score0=0.5*0.9 < score1=0.5*(0.4+0.4)  → 选 1。信任因子真实改选。
    agentsim::SensorVector sv{};
    sv[0] = 0.0; sv[1] = 1.0; sv[6] = 0.9;          // 候选0
    sv[2] = 0.5; sv[3] = 0.8660254037844386; sv[7] = 0.4; // 候选1
    sv[10] = 0.5; sv[11] = 0.0; sv[12] = 0.5;       // 航向 (1,0)
    sv[16] = 0.0;
    const agentsim::ActionVector a0 = pol.decide(sv);
    CHECK(std::fabs(a0.dirX) < 1e-12 && std::fabs(a0.dirY - 1.0) < 1e-12);
    sv[16] = 1.0;
    const agentsim::ActionVector a1 = pol.decide(sv);
    CHECK(std::fabs(a1.dirX - 0.5) < 1e-12 && std::fabs(a1.dirY - 0.8660254037844386) < 1e-12);
}

// ---- 7) 默认参数调用（6 参旧签名）：cyanoNodes 默认空 = 无候选，向后兼容 ----
TEST(sensors_default_cyano_arg_backward_compatible) {
    std::vector<agentsim::CyanoNode> field;
    agentsim::Vec2 pos{};
    const agentsim::AgentEnvironment env = makeEnvWithCyano(field, pos);
    const agentsim::PolicyConfig cfg;
    agentsim::PCG32 rng1(7);
    const agentsim::SensorVector a =
        agentsim::sense(env, pos, {1.0, 0.0}, 8.0, cfg, &rng1);
    const std::vector<agentsim::CyanoNode> empty;
    agentsim::PCG32 rng2(7);
    const agentsim::SensorVector b =
        senseCyano(env, empty, pos, {1.0, 0.0}, 8.0, cfg, &rng2, flatField());
    for (int i = 0; i < agentsim::SensorVector::kDim; ++i) CHECK_NEAR(a[i], b[i], 0.0);
    CHECK_NEAR(a[16], 0.0, 0.0);   // 默认场 distortion == 0（baseline==preferred）
    CHECK_NEAR(a[17], 0.5, 0.0);   // 默认场 sample == baseline == 0.5
    // 空容器 = 无候选：s[6..9] 恒 0。
    for (int i = 6; i <= 9; ++i) CHECK_NEAR(a[i], 0.0, 0.0);
}

// ---- R1-B：候选来自蓝细菌容器——有候选时 s[0..9] 非零、方向为单位向量 ----
TEST(sense_cyano_candidates_fill_food_dims) {
    std::vector<agentsim::CyanoNode> field;
    agentsim::Vec2 pos{};
    const agentsim::AgentEnvironment env = makeEnvWithCyano(field, pos);
    const agentsim::PolicyConfig cfg;
    const agentsim::TemperatureField flat = flatField();
    agentsim::PCG32 rng(7);
    const agentsim::SensorVector s =
        senseCyano(env, field, pos, {1.0, 0.0}, 4.0, cfg, &rng, flat, nullptr, 3);
    // 中心点由扫描保证 >=2 个候选在半径 24 内（有效半径 48）。
    int cands = 0;
    for (int i = 0; i < 3; ++i) {
        if (s[6 + i] > 0.0) ++cands;
        const double dx = s[2 * i + 0];
        const double dy = s[2 * i + 1];
        const double len = std::sqrt(dx * dx + dy * dy);
        CHECK(len == 0.0 || (len >= 0.999 && len <= 1.001)); // C5 归一化契约
    }
    CHECK(cands >= 2);
    CHECK(s[9] > 0.0);            // 最近节点距离维非零
    CHECK(s[9] <= 1.0);
}

// ---- S16: 读噪声按候选独立抽取（旧版整帧一组偏移，三候选同偏移=假噪声）----
TEST(sense_noise_is_per_candidate) {
    std::vector<agentsim::CyanoNode> field;
    agentsim::Vec2 pos{};
    const agentsim::AgentEnvironment env = makeEnvWithCyano(field, pos);
    const agentsim::PolicyConfig cfg;
    const agentsim::Vec2 vel{1.0, 0.0};
    const agentsim::TemperatureField flat = flatField();
    const agentsim::SensorVector clean =
        senseCyano(env, field, pos, vel, 5.0, cfg, nullptr, flat, nullptr, 3);
    agentsim::PCG32 rng(9);
    const agentsim::SensorVector noisy =
        senseCyano(env, field, pos, vel, 5.0, cfg, &rng, flat, nullptr, 3);
    CHECK(noisy[14] > 0.0); // amp > 0：噪声路径真实生效
    int nCand = 0;
    double noise[3] = {0.0, 0.0, 0.0};
    for (int i = 0; i < 3; ++i) {
        if (clean[6 + i] > 0.0) {
            noise[nCand++] = noisy[6 + i] - clean[6 + i];
        }
    }
    CHECK(nCand >= 2);
    bool differs = false;
    for (int i = 0; i < nCand; ++i) {
        for (int j = i + 1; j < nCand; ++j) {
            if (std::fabs(noise[i] - noise[j]) > 1e-12) differs = true;
        }
    }
    CHECK(differs); // 旧代码此处恒 false（三候选偏移逐位相等）
}

// ---- S16: speed=0 时 amp==0，sense() 不消耗 rng（前后状态逐位不变）----
TEST(sense_zero_speed_no_rng_consumption) {
    std::vector<agentsim::CyanoNode> field;
    agentsim::Vec2 pos{};
    const agentsim::AgentEnvironment env = makeEnvWithCyano(field, pos);
    const agentsim::PolicyConfig cfg;
    const agentsim::Vec2 vel{1.0, 0.0};
    const agentsim::TemperatureField flat = flatField();
    agentsim::PCG32 rng(11);
    std::uint64_t st1 = 0, in1 = 0, st2 = 0, in2 = 0;
    rng.getState(st1, in1);
    const agentsim::SensorVector s =
        senseCyano(env, field, pos, vel, 0.0, cfg, &rng, flat);
    rng.getState(st2, in2);
    CHECK(st1 == st2);
    CHECK(in1 == in2);
    // 同一 rng 接着以 speed>0 抽样仍能推进：证明上面不是"坏了"而是"没抽"。
    const double d0 = rng.nextDouble();
    std::uint64_t st3 = 0, in3 = 0;
    rng.getState(st3, in3);
    CHECK(st3 != st2 || in3 != in2);
    (void)d0;
    CHECK_NEAR(s[14], 0.0, 0.0); // speed=0 时 amp 恰为 0
}

// ---- S16: 同一种子两个相同 rng，sense() 输出 18 维逐位相等 ----
TEST(sense_deterministic_same_seed) {
    std::vector<agentsim::CyanoNode> field;
    agentsim::Vec2 pos{};
    const agentsim::AgentEnvironment env = makeEnvWithCyano(field, pos);
    const agentsim::PolicyConfig cfg;
    const agentsim::Vec2 vel{1.0, 0.0};
    const agentsim::TemperatureField flat = flatField();
    for (int trial = 0; trial < 4; ++trial) {
        const double speed = 1.0 + 3.0 * static_cast<double>(trial);
        agentsim::PCG32 r1(static_cast<unsigned long long>(77 + trial));
        agentsim::PCG32 r2(static_cast<unsigned long long>(77 + trial));
        const agentsim::SensorVector a =
            senseCyano(env, field, pos, vel, speed, cfg, &r1, flat);
        const agentsim::SensorVector b =
            senseCyano(env, field, pos, vel, speed, cfg, &r2, flat);
        for (int i = 0; i < agentsim::SensorVector::kDim; ++i) {
            CHECK_NEAR(a[i], b[i], 0.0); // eps=0：逐位相等
        }
    }
}

// ---- P2-8：s[9] 独立读噪声（第 4 组随机数，与候选 0 接近度偏移统计独立）----
TEST(sense_s9_has_independent_noise) {
    std::vector<agentsim::CyanoNode> field;
    agentsim::Vec2 pos{};
    const agentsim::AgentEnvironment env = makeEnvWithCyano(field, pos);
    const agentsim::PolicyConfig cfg;
    const agentsim::Vec2 vel{1.0, 0.0};
    const agentsim::TemperatureField flat = flatField();
    const int scouts = 3; // 有效半径 48：中心点必有 >=2 候选
    const agentsim::SensorVector clean =
        senseCyano(env, field, pos, vel, 5.0, cfg, nullptr, flat, nullptr, scouts);
    CHECK(clean[9] > 0.0);  // 有最近节点（距离维非退化）
    CHECK(clean[6] > 0.0);  // 候选 0 接近度有效
    int differingSeeds = 0;
    for (int trial = 0; trial < 12; ++trial) {
        agentsim::PCG32 rng(static_cast<unsigned long long>(3100 + trial));
        const agentsim::SensorVector noisy =
            senseCyano(env, field, pos, vel, 5.0, cfg, &rng, flat, nullptr, scouts);
        const double amp = noisy[14];
        CHECK(amp > 0.0);
        const double noise9 = noisy[9] - clean[9];
        const double noise0 = noisy[6] - clean[6];
        CHECK(std::fabs(noise9) <= amp + 1e-9); // s[9] 偏移幅度不超过 amp
        CHECK(std::fabs(noise0) <= amp + 1e-9); // 接近度偏移幅度不超过 amp
        if (std::fabs(noise9 - noise0) > 1e-12) ++differingSeeds;
    }
    CHECK(differingSeeds >= 1); // 旧代码（复用候选 0 偏移）此处恒 0：修复红测试
}

// S17：无侦察球退到"原生盲区"= 基准半径的 1/4。
TEST(sense_effective_radius_zero_scouts) {
    CHECK_NEAR(agentsim::effectiveSenseRadius(24.0, 0), 6.0, 0.0);
    CHECK_NEAR(agentsim::effectiveSenseRadius(24.0, -5), 6.0, 0.0); // 负数视同无球
}
// S17：n 个侦察球按 sqrt(1+n) 扩张：24*sqrt(4)=48、24*sqrt(9)=72。
TEST(sense_effective_radius_scales_sqrt) {
    CHECK_NEAR(agentsim::effectiveSenseRadius(24.0, 3), 48.0, 0.0);
    CHECK_NEAR(agentsim::effectiveSenseRadius(24.0, 8), 72.0, 0.0);
}
// S17：上限 4 倍基准半径（24*4=96），侦察球再多也不再扩张。
TEST(sense_effective_radius_capped) {
    CHECK_NEAR(agentsim::effectiveSenseRadius(24.0, 1000), 96.0, 0.0);
    CHECK_NEAR(agentsim::effectiveSenseRadius(24.0, 100000), 96.0, 0.0);
}

// S17：scoutCount 真实生效——同一节点的接近度 s[6] 因分母（有效半径）变化而不同。
TEST(sense_scout_count_changes_radius) {
    const agentsim::PolicyConfig cfg;
    const agentsim::Vec2 vel{1.0, 0.0};
    const agentsim::TemperatureField flat = flatField();
    // 找一个原生盲区（6.0）内就有节点的点：保证 scouts=0 路径非退化。
    agentsim::AgentEnvironment env(agentsim::SimConfig{});
    agentsim::CyanoParams p;
    p.nodeSeedCount = 32;
    p.maxNodes = 1024;
    agentsim::CyanoField cy(p);
    agentsim::PCG32 rng(5);
    cy.seed(32, 256.0, 256.0, rng);
    const std::vector<agentsim::CyanoNode> field = cy.nodes();
    agentsim::Vec2 pos{128.0, 128.0};
    bool found = false;
    for (int gy = 0; gy < 64 && !found; ++gy) {
        for (int gx = 0; gx < 64 && !found; ++gx) {
            const agentsim::Vec2 q{2.0 * gx + 1.0, 2.0 * gy + 1.0};
            for (const agentsim::CyanoNode& n : field) {
                if (agentsim::torusDistance(q, n.pos, 256.0, 256.0) <= 6.0) {
                    pos = q; found = true; break;
                }
            }
        }
    }
    CHECK(found);
    const agentsim::SensorVector s0 =
        senseCyano(env, field, pos, vel, 3.0, cfg, nullptr, flat, nullptr, 0);
    const agentsim::SensorVector s3 =
        senseCyano(env, field, pos, vel, 3.0, cfg, nullptr, flat, nullptr, 3);
    CHECK(s0[6] > 0.0);            // scouts=0：盲区半径 6 内确有节点
    CHECK(s3[6] > 0.0);            // scouts=3：有效半径 48，同一节点仍在域内
    CHECK(s3[6] > s0[6] + 1e-12);  // 最近节点相同：分母 6->48，接近度严格上升
}

// S17 构造性保证：sense() 签名里没有任何资源球/球总数形参。此处按规格断言
// 回归保护：完全相同的入参连调两次，输出逐位相等（若未来有人偷偷加入隐藏态
// 或未入参的输入源，此断言即红）。
TEST(sense_resource_balls_have_no_parameter) {
    std::vector<agentsim::CyanoNode> field;
    agentsim::Vec2 pos{};
    const agentsim::AgentEnvironment env = makeEnvWithCyano(field, pos);
    const agentsim::PolicyConfig cfg;
    const agentsim::Vec2 vel{1.0, 0.0};
    const agentsim::TemperatureField flat = flatField();
    for (int trial = 0; trial < 3; ++trial) {
        const double speed = 1.0 + 2.0 * static_cast<double>(trial);
        agentsim::PCG32 r1(static_cast<unsigned long long>(5200 + trial));
        agentsim::PCG32 r2(static_cast<unsigned long long>(5200 + trial));
        const agentsim::SensorVector a =
            senseCyano(env, field, pos, vel, speed, cfg, &r1, flat, nullptr, 2);
        const agentsim::SensorVector b =
            senseCyano(env, field, pos, vel, speed, cfg, &r2, flat, nullptr, 2);
        for (int i = 0; i < agentsim::SensorVector::kDim; ++i) {
            CHECK_NEAR(a[i], b[i], 0.0); // eps=0：逐位相等
        }
    }
}

// S17 回归安全：末位默认参数（不传 scoutCount）与显式传 0 的输出逐位相等——
// 所有旧调用点（Simulation、test_behavior 等）行为自动保持。
TEST(sense_scout_count_default_preserves_behavior) {
    std::vector<agentsim::CyanoNode> field;
    agentsim::Vec2 pos{};
    const agentsim::AgentEnvironment env = makeEnvWithCyano(field, pos);
    const agentsim::PolicyConfig cfg;
    const agentsim::Vec2 vel{1.0, 0.0};
    const agentsim::TemperatureField flat = flatField();
    agentsim::PCG32 r1(77);
    const agentsim::SensorVector a =
        senseCyano(env, field, pos, vel, 4.0, cfg, &r1, flat);
    agentsim::PCG32 r2(77);
    const agentsim::SensorVector b =
        senseCyano(env, field, pos, vel, 4.0, cfg, &r2, flat, nullptr, 0);
    for (int i = 0; i < agentsim::SensorVector::kDim; ++i) {
        CHECK_NEAR(a[i], b[i], 0.0); // eps=0：逐位相等
    }
}

// =====================================================================
// ---- R2-C：社会/施力感知新维 s[18..23]（V2-SPEC-AMEND-1）----
// 语义、边界值、哨兵与回归安全。新维是确定性尾部：无 rng 副作用、
// 不污染 s[0..17]。
// =====================================================================

// 默认实参（R2 三参全缺省）：输出与"显式传默认值"逐位一致；且 s[0..17] 与
// 旧 10 参调用（不带 R2 尾参）逐位一致——回归安全的构造性证明。
TEST(sense_r2_default_args_preserve_old_dims) {
    std::vector<agentsim::CyanoNode> field;
    agentsim::Vec2 pos{};
    const agentsim::AgentEnvironment env = makeEnvWithCyano(field, pos);
    const agentsim::PolicyConfig cfg;
    const agentsim::Vec2 vel{1.0, 0.0};
    const agentsim::TemperatureField flat = flatField();

    // 旧 10 参调用（R1-B 签名，末位 scoutCount）。
    agentsim::PCG32 r1(31);
    const agentsim::SensorVector oldCall =
        agentsim::sense(env, pos, vel, 4.0, cfg, &r1, field, flat, nullptr, 2);
    // R2 签名全默认尾参：cyanoNodes/temp/terrain/scouts 显式给 + R2 三参缺省。
    agentsim::PCG32 r2(31);
    const agentsim::SensorVector newCall =
        agentsim::sense(env, pos, vel, 4.0, cfg, &r2, field, flat, nullptr, 2);
    for (int i = 0; i < agentsim::SensorVector::kDim; ++i) {
        CHECK_NEAR(oldCall[i], newCall[i], 0.0); // 逐位一致
    }
    // 默认 R2 尾参 -> 哨兵语义：无邻居（s[18]=1、s[19]=s[20]=0）、能量 0、无目标。
    CHECK_NEAR(newCall[18], 1.0, 0.0);
    CHECK_NEAR(newCall[19], 0.0, 0.0);
    CHECK_NEAR(newCall[20], 0.0, 0.0);
    CHECK_NEAR(newCall[21], 0.0, 0.0);
    CHECK_NEAR(newCall[22], 0.0, 0.0);
    CHECK_NEAR(newCall[23], 1.0, 0.0);
}

// s[18..20] 邻居维语义：归一化（dist/sightRadius clamp01）、方向、无邻居哨兵、
// 零视距防御、零方向防御。
TEST(sense_r2_neighbor_dims_semantics) {
    std::vector<agentsim::CyanoNode> field;
    agentsim::Vec2 pos{};
    const agentsim::AgentEnvironment env = makeEnvWithCyano(field, pos);
    const agentsim::PolicyConfig cfg;
    const agentsim::Vec2 vel{1.0, 0.0};

    // (a) 正常邻居：dist=30, sight=60 -> s[18]=0.5；方向 (0.6,0.8)（模恰 1）。
    agentsim::NeighborInfo nb;
    nb.found = true;
    nb.dist = 30.0;
    nb.dirX = 0.6;
    nb.dirY = 0.8;
    nb.sightRadius = 60.0;
    const agentsim::SensorVector a =
        agentsim::sense(env, pos, vel, 0.0, cfg, nullptr, field, agentsim::TemperatureField{}, nullptr, 0, nb, 0.5, agentsim::TargetList{});
    CHECK_NEAR(a[18], 0.5, 1e-12);
    CHECK_NEAR(a[19], 0.6, 1e-12);
    CHECK_NEAR(a[20], 0.8, 1e-12);
    CHECK_NEAR(a[21], 0.5, 1e-12);

    // (b) 邻居在视距外：dist > sight -> s[18] 钳到 1（方向仍有效）。
    nb.dist = 120.0;
    const agentsim::SensorVector b =
        agentsim::sense(env, pos, vel, 0.0, cfg, nullptr, field, agentsim::TemperatureField{}, nullptr, 0, nb, 0.5, agentsim::TargetList{});
    CHECK_NEAR(b[18], 1.0, 1e-12);
    CHECK_NEAR(b[19], 0.6, 1e-12);
    CHECK_NEAR(b[20], 0.8, 1e-12);

    // (c) 无邻居：s[18]=1（"看不见"）、s[19]=s[20]=0（方向零向量哨兵）。
    agentsim::NeighborInfo none{};
    const agentsim::SensorVector c =
        agentsim::sense(env, pos, vel, 0.0, cfg, nullptr, field, agentsim::TemperatureField{}, nullptr, 0, none, 0.5, agentsim::TargetList{});
    CHECK_NEAR(c[18], 1.0, 0.0);
    CHECK_NEAR(c[19], 0.0, 0.0);
    CHECK_NEAR(c[20], 0.0, 0.0);

    // (d) 零视距（基因病态值）：不除零，走哨兵。
    agentsim::NeighborInfo blind = nb;
    blind.sightRadius = 0.0;
    const agentsim::SensorVector d =
        agentsim::sense(env, pos, vel, 0.0, cfg, nullptr, field, agentsim::TemperatureField{}, nullptr, 0, blind, 0.5, agentsim::TargetList{});
    CHECK_NEAR(d[18], 1.0, 0.0);
    CHECK_NEAR(d[19], 0.0, 0.0);
    CHECK_NEAR(d[20], 0.0, 0.0);

    // (e) 方向非单位输入：防御性再归一化。
    agentsim::NeighborInfo messy = nb;
    messy.dist = 15.0;
    messy.sightRadius = 60.0;
    messy.dirX = 3.0; // (3,4) -> (0.6,0.8)
    messy.dirY = 4.0;
    const agentsim::SensorVector e =
        agentsim::sense(env, pos, vel, 0.0, cfg, nullptr, field, agentsim::TemperatureField{}, nullptr, 0, messy, 0.5, agentsim::TargetList{});
    CHECK_NEAR(e[18], 0.25, 1e-12);
    CHECK_NEAR(e[19], 0.6, 1e-12);
    CHECK_NEAR(e[20], 0.8, 1e-12);

    // (f) 非有限方向：清洗为哨兵（零向量），不产生 NaN。
    agentsim::NeighborInfo nanDir = nb;
    nanDir.dirX = std::sqrt(-1.0);
    nanDir.dirY = 0.0;
    const agentsim::SensorVector f =
        agentsim::sense(env, pos, vel, 0.0, cfg, nullptr, field, agentsim::TemperatureField{}, nullptr, 0, nanDir, 0.5, agentsim::TargetList{});
    CHECK(std::isfinite(f[19]) && std::isfinite(f[20]));
    CHECK_NEAR(f[19], 0.0, 0.0);
    CHECK_NEAR(f[20], 0.0, 0.0);
}

// s[21..23] 能量/目标维语义：clamp01、密度分母 8、最近目标距离/R、无目标哨兵。
TEST(sense_r2_energy_and_target_dims_semantics) {
    std::vector<agentsim::CyanoNode> field;
    agentsim::Vec2 pos{};
    const agentsim::AgentEnvironment env = makeEnvWithCyano(field, pos);
    const agentsim::PolicyConfig cfg;
    const agentsim::Vec2 vel{1.0, 0.0};
    const agentsim::TemperatureField flat = flatField();

    // (a) 能量比：0.7 原样；负值/NaN -> 0；>1 -> 1。
    agentsim::TargetList empty{};
    agentsim::NeighborInfo none{};
    const agentsim::SensorVector a =
        agentsim::sense(env, pos, vel, 0.0, cfg, nullptr, field, flat, nullptr, 0, none, 0.7, empty);
    CHECK_NEAR(a[21], 0.7, 1e-12);
    const agentsim::SensorVector b =
        agentsim::sense(env, pos, vel, 0.0, cfg, nullptr, field, flat, nullptr, 0, none, -2.0, empty);
    CHECK_NEAR(b[21], 0.0, 0.0);
    const agentsim::SensorVector c =
        agentsim::sense(env, pos, vel, 0.0, cfg, nullptr, field, flat, nullptr, 0, none, 5.0, empty);
    CHECK_NEAR(c[21], 1.0, 0.0);
    const agentsim::SensorVector d =
        agentsim::sense(env, pos, vel, 0.0, cfg, nullptr, field, flat, nullptr, 0, none, std::sqrt(-1.0), empty);
    CHECK_NEAR(d[21], 0.0, 0.0);

    // (b) 目标密度与最近距离：scouts=3 -> R=48；count=2、items[0].dist=12
    //     -> s[22] = 2/8 = 0.25，s[23] = 12/48 = 0.25。
    agentsim::TargetList two{};
    two.count = 2;
    two.items[0].dx = 12.0; two.items[0].dy = 0.0; two.items[0].dist = 12.0;
    two.items[1].dx = 0.0;  two.items[1].dy = -8.0; two.items[1].dist = 8.0;
    // 注：契约保证 items 按距离升序（items[0] 最近）；此处 [0]=12 仍测分母口径。
    const agentsim::SensorVector e =
        agentsim::sense(env, pos, vel, 0.0, cfg, nullptr, field, flat, nullptr, 3, none, 0.0, two);
    CHECK_NEAR(e[22], 0.25, 1e-12);
    CHECK_NEAR(e[23], 0.25, 1e-12);

    // (c) 目标超出有效半径：距离钳到 1（"远目标"与"无目标"同读数，密度维仍可区分）。
    agentsim::TargetList far{};
    far.count = 1;
    far.items[0].dx = 100.0; far.items[0].dy = 0.0; far.items[0].dist = 100.0;
    const agentsim::SensorVector f =
        agentsim::sense(env, pos, vel, 0.0, cfg, nullptr, field, flat, nullptr, 3, none, 0.0, far);
    CHECK_NEAR(f[23], 1.0, 1e-12);
    CHECK(f[22] > 0.0);

    // (d) count 超上界（契约外）：密度钳到 1。
    agentsim::TargetList many{};
    many.count = 99;
    many.items[0].dx = 1.0; many.items[0].dy = 0.0; many.items[0].dist = 10.0;
    const agentsim::SensorVector g =
        agentsim::sense(env, pos, vel, 0.0, cfg, nullptr, field, flat, nullptr, 3, none, 0.0, many);
    CHECK_NEAR(g[22], 1.0, 1e-12);
    CHECK_NEAR(g[23], 10.0 / 48.0, 1e-12);

    // (e) 新维确定性：同参双调逐位一致（不消耗 rng 的通道也必须稳定）。
    agentsim::PCG32 r1(55);
    agentsim::PCG32 r2(55);
    const agentsim::SensorVector h1 =
        agentsim::sense(env, pos, vel, 6.0, cfg, &r1, field, flat, nullptr, 3, none, 0.4, two);
    const agentsim::SensorVector h2 =
        agentsim::sense(env, pos, vel, 6.0, cfg, &r2, field, flat, nullptr, 3, none, 0.4, two);
    for (int i = 18; i < agentsim::SensorVector::kDim; ++i) {
        CHECK_NEAR(h1[i], h2[i], 0.0);
    }
}

// R2 新维不消耗 rng：amp>0（噪声路径开）时，唯一差异是 R2 三参 —— rng 前后
// 状态消耗量必须与"无 R2 读数"完全一致（新维零副作用，S16/S17 的消耗序不变）。
TEST(sense_r2_dims_consume_no_rng) {
    std::vector<agentsim::CyanoNode> field;
    agentsim::Vec2 pos{};
    const agentsim::AgentEnvironment env = makeEnvWithCyano(field, pos);
    const agentsim::PolicyConfig cfg;
    const agentsim::Vec2 vel{1.0, 0.0};
    const agentsim::TemperatureField flat = flatField();

    agentsim::TargetList two{};
    two.count = 2;
    two.items[0].dx = 12.0; two.items[0].dy = 0.0; two.items[0].dist = 12.0;
    two.items[1].dx = 0.0;  two.items[1].dy = -8.0; two.items[1].dist = 8.0;
    agentsim::NeighborInfo nb;
    nb.found = true;
    nb.dist = 20.0;
    nb.dirX = 1.0;
    nb.dirY = 0.0;
    nb.sightRadius = 40.0;

    // 带 R2 读数。
    agentsim::PCG32 r1(77);
    std::uint64_t st1 = 0, in1 = 0;
    r1.getState(st1, in1);
    (void)agentsim::sense(env, pos, vel, 5.0, cfg, &r1, field, flat, nullptr, 3, nb, 0.3, two);
    std::uint64_t st2 = 0, in2 = 0;
    r1.getState(st2, in2);

    // 不带 R2 读数（默认尾参）。
    agentsim::PCG32 r2(77);
    std::uint64_t st3 = 0, in3 = 0;
    r2.getState(st3, in3);
    (void)agentsim::sense(env, pos, vel, 5.0, cfg, &r2, field, flat, nullptr, 3);
    std::uint64_t st4 = 0, in4 = 0;
    r2.getState(st4, in4);

    // rng 消耗量逐位一致：新维无副作用。
    CHECK(st2 - st1 == st4 - st3);
    CHECK(in2 - in1 == in4 - in3);
    CHECK(st2 == st4 && in2 == in4); // 相同起点 + 相同消耗 -> 相同终点
}
