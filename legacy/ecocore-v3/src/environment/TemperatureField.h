// agentsim - environment/TemperatureField.h
// 环境层温度场：只读状态提供者，不认识智能体（温度偏好失真机制的地基）。
// 场 = 基线温度 + 若干高斯热/冷源叠加；采样用 Torus 最小环距，跨边界连续。
#pragma once
#include <cstddef>
#include <vector>

#include "../core/Config.h"
#include "../core/PCG32.h"
#include "../core/Vec2.h"

namespace agentsim {

class TemperatureField {
public:
    TemperatureField() = default;

    // 按配置生成温度场。worldW/H 为环面尺寸（<=0 时 torusDelta 退化为直线距离）。
    TemperatureField(const TemperatureConfig& cfg, double worldW, double worldH, PCG32& rng);
    // 便捷构造：从 SimConfig 取温度配置与环面尺寸。
    explicit TemperatureField(const SimConfig& config, PCG32& rng);
    // 兼容签名：沿用 setWorldSize()/上次 generate() 存下的世界尺寸（未设置时源落原点）。
    explicit TemperatureField(const TemperatureConfig& cfg, PCG32& rng);

    // （重新）生成。cfg.sources 非空时直接采用（不消耗 rng）；否则用 rng 均匀撒
    // sourceCount 个源：位置均匀分布于世界，amplitude ∈ [-0.4, 0.4]，radius ∈ [100, 300)。
    void generate(const TemperatureConfig& cfg, PCG32& rng);
    void generate(const TemperatureConfig& cfg, double worldW, double worldH, PCG32& rng);

    void setWorldSize(double w, double h);

    // 采样某点温度，返回 [0,1]；跨边界连续（Torus 最小环距）。
    double sample(Vec2 pos) const;
    // 最适温度（物种偏好），初版全局常量。
    double preferred() const;
    // 该点温度偏离最适温度的归一化失真度 [0,1]，供感知层使用。
    double distortion(Vec2 pos) const;

    std::size_t sourceCount() const;
    const std::vector<TemperatureSource>& sources() const { return sources_; }
    double worldWidth() const { return worldW_; }
    double worldHeight() const { return worldH_; }

private:
    static double clamp01(double v);
    double contribution(const TemperatureSource& s, Vec2 pos) const;

    TemperatureConfig cfg_{};
    std::vector<TemperatureSource> sources_;
    double worldW_ = 0.0;
    double worldH_ = 0.0;
};

} // namespace agentsim
