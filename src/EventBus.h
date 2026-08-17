#pragma once
#include <functional>
#include <vector>

class Nucleus;
class Ball;

// 类型化事件：模拟核心在固定时机发射，mod 订阅后回调。
enum class EventType {
    FRAME_START,      // 帧开始（++frame 之后、物理更新之前）
    FRAME_END,        // 帧结束（step() 末尾）
    BALL_ABSORBED,    // 吸收（按帧聚合发射）
    NUCLEUS_BORN,     // 子核诞生
    NUCLEUS_DIED,     // 核死亡（EnergySystem::apply 内部发射）
    NUCLEUS_REPRODUCED,  // 本阶段定义，暂不发射（预留给后续 mod）
    PLAGUE_TICK       // 本阶段定义，暂不发射（预留给后续 mod）
};

// 一次事件：携带相关实体指针与数值。
// 指针仅在 emit 回调期间有效，mod 不得保存。
struct SimEvent {
    EventType type = EventType::FRAME_START;
    int frame = 0;
    const Nucleus* nucleus = nullptr;
    const Ball* ball = nullptr;
    double value = 0.0;   // 如吸收总量、扣减量
    const void* extra = nullptr;
};

// 事件总线：按类型分发。subscribe 按插入顺序追加，emit 按插入顺序遍历。
class EventBus {
public:
    void subscribe(EventType type, std::function<void(const SimEvent&)> fn) {
        listeners_[static_cast<int>(type)].push_back(std::move(fn));
    }

    void emit(const SimEvent& e) {
        auto& v = listeners_[static_cast<int>(e.type)];
        for (const auto& fn : v) fn(e);
    }

private:
    static constexpr int kTypeCount = 7;  // 与 EventType 枚举项数一致
    std::vector<std::function<void(const SimEvent&)>> listeners_[kTypeCount];
};
