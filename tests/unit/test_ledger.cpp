// agentsim - tests/unit/test_ledger.cpp
// S3 基础：账本注入/耗散/零和转移/审计通过与失败捕获；S10 相关断言预留。
#include "test_framework.h"

#include <cstdint>

#include "../../src/ledger/AgentLedger.h"

TEST(ledger_inject_and_dissipate_accounting) {
    agentsim::AgentLedger l;
    l.injectInitial(1, 100.0);
    l.injectPrimary(1, 50.0);
    l.injectPrimary(2, 30.0);
    CHECK_NEAR(l.totalInjected(), 180.0, 1e-12);
    l.dissipate("respiration", 12.5);
    l.dissipate("move", 7.5);
    l.dissipate("respiration", 5.0);
    CHECK_NEAR(l.totalDissipated(), 25.0, 1e-12);
    CHECK_NEAR(l.dissipatedBy("respiration"), 17.5, 1e-12);
    CHECK_NEAR(l.dissipatedBy("move"), 7.5, 1e-12);
    CHECK_NEAR(l.dissipatedBy("nonexistent"), 0.0, 1e-12);
    CHECK_NEAR(l.netInjected(), 155.0, 1e-12);
}

TEST(ledger_transfer_zero_sum) {
    agentsim::AgentLedger l;
    l.injectInitial(1, 100.0);
    l.injectInitial(2, 50.0);
    const double beforeIn = l.totalInjected();
    const double beforeDis = l.totalDissipated();
    l.transfer(1, 2, 30.0); // 纯搬运：不改总量
    CHECK_NEAR(l.totalInjected(), beforeIn, 1e-12);
    CHECK_NEAR(l.totalDissipated(), beforeDis, 1e-12);
    CHECK_NEAR(l.netInjected(), 150.0, 1e-12);
    CHECK_NEAR(l.totalTransferred(), 30.0, 1e-12);
    // 审计：1 号剩 70，2 号剩 80，总和 150 —— 恒等式成立。
    CHECK(l.audit(1e-9, 150.0));
}

TEST(ledger_audit_pass_within_tolerance) {
    agentsim::AgentLedger l;
    l.injectPrimary(7, 10.0);
    CHECK(l.audit(1e-9, 10.0));
    CHECK(l.audit(2.0, 8.5)); // 容差内仍通过
    CHECK(!l.audit(0.5, 8.5)); // 超容差即失败
}

TEST(ledger_audit_failure_is_detected) {
    agentsim::AgentLedger l;
    l.injectInitial(1, 100.0);
    l.dissipate("respiration", 40.0);
    // 正确记账：sumLiving == 60，audit 通过。
    CHECK(l.audit(1e-9, 60.0));
    // 漏记/贪墨场景：sumLiving=70 偏离 10，audit 必须失败可捕获。
    CHECK(!l.audit(1e-9, 70.0));
    CHECK(!l.audit(5.0, 70.0)); // 容差 5 也兜不住偏差 10
    CHECK(l.audit(10.0 + 1e-9, 70.0)); // 容差足够大才放过
}

TEST(ledger_reset_clears_state) {
    agentsim::AgentLedger l;
    l.injectPrimary(1, 10.0);
    l.dissipate("move", 3.0);
    l.transfer(1, 1, 2.0);
    l.reset();
    CHECK_NEAR(l.totalInjected(), 0.0, 1e-12);
    CHECK_NEAR(l.totalDissipated(), 0.0, 1e-12);
    CHECK_NEAR(l.totalTransferred(), 0.0, 1e-12);
    CHECK_NEAR(l.dissipatedBy("move"), 0.0, 1e-12);
    CHECK(l.audit(1e-9, 0.0));
}
