# M2 平衡标定记录

> 本文全部数据基于 M1–M4 的过渡模型（静止生产者 + 独立 Agent）。
> 该模型已确认偏离 v2.3.1 生态语义，数据**作废**，M5–M7 完成后需重新标定。

所有实验使用同一套确定性模拟、默认 2000×2000 世界、seed 42/7，关闭渲染，开启逐 tick 能量审计。
审计在全部实验中均为 `ok`；失败的是种群持续性，不是能量守恒。

## 默认基线

```powershell
build\simulator.exe --no-render --ticks 20000 --seed 42 --audit --stats build/stats.csv --stats-interval 1000
```

结果：producers=300（上限）、agents=200（上限），总能量约 49690，20k tick 存活，无灭绝。
种群在约 5k tick 内触顶，之后由上限制约；这是当前“光照不因生产者密度衰减”模型的自然结果。

## 实验参数

M2 新增了四个默认关闭/中性机制：

- `--producer-crowding <k> --producer-radius <r>`：局部遮荫，光合作用除以 `1 + k * 邻居数`。
- `--agent-lifespan <tick>`：Agent 到龄死亡并结算剩余能量；0 关闭。
- `--agent-reserve <amount>`：每次收获给生产者保留的最低能量；0 表示可采空。
- `--agent-harvest <amount>`、`--photo`、`--producer-maint`、`--agent-metab` 等用于扫掠。

## 扫掠结果

### 无猎物保留量

| 配置 | seed | 结果 |
| --- | --- | --- |
| 默认基线 | 42 | 300/200 存活，总能量 49690，audit ok |
| crowding=0.5, harvest=0.1, lifespan=2000 | 42 | 14190 tick 全灭（生产者为 0） |
| crowding=0.5, harvest=0.1, lifespan=2000 | 7 | 20000 tick 时 producers=0 / agents=200 |
| crowding=0.5, harvest=0.2, lifespan=3000 | 42 | 13442 tick 全灭 |
| crowding=0.5, harvest=0.2, lifespan=3000 | 7 | 15933 tick 全灭 |

### 加入猎物保留量

| 配置 | seed | 结果 |
| --- | --- | --- |
| crowding=0.5, harvest=0.1, lifespan=2000, reserve=5 | 42 | 20000 tick 时 300/151，audit ok |
| crowding=0.5, harvest=0.1, lifespan=2000, reserve=5 | 7 | 20000 tick 时 300/200，audit ok |
| crowding=0.5, harvest=0.2, lifespan=3000, reserve=10 | 42 | 20000 tick 时 300/200，总能量 39099 |
| crowding=0.5, harvest=0.2, lifespan=3000, reserve=10 | 7 | 20000 tick 时 300/200，总能量 45001 |
| 同上，50k 耐久 | 42 | 50000 tick 时 300/200，总能量 37351，4161 次 Agent 到龄死亡，audit ok |

无保留量时，Agent 会把生产者能量采到 0 并触发 `ProducerDeath`，随后生产者种群崩溃、Agent 饿死。
保留量 10 让最后一次收获留下可再生生物量，生产者不再灭绝，50k tick 内保持稳定。

## 推荐稳定配置

```powershell
build\simulator.exe --no-render --ticks 50000 --seed 42 --audit `
  --producer-crowding 0.5 --producer-radius 120 `
  --agent-harvest 0.2 --agent-reserve 10 --agent-lifespan 3000 `
  --stats build/balanced.csv --stats-interval 1000
```

结果：两个 seed 的 20k tick 与 seed 42 的 50k tick 都保持 300 生产者 / 200 Agent，审计通过，
Agent 有持续的出生与到龄死亡。该配置仍由上限制约，但不再发生“采空—崩溃”。

## M2.5 资源竞争标定

M2.5 在 M2 基础上加入四项机制（默认 0 = 关闭，保证 M1/M2 基线不变）：

- 生产者局部光竞争：`shading = 1 / (1 + crowding * localDensity + competition * localBiomass / cap)`。
- 生产者密度依赖繁殖：阈值为 `repro * (1 + densityRepro * localDensity)`。
- Agent 饱和收获：`harvest * food / (food + halfSaturation)`，`food = target.energy - reserve`。
- Agent 密度依赖繁殖：阈值为 `repro * (1 + densityRepro * localAgentDensity)`，配合 `lifespan` 周转。

### 推荐配置

```powershell
build\simulator.exe --no-render --ticks 20000 --seed 42 --audit `
  --producer-max 1000 --agent-max 1000 `
  --producer-competition 0.2 --producer-radius 400 --producer-density-repro 0.02 `
  --agent-move-k 0.00002 --agent-harvest 0.5 --agent-reserve 10 `
  --agent-half-saturation 20 --agent-density-repro 0.01 --agent-density-radius 200 `
  --agent-lifespan 3000
```

上限 1000/1000 只是数值安全阀；平衡点由局部竞争、密度阈值和饱和收获决定。

### 验收结果（tools/acceptance.ps1，14 项全部 PASS）

| seed | tick | producers | agents | total_energy | audit |
| --- | --- | --- | --- | --- | --- |
| 42 | 20000 | 202 | 43 | 22027.60411 | ok |
| 7 | 20000 | 214 | 111 | 27331.11735 | ok |
| 123 | 20000 | 255 | 71 | 28980.44196 | ok |
| 42 | 50000 | 202 | 65 | 23826.62187 | ok |

- 5k tick 预热后，三个 seed 的 producers 与 agents **100% 采样点**都低于 800（0.8 × 1000 上限）。
- 每 tick 审计通过；进程退出码 0。
- 同 seed 双跑状态哈希一致；seed 7 额外重复 3 次均为 `2faf3ce0d75683b9`。
- 默认基线行为不变：20k 仍是 300/200、总能量 49690.20045；stateHash 覆盖全部 SimConfig 与策略 state 后，默认 5k 哈希为 `7d6686d72a87853e`。

### 与 M2 的差异

- M2 推荐配置在 300/200 上限处触顶；M2.5 推荐配置的生产者稳定在 202–255，Agent 稳定在 43–111，
  不再由 max 决定。
- Agent 平衡数量对 seed 与局部聚集敏感，但 20k/50k 均未灭绝；提高下限需要进一步调整
  Agent 觅食效率或繁殖阈值，而非放宽 max。
- 默认 20k tick 耗时从约 102 秒降到约 16 秒（SpatialIndex 接入所有查询）。

## 下一步

- 用更长 tick 与更多 seed 检查 Agent 低密度平衡的稳健性。
- 若要提高 Agent 数量，优先调 `--agent-harvest / --agent-half-saturation / --agent-density-repro`，
  不要放宽 max。
- M3 存档与快照仍需保存 sim 组合根、实体 id、策略基因组与 PCG32 状态。

CSV 列包含种群、能量、出生/死亡、基因均值和审计标志，可直接用于后续拟合与可视化。