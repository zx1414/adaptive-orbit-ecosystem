# M2 平衡标定记录

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

## 结论

1. **默认基线**能量守恒、确定性和长期存活都成立，但种群由上限制约。
2. **仅靠遮荫/寿命调参无法避免崩溃**：Agent 会把生产者采到 0，缺少猎物 refuge。
3. **猎物保留量是当前最有效的稳定器**：reserve=10 时 50k tick 不灭绝，且保留年龄结构周转。
4. 遮荫、寿命、保留量默认关闭/为 0，避免改变 M1 基线；推荐配置通过命令行显式开启。
5. 低于上限的真正共存仍未实现：生产者光照对密度不敏感，种群最终由 `ProducerConfig::max` / `AgentConfig::max` 决定。

## 下一步（M2.5）

- 引入真正的资源竞争：局部光竞争、密度依赖繁殖抑制，或可耗尽的光资源。
- 让 Agent 的收获具有饱和功能反应，并让繁殖阈值随能量状态变化。
- 用 `--stats` 的种群/基因序列做多 seed 参数搜索，而不是只看最终 tick。

CSV 列包含种群、能量、出生/死亡、基因均值和审计标志，可直接用于后续拟合与可视化。
