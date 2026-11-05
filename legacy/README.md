# legacy/ — 旧版源码（仅供参考，不再维护）

本目录存放 **EcoCore v3 / v2.3.1 时代**的旧版源码与文档，即本项目
`zx1414/adaptive-orbit-ecosystem` 仓库在 v1.0.0 之前的全部受版本控制内容。

## 状态：冻结 / 只读参考

- **不参与构建**：仓库根的 `tools/build.ps1` / `tools/check.ps1` 只编译 `src/`，
  与 `legacy/` 完全隔离。
- **不再维护**：旧版生态模型（静止生产者 + 独立 Agent 追食物）已被 v1.0.0 的
  四层管线（环境 → 感知 → 决策 → 行为 → 账本）取代；旧版平衡的
  `docs/BALANCE.md` 等文档数值已作废，仅作历史脉络保留。
- **旧版基线仍在 git 历史里**：tag `v1.0.0` ~ `v2.3.1`、`legacy-v2.3.1`
  指向合并前的提交，可直接 `git show <tag>:<path>` 查看。

## 目录

| 路径 | 旧版内容 |
| --- | --- |
| `legacy/ecocore-v3/src` | 旧四层拆分前的 C++ 源码（colony / world / producers 等） |
| `legacy/ecocore-v3/docs` | GOAL / TODO / INDEX / ADR / M2.5 / M3 / M4 / M5 计划与交接文档 |
| `legacy/ecocore-v3/frontend` | 旧 EcoScope 轨迹可视化前端 |
| `legacy/ecocore-v3/tests` | 旧单测 |
| `legacy/ecocore-v3/tools` | 旧构建/验收脚本 |
| `legacy/ecocore-v3/CMakeLists.txt` | 旧 CMake 工程 |
| `legacy/ecocore-v3/README.md` | 旧版 README（其"当前状态"描述已过时） |

## 现行版本

仓库根的 `src/`、`frontend/`、`tests/`、`tools/`、`docs/` 是 **agentsim v1.0.0**
（R11 交付：四层管线生态模拟、HTTP 实时调控、桌面发布形态）。请看根
[README.md](../README.md) 与 [CHANGELOG.md](../CHANGELOG.md)。
