# 实时环境调控（v2.3.0）

玩家扮演“环境意志”：不直接操控核，而是通过光照、食物、压力、生产者四条轴塑造选择压力，让不同策略在演化中此消彼长。

## 1. 快调：四轴五档

| 轴 | 0 | 1 | 2 | 3 | 4 | 涌现作用 |
|---|---|---|---|---|---|---|
| 光照 `light` | 极夜 | 阴天 | 标准 | 晴朗 | 灼热 | 光照↑生产者繁盛→捕食者受益；光照↓生产崩溃→保守型存活 |
| 食物 `food` | 贫瘠 | 稀少 | 标准 | 丰饶 | 泛滥 | 食物↑高机动/高繁殖占优；食物↓高效吸收/低代谢占优 |
| 压力 `pressure` | 安逸 | 温和 | 标准 | 严酷 | 炼狱 | 压力↑慢速/高消化/耐饿生存；压力↓快速扩张 |
| 生产者 `producer` | 停滞 | 稀疏 | 标准 | 繁盛 | 爆发 | 改变“自养能量→捕食能量”的比例 |

- 快调条：画面底部常驻，显示影响力、四轴当前档位、`−/+`、事件、细调。
- 每次快调一档消耗 **1 影响力**；档位未变化时幂等成功、不扣费。
- 四轴具体数值只写在 `src/EnvControl.cpp`，前端通过 `GET /env` 读取，不复制常量。

## 2. 细调：精确滑杆（可展开高级参数）

细调抽屉按轴列出可调键（均来自 `ParamSchema`）。默认只显示基础键，点右上角「展开更多」显示高级键，并附一句简短说明：

- 光照：`sun_base_level`、`sun_day_amplitude`；高级：`sun_day_period_frames`、`sun_vertical_gradient`、`sun_noise_amplitude`
- 食物：`ball_spawn_probability`、`base_ball_energy`；高级：`ball_vel_range`
- 压力：`basal_cost`、`speed_cost_k`；高级：`speed_cost_exponent`、`reproduction_cost_ratio`
- 生产者：`cyano_photosynthesis_rate`、`cyano_metabolism_rate`；高级：`cyano_reproduction_probability`、`cyano_reproduction_offset`（默认 125）、`cyano_ball_spawn_probability`、`cyano_transfer_rate`、`cyano_predation_radius`、`cyano_min_node_separation`

**调节后立即应用**：松开滑杆或离开数字框即自动提交（也可点「立即应用本轴」按钮）。一次提交（一次 `POST /env`）中所有 `fine.*` **合计只扣 3 影响力**（无论几个键）；影响力不足 3 时整批拒绝；无变化不扣费。白名单外的配置键（如 `width/height/seed`）返回“该参数不支持实时细调”。

## 3. 影响力

| 项目 | 值 |
|---|---|
| 上限 / 开局 | 100 / 50 |
| 恢复 | 每模拟帧 +0.08（30 FPS 约 2.4/s；暂停不恢复） |
| 快调 | 1 |
| 细调（一批） | 3 |
| 预设 | 5 |
| 蓝细菌开关 | 10 |

目的是让干预成为有代价的取舍：不能无限拉满食物/光照；事件只受影响力约束，**没有冷却**，影响力够就可以连续触发。不干预时环境仍按当前档位运行。

## 4. 事件卡（一次性生态冲击）

| 事件 | 费用 | 效果 |
|---|---|---|
| 丰收 `harvest` | 15 | 全体存活核 +8 能量（总注入 ≤2000） |
| 寒冬 `winter` | 10 | 全体存活核 −5，能量 <5 跳过 |
| 瘟疫 `plague` | 20 | 能量 >100 的核 −12 |
| 蓝细菌爆发 `bloom` | 25 | 关闭则开 150 节点；否则追加 120 节点 |

手动事件**无冷却**：每次只检查影响力余额，余额足够即可连续触发。

事件在核心 `World::applyEnvEvent` 实现，能量走 `EnergyReason::BOOM`；无论 mod 是否启用，事件卡都可用。

## 5. 事件与 mod 的关系

- `climate_event` mod 是**自动事件适配器**：每 800 帧用 `api.rng` 抽一次丰收/寒冬/瘟疫，再调用同一个 `World::applyEnvEvent`；不会重复结算，也不扣玩家影响力。
- `season_cycle` 是自动季节环境，保留；面板不提供点击。
- `symbiosis_bond` / `energy_spill` 是**配对型**：需要 ≥2 个核，单核时必然空转；不作为事件按钮。推荐与 `ball_loss_relief` 组合。
- `rare_ball_subsidy` 建议配合失球宽限；`niche_guild` 与 relief 同开时收益可能抵消。
- Mod 页通过 `requires` 徽章显示上述运行条件。

## 6. HTTP 接口

`GET /env` 返回当前环境状态（运行/暂停、影响力、四轴档位与档位表、细调范围、事件费用、预设、蓝细菌计数、mods 自动事件状态、stats、costs）。

`POST /env`（`text/plain`，行式 `key=value`）：

```
axis.light=3
fine.sun_base_level=1.35
event.harvest=1
preset.balance=1
cyano=on
```

返回 `{ok, applied, rejected:[{key,reason}], influence, cooldowns}`；`cooldowns` 仅为旧版兼容字段，手动事件已无冷却、恒为 0。
菜单态写入下一次启动配置（不扣影响力）；运行/暂停态在下一帧生效；结束态拒绝。

## 7. 存档时间轴与世界线分支（v2.3.0）

- 运行中每 200 帧记录一个完整世界检查点（内存上限 2000 个，超出后自动抽稀）。
- 保存时写 `saves/<名字>/timeline.bin`；读档后前端自动打开时间轴，可拖动/输入帧号查看历史帧（内部从最近检查点快进到目标帧）。
- `GET /timeline`：`{ok, source, savedFrame, current, min, max, checkpoints:[...]}`。
- `POST /seek`：`frame=N`，暂停并定位到该帧。
- `POST /branch`：`frames=N` + 任意 schema `key=value`，保留当前球/核/蓝细菌状态，从当前帧开始新世界线（几何尺寸、seed、初始数量、初始蓝细菌数量不重放）。新世界线可继续保存。
- 旧存档没有 `timeline.bin` 时只能查看保存帧；继续运行并保存后会积累时间轴。

## 8. 存档格式

存档 v2 额外保存：光色相 RNG、蓝细菌节点/连接/nextNodeId、EnvState（影响力/旧冷却字段/干预计数/峰值）。读档后节点、连线、影响力与画面立即恢复；旧档里的冷却值会被清零（手动事件已无冷却）。v1 旧存档可读，但旧档不含蓝细菌状态，载入后按关闭处理，可再用 `cyano=on` 激活。
