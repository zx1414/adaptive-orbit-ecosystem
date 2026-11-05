#include "TestHarness.h"
#include "decision/PolicyRegistry.h"
#include "decision/RulePolicy.h"
#include "core/Random.h"

TEST("rule policy is deterministic for identical observations") {
    decision::RulePolicy policy;
    decision::Observation observation;
    observation[0] = 0.9f;
    observation[8] = 0.1f;
    observation[9] = -0.05f;
    observation[10] = 0.2f;
    const decision::Action first = policy.decide(observation);
    const decision::Action second = policy.decide(observation);
    CHECK(first.kind == second.kind);
    CHECK_NEAR(first.dirX, second.dirX, 0.0);
    CHECK_NEAR(first.dirY, second.dirY, 0.0);
    CHECK_NEAR(first.magnitude, second.magnitude, 0.0);
}

TEST("rule policy rejects observations with an unknown version") {
    decision::RulePolicy policy;
    decision::Observation observation;
    observation.version = decision::kObservationVersion + 1;
    const decision::Action action = policy.decide(observation);
    CHECK(action.kind == decision::Action::Kind::Rest);
    CHECK_NEAR(action.magnitude, 0.0, 0.0);
}

TEST("policy clones preserve genome and mutate deterministically") {
    auto original = decision::PolicyRegistry::create("rule");
    CHECK(original != nullptr);
    auto copy = original->clone();
    CHECK(copy != nullptr);
    CHECK(original->genome() == copy->genome());
    core::Random a(11), b(11);
    copy->mutate(a, 0.1f);
    auto second = original->clone();
    second->mutate(b, 0.1f);
    CHECK(copy->genome() == second->genome());
    CHECK(copy->spec().id == "rule");
    CHECK(copy->spec().observationDim == decision::Observation::kDim);
}

TEST("stub policy is a fixed deterministic second implementation") {
    auto stub = decision::PolicyRegistry::create("stub");
    CHECK(stub != nullptr);
    decision::Observation observation;
    const decision::Action first = stub->decide(observation);
    const decision::Action second = stub->decide(observation);
    CHECK(first.kind == decision::Action::Kind::Move);
    CHECK_NEAR(first.dirX, second.dirX, 0.0);
    CHECK_NEAR(first.dirY, second.dirY, 0.0);
    CHECK(stub->genome().empty());
}
TEST("stub policy also rejects observations with an unknown version") {
    auto stub = decision::PolicyRegistry::create("stub");
    CHECK(stub != nullptr);
    decision::Observation observation;
    observation.version = decision::kObservationVersion + 1;
    const decision::Action action = stub->decide(observation);
    // A version mismatch must never be silently answered with a movement order.
    CHECK(action.kind == decision::Action::Kind::Rest);
    CHECK_NEAR(action.magnitude, 0.0, 0.0);
}

TEST("policy opaque state round-trips through the snapshot hooks") {
    auto policy = decision::PolicyRegistry::create("rule");
    CHECK(policy != nullptr);
    std::vector<unsigned char> state;
    policy->serializeState(state);
    CHECK(state.empty());  // RulePolicy is stateless in M4
    CHECK(policy->deserializeState(state.data(), state.size()));
    const unsigned char junk = 0;
    // The default hooks only accept an empty blob, so stray bytes must be rejected.
    CHECK(!policy->deserializeState(&junk, 1));
}
