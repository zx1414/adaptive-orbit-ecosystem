#pragma once
#include "core/Config.h"
#include "core/Random.h"
#include "core/Types.h"
#include "world/Energy.h"
#include "world/Environment.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace producers {
class Producer;

struct ProducerContext {
    const world::Environment* environment = nullptr;
    world::EnergyLedger* ledger = nullptr;
    core::Random* random = nullptr;
    core::Tick tick = 0;
    std::size_t population = 0;
    std::size_t maxPopulation = 0;
    std::size_t localDensity = 0;
    std::vector<std::unique_ptr<Producer>>* newborns = nullptr;
};

enum class ProducerKind : std::uint8_t { Photo = 0 };

class Producer {
public:
    virtual ~Producer() = default;
    virtual ProducerKind kind() const = 0;
    virtual void tick(ProducerContext& context) = 0;
    virtual std::unique_ptr<Producer> clone() const = 0;
    virtual double harvestable() const { return energy.energy; }

    core::EntityId id = core::kInvalidEntityId;
    core::Vec2 pos{};
    core::EnergyState energy{};
    bool alive() const { return energy.alive; }
};

class PhotoProducer final : public Producer {
public:
    explicit PhotoProducer(core::ProducerConfig config, core::EnergyState state);
    ProducerKind kind() const override { return ProducerKind::Photo; }
    void tick(ProducerContext& context) override;
    std::unique_ptr<Producer> clone() const override;
private:
    core::ProducerConfig config_;
};
} // namespace producers
