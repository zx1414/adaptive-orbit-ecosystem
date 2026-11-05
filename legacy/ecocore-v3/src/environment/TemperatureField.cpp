// agentsim - environment/TemperatureField.cpp
// 环境层温度场实现：高斯源叠加 + Torus 环距采样。确定性仅依赖传入 rng 状态。
#include "TemperatureField.h"

#include <cmath>

namespace agentsim {

TemperatureField::TemperatureField(const TemperatureConfig& cfg, double worldW,
                                   double worldH, PCG32& rng) {
    generate(cfg, worldW, worldH, rng);
}

TemperatureField::TemperatureField(const SimConfig& config, PCG32& rng)
    : TemperatureField(config.temperature, config.world.width, config.world.height, rng) {}

TemperatureField::TemperatureField(const TemperatureConfig& cfg, PCG32& rng) {
    generate(cfg, rng);
}

void TemperatureField::setWorldSize(double w, double h) {
    worldW_ = w;
    worldH_ = h;
}

void TemperatureField::generate(const TemperatureConfig& cfg, PCG32& rng) {
    generate(cfg, worldW_, worldH_, rng);
}

void TemperatureField::generate(const TemperatureConfig& cfg, double worldW,
                                double worldH, PCG32& rng) {
    cfg_ = cfg;
    worldW_ = worldW;
    worldH_ = worldH;
    sources_ = cfg.sources; // 显式给源时直接采用（不消耗 rng）。
    if (!sources_.empty()) return;
    sources_.resize(cfg.sourceCount);
    for (std::size_t i = 0; i < sources_.size(); ++i) {
        TemperatureSource& s = sources_[i];
        s.x = rng.nextDouble() * (worldW > 0.0 ? worldW : 1.0);
        s.y = rng.nextDouble() * (worldH > 0.0 ? worldH : 1.0);
        s.amplitude = -0.4 + rng.nextDouble() * 0.8;           // [-0.4, 0.4)
        s.radius = 100.0 + rng.nextDouble() * 200.0;           // [100, 300)
        if (s.radius <= 0.0) s.radius = 1.0;                   // 数值容错
    }
}

double TemperatureField::clamp01(double v) {
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

double TemperatureField::contribution(const TemperatureSource& s, Vec2 pos) const {
    const Vec2 center{s.x, s.y}; // TemperatureSource -> Vec2（不隐式转换）
    const Vec2 d = torusDelta(center, pos, worldW_, worldH_); // 最小环距：跨边界连续
    const double r = s.radius > 0.0 ? s.radius : 1.0;
    const double q = (d.x * d.x + d.y * d.y) / (2.0 * r * r);
    return s.amplitude * std::exp(-q);
}

double TemperatureField::sample(Vec2 pos) const {
    double t = cfg_.baseline;
    for (const TemperatureSource& s : sources_) t += contribution(s, pos);
    return clamp01(t);
}

double TemperatureField::preferred() const { return cfg_.preferred; }

double TemperatureField::distortion(Vec2 pos) const {
    return clamp01(std::fabs(sample(pos) - cfg_.preferred) * cfg_.distortionK);
}

std::size_t TemperatureField::sourceCount() const { return sources_.size(); }

} // namespace agentsim
