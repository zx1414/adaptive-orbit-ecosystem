# Mod 开发指南（v1.1.0 · 面向人类与 AI 协作）

本指南写给**人**和**AI 编程助手**：所有接口签名、文件路径、规则都以可复制执行的形式给出。
写一个 mod = 新建 1 个 .cpp 文件 + 在 2 个地方登记。全程不需要修改模拟核心。

---

## 0. 一句话概览

- 模拟核心（`src/World.*`）**不可修改**；mod 只通过 **ModAPI** 四个接口与核心交互。
- mod 是 C++17、零第三方依赖、编译进 `simulator.exe`；通过 `mods.list` 开关与排序。
- **确定性铁律**：mod 内所有随机一律用 `api.rng`；不注册任何 mod 时，输出与无 mod 版本逐字节一致。

## 1. ModAPI（mod 能做什么）

定义在 `src/mod_api.h`：

```cpp
struct ModAPI {
    World&        world;   // 只读状态（约定：要改能量必须走 energy.apply）
    EnergySystem& energy;  // 写能量的唯一通道
    std::mt19937& rng;     // 种子驱动的确定性随机（禁止 rand/random_device）
    EventBus&     events;  // 事件订阅
};
using ModFactory = void (*)(ModAPI&);   // mod 入口函数签名
```

**生命周期约定**：`ModAPI` 必须比 `World` 存活更久——订阅回调按引用捕获它。
启动器（`LauncherApp`）用成员 `modApi_` 持有并保证顺序；你自己写测试程序时也要遵守。

常用只读访问器（`src/World.h`）：

```cpp
const std::vector<Ball>&    balls() const;     // pos/vel/type/ownerId
const std::vector<Nucleus>& nuclei() const;    // pos/vel/energy/alive/id/age/params(22 遗传参数)
std::vector<Nucleus>&       nuclei();          // 仅用于交给 energy.apply；不得改其它字段
int frame() const;  bool finished() const;  const WorldConfig& config() const;
```

## 2. 事件（EventBus）

`src/EventBus.h`，订阅后**按注册顺序**回调（顺序 = mods.list 顺序 = 优先级）。

| 事件 | 时机 | SimEvent 载荷 |
|------|------|---------------|
| `FRAME_START` | 每帧 `++frame` 之后 | `frame` |
| `FRAME_END` | 每帧 step 末尾 | `frame` |
| `BALL_ABSORBED` | 每帧吸收结束后聚合一次 | `value`=本帧吸收总能量；`extra`=指向本帧吸收球数(int)的指针 |
| `NUCLEUS_BORN` | 子核 push_back 后 | `nucleus`=子核 |
| `NUCLEUS_DIED` | `EnergySystem::apply` 内 | `nucleus`=死亡核 |
| `NUCLEUS_REPRODUCED` | （已定义，暂未发射） | — |
| `PLAGUE_TICK` | （已定义，暂未发射） | — |

`SimEvent` 中的实体指针**只在回调期间有效**，不要保存。

## 3. 能量（EnergySystem）

`src/EnergySystem.h`。**所有能量变化必须走 `apply`**（它会统一判死并发射 NUCLEUS_DIED）：

```cpp
double apply(Nucleus& n, double delta, EnergyReason why);
```

`EnergyReason`：`ABSORB, COMBAT, METABOLISM, REPRODUCTION, AGING, CROWDING, PLAGUE, BALL_LOSS`
（后四个是给 mod 预留/使用的；`AGING/CROWDING` 尚未被核心使用）。
调试：结束加 `--energy-summary` 可打印按原因汇总的收支。

## 4. 最小完整示例（可直接复制）

新建 `mods/my_mod.cpp`：

```cpp
#include "mod_api.h"
#include "World.h"

// 我的 mod：每 200 帧对所有存活核扣 3 能量。
void registerMyMod(ModAPI& api) {
    api.events.subscribe(EventType::FRAME_END, [&](const SimEvent& e) {
        if (e.frame > 0 && e.frame % 200 == 0) {
            for (auto& n : api.world.nuclei()) {
                if (n.alive) api.energy.apply(n, -3.0, EnergyReason::PLAGUE);
            }
        }
    });
}
```

登记（两处）：

1. `src/mods_registry.cpp` 的 `allMods()` 数组加一行（**id 必须唯一且稳定**）：

```cpp
{"my_mod", "我的 mod", "每 200 帧对所有存活核扣 3 能量（描述写清楚）",
 "PLAGUE", "", registerMyMod},
```

字段含义：`{id, 名称, 描述, touches, incompatible, 注册函数, saveState, loadState}`

