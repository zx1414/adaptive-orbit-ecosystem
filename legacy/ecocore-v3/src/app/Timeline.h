#pragma once
// agentsim - app/Timeline.h（R5-C 时间轴：检查点 + 世界线分支）
// 旧版 v2.3.1 的核心器官之一：世界不再是"关掉就蒸发"的一次性流，而是
//   1) 可回看的（每 N 帧一个完整内存检查点，超上限自动抽稀、保首尾）；
//   2) 可分支的（世界线 branch：保留全部实体状态与 RNG 流，只换运行参数，
//      从当前帧开新局——"变动世界线"）。
// 确定性契约：检查点 payload = Simulation::serializeState 的全量输出（含 rng_
// 状态），seek 回放后 stateHash() 与当时逐位一致；branch 不触碰 sim（调用方
// 显式 sim.applyConfig(新参) + sim.restartClock()），实体状态与随机流原样保留。
// 检查点 hash 在**捕获时**由调用方世界算得并存档，seek 后用于损坏检测。
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/Config.h"

namespace agentsim {

class Simulation;

namespace app {

class Timeline {
public:
    // 单个检查点：frame = 捕获时的世界 tick；hash = 捕获时 sim.stateHash()；
    // payload = Simulation::serializeState 的原始字节（自包含、含 rng 状态）。
    struct Checkpoint {
        std::uint64_t frame = 0;
        std::uint64_t hash = 0;
        std::vector<unsigned char> payload;
    };

    // 内存上限：检查点条数硬顶（任务卡：2000），超出自动抽稀（保首尾）。
    static constexpr std::size_t kMaxCheckpoints = 2000;

    explicit Timeline(SimConfig cfg);

    // 每 checkpointInterval() 帧一个完整检查点（tick % interval == 0 时捕获；
    // interval 0 = 关闭）。捕获 = sim.serializeState 到内存（完整世界，含
    // rng），超上限时抽稀：保首尾、中间隔一去一（确定性策略，见实现注释）。
    void maybeCheckpoint(const Simulation& sim, std::uint64_t tick,
                         std::uint64_t hash);

    // 回看：恢复"帧号 <= frame 的最近检查点"到 sim（精确帧检查点直接命中；
    // 任意帧 = 回到最近的过去检查点，调用方继续 step 前进补齐）。没有可用
    // 检查点（frame 早于首个检查点 / 时间轴为空）返回 false。恢复后校验
    // sim.stateHash() == 捕获时 hash，不符 = payload 损坏，返回 false。
    bool seek(std::uint64_t frame, Simulation& sim, std::string& err);

    // 世界线分支：保留当前实体状态（本函数不触碰 sim），只换运行参数。
    // 语义选择（注释 = 契约）：**tick 归零**——分支是"从当前帧开新局"：
    //   - 实体状态与 rng_ 流：原样保留（调用方不动 sim，或只 applyConfig）；
    //   - 昼夜相位 / 统计节拍 / --ticks 预算：从 0 重算（sim.restartClock()），
    //     否则"新局"第一帧就带着旧世界的半截昼夜相位，与新开局直觉矛盾；
    //   - world.width/height/seed：**不可换**（世界几何与种子是确定性根，
    //     改了等于另一个世界——与 Simulation::applyConfig 的不热改清单同源），
    //     本函数强制保留旧值，newCfg 里写的几何/种子被忽略；
    //   - 其余全部采用 newCfg（阳光/policy/colony 护栏/温度/地形/运行面参数）；
    //   - 检查点序列清空：新世界线的过去不可用旧时间轴回看（分支点之前的
    //     历史属于旧世界线，payload 仍兼容旧 cfg 吗？不保证——清空最诚实）。
    //   - terrained 分支注意：newCfg 若改地形/温度 blob 计数，这些是构造期
    //     实体（terrain_/temp_），本分支通道不重建它们——分支只换"运行参数"，
    //     不是重开局（那是 --load 的职责）。
    void branch(const SimConfig& newCfg);

    const SimConfig& cfg() const { return cfg_; }

    // ---- 只读视图（测试/前端消费）----
    std::vector<Checkpoint>::const_iterator begin() const { return checkpoints_.begin(); }
    std::vector<Checkpoint>::const_iterator end() const { return checkpoints_.end(); }
    std::size_t size() const { return checkpoints_.size(); }
    // 最新检查点的帧号（0 = 尚无检查点；分支后重置为 0）。
    std::uint64_t frames() const {
        return checkpoints_.empty() ? 0 : checkpoints_.back().frame;
    }
    std::uint64_t checkpointInterval() const { return interval_; }
    void setCheckpointInterval(std::uint64_t n) { interval_ = n; }
    bool branched() const { return branched_; }

    // ---- timeline.bin 持久化（SaveStore 目录约定的第三件套）----
    // 布局：u32 magic "ATML"(0x41544D4C) + u32 version=1 + u64 interval +
    // u8 branched + u8[7] pad + cfg 块（SaveStore::savedetail 同源）+
    // u64 count + {u64 frame, u64 hash, u64 len, len×u8 payload}×count。
    bool exportTo(const std::string& path, std::string& err) const;
    bool importFrom(const std::string& path, std::string& err);

private:
    void thin();
    SimConfig cfg_{};
    std::vector<Checkpoint> checkpoints_;
    std::uint64_t interval_ = 200;   // 默认 200 帧（任务卡）
    bool branched_ = false;          // branch() 后为 true（观测/持久化用）
};

} // namespace app
} // namespace agentsim
