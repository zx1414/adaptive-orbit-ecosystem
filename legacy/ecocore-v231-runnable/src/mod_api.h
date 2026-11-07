#pragma once
#include <random>
#include "EnergySystem.h"
#include "EventBus.h"

class World;

// Mod 与模拟核心的唯一接口。
// 约定：
//   - world  只读状态（要改能量必须走 energy.apply）；
//   - energy 写能量的唯一通道；
//   - rng    确定性随机（同种子可复现；mod 内禁止使用未种子随机）；
//   - events 事件订阅（回调内指针只在 emit 期间有效）。
struct ModAPI {
    World& world;
    EnergySystem& energy;
    std::mt19937& rng;
    EventBus& events;
};

// 生命周期约定：ModAPI 必须比 World 存活更久——mod 订阅回调通常按引用捕获
// ModAPI，在模拟期间（包括最后几帧）都可能被触发；请把它放在比 World 更外层的作用域。
using ModFactory = void (*)(ModAPI&);
