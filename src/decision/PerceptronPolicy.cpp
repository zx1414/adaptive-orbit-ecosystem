// agentsim - decision/PerceptronPolicy.cpp
// 感知机策略实现。纯线性代数 + sign，无 RNG、无隐藏魔法。
// decide 流程：
//   Z_i = sum_j w[i][j]*s[j] + b[i]（j 遍历全部 kDim=24 维，s[18..23] 全额参与）
//   y_i = (Z_i >= 0 ? +1 : -1)   （sign 边界：Z=0 -> +1）
//   dirX = clamp01((Z0+1)/2)*2-1, dirY 同理（线性映射回 [-1,1]）
//   boost = (Z0+Z1 > 0 ? 0.5 : 0.0)
//   最后把 (dirX,dirY) 归一化；长度为 0 时输出 {0,0}。
//
// R2 说明：
//   1) 权重数组开满 [2][SensorVector::kDim]，kDim_ 只是"已配置前缀"——旧序列化
//      文本（kDim=18）反序列化后 18..23 列保持 0，扩展维自动不参与，行为不变；
//      决策一律消费全部 kDim 维（kDim_ 之后的列恒 0，乘 0 等价不消费），
//      不存在"硬编码 18"的越界/漏算路径。
//   2) 不消费 targets：线性反射体没有点名施力的语义；forceSlot/mod 保持
//      ActionVector 默认值（-1 / 全 0），由测试钉死该契约。
#include "decision/PerceptronPolicy.h"

#include <cmath>
#include <cstdio>
#include <sstream>

#include "core/PCG32.h"

namespace agentsim {
namespace {
double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// Historical saves counted the last six inputs twice.
double extended(const SensorVector& s, const double (&w)[SensorVector::kDim]) {
    constexpr int kExtFirst = 18; // R2 扩展维起始下标（SensorTypes.h 契约）
    double z = 0.0;
    for (int j = kExtFirst; j < SensorVector::kDim; ++j) z += w[j] * s[j];
    return z;
}
} // namespace

PerceptronPolicy::PerceptronPolicy(const Params& p)
    : kDim_(p.kDim), kOutputs_(p.kOutputs) {
    if (kDim_ < 1) kDim_ = 1;
    if (kDim_ > SensorVector::kDim) kDim_ = SensorVector::kDim;
    if (kOutputs_ < 1) kOutputs_ = 1;
    if (kOutputs_ > 2) kOutputs_ = 2;
    for (int i = 0; i < kOutputs_; ++i) {
        b_[i] = p.b[i];
        for (int j = 0; j < kDim_; ++j) w_[i][j] = p.w[i][j];
    }
}

PerceptronPolicy::PerceptronPolicy(std::uint64_t seed, double scale, bool legacyTail)
    : kDim_(SensorVector::kDim), kOutputs_(2), legacyTail_(legacyTail) {
    // 仅构造期使用 PCG32 生成初值；decide() 内绝无 RNG。
    PCG32 rng(seed);
    for (int i = 0; i < kOutputs_; ++i) {
        b_[i] = (rng.nextDouble() * 2.0 - 1.0) * scale;
        for (int j = 0; j < kDim_; ++j) {
            w_[i][j] = (rng.nextDouble() * 2.0 - 1.0) * scale;
        }
    }
}

double PerceptronPolicy::Z(int i, const SensorVector& s) const {
    double z = b_[i];
    for (int j = 0; j < kDim_; ++j) z += w_[i][j] * s[j];
    return z;
}

ActionVector PerceptronPolicy::decide(const SensorVector& s, const TargetList& /*targets*/,
                                     PolicyMemory& /*memory*/) {
    // kOutputs_ 可能为 1：缺省神经元按 Z=0 -> y=+1 处理（偏置/权重未配置）。
    const double z0 = Z(0, s) + (legacyTail_ ? extended(s, w_[0]) : 0.0);
    const double z1 = (kOutputs_ > 1 ? Z(1, s) : 0.0) +
                      (legacyTail_ ? extended(s, w_[1]) : 0.0);

    const double dirX = clamp01((z0 + 1.0) / 2.0) * 2.0 - 1.0;
    const double dirY = clamp01((z1 + 1.0) / 2.0) * 2.0 - 1.0;
    const double boost = (z0 + z1 > 0.0) ? 0.5 : 0.0;

    ActionVector a;
    a.boost = boost;
    if (s.foodIds[0] != 0 && s.biteRadius > 0.0 &&
        s.foodDistance[0] <= 0.85 * s.biteRadius) a.speedScale = 0.0;
    const double len = std::sqrt(dirX * dirX + dirY * dirY);
    if (len < 1e-12) {
        a.dirX = 0.0; // 长度为 0：不产生 NaN/Inf
        a.dirY = 0.0;
    } else {
        a.dirX = dirX / len;
        a.dirY = dirY / len;
    }
    // forceSlot/forceX/forceY/mod 保持默认（-1 / 0 / 0）：反射体不施力、不调制。
    return a;
}

std::string PerceptronPolicy::serialize() const {
    // 文本格式（空格分隔，便于 diff 与后续存档）：
    //   perceptron <kOutputs> <kDim>\n
    //   b_i w_i0 w_i1 ... w_i<kDim-1>\n   （每个神经元一行）
    std::string out = "perceptron " + std::to_string(kOutputs_) + " " +
                      std::to_string(kDim_) + "\n";
    for (int i = 0; i < kOutputs_; ++i) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.17g", b_[i]);
        out += buf;
        for (int j = 0; j < kDim_; ++j) {
            std::snprintf(buf, sizeof(buf), " %.17g", w_[i][j]);
            out += buf;
        }
        out += "\n";
    }
    return out;
}

PerceptronPolicy PerceptronPolicy::deserialize(const std::string& text) {
    Params p;
    return PerceptronPolicy(readWeights(text, p) ? p : Params{});
}

bool PerceptronPolicy::readWeights(const std::string& text, Params& params) {
    Params p;
    std::istringstream in(text);
    std::string tag;
    int outs = 0, dim = 0;
    if (!(in >> tag >> outs >> dim) || tag != "perceptron" ||
        outs < 1 || outs > 2 || dim < 1 || dim > SensorVector::kDim) {
        return false;
    }
    p.kOutputs = outs;
    p.kDim = dim;
    for (int i = 0; i < p.kOutputs; ++i) {
        if (!(in >> p.b[i]) || !std::isfinite(p.b[i])) return false;
        for (int j = 0; j < p.kDim; ++j) {
            if (!(in >> p.w[i][j]) || !std::isfinite(p.w[i][j])) return false;
        }
    }
    in >> std::ws;
    if (!in.eof()) return false;
    params = p;
    return true;
}

} // namespace agentsim
