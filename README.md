# agentsim

**自适应环绕核生态模拟与生存游戏**

在环面世界里观察蓝细菌光合生长、核觅食和繁殖，以及死亡组织如何经分解回到养分场。可以调节光照和代谢压力、触发寒冬或播种蓝细菌，也可以在同一场景里比较不同的决策策略。

**当前版本 3.2.1 · Windows 10/11 x64 · MIT**

## 开始游戏

1. 从 GitHub 下载 [Windows 单文件版](https://github.com/zx1414/adaptive-orbit-ecosystem/releases/download/v3.2.1/agentsim-3.2.1-win64.exe)，双击打开。
2. 选择场景、策略和材质包，点击「开始模拟」。默认地图为 1024×512，开局展示完整地图。

单文件版在 `%LOCALAPPDATA%\agentsim\portable\3.2.1\` 准备游戏文件。应用数据目录无法写入时，使用 EXE 旁的 `.agentsim\3.2.1\`。游戏以独立 Edge 配置开窗，存档在游戏目录的 `saves/`。需要命令行、开发示例或独立的运行目录时，可下载 [Windows ZIP](https://github.com/zx1414/adaptive-orbit-ecosystem/releases/download/v3.2.1/agentsim-3.2.1-win64.zip)，全部解压后双击 `agentsimw.exe`。ZIP 版的 `saves/` 与启动器同目录。手动开服可运行 `agentsim.exe --serve 8080 --no-open`，并访问命令输出的地址。

迁移旧存档时，把整个 `saves/<存档名>/` 复制到新版的 `saves/`，再从「打开存档」选择它。

| 游戏中看到的内容 | 含义 |
| --- | --- |
| 核的外环 | 当前能量占自身容量的比例 |
| 圆形、菱形、六边形器官 | 资源球、侦察球、护盾球 |
| 绿色节点、琥珀色小菱形 | 成熟蓝细菌和孢子。节点颜色深浅表示储能 |
| 核周围的橙色外环 | 靠近同一成熟节点并连续低速停留的进度 |
| 养分和残骸 | 当前世界的可吸收库存和待分解库存 |
| 时间轴 | 检查点回放，暂停后可选取历史帧并从那里继续 |

拖拽移动地图，滚轮缩放，`F` 切换铺满和全图，`R` 复位视角，空格暂停或继续。开局即显示侧栏和时间轴。「专注地图」收起周边面板，按 `Esc` 恢复。用时间轴旁的「保存进度」保存当前世界。

## 生态规则

成熟蓝细菌用光能和养分生长，以自身储能和组织繁殖孢子。孢子可以被直接吃掉，或在 90 帧后成熟。核要在成熟节点的摄食半径内以不高于 0.8 的速度持续停留 12 帧，才能摄食组织。同节点同帧的竞争按请求比例分配。死亡组织和未被同化的组织进入残骸场，随后分解成养分。

默认「巡游觅食」策略会优先寻找近处孢子，接近成熟节点时刹车并驻留。资源球为核提供额外储能，侦察球从自身位置寻找食物，护盾球降低温度压力引起的额外代谢。游离或被接管的球保留自身储能。

种群可能耗竭局部资源，也可能整体灭绝。寒冬默认持续 600 个模拟帧并压低光照基线。显式播种是一次外部输入，能量和物质分别入账。

## 扩展与实验

| 想做什么 | 入口 |
| --- | --- |
| 增加场景或改变养分运输 | [生态 Mod 开发](docs/MODS.md) |
| 自制地图和器官配色 | [材质包开发](docs/RESOURCE-PACKS.md) |
| 用 Python 运行自定义策略，训练并导入模型 | [算法研究与模型接口](docs/RESEARCH.md) |
| 阅读逐帧管线、数据边界、HTTP 与存档格式 | [当前架构](docs/ARCHITECTURE.md) |

## 从源码构建

需要 Visual Studio 2022 的 C++ x64 工具链和 Node.js。Python 3.10+ 用于算法研究。以下命令在仓库根目录运行。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/build.ps1
node frontend/tools/bundle.mjs
.\build\agentsimw.exe
```

同样可以直接批跑，开启能量审计并导出轨迹。

```powershell
.\build\agentsim.exe --no-render --seed 42 --mod nutrient_diffusion --nuclei 12 --cyano-nodes 48 --ticks 500 --audit --trace build/trace.json --trace-interval 20
node tools/analyze.mjs build/trace.json --out build/analysis.md
```

运行 `cmd /d /c tools\gate.bat` 可以构建并执行项目门禁。运行 `powershell -NoProfile -ExecutionPolicy Bypass -File tools/package.ps1` 会将单文件 EXE 放在仓库根目录，ZIP 放在 `build/`。

[版本记录](CHANGELOG.md) · [MIT 许可证](LICENSE)
