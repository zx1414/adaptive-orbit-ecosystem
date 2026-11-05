// agentsim - ledger/AgentLedger.h
// 能量账本：一切能量变化唯一入口。零和恒等式（S3）：
//   injected - dissipated == sum(所有活实体能量)
// transfer 只在实体间搬运能量，不改总量。
#pragma once
#include <cstdint>
#include <map>
#include <string>

namespace agentsim {

class AgentLedger {
public:
    // 注入初始能量（实体诞生）。
    void injectInitial(std::uint64_t entityId, double amount);
    // 注入一级能量（食物转化等）。
    void injectPrimary(std::uint64_t entityId, double amount);
    // 耗散能量（reason: "respiration"/"move"/"spawnBall"/"reproduce"/"recycleBall"...）。
    void dissipate(const std::string& reason, double amount);
    // 实体间转移：from/to 为实体 id（from 侧扣减记为转移流出）。零和，不改总注入/总耗散。
    void transfer(std::uint64_t from, std::uint64_t to, double amount);

    double totalInjected() const { return injected_; }
    double totalDissipated() const { return dissipated_; }
    double totalTransferred() const { return transferred_; }
    double netInjected() const { return injected_ - dissipated_; }

    // 按原因查询耗散量。
    double dissipatedBy(const std::string& reason) const;
    // R5-C：按原因耗散表的只读视图（存档序列化 ledger 段用）。
    const std::map<std::string, double>& dissipatedByReason() const { return dissipatedByReason_; }
    // R5-C：原子恢复器（读档时一次性恢复三个计数器与按原因表）。
    void injectLedgerSnapshot(double injected, double dissipated, double transferred,
                              const std::map<std::string, double>& byReason) {
        injected_ = injected; dissipated_ = dissipated; transferred_ = transferred;
        dissipatedByReason_ = byReason;
    }

    // 审计：|netInjected - sumLiving| <= tolerance 则通过。
    // sumLiving 由调用方传入（行为层/主循环每 tick 汇总实体能量）。
    bool audit(double tolerance, double sumLiving) const;

    void reset();

private:
    double injected_ = 0.0;
    double dissipated_ = 0.0;
    double transferred_ = 0.0;
    std::map<std::string, double> dissipatedByReason_;
};

} // namespace agentsim
