// agentsim - decision/PerceptronPolicy.h
// 感知机策略（GOAL 后续项）：规格 Z = W^T X + b，y_hat = sign(Z)。
// 两个输出神经元：Z0 -> dirX、Z1 -> dirY 的二分类倾向。
//
// R2 生态位假设：一个"无模型反射体"——它的生存完全由进化押注在权重上：
//   初始权重随机（由上游变异/选择决定存亡），食物维、温度维、邻居维、能量维
//   谁被加权完全交给自然选择筛选。它不解释自己的行为，也没有任何主动的社会
//   策略；在食物规律分布的稳定环境里可能演化出超强反射，在剧变环境里大概率
//   第一个死。它不点名施力、不做 mod 调制——线性反射没有"意图"可言，但
//   扩展维（s[18..23]）全部参与线性组合：邻居/能量/目标密度会真实改变输出。
// 红线：决策期绝不使用 RNG，不 include environment/ sim/ ledger/。
#pragma once
#include <cstdint>
#include <string>

#include "decision/IDecisionPolicy.h"

namespace agentsim {

class PerceptronPolicy : public IDecisionPolicy {
public:
    struct Params {
        // 输入维跟随 SensorVector::kDim（R2 契约：18 -> 24）。**不要硬编码 18**：
        // 曾经写成字面 18 而 kDim 提升到 24 时，构造器把 kDim_ 钳到 24 却只开了
        // w[2][18]，PerceptronPolicy(seed,scale) 的写入循环直接越界（栈溢出
        // 0xC0000409）。用 kDim 表达式声明数组大小，维度改动即自动跟随。
        int kDim = SensorVector::kDim;
        int kOutputs = 2;          // 两个输出神经元：dirX / dirY 的二分类倾向
        double w[2][SensorVector::kDim] = {};  // 权重（开满 kOutputs x kDim）
        double b[2] = {};          // 偏置
    };

    explicit PerceptronPolicy(const Params& p);

    // 用 PCG32 生成随机初值（仅构造期，不算决策期用 RNG）。
    explicit PerceptronPolicy(std::uint64_t seed, double scale);

    // 内部绝不用 RNG：Z = sum_j w[i][j]*s[j] + b[i]（j 遍历全部 kDim 维，
    // 含 R2 新增的 s[18..23]），y = sign(Z)。
    ActionVector decide(const SensorVector& sensors, const TargetList& targets) override;
    using IDecisionPolicy::decide; // 旧单参调用面（基类非虚重载）

    const char* name() const override { return "perceptron"; }

    // 权重导出/导入，供可复现实验与后续存档。格式：
    // "perceptron <kOutputs> <kDim>\n" + 每个神经元一行 "b w0 w1 ... w<kDim-1>"。
    std::string serialize() const;
    static PerceptronPolicy deserialize(const std::string& text);

private:
    double Z(int i, const SensorVector& s) const; // Z = sum_j w[i][j]*s[j] + b[i]

    int kDim_ = SensorVector::kDim;
    int kOutputs_ = 2;
    double w_[2][SensorVector::kDim] = {};
    double b_[2] = {};
};

} // namespace agentsim
