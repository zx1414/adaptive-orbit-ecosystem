# 贡献指南

感谢你愿意改进 agentsim！本项目把「可审计、确定性、零回归」看得很重，请遵守以下约定。

## 环境

- Windows 10/11 + Visual Studio 2022（MSVC v143，含 C++ 桌面开发）
- Node.js 18+（仅前端 `smoke.mjs` / `bundle.mjs` / `analyze.mjs` 需要）
- PowerShell 5.1+（构建脚本）

## 铁律（门禁静态检查，违反直接 FAIL）

1. **四层单向依赖**：`src/decision` 不得 include `environment/`、`sim/`、`ledger/`；`src/perception` 不得 include `decision/`。
2. **行为层是唯一写点**：除 `src/behavior/` 外，任何 `src/` 文件不得给核/球实体状态赋值（出生引导两行有历史豁免，见 `tools/check.ps1`）。
3. **确定性**：全 `src/` 禁用 `rand()` / `<random>` / `random_device`；`src/decision` 禁用 RNG（构造期感知机初值有窄豁免）。随机数一律走 `core/PCG32.h`。
4. **零警告**：MSVC `/W4 /WX /permissive- /utf-8`，全绿才合并。
5. **参数必须活**：新增 CLI / Config 参数必须能被 `tools/check.ps1` 的死参数探针观察到输出变化。

## 流程

```powershell
# 0) 从任意目录 clone 后（脚本用 %~dp0/$PSScriptRoot 相对定位，无绝对路径硬编码）
git clone https://github.com/zx1414/adaptive-orbit-ecosystem.git
cd adaptive-orbit-ecosystem

# 1) 构建
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1

# 2) 改代码 + 补单测（tests/unit/，自研框架，TEST() 宏）

# 3) 全套门禁 —— 必须输出 "check PASSED"
powershell -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1

# 4) 前端改动后重打单文件 bundle 并跑 smoke
node frontend\tools\bundle.mjs
node frontend\tools\smoke.mjs

# 5) 位级回归：默认参数 2000t/seed42 的 total_energy 若变化，必须在 PR 里解释
build\agentsim.exe --no-render --ticks 2000 --seed 42
```

## 提交规范

- 提交信息用英文祈使句：`fix(cyano): ...` / `feat(frontend): ...` / `docs: ...`
- 每个 PR 只做一件事；涉及行为/公式变更必须附：① 单测 ② 位级回归结论 ③ 文档更新
- 前端 `frontend/js/*.js` / `index.html` / `styles.css` 改动后必须重新 `bundle.mjs`，否则发布包 `frontend/agentsim.html` 会与源码不一致

## 报告问题

请附：操作系统/VS 版本、复现命令、`build\gate_out.txt` 或 `--audit` 输出、seed 与 ticks。确定性问题的报告请务必给出完整命令行。

## 许可

贡献即表示你同意以 [MIT](LICENSE) 许可发布你的代码。