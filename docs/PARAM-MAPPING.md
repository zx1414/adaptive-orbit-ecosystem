# ParamSchema key → SimConfig 字段映射表

来源：`src/app/ParamSchema.cpp` 的 113 个键（9 组）。
用途：R3 接线时把 `EnvControl::apply()` 产出的 `EnvChange{key, value}` 落到真实字段。

状态列：✅ 已有字段可直接映射 / ⚠️ 需要新增字段 / ❌ 废弃（用户取消或实体已删）

## world (11)
| key | 目标 | 状态 |
|---|---|---|
| `width` `height` | `SimConfig::world.width/height` | ✅ |
| `seed` | `SimConfig::world.seed` | ✅ |
| `ticks` | `SimConfig::world.maxTicks` | ✅ |
| `nuclei` | `SimConfig::colony.nuclei` | ✅ |
| `balls` | `SimConfig::colony.food`（现已删，语义改为初始自由球数） | ⚠️ 改名为 `initialFreeBalls` |
| `cyano_nodes` | `SimConfig::cyanoNodeCount`（B 已加） | ✅ |
| `sense_radius` | `SimConfig::policy.senseRadius` | ✅ |
| `max_fps` | 运行时限速 | ⚠️ 新增 |
| `render_interval` | 前端快照间隔 | ⚠️ 新增 |
| `autosave_interval` | 自动存档帧间隔 | ⚠️ 新增（R4） |

## light (10) —— B 已加 SunConfig
| key | 目标 | 状态 |
|---|---|---|
| `sun_base_level` | `SunConfig::baseLevel` | ✅ |
| `sun_day_amplitude` | `SunConfig::dayAmplitude` | ✅ |
| `sun_day_period_frames` | `SunConfig::dayPeriodFrames` | ✅ |
| `sun_vertical_gradient` | `SunConfig::verticalGradient` | ✅ |
| `sun_noise_amplitude/frequency/speed` | `SunConfig::noise*` | ✅ |
| `cyano_light_half_saturation` | `CyanoParams::lightHalfSaturation` | ✅ |
| `light_warmth` `light_sample_grid` | 渲染/采样辅助 | ⚠️ 可选 |

## food (12) —— 注意：Food 实体已删，这组现在描述"自由球"
| key | 目标 | 状态 |
|---|---|---|
| `ball_energy` `ball_radius` `ball_energy_cap` `ball_initial_energy` | 球能量参数 | ⚠️ 新增到 `BallConfig` |
| `ball_spawn_probability/cost/min_dist/max_dist` | `CyanoParams::ballSpawn*` | ✅ |
| `ball_vel_range` | 自由球初速 | ⚠️ 新增 |
| `ball_loss_cost` | 脱附惩罚（球动力学实现后生效） | ⚠️ R4 |
| `detach_radius` | 球脱附半径 | ⚠️ R4 |
| `regrow_food` | **废弃** —— Food 没了 | ❌ |

## pressure (9)
| key | 目标 | 状态 |
|---|---|---|
| `basal_cost` | `BehaviorParams::respirationBase` | ✅ |
| `respiration_base` | 同上（二选一，建议合并） | ⚠️ 去重 |
| `speed_cost_k` `speed_cost_exponent` | `BehaviorParams::locomotionCost` 等 | ✅ |
| `reproduction_cost_ratio` `child_energy_ratio` | 繁殖能量分配 | ⚠️ 新增（现在是常量） |
| `reproduction_offset` | 子代位置偏移 | ⚠️ 新增 |
| `mutation_coefficient` | `ColonyConfig::mutationCoefficient`（R2-A 加） | ✅ |
| `water_cost_multiplier` | `TerrainConfig::waterCostMultiplier` | ✅ |

## producer (4) + cyano (20)
全部映射到 `CyanoParams` 同义字段（B 已补全）。✅
注意：`cyano_conversion_mode` 的 0/1/2/3 现在**只影响初始值**，
真正的转化率由核的 `cyanoPreference/cyanoDigestion` 基因驱动（R2-A 接线后）。

## genome (24) —— 只影响**新生**个体
| 说明 |
|---|
| 这 24 个键是 `GenomeRanges` 的 Init/Clamp 范围，不是单个核的基因值 |
| 运行时改它们只影响**之后出生**的核，已存在的核不受影响（这是自然的：基因不能后天改） |
| 必须提供单测证明这一点 |

## ball (15) —— 球力场模型，R4 实现
全部 ⚠️ 新增到 `BallConfig`。其中：
- `follow_coupling` `follow_boost`：快核甩不掉球
- `bounce_restitution` vs `wrap_boundary`：**互斥**，二选一（我们当前是 wrap）
- 三个 `ball_max_speed_*`：三类球差异化（Shield/Resource/Scout）

## nucleus (8)
| key | 目标 | 状态 |
|---|---|---|
| `nucleus_damping` | 核速度阻尼 | ⚠️ 新增 |
| `nucleus_min_separation` | 硬分离距离 | ⚠️ 新增（R2-A 用） |
| `nucleus_repel_k` | 贴脸斥力刚度 | ⚠️ 与 genome 的 nucleusRepelStrength 关系需澄清 |
| `forage_k` `forage_radius` | 觅食引力 | ⚠️ 新增（R2-A 用） |
| `wander_k` `wander_turn_rate` | 随机游走 | ⚠️ 新增 |
| `nucleus_init_energy_ratio` | 初始能量 = ratio × energyThreshold | ⚠️ 新增 |

## 接线优先级（R3）
1. **P0**：genome 组（24 键）→ `GenomeRanges`，否则参数页改了没反应
2. **P0**：light 组 → SunConfig（B 已做，接线即可）
3. **P1**：cyano 组 → CyanoParams（大部分已通）
4. **P1**：pressure 组
5. **P2**：ball/nucleus 组（球力场与核动力学实现后）
6. **P3**：world 组的 max_fps/render_interval/autosave_interval（R4 存档用）

## 接线机制建议
不要给每个 key 写一个 if-else（113 个）。用一张静态映射表：
```cpp
struct FieldMap { const char* key; double* (*addr)(SimConfig&); };
```
或更简单：`EnvControl::apply()` 产出 `EnvChange`，主循环里用一个
`std::map<std::string, double*>` 构建一次的查表。缺 key 时打 stderr 警告（不许静默吞掉）。