- `touches`：逗号分隔的"影响项"（EnergyReason 名或配置键名），用于**冲突提示**——两个启用的 mod 影响同一项时启动器给出黄条警告。
- `incompatible`：逗号分隔的互斥 mod id，命中则红条报错提示。

2. `src/mods_registry.cpp` 顶部加声明：

```cpp
void registerMyMod(ModAPI&);
```

3. `CMakeLists.txt` 的 `mods/` 文件列表加一行（g++ 命令行编译用 `mods/*.cpp` 则自动包含）。

构建（Windows）：

```bash
g++ -std=c++17 -O2 -Wall -Wextra -Isrc -o simulator.exe src/*.cpp mods/*.cpp -lws2_32 -lshell32
```

启用/排序：启动器 Mod 页勾选并保存，或直接写 exe 旁的 `mods.list`（每行一个 id，顺序 = 优先级）。下一次启动模拟生效。

## 5. 进阶：带内部状态的 mod（存档钩子）

`ModInfo` 的 `saveState(std::ostream&)` / `loadState(std::istream&)` 为可选钩子（默认为空 = 无状态）。
在 `registerXxx` 里捕获自己的状态变量，并实现两个函数（**自己负责读写自己那一段数据**）：

```cpp
struct MyState { int counter = 0; };
static MyState g_state;

void registerMyMod(ModAPI& api) {
    // ... 订阅事件时读写 g_state ...
}
void saveMyMod(std::ostream& os) { os << g_state.counter << "\n"; }
void loadMyMod(std::istream& is) { is >> g_state.counter; }

// mods_registry.cpp 里登记时把后两个字段填上：
{"my_mod", "我的 mod", "...", "PLAGUE", "", registerMyMod, saveMyMod, loadMyMod},
```

存档时按 mods.list 顺序依次调用 saveState；读档时按存档内记录调用 loadState。
**要求**：`loadState` 必须恰好消费 `saveState` 写入的字节，否则存档损坏。

## 6. 规则与红线（AI 必须遵守）

1. **不改核心**：`src/World.*`、`NucleusParams.*`、22 遗传参数含义、CSV 列顺序不得改动。
2. **确定性**：随机只用 `api.rng`；不得引入 `rand()`/`random_device`/时间种子；不得在回调里修改 `world` 的实体字段（能量除外，且只经 `apply`）。
3. **顺序敏感**：你的 mod 与其它 mod 的执行顺序 = mods.list 顺序；依赖顺序的行为要在描述里写清楚。
4. **事件效率**：帧级事件每帧只有 2~3 个（FRAME_START/FRAME_END/聚合吸收），回调里避免 O(N²) 逻辑；大遍历用 world 的现有访问器（已有空间哈希）。
5. **新机制需要新枚举**：新 EnergyReason/EventType 在对应头文件末尾追加（不改动已有值），并在 registry 的 touches 里声明。
6. **必须回归**：不启用你的 mod 时，`--seed 42 --balls 1000 --nuclei 20 --frames 10000 --no-render` 的 survivors.csv 与基线逐字节一致；启用后行为可复现（同清单同种子输出一致）。

## 7. AI 开发一个 mod 的建议工作流（给 AI 的提示词模板）

> 你是本项目的协作开发者。请在 `mods/<名字>.cpp` 新建一个 mod：<一句话描述效果与参数>。
> 按 docs/MOD_DEV_GUIDE.md 的第 4 节模板实现；在 src/mods_registry.cpp 登记（id=<名字>，
> touches=<影响的 EnergyReason>）；在 CMakeLists.txt 登记。完成后运行：
> `g++ -std=c++17 -O2 -Wall -Wextra -Isrc -o simulator.exe src/*.cpp mods/*.cpp -lws2_32 -lshell32`
> 确认零警告；写 mods.list 启用它并跑一次短模拟验证效果；关闭后与基线回归对比。

## 8. 验收清单（每步给命令）

- [ ] `g++ -std=c++17 -O2 -Wall -Wextra -Isrc -o simulator.exe src/*.cpp mods/*.cpp -lws2_32 -lshell32` 零警告
- [ ] `simulator.exe --schema-check` 输出「schema 键覆盖检查通过」（核心自检）
- [ ] 启动器 Mod 页能看到新 mod（名称/描述/开关/排序），冲突提示正确
- [ ] 启用后模拟行为变化可复现（同 mods.list 同种子输出一致）；关闭后与无 mod 基线一致
- [ ] `--energy-summary` 能按你的 EnergyReason 输出收支
- [ ] 若有 saveState/loadState：存档 → 读档 → 继续运行与不中断直跑一致
