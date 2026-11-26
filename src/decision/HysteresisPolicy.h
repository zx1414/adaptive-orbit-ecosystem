// agentsim - decision/HysteresisPolicy.h
// 滞回选择策略：消除两个分数接近的食物候选之间的每帧抖动。
// 动机：RulePolicy 每帧贪心 argmax，候选 A/B 距离接近时会来回翻转，运动方向
// 抖动、白耗能量。本策略记住上一帧的选择（"现任"），只有当最佳挑战者的分数
// 超过现任分数 + hysteresisGap 时才切换 —— 在分数轴上形成宽度为 gap 的滞回带，
// 小扰动不再引发方向翻转。若现任候选已消失则强制重选当帧最高分。
// 输出方向 = 选中候选的方向 (s[2i], s[2i+1])（感知层已归一化，输出即单位向量）；
// 无任何候选时输出零向量、boost=0。纯确定性算术，绝不使用 RNG（决策层红线）；
// 绝不 include environment/ sim/ ledger/，只消费 core/SensorTypes.h 感知向量。
//
// R2 生态位假设：一个"不折腾的跟屁虫"——它生存的核心不是找得准，而是走得稳：
// 紧贴既有目标、绝不为小波动改道，把转向能耗压到最低。食物稀疏且分布缓慢变化
// 的环境里它的净积累最高；食物闪现闪灭或竞争激烈需要抢时间窗时，它的迟疑就是
// 死亡。R2 通道用法：用 mod[1]（个人空间）实现"聚焦调制"——目标越近越值得
// 专注，缩小个人空间方便贴身取食；目标越远/没有目标时恢复（放大个人空间、
// 顺路让邻居保持距离）。强度可配，默认 0 = 关闭（回归安全）。不点名施力。
//
// P0-4（记忆归个体）：滞回"现任"下标 `hysteresisLastChoice`住在 PolicyMemory 里，
// 每个核各自持有一份；本类实例只保留不可变参数 p_。
#pragma once
#include "decision/IDecisionPolicy.h"
#include "decision/PolicyMemory.h"

namespace agentsim {

class HysteresisPolicy : public IDecisionPolicy {
public:
    struct Params {
        double hysteresisGap = 0.08; // 切换门槛：挑战者须比现任好这么多才换
        double wProximity = 1.0;     // 接近度权重 s[6+i]：越近分越高
        double wAlignment = 0.8;     // 对齐权重：候选方向·当前航向，顺路加分
        // R2：聚焦调制强度（>0 时 mod[1] 生效）。聚焦 = 目标越近，个人空间越小
        // （贴身取食），没有目标时输出反向（放宽空间）。默认 0 = 关闭。
        double kModFocus = 0.0;
    };
    explicit HysteresisPolicy(const Params& p = {});

    // 纯确定性；非有限输入一律清洗为 0，全 NaN 输入也不产生 NaN 输出。
    // memory 是本个体的滞回历史（就地更新），默认值 = 尚未选择（-1）。
    ActionVector decide(const SensorVector& s, const TargetList& targets,
                        PolicyMemory& memory) override;
    using IDecisionPolicy::decide; // 基类两参/单参兼容重载（一次性无状态语义）
    const char* name() const override { return "hysteresis"; }

    // ---- 可解释性接口（供单测直接验证打分与滞回状态）----
    // 候选 i 得分 = wProximity*s[6+i] + wAlignment*alignment_i；
    // alignment_i = dot(候选方向 i, 当前航向单位向量)，速度近零取 0。
    double candidateScore(const SensorVector& s, int i) const;
    // 该个体上一帧选中的候选下标；-1 = 尚未选择。静态 + 收记忆参数（状态已不在实例里）。
    static int lastChoice(const PolicyMemory& memory) { return memory.hysteresisLastChoice; }
    // 清空本策略的滞回记忆（单测确定性验证用）；只动自己的字段。
    static void reset(PolicyMemory& memory) { memory.hysteresisLastChoice = -1; }

private:
    Params p_; // 构造期标定，不可变
};

} // namespace agentsim
