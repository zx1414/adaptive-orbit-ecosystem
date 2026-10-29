# v2.3.1 架构与计划归档

这里保存旧版 v2.3.1 的原始文档副本，内容按原文件保留，仅用于查阅历史设计。
它们**不参与 v3 构建**，也不是当前实现规范。

## 为什么在这里

v3 重构时，旧实现被移到 `legacy/`（被 `.gitignore` 忽略）并冻结在 Git 标签
`legacy-v2.3.1`。当时只保留了标签，没有在活动 `docs/` 里留下索引，导致旧架构文档
在仓库里“看不见”。这是一次交接失误；现在把文档副本放回 `docs/archive/` 并纳入版本管理。

当前架构请看 [../ARCH.md](../ARCH.md)，当前路线图看 [../ROADMAP.md](../ROADMAP.md)。

## 文件索引

| 文件 | 内容 |
| --- | --- |
| [PLAN.md](PLAN.md) | 旧版 Phase 0 实施计划：EventBus、EnergySystem、ModAPI、Renderer 抽象，以及当时的整体代码结构 |
| [PLAN_AGENTS.md](PLAN_AGENTS.md) | Agent 范式 A0：环境/探测/决策/行为模块化解耦的原始规格，是这次 v3 重构的起点 |
| [PLAN_LAUNCHER.md](PLAN_LAUNCHER.md) | 浏览器启动器、HTTP/Web 渲染、交互控制 |
| [MOD_SPEC.md](MOD_SPEC.md) | 旧版 Mod 红线与扩展接口约定 |
| [MOD_PLAN.md](MOD_PLAN.md) | Mod 体系规划 |
| [MOD_DEV_GUIDE.md](MOD_DEV_GUIDE.md) | Mod 开发指南 |
| [MOD_SUITE.md](MOD_SUITE.md) | Mod 套件说明 |
| [CYANO.md](CYANO.md) | 蓝细菌生产者网络 |
| [ENV_CONTROLS.md](ENV_CONTROLS.md) | 实时环境调控 |
| [PUBLISH.md](PUBLISH.md) | 发布流程 |
| [README-v2.3.1.md](README-v2.3.1.md) | 旧版总览、构建、CLI、架构与玩法说明 |

注意：旧 README 里的相对链接（如 `docs/ENV_CONTROLS.md`）是按旧目录写的；在本归档里
对应文件就在同目录，直接把路径改成文件名即可。

## 恢复完整旧源码

标签里是完整快照：

```powershell
git worktree add ../eco-legacy-v2.3.1 legacy-v2.3.1
```

或只取某个文件：

```powershell
git show legacy-v2.3.1:src/World.cpp
```

磁盘上的 `legacy/` 也有完整副本（含构建产物、mods、web、resourcepacks），但被
`.gitignore` 忽略，不会进入提交。
