// ============================================================
// mod id:        cyanobacteria
// 文件:          mods/cyano_producer.cpp
//                ⚠️ 文件名不可叫 cyanobacteria.cpp —— Windows 文件名不区分大小写，
//                会与 src/Cyanobacteria.cpp 争用同一个 Cyanobacteria.obj，
//                导致后者的实现被覆盖、全项目符号链接失败（LNK2019 一片）。
// 名称:          蓝细菌生产者
// 机制:          自养生产者群落——光合作用（昼夜 + 垂直梯度 + 局部扰动）产能，
//                代谢消耗，能量枯竭则死亡移除；节点按距离自动建立双向连接网络
//                （每节点有上限），能量沿连接由高向低流动；概率性产出自由小球、
//                概率性繁殖出新节点（过近则取消/合并），形成聚落式扩张。
//                核可捕食节点：获得能量 = 节点能量 × 转化率 × 捕食偏好，
//                转化率由核的 maxSpeed / maxAccel 决定（机动性↑ ⇒ 消化效率↓ 的取舍）。
// touches:       CYANO_GAIN
// incompatible:
// 依赖:          env_config 的 cyano_nodes > 0（本 mod 只接管"驱动"，
//                群落的构造期初始化与配置解析仍由 World 负责）
// ============================================================
//
// 【定位说明】本 mod **只接管每帧驱动**，不拥有群落状态：
//   - 群落本体是 World 的私有成员 Cyanobacteria，构造期按 cyano_nodes 初始化并撒点；
//   - 快照 v2 / 采样 / 控制台渲染 都通过 World::cyano() 只读访问，因此本 mod
//     启用与否不改变任何对外数据格式；
//   - 注册时调用 World::setCyanoExternalDriver(true)，此后 World::updateCyanobacteria()
//     主动让位，保证同一帧**不会被驱动两次**。
//   - 产球必须走 World::addFreeBall()（prevOwner_ 是与 balls_ 并行的私有数组，
//     mod 直接 push_back 会导致两数组错位）。
//
// 【驱动时机】订阅 CYANO_TICK（不是 FRAME_START）——World 在
//   updateCyanobacteria() 的原始调用点发射它，即 ++age 之后、buildGrid() 之前，
//   且 e.frame == frame_（未自增）。这与内部驱动的时序、帧号完全一致，
//   因此 mod 驱动与内部驱动逐帧等价。
//   用 FRAME_START 是错的：它晚于 buildGrid() 且帧号已 +1，会改变 rng 消耗顺序。
//
// 【确定性红线】只使用 api.rng（与核心共享的 std::mt19937）。
// 禁止 std::rand / random_device / 自建带种子引擎——那会破坏同种子可复现性。
// 核捕食（CYANO_GAIN）保留在 World::absorbBalls() 内部执行，本 mod 不重复处理，
// 以免 frameAbsorbEnergy_ / frameCyanoCount_ 统计被重复累加。
//
// 【状态】无跨帧自有状态（群落状态活在 World 里），因此不提供 save/load 钩子。

#include "mod_api.h"
#include "World.h"
#include "Cyanobacteria.h"

#include <vector>

namespace {

// 产球缓冲复用：避免每帧堆分配（热路径）。
// thread_local 与 World::updateCyanobacteria() 原实现保持一致。
std::vector<CyanoBall>& spawnBuffer() {
    static thread_local std::vector<CyanoBall> buf;
    return buf;
}

}  // anonymous namespace

void registerCyanobacteria(ModAPI& api) {
    // 未启用（cyano_nodes<=0）时群落恒空，直接让 World 走原来的零开销短路路径。
    if (!api.world.cyanoEnabled()) return;

    // 接管驱动：World::updateCyanobacteria() 此后让位，防止双重驱动。
    api.world.setCyanoExternalDriver(true);

    // 必须用 CYANO_TICK，不能用 FRAME_START：
    // FRAME_START 在 ++frame 之后且晚于 buildGrid()，帧号比内部驱动点大 1，
    // 且当帧产出的球不入网格 —— 会导致 rng 顺序改变、生态行为发散（实测全核死光）。
    api.events.subscribe(EventType::CYANO_TICK, [&](const SimEvent& e) {
        World& w = api.world;
        if (w.finished()) return;
        if (!w.cyanoEnabled()) return;

        Cyanobacteria& cy = w.cyanoMutable();
        if (cy.empty()) return;

        std::vector<CyanoBall>& spawned = spawnBuffer();
        spawned.clear();
        // 与 World::updateCyanobacteria() 原调用完全一致的参数与顺序。
        cy.step(api.rng, e.frame, w.sunParams(), spawned);

        // 产出的自由小球入库：唯一合法通道（同步 prevOwner_）。
        for (size_t i = 0; i < spawned.size(); ++i) {
            w.addFreeBall(spawned[i].pos, spawned[i].vel, spawned[i].type);
        }
    });
}
