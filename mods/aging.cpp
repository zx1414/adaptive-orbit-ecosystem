// ============================================================
// mod id:        aging
// 名称:          衰老
// 机制:          每个存活核每帧流失 kAgingBase(0.00001) × min(核龄,
//                kAgingCapAge(3000)) 能量（线性衰老：核龄越大流失越快，
//                3000 帧封顶，避免老核被无限盘剥）。核龄直接读世界字段
//                Nucleus::age（核心每帧 ++age 维护），无随机、无跨帧状态；
//                扣能唯一经 EnergySystem::apply（EnergyReason::AGING）。
// 平衡作用:      基线核没有寿命概念，只要策略稳态就能无限存活，种群容易
//                锁死在单一最优解上。本 mod 引入与年龄成正比的持续压力：
//                高龄核（流失大）逐渐让位，新生核（流失小 + 出生保护期）
//                有更多接管窗口，把「寿命/世代更替速度」变成被选择的性状。
// touches:       AGING
// incompatible:
// 依赖:          无
// ============================================================
//
// 【非零和说明】本 mod 是单向能量汇：扣走的能量不转移给任何核，等同于
// 一笔随年龄增长的额外代谢。AGING 科目只有 out 没有 in，会降低系统总能
// 量——与失球宽限（TERRITORY，从虚空返还）方向相反。这是设计意图：
// 衰老在自然界就是不可逆损耗，不是核间搬运。
//
// 【封顶设计与取值标定】流失 = kAgingBase × min(age, kAgingCapAge)，线性爬升到
// 3000 帧后恒定为 kAgingBase × 3000 = 0.03 能量/帧（≈3×基础代谢）。若不封顶，长寿核的
// 流失随年龄无界增长，任何核都活不过理论寿命上限，年龄选择压退化为
// 「人人都早死」的纯噪声；封顶后压力有上界，与基础代谢 basalCost(0.01)、
// 繁殖代价同一套能量科目体系可比。
//
// 【事件时机】订阅 FRAME_END：此时本帧的吸收/争斗/代谢/繁殖/失球结算与
// 死亡移除均已完成，衰老扣费不与核心结算交错，账目归属清晰。FRAME_END
// 时 nuclei() 已无死亡残留，遍历即「每个存活核」；仍按 n.alive 防御性
// 跳过（apply 对死核是 no-op，跳过只是省一次调用）。
//
// 【状态保存】零跨帧状态：核龄即世界状态字段 Nucleus::age，本 mod 不持有
// 任何 map/游标/快照，因此不需要（也不提供）saveAging / loadAging；
// 读档后 age 随世界状态一并恢复，行为天然一致。

#include "mod_api.h"
#include "World.h"

#include <algorithm>
#include <vector>

namespace {

// ---- 可调常数 ----
constexpr double kAgingBase   = 0.00001; // 每帧单位核龄流失（能量/帧/龄）
constexpr int    kAgingCapAge = 3000;   // 封顶核龄：流失按 min(age, 本值) 计

}  // namespace

void registerAging(ModAPI& api) {
    // 函数内无需 static：本 mod 无任何跨帧状态，注册函数本身无副作用。
    api.events.subscribe(EventType::FRAME_END, [&api](const SimEvent&) {
        std::vector<Nucleus>& nuclei = api.world.nuclei();
        for (Nucleus& n : nuclei) {
            if (!n.alive) continue;  // 防御：FRAME_END 时理论已无死核
            const int effectiveAge = std::min(n.age, kAgingCapAge);
            const double drain = kAgingBase * static_cast<double>(effectiveAge);
            api.energy.apply(n, -drain, EnergyReason::AGING);
        }
    });
}

// 零跨帧状态：不提供 saveAging / loadAging（理由见文件头【状态保存】）。
//
// 【预期可观测效果】单独启用后跑 --energy-summary：
//   * AGING: out > 0 且随模拟时长增长（in 恒为 0）；
//   * out 量级 ≈ Σ帧 Σ存活核 kAgingBase×min(age,3000)；
//   * 存活核数相对基线下降、高龄核被加速淘汰（恶化不属异常，见登记表）。
