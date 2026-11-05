# 夜班运行手册（NIGHT-SHIFT）

> 指挥者（主 agent）维护。夜间唯一循环指令。
> 铁律：**主 agent 不写实现代码、不参与审查**，只写契约、派活、跑门禁、仲裁、更新本文件。

## 0. 角色

| 角色 | 谁 | 能做什么 |
| --- | --- | --- |
| Lead | 主 agent | 写契约、派活、跑门禁命令、仲裁 P0/P1、更新本手册 |
| Generator | 子代理 | 实现 + 自测，不审自己 |
| Red Team | 子代理（新上下文） | 只读审查，只输出 P0/P1/P2 |
| Verifier | 主 agent | 跑构建/单测/check，不改代码 |

## 1. 项目

- **主项目**：`D:\ScuStu\Q\agentsim`（agentsim，用户新规格）
- 规格源：`D:\ScuStu\Q\agentsim\docs\SPEC-SOURCE.md`（原文，勿改）
- 旧项目：`D:\ScuStu\Q\自适应环绕核生态模拟与生存游戏`（EcoCore，已暂停，勿动）

## 2. 模型与并发

- 全部子代理：`provider=zcode-start-plan-acct-1e99ffd8-0ecc-4b88-b477-7be8d2f78439`, `model=GLM-5.3-Flash`
- 备选账号：`zcode-start-plan-acct-4e049fa5-29da-4e8e-9d84-4f95ff6e657c`（GLM-5.3 限流，Flash 可用）
- **每账号并发 ≤ 2**（用户硬约束）。当前节奏：账号 1 同时跑 2 个子代理。
- 需要更大并行时，第二路用账号 2 的 Flash，再凑 2 个 = 全机 4 路。

## 3. 文件归属（防止并发写冲突）

| 子代理类型 | 独占路径 |
| --- | --- |
| 文档架构师 | `docs/GOAL.md` `docs/TODO.md` `docs/INDEX.md` `docs/DECISIONS/` |
| 骨架/生成者 | `src/` `tests/` `tools/` `CMakeLists.txt` |
| 红队 | 只读，只写 `docs/reviews/R*-*.md` |

派活时必须在契约里写明"你只能动这些路径"。

## 4. 循环（每 20 分钟提醒触发一次）

1. `list_agents` 收状态；已完成则读其交付物与报告。
2. 主 agent 自己跑门禁：
   ```powershell
   cd D:\ScuStu\Q\agentsim
   powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1
   build\unit_tests.exe
   build\agentsim.exe --no-render --ticks 500 --seed 42 --audit
   ```
3. 门禁过了 → 派**红队**（新子代理，新上下文）审 diff；红队输出写 `docs/reviews/`。
4. 红队出 P0/P1 → 派**生成者**修；修完回第 2 步。
5. 红队无 P0/P1 → 把任务在 TODO.md 移到"已完成"、INDEX.md 补证据（**派文档子代理做**，主 agent 不写）。然后派下一个任务。
6. 每完成一个任务，本手册 §6 追加一行。

## 5. 任务队列

Phase 1（初版最小闭环，不含地形、不含感知机）→ 见 `docs/TODO.md`。
初版跑通后自行创新（用户明示）：温度场失真、感知机策略、地形、探测球、编队禁止但探索个体策略多样性等。
每次创新前先写 ADR 再实现。

## 6. 夜班日志

| 时间 | 事件 |
| --- | --- |
| 00:06 | 用户下达新规格：agentsim。旧 EcoCore 暂停 |
| 00:20 | 规格落盘 SPEC-SOURCE.md；并行派 文档架构师(c95ccdab) + 骨架工程师(dd41ba7e) |
| 00:22 | 重设唤醒 cron 为 20 分钟；approval=never，子代理不可提权 |

## 7. 降级与中断

- 生成者连续 2 次失败 → 缩小切片（只做一个文件/一个测试）再试。
- 429 → 等一个周期再派，不立刻重试。
- 账号 1 额度耗尽 → 全切账号 2 的 Flash。
- 卡死超 2 个周期 → 记 `STALL`，任务降级。
## 8. 子代理工程纪律（血泪总结，派活必须复制进契约）

