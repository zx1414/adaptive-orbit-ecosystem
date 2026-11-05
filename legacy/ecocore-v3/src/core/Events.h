#pragma once

#include "core/Types.h"

#include <cstdint>
#include <functional>
#include <vector>

namespace core {

enum class EventType : std::uint8_t {
    TickStarted,
    TickEnded,
    EntityBorn,
    EntityDied,
    EnergyChanged,
    Harvested,
    Reproduced,
    Custom
};

struct Event {
    EventType type = EventType::Custom;
    Tick tick = 0;
    EntityId source = kInvalidEntityId;
    EntityId target = kInvalidEntityId;
    EntityKind kind = EntityKind::Agent;
    Vec2 position{};
    double value = 0.0;
    std::uint64_t tag = 0;
};
using ValueEvent = Event;

class EventBus {
public:
    using ListenerId = std::uint64_t;
    using Listener = std::function<void(const Event&)>;

    ListenerId subscribe(EventType type, Listener listener);
    ListenerId subscribe(Listener listener);
    bool unsubscribe(ListenerId id);
    void emit(const Event& event) const;
    void publish(const Event& event) const { emit(event); }
    void clear();
    std::size_t size() const { return subscriptions_.size(); }

private:
    struct Subscription {
        ListenerId id;
        EventType type;
        bool allTypes;
        Listener listener;
    };
    ListenerId nextId_ = 1;
    std::vector<Subscription> subscriptions_;
};

} // namespace core
