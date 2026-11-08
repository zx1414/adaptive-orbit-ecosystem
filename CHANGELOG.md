# Changelog

本文件记录 **agentsim**（仓库根 `src/` 现行版）的发布历史。
旧版 EcoCore 代码线属于**另一个代码线**，
其源码已冻结移入 [`legacy/ecocore-v3/`](legacy/ecocore-v3/) 与 [`legacy/ecocore-v231-runnable/`](legacy/ecocore-v231-runnable/)，不参与本版本线。

格式遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) + 语义化版本。

## [v3.0.0] — 2026-11-05（首个开源发布）

> **版本号说明**：旧 EcoCore 代码线曾发布 `v1.0.0`～`v2.3.1`（旧 tag 与各版源码包已恢复到
> [Releases](https://github.com/zx1414/adaptive-orbit-ecosystem/releases)，旧源码保留在 [`legacy/`](legacy/)）。
> 为避免与旧 tag 重名造成 `git checkout` 歧义，agentsim 首发版本取 **v3.0.0**
> （延续旧版谱系的下一个主版本），`VERSION` 文件与 `--version` 输出同步为 `3.0.0`。

### 新增（开源发布 / 版本控制）

- **完整数理规格**：README 新增「数理模型与实现细节」一章（约 3600 行），覆盖环境 / 个体 / 决策 / 基础设施四层——全部公式、常数、钳制条件均带 `文件:行号` 引用；同步拆分为 `docs/model/` 四份独立全文
- **完全开源**：MIT LICENSE、重写 README、CHANGELOG、CONTRIBUTING、VERSION 单一真源、`.gitignore` 规范化
- **旧版归档**：旧 EcoCore v3 源码 `git mv` 进 `legacy/ecocore-v3/`，附 legacy/README.md 明确标注「冻结/只读参考/不参与构建」
- **发布包**：`release/` 纳入版本控制（双 exe / 单文件前端 / 材质包 / README.txt），`tools/package.ps1` 一键重新打包（zip 产物走 GitHub Releases，不入 git）
- **便携构建**：新增 `tools/rebuild.bat` / `tools/gate.bat`（基于 `%~dp0` 相对定位，取代绝对路径硬编码）

### 现行版能力（R11 交付，2026-11-02）

- 四层单向管线生态模拟：24 基因 / 8 决策策略 / 蓝细菌网络 / 自遮光 / 捕食避难所 / 营养循环
- HTTP 实时调控（`--serve`、/env /state v2 /control /run /save /load /seek /timeline /packs）
- 单文件 Web 前端 + 存档/时间轴/回放 + 材质包 + 六判据生态分析
- 桌面形态：无控制台启动器、单实例、Edge `--app` 独立窗口
- 395 单测 / 20873 断言 / 0 失败；8 道门禁（含双跑逐字节确定性、死参数探针）

### R11 修复要点（相对 R10 验收反馈）

| 反馈 | 修复 | 结果 |
| --- | --- | --- |
| 帧率太低 | `CyanoField::linkTransfer` 环面空间网格化（O(n²)→桶邻域，遮蔽对字典序回放保浮点逐位一致） | **166.8 → 10.65 ms/tick（15.7×）**，2000t/seed42 `36367.17200929` 逐位不变 |
| 连线跨全屏 | 前端环面 wrap 分段绘制（主段 + 镜像段），>4000 条 4 取 1 抽稀 | 左出右进 |
| 看不到光照数值 | HUD 追加「阳光 x.xx」，读 `/state` 头 sun 字段 | 与后端 628 帧昼夜周期同步 |
| 无明暗变化 | `drawSky` 以 `frame.live.sun` 为唯一光照源（极简日罩/夜幕平涂） | 拉光照轴立见变化 |
| — | 三死参数接线（speed_cost_exponent / nucleus_damping / reproduction_cost_ratio） | 恒等默认值法逐位零回归 |
| — | 结束原因（`extinct` / `max_ticks`）写入 /state 头保留区并在前端显示 | 灭绝可判定 |

同核吸引曾按原版恢复 40/0.15，因破坏 256 世界机制测试而回滚（归因注释保留在 Config.h）。

### 已知问题

- 判据 4（谱系 hue 圆标准差 > 30°）未达标（seed42 稳态 9.9°）；稳态标定列为后续版本
- `maxNuclei=1200` 护栏在部分 seed 会撞顶（T57）
- 影响力经济、`--config` 开局注入、rebranch、socialAccelerations 网格化尚未实现
- 详见 [docs/TODO.md](docs/TODO.md) 与 [docs/accept/ACC-R11-acceptance.md](docs/accept/ACC-R11-acceptance.md)

## 旧代码线（归档，仅供参考）

- EcoCore v3 重构过程（静止生产者 + 独立 Agent 模型）：源码位于
  [`legacy/ecocore-v3/`](legacy/ecocore-v3/)，v2.3.1 完整可运行副本位于
  [`legacy/ecocore-v231-runnable/`](legacy/ecocore-v231-runnable/)。
  该线已冻结，不再维护；各历史版本的源码包见 [Releases](https://github.com/zx1414/adaptive-orbit-ecosystem/releases)。