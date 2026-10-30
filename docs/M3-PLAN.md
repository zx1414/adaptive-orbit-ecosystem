# M3 实施计划：存档 / 快照

> 适用于 M1–M4 的过渡模型。快照格式在 M5 之后需升级到 version 2，见 [M5-PLAN.md](M5-PLAN.md)。

> 状态：已完成。33 个单测、tools/m3_acceptance.ps1 全 PASS；
> 20k 直跑 vs 10k 存档 + 10k 续跑哈希/能量/种群/账本一致。实现与格式见 ARCH.md。

目标：保存并恢复完整 Simulation 状态，读档续跑与不中断运行逐位一致；损坏/版本不匹配明确拒绝。

## 1. 不变量

- 读档不重新注入初始能量；账本统计与实体能量同时恢复，加载后立即 audit。
- 快照包含 tick、PCG32 全状态（state/increment/cachedNormal/hasCachedNormal）、EnergyLedger 全部统计、
  Producer/Agent 的 id/位置/速度/能量/cap/age、nextId、births/deaths、策略 spec.id/genome/预留 state。
- 恢复后实体顺序、id、繁殖顺序与随机数消耗顺序必须与原运行一致。
- 快照失败不得留下半初始化对象；错误路径不崩溃、不静默成功。

## 2. 二进制格式 v1

```text
Header (32 bytes, little-endian)
  magic[8] = "ECOCORE3"
  formatVersion u32 = 1
  headerSize u32 = 32
  payloadSize u64
  payloadChecksum u64 (FNV-1a over payload)
Payload
  SimConfig: width,height,seed,maxTicks,light...,producer...,agent...,policy rule(string)
  tick u64
  producerNextId u64, producerBirths u64, producerDeaths u64
  agentNextId u64, agentBirths u64, agentDeaths u64
  RandomState: state u64, increment u64, cachedNormal f64, hasCachedNormal u8
  EnergyLedger: injected/dissipated/transferred f64, initialSealed u8,
                reasons[10]{in f64, out f64}
  producers u64 count, each { id u64, kind u8, pos.x f64, pos.y f64, energy f64, cap f64 }
  agents u64 count, each { id u64, pos.x/y f64, vel.x/y f64, energy f64, cap f64, age u64,
                           policyId string, genomeCount u32, genome f32[],
                           policyStateSize u32, policyState bytes[] }
```

- 字符串：u32 长度 + UTF-8 字节；上限 1 MiB。
- 计数上限：1,000,000 实体；genome <= 4096；policy state <= 1 MiB；payload <= 1 GiB。
- 校验：magic、version、headerSize、payloadSize 与实际文件长度、checksum、EOF、无尾随垃圾。
- 语义校验：坐标/速度/能量/基因有限；cap > 0；energy ∈ [0, cap+eps]；id 唯一；
  nextId > max(id)；policy id 可创建；genome 维度匹配；config 合法；ledger 统计有限。

## 3. 代码改动

- 新增 `src/sim/Snapshot.h/.cpp`：Header/Writer/Reader、save/load 自由函数。
- `IPolicy` 增加默认实现 `serializeState/deserializeState`（默认空；NNet 以后覆盖）。
- `EnergyLedger`、`ProducerSystem`、`AgentSystem`、`Simulation` 增加 friend 或窄接口，
  只允许 Snapshot 读写私有状态；不新增公开可变状态。
- `Simulation::stateHash` 扩展：policy spec id/版本/维度、energy cap、age、nextId、births/deaths；
  默认基线哈希会变化，文档同步更新。
- `src/app/Cli` 与 `main`：`--save <path>` 在运行结束保存；`--load <path>` 从快照恢复，
  `--ticks` 作为绝对结束 tick 覆盖快照 maxTicks；load 模式拒绝 seed/世界/平衡参数覆盖。
- 退出码：2 参数错误，3 审计失败，4 I/O 失败，5 快照格式/版本/语义错误。
- CMake 加 Snapshot.cpp；build.ps1 自动 glob 无需改。

## 4. 测试

单元（`tests/unit/test_snapshot.cpp`）：
1. 100 tick 存档 roundtrip：hash/tick/种群/总能量/账本/audit 一致。
2. 1000 tick 直跑 vs 500 存档 + 500 续跑：stateHash 逐位一致；再续跑 100 tick 仍一致。
3. tick 0 存档；0 生产者/0 Agent；RNG cachedNormal 非空时存档。
4. 截断、坏 magic、version+1、checksum 错误、尾随垃圾、计数超大、重复 id、NaN、cap<=0：
   全部返回明确错误且不崩溃。
5. CLI 集成：`--save` 后 `--load` 哈希一致；损坏文件退出码 5；`--load` + 覆盖参数退出码 2。

验收脚本 `tools/m3_acceptance.ps1`：
- seed 42/7：20k 直跑 vs 10k 存档 + 10k 续跑；比较 stdout 的 hash/总能量/种群/injected/dissipated/audit。
- 坏文件/版本拒绝退出码 5；空文件不崩溃。

## 5. 对抗审查重点

- 快照字段漏项：RNG 缓存、账本 reason 统计、nextId、births/deaths、age、policy state。
- 读档路径误调用 initialize/injectInitial。
- 计数/长度溢出、负值、重复 id、nextId 回退。
- 同一 tick 繁殖后存档、死亡后存档、审计失败时存档。
- 存档写入中途失败/磁盘只读；CLI 旧参数回归。

## 6. 任务切片

1. Generator A：stateHash 扩展 + 单测。
2. Generator B：Snapshot 格式/校验原语 + 单测（不接 Simulation）。
3. Generator C：各系统序列化访问器 + Snapshot save/load + 单测。
4. Generator D：CLI/main 集成 + 集成脚本。
5. Red Team：隔离上下文逐条攻击格式/状态完整性/CLI。
6. Generator 修 P0/P1；Red Team 复审；Verifier 跑全量门禁；Lead 提交。

每步保持 /W4 /WX、零第三方依赖、确定性；只有最终门禁通过才提交。