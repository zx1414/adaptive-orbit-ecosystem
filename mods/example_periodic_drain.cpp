#include "mod_api.h"
#include "World.h"

// 示例 mod：周期性瘟疫——每 100 帧对所有存活核扣 10 能量。
// 演示完整 mod 写法：订阅 FRAME_END 事件 + 通过 energy.apply 改能量。
// 默认不注册（见 src/mods_registry.cpp），启用后核会更早因能量耗尽而死亡。
void registerPeriodicDrain(ModAPI& api) {
    api.events.subscribe(EventType::FRAME_END, [&](const SimEvent& e) {
        if (e.frame > 0 && e.frame % 100 == 0) {
            for (auto& n : api.world.nuclei()) {
                if (n.alive) api.energy.apply(n, -10.0, EnergyReason::PLAGUE);
            }
        }
    });
}
