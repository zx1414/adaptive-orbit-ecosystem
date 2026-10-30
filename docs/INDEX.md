# INDEX —— 已完成的证据

> 每条必须指向可执行的验证动作或具体路径。禁止主观描述。
> 格式固定：`任务 | 对应成功标准 | 证据 | 验证方式`
> 证据必须映射回 GOAL.md 的某条成功标准。

| 任务 | 对应成功标准 | 证据 | 验证方式 |
| --- | --- | --- | --- |
| 蓝细菌光合是唯一初级注入 | S9 | `EnergyLedger::injectPrimary` 只被 `producers::PhotoProducer`（已删）与 `environment::Cyanobacteria` friend 化；`energyReasonName` 含 5 个 Cyano 科目 | `grep -r "injectPrimary" src/` 只有 Energy.cpp 定义与 Cyanobacteria.cpp 调用 |
| 每 tick 审计覆盖三层 | S2, S4 | 单元测试 `simulation audits the whole ecosystem, not just one layer` | `build\unit_tests.exe`（43 passed, 0 failed） |
| 同 seed 双跑一致 | S3 | 两次 `--ticks 600 --seed 42` 的 stdout 字符串相等（`determinism=True`） | `simulator.exe --no-render --ticks 600 --seed 42 --audit` 跑两次并比较 |
| 小球绕核公转 | S4 | 追踪球 id=1（owner=1）101 帧：角位移累计 **10.09 圈** | `simulator.exe --no-render --ticks 500 --cyano 4000 --trace-json b.json --trace-json-interval 5`，按 `b[4]` 球 id 统计 atan2 累计 |
| 轨道半径稳定性 | S5（**未达标**） | 同一球 101 帧半径 `mean=64.5`、`std/mean=0.356`，超过 20% 阈值 | 同上；结论：S5 当前失败，已挂 TODO「正在进行」 |
| 球内能量参与审计 | S6, S7 | 单元测试 `resource balls raise the colony capacity and recycle returns only a fraction`、`a detached ball keeps its energy and can be adopted by another nucleus` | `build\unit_tests.exe` |
| 容量裁剪记账 | S6 | 单元测试 + 实测 `CAP_OVERFLOW out=72.39603385` 出现在 `--energy-summary` | `simulator.exe --no-render --ticks 1500 --seed 42 --energy-summary` |
| 侦察采样与噪声 | S8 | 单元测试 `cyanobacteria queries respect the radius and skip dead nodes`；观测断言见 `test_environment.cpp` | `build\unit_tests.exe` |
| 轨迹契约含 c/n/b 且带 id | S11 | `--trace-json` 输出 `frames[0]` 同时含 `c`/`n`/`b`；球元素 7 字段 `[x,y,energy,cap,id,ownerId,type]` | `simulator.exe --no-render --ticks 400 --cyano 4000 --trace-json t.json`，解析 `frames[-1].b[0].Count == 7` |
| 快照覆盖菌落状态 | S12（部分） | 单元测试 `snapshot save/load preserves the colony simulation state`、`snapshot split run matches uninterrupted run`（600 tick 拆分） | `build\unit_tests.exe`；**尚未做 10k+10k 全量验收** |
| 全量门禁 | S13 | `check.ps1` 输出 `Build, dependency, unit and integration checks passed.` | `powershell -File tools/check.ps1` |
| 决定论 43 项单测 | S13 | `43 passed, 0 failed` | `build\unit_tests.exe` |

## 待补证据（TODO 已登记的已完成任务，但尚无 INDEX 条目）

按红线，以下任务在证据补齐前**不进入本表**：

- 文档纠正（M5-PLAN / ARCH 偏离记录）——缺 `git show` 输出摘录
- 环境层的繁殖与代谢守恒细节——缺独立验收脚本
- 轨道物理的参数化论证（`tangentialK` 取舍）——已在 ADR-003 记录，但缺回归测试
- 组合根重写后的性能基线——缺 20k tick 耗时记录
