#include "mod_api.h"
#include "World.h"

// 示例 mod：瘟疫打击——每 50 帧对能量超过 100 的存活核扣 8 能量。
// 与 periodic_drain 的 touches 同为 PLAGUE（演示冲突提示与优先级效果）。
void registerPlagueStrike(ModAPI& api) {
    api.events.subscribe(EventType::FRAME_END, [&](const SimEvent& e) {
        if (e.frame > 0 && e.frame % 50 == 0) {
            for (auto& n : api.world.nuclei()) {
                if (n.alive && n.energy > 100.0) {
                    api.energy.apply(n, -8.0, EnergyReason::PLAGUE);
                }
            }
        }
    });
}
