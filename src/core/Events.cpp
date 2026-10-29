#include "core/Events.h"

namespace core {

EventBus::ListenerId EventBus::subscribe(EventType type, Listener listener) {
    if (!listener) return 0;
    const ListenerId id = nextId_++;
    subscriptions_.push_back({id, type, false, std::move(listener)});
    return id;
}
EventBus::ListenerId EventBus::subscribe(Listener listener) {
    if (!listener) return 0;
    const ListenerId id = nextId_++;
    subscriptions_.push_back({id, EventType::Custom, true, std::move(listener)});
    return id;
}
bool EventBus::unsubscribe(ListenerId id) {
    for (auto it = subscriptions_.begin(); it != subscriptions_.end(); ++it) {
        if (it->id == id) { subscriptions_.erase(it); return true; }
    }
    return false;
}
void EventBus::emit(const Event& event) const {
    // Snapshot so listeners may subscribe or unsubscribe during dispatch without
    // invalidating the iteration.
    const std::vector<Subscription> snapshot = subscriptions_;
    for (const Subscription& subscription : snapshot) {
        if (subscription.allTypes || subscription.type == event.type) {
            subscription.listener(event);
        }
    }
}
void EventBus::clear() { subscriptions_.clear(); }

} // namespace core