1. **大文件必被截断**：一次 `write` 超过 ~100 行，输出会断，子代理直接失败。必须拆成 ≤80 行片段，
   第一段 `fs.writeFileSync(p, '\uFEFF'+text, 'utf8')`，后续段 `fs.appendFileSync(p, text, 'utf8')`。
2. **UTF-8 BOM 必须有**：无 BOM 的中文注释在 MSVC 936 代码页下触发 C4819，`/WX` 直接失败。
3. **卡死识别**：子代理"心里规划"太久会导致会话日志停止增长。判据：session.v4.jsonl.zstd 的
   LastWriteTime 超过 10 分钟不变 = 悬挂，直接 `interrupt_agent` 杀掉重派。
4. **任务粒度**：一个子代理 ≤6 个文件。超过就拆成两个并行代理，各自独占目录。
5. **reasoning_effort 用 low**：high 会让单步思考过久，实测更容易悬挂。
6. **契约里要写死真实签名**：让子代理先读依赖的头文件，不要臆造 API。

## 9. 当前进度快照

| 模块 | 状态 |
| --- | --- |
| src/core（Vec2/PCG32/Config/Types） | ✅ 完成，19 测试 3175 断言通过 |
| src/ledger（AgentLedger 零和） | ✅ 完成 |
| src/environment（Torus + 食物） | ✅ 完成 |
| src/perception（16 维 SensorVector） | ✅ 完成 |
| src/decision（RulePolicy/StubPolicy） | ✅ 完成 |
| src/behavior（唯一写点 + 双通道干扰 + 球远失控） | ✅ 完成 |
| tests/unit（19 tests） | ✅ 通过 |
| src/sim（组合根） | 🔄 进行中 (8028adf8) |
| src/app + tools（CLI/构建/check） | 🔄 进行中 (df4cd263) |
| 红队审查 | ⏳ 待跑 |
| TODO/INDEX 更新 | ⏳ 待跑 |

## 10. 夜间进度（截至 04:30）

| 阶段 | 状态 |
| --- | --- |
| Phase 1 最小闭环（Torus + 食物 + 规则 + 移动/吃/耗能 + 双通道干扰） | ✅ 全部通过 |
| 红队 R1（12 条，5 P1） | ✅ 已全部修复并复审 |
| 双通道干扰（读噪声 s[0..9] + 写扰动，强度 ∝ speed） | ✅ test_behavior.cpp 覆盖 |
| 球远失控（ballControlGain + lastOmega 漂移） | ✅ 可达且有测试 |
| deploy / recycle / reproduce 三件套 | ✅ 全走账本，子代初能 ≤ 亲代消耗 |
| 感知机策略 PerceptronPolicy（Z=W^T X+b, sign） | ✅ 已接线，3 策略产出互异 |
| 感知机 20000 tick | ✅ audit=ok, nuclei=12, food=25 |
| 温度场 TemperatureField | 🔄 (30a1c5c7) |
| 感知机参数进化实验 E1 | 🔄 (e9b734f9) |

**门禁基线（主代理亲测）**：
```
tools\check.ps1                  -> check PASSED, exit 0
build\unit_tests.exe             -> 40 tests, 3456 checks, 0 failures
--ticks 200 --seed 42 --policy rule       -> food=38  total_energy=2818.88
--ticks 200 --seed 42 --policy stub       -> food=411 total_energy=5292.80
--ticks 200 --seed 42 --policy perceptron -> food=19  total_energy=2748.80
--ticks 20000 --seed 42 --policy perceptron -> audit=ok, nuclei=12, food=25
```

## 11. 已知阻塞

- **goal 工具需要真人回合**：`update_goal` 的 edit/resume 在当前（自动续轮）上下文中被拒绝。
  如需改 goal 目标或重新武装，必须由用户在**正常对话回合**中让我操作。
- **agent_mode=0（轨道范式）未实现**：CLI 已明确 `exit 2` 报错，不静默忽略。S9 仍未完成。
- **地形（障碍/水域/温度区）未开始**：排在温度场之后。
