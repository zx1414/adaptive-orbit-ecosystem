# HANDOVER — 下一轮工作从这里开始

> 本文件是给**下一轮工作会话**（人或 AI 代理）的交接单。读完这一篇 + [README.md](README.md) 就能接手。

## 1. 这是什么

**agentsim v3.0.0** —— 自适应环绕核生态模拟与生存游戏。
C++17 单进程模拟器（MSVC，零第三方库）+ 原生 JS 前端 + HTTP 实时调控。
完整机制说明见 [README.md](README.md)，逐条公式的数理规格见 README「数理模型与实现细节」一章（独立全文在 [`docs/model/`](docs/model/)）。

## 2. 五分钟上手

| 想做什么 | 命令 / 操作 |
| --- | --- |
| 直接玩 | 双击 `release/agentsimw.exe`（或 [`tools/rebuild.bat`](tools/rebuild.bat) 重编译后跑） |
| 跑批处理实验 | `build\agentsim.exe --no-render --ticks 2000 --seed 42 --audit --stats` |
| 实时调控 | `build\agentsim.exe --serve 8080`，浏览器开 `http://127.0.0.1:8080` |
| 完整门禁 | `cmd /d /c tools\gate.bat`（成功标志：输出 `check PASSED`，408 PASS / 0 FAIL） |
| 金标准回归 | `--no-render --ticks 2000 --seed 42` 的 total_energy 必须逐位等于 **36367.17200929** |
| 重新打包发布 | `powershell -File tools/package.ps1` → 更新 `release/` 并重打 zip |

构建要求：Visual Studio 2022（MSVC v143）、Windows 10/11 x64。详见 [CONTRIBUTING.md](CONTRIBUTING.md)。

## 3. 关键不变量（改动前必读）

1. **零和恒等式**：每 tick `injected − dissipated == Σ存活实体能量`，容差 1e-9，不平即 exit 3。任何新能量流必须走 `AgentLedger`。
2. **四层单向管线**：environment → perception → decision → behavior（唯一写点）→ ledger。红线由 `tools/check.ps1` 静态扫描强制，违反即构建失败。
3. **确定性**：同 seed 双跑逐字节一致（门禁里有对拍）。改任何 RNG 消耗点都会破坏它——RNG 消耗序契约写在各模块头注释里（`CyanoNode.h` 文件头是范例）。
4. **先读 [2.11 已删除或未实现的机制](README.md#211-已删除或未实现的机制避免误读)**：代码里有一批"注释声称存在但实际没有"的东西（硬分离 pass、Q10、核的质量/年龄等），别照注释理解模型。

## 4. 当前状态与已知问题

- 395 单测 / 20873 断言 / 0 失败；8 道门禁全绿（2026-11-07 验证）。
- 已知问题清单：[CHANGELOG.md 已知问题段](CHANGELOG.md) 与 [docs/TODO.md](docs/TODO.md)。
- 短板：判据 4（谱系 hue 圆标准差 > 30°）未达标（实测 9.9°），用户表示"稳态由我后续实现"。
- 未实现（验收报告 P1，均已在 TODO 排队）：影响力经济、`--config` 开局注入、rebranch、socialAccelerations 网格化。

## 5. 目录导航

| 位置 | 内容 |
| --- | --- |
| [docs/GOAL.md](docs/GOAL.md) | S1–S25 成功标准全表（含证据索引） |
| [docs/INDEX.md](docs/INDEX.md) | 每条标准的**可执行验证命令** |
| [docs/TRACE-CONTRACT.md](docs/TRACE-CONTRACT.md) | trace JSON v3 契约（冻结，改前先看） |
| [docs/LOG.md](docs/LOG.md) | 31 轮开发日志——**踩坑史**，改核心机制前先搜这里 |
| [docs/experiments/](docs/experiments/) | E1/E2/E4 实验数据（八策略对比等） |
| [docs/PARAM-MAPPING.md](docs/PARAM-MAPPING.md) | 113 个实时调控键 → Config 字段映射 |
| `saves/demo-showcase/` | 演示存档（本地，不入 git；当前序列化格式可直接载入） |

## 6. 发布纪律

- 版本号单一真源：[`VERSION`](VERSION) 文件，与 `--version` 输出必须一致。
- GitHub Releases 只保留**发布级**条目：v3.0.0（现行）+ 历代 EcoCore 源码包（v1.0.0~v2.3.1，旧代码线）。
- 发新版流程：改 VERSION → CHANGELOG 加段 → `tools/package.ps1` → commit → tag → GitHub Release 上传 zip。

## 7. 环境备忘（AI 代理会话专用）

- 仓库即工作目录：`D:\ScuStu\Q\自适应环绕核生态模拟与生存游戏`。
- 构建走 `tools/gate.bat`（内部设好 vcvars）；沙箱环境若变量丢失，至少需要 `Path / SystemRoot / COMSPEC / TEMP / HOME`。
- 同事协作产物一律进 `docs/`（报告、卡片），临时探针脚本**用完即删**，不要污染仓库根。
