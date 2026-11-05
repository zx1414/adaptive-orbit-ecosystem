# agentsim — 自适应环绕核生态模拟与生存游戏

**一个能自己"长"出生态的模拟器。** 阳光照下来，蓝细菌光合生长、互相连线分能量、分裂繁殖；
带着可遗传基因的核去觅食、移动、繁衍、变异；小球绕核旋转、存着能量。**没有剧本，没有预设平衡**——
种群数量与谱系兴衰由能量收支和遗传漂变自己决定。你调光照、调生产者，然后看生态怎么回应。

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)](src/)
![platform](https://img.shields.io/badge/platform-Windows%2010%2F11-lightgrey)

**当前版本 `3.0.0`** · 完全开源（MIT）· 绿色免安装 · Windows 桌面 + 浏览器双形态

---

## 目录

- [30 秒看懂](#30-秒看懂)
- [立即开始玩（不用编译）](#立即开始玩不用编译)
- [游戏里能看到什么 / 怎么玩](#游戏里能看到什么--怎么玩)
- [从源码构建](#从源码构建)
- [跑一个生态实验（命令行）](#跑一个生态实验命令行)
- [生态判据：它到底"健康"吗](#生态判据它到底健康吗)
- [架构与数据流](#架构与数据流)
- [命令行参数全表](#命令行参数全表)
- [HTTP API 全表（实时模式）](#http-api-全表实时模式)
- [构建门禁：8 道检查](#构建门禁8-道检查)
- [目录结构](#目录结构)
- [文档索引](#文档索引)
- [版本控制与发布](#版本控制与发布)
- [常见问题与排错](#常见问题与排错)
- [贡献与许可](#贡献与许可)

---

## 30 秒看懂

### 这个世界在模拟什么

```
      ☀ 阳光（昼夜 628 帧循环，可调）
           │  光合作用（唯一能量入口）
           ▼
      ● 蓝细菌 ────连线──── ● 蓝细菌      ← 生产者：光合、互传能量、分裂、被吃
           │                                  （密度高会互相遮光 = 天然负反馈）
           │  捕食 graze
           ▼
      ◉ 核（消费者 + 唯一决策体）          ← 24 个可遗传基因，会变异、会被淘汰
         ╱ │ ╲
        ○  ○  ○   小球（器官）             ← 资源/侦察/护盾三类，绕核旋转，能量存在球里
```

### 三句话说明它的设计

1. **能量是唯一约束**：每个 tick 都校验 `注入 − 耗散 == 所有活着的实体的能量总和`。
   账不平就当场失败退出（exit 3）。所以"生态好不好"不是调参调出来的假象，是算出来的结果。
2. **四层单向管线**：`环境 → 感知 → 决策 → 行为 → 账本`。决策层看不到世界、不掷骰子；
   只有行为层能改实体状态。这条约束由 `tools/check.ps1` 静态扫源码强制执行。
3. **完全确定性**：同一 seed 跑两次，输出逐字节相同。所以任何"生态变化"都可以精确复现和对比。

---

## 立即开始玩（不用编译）

### 第 1 步：启动

进入 [`release/`](release/) 目录，**双击 `agentsimw.exe`**。

- 不会弹黑框控制台，直接开一个独立应用窗口（Edge `--app` 模式，无浏览器标签栏）。
- 如果已经有实例在跑，双击**不会**再开一个新窗口，而是复用已有的（单实例保护）。
- 关闭窗口 = 退出模拟。

**系统要求**：Windows 10/11 x64，自带 Edge/WebView2 运行时（系统默认就有）。
整个 `release/` 文件夹可以拷贝到任何 Windows 机器直接跑，**绿色免安装**。

### 第 2 步：开一局

1. 在菜单页填种子（seed）、初始核数、蓝细菌数、帧数（或直接用默认）
2. 点 **开始** → 自动连上实时模拟，画面开始动
3. 底部是**四轴环境调控条**，拖动立即生效（免费、无限次）
4. 鼠标**悬停任意核**，弹出它的 24 个基因参数卡；点一下可以钉住，方便对比

### 第 3 步：观察与调控

| 你看到 | 含义 |
| --- | --- |
| 大圆，颜色不同 | 核。**颜色 = hue = 血统**，同色系是近亲 |
| 核中间的灰点 | 交互指示：靠近食物/球时会闪白 |
| 小圆点 + 半透明绿线 | 蓝细菌 + 它们的能量传输网络 |
| 彩色小圆/菱形/六边形 | 小球：资源球 / 侦察球 / 护盾球 |
| HUD 上的「阳光 x.xx」 | 当前光照强度（0~1），画面明暗跟着它变 |
| 右侧曲线 | 核数、蓝细菌、总能量、审计差值、hue 多样性（谱系分化程度） |

**四轴调控**（底部，0~4 五档）：

- **光照**：改阳光基线与昼夜幅度 → 蓝细菌产能上限
- **蓝细菌**：改光合速率与连线传输 → 生产者层的活跃度
- **环境压力**：改代谢成本与衰减率 → 整体生存难度
- **生产者**：改初始节点数与繁殖成本 → 供给规模

> 想看效果：把光照从「中性」拉到「很高」，再看蓝细菌曲线和核的种群反应——这条因果链是实时可见的。

---

## 游戏里能看到什么 / 怎么玩

### 这游戏的目标是什么

**没有通关条件**。它是个"生态沙盘"：你可以把生态逼到崩溃边缘再拉回来，也可以观察
不同决策策略下种群命运如何分化。真正好玩的地方在于**机制之间的耦合**——
改一个参数，影响会沿食物链传导好几层，还常常不是你想的方向。

### 几个值得试的实验

1. **饿死它们**：光照拉到最低，看核群如何在几十帧内萎缩、最后是否灭绝（状态栏会显示"结束原因：灭绝"）
2. **策略对照**：同一个 seed，用 `--policy rule` 和 `--policy bandit` 各跑一遍，核数曲线完全不同
3. **看谱系分化**：跑够长时间，观察右侧 "hue 圆标准差" 曲线——数值越大说明生态位分化越明显
4. **手动干预**：加载存档 → 调一个参数 → 继续跑，比较分叉后的走向

### 界面快捷键

| 键 | 作用 |
| --- | --- |
| `空格` | 播放 / 暂停 |
| `←` `→` | 单步（按住 Shift 一次 10 帧） |
| `R` | 视角复位（回到全图） |
| `F` | 影院模式（隐藏所有面板） |
| `H` | 折叠 / 展开右侧状态栏 |
| `L` | 循环播放开关 |
| 滚轮 / 拖拽 | 缩放 / 平移 |
| `Esc` | 返回 / 退出影院模式 |

---

## 从源码构建

### 前置要求

| 需要 | 说明 |
| --- | --- |
| **Visual Studio 2022** | 必须勾选「使用 C++ 的桌面开发」（含 MSVC v143 + Windows SDK） |
| PowerShell 5.1+ | Windows 自带 |
| Node.js 18+ | **仅**前端 smoke 测试 / 打包 / 生态分析脚本需要，跑模拟器本身不需要 |

### 构建（3 条命令）

```powershell
git clone https://github.com/zx1414/adaptive-orbit-ecosystem.git
cd adaptive-orbit-ecosystem

# 构建三个产物：agentsim.exe（控制台/CLI）、agentsimw.exe（无控制台启动器）、unit_tests.exe
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1
```

**预期输出**（最后几行）：

```text
vcvars: C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
flags: /nologo /std:c++17 /W4 /WX /EHsc /permissive- /utf-8 /I src /DAGENTSIM_HAS_GENOME=1
cl agentsim.exe ...
cl unit_tests.exe ...
build OK
```

产物在 `build\` 下。编译使用 `/W4 /WX`（警告即错误），所以**能编过就等于零警告**。

### 验证构建是否正确

```powershell
# 版本号
build\agentsim.exe --version
# 预期：agentsim 3.0.0

# 金标准回归：这个数字必须一模一样（生态确定性 + 位级未回归）
build\agentsim.exe --no-render --ticks 2000 --seed 42
# 预期最后一行末尾：total_energy=36367.17200929 audit=ok
```

> 如果 `total_energy` 不是这个值，说明默认参数的行为被改动了。若不是有意为之，请查 `git diff`。

### 完整门禁

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1
# 预期最后一行：check PASSED
# 会跑约 5-6 分钟（含 395 条单测 + 前端 smoke + 双跑确定性）
```

或用便携包装（自动把输出存到 `build\gate_out.txt`）：

```powershell
tools\gate.bat
```

---

## 跑一个生态实验（命令行）

### 基本跑法

```powershell
# 5000 帧，seed 42，带每帧能量审计
build\agentsim.exe --no-render --ticks 5000 --seed 42 --audit

# 换成别的决策策略（8 选 1）
build\agentsim.exe --no-render --ticks 5000 --seed 42 --policy bandit

# 自定义世界与初始条件
build\agentsim.exe --no-render --ticks 10000 --seed 7 --nuclei 20 --cyano-nodes 800

# 导出种群/能量趋势 CSV（每 100 帧一行）
build\agentsim.exe --no-render --ticks 20000 --seed 42 --stats build\stats.csv --stats-interval 100
```

**输出最后一行格式**：

```text
done tick=5000 nuclei=42 cyano=731 total_energy=18342.51 audit=ok policy=rule
```

`audit=ok` 表示这一路跑下来能量账一直是平的。若变成 `audit=fail`，进程以 exit 3 退出。

### 8 种决策策略

同样世界、同样 seed，换策略会产生**实质不同**的生态命运：

| 策略 | 决策风格 |
| --- | --- |
| `rule` | 规则驱动：朝最近食物走，饿急了才考虑繁殖（默认，基线） |
| `stub` | 空策略：几乎不动，用于对照实验 |
| `perceptron` | 单层感知机：权重在构造期随机初始化，可随进化漂移 |
| `thermal` | 趋温策略：按温度适宜度 + 失真规避打分 |
| `hysteresis` | 迟滞策略：带状态记忆，避免来回抖动 |
| `foragebank` | 觅食-储蓄双目标：在"吃"和"存"之间权衡 |
| `bandit` | 多臂老虎机：探索/利用权衡 |
| `riskaverse` | 风险规避：偏好安全但收益低的路径 |

### 导出轨迹（给分析脚本用）

```powershell
build\agentsim.exe --no-render --ticks 20000 --seed 42 `
  --trace build\t.json --trace-interval 30 --trace-genome
node tools\analyze.mjs build\t.json
```

`--trace-genome` 会把每个核的 24 个基因一起导出（文件会大很多），
分析脚本的判据 3/4（基因多样性、谱系分化）需要它。

---

## 生态判据：它到底"健康"吗

`tools/analyze.mjs` 对轨迹自动判定 6 条生态判据（测量口径：取轨迹**最后 30%** 窗口，
并带稳态门——若种群仍在趋势中则判 DEFER 而不是硬判 PASS/FAIL）：

| # | 判据 | 通过条件 | 当前状态 |
| --- | --- | --- | --- |
| 1 | 能量收支紧平衡 | 注入−耗散−存量 相对误差 < 0.15 | ✅ 通过（0.1484） |
| 2 | 种群在承载力附近波动 | 不爆炸也不灭绝 | ✅ 通过 |
| 3 | 遗传多样性不塌缩 | 基因维度的 CV 高于阈值 | ✅ 通过 |
| 4 | 谱系 hue 分化 | 色相圆标准差 > 30° | ❌ **未达标**（实测 9.9°） |
| 5 | 捕食者-猎物振荡 | 窗口内相关性与过零点符合 | ✅ 通过（窗口 r=0.018, lag=169） |
| 6 | 调控有效性 | 改光照轴后基因偏好漂移 > 10% | ✅ 通过（+55%） |

> **已知问题**：判据 4 未达标是当前版本最大的缺口。用户已明确"稳态参数后续实现"，
> 因此 v3.0.0 按框架完整性发布，判据 4 的标定列入下个版本路线图。

---

## 架构与数据流

### 四层单向管线

```
┌─────────────┐   ┌─────────────┐   ┌─────────────┐   ┌─────────────┐
│  环境        │──▶│   感知       │──▶│   决策       │──▶│   行为       │
│ Environment │   │ Perception  │   │ Decision    │   │ Behavior    │
│             │   │             │   │             │   │ (唯一写点)   │
│ 蓝细菌/温度  │   │ 24 维观察    │   │ 8 种策略     │   │ 移动/吃/繁殖 │
│ 地形/光照    │   │ 只读、无 RNG │   │ 不读世界     │   │ 产球/死亡    │
└─────────────┘   └─────────────┘   └─────────────┘   └──────┬──────┘
       ▲                                                      │
       │                                                      ▼
       └──────────────────────────────────────────── ┌─────────────┐
                                                      │   账本       │
                                                      │  Ledger     │
                                                      │ 零和恒等式   │
                                                      └─────────────┘
```

**不可违反的三条红线**（由门禁静态检查）：

1. `src/decision` 不能 include `environment/`、`sim/`、`ledger/` —— 决策层看不到世界
2. `src/perception` 不能 include `decision/` —— 感知层不能调策略
3. 除 `src/behavior` 外，任何文件不能给核/球的状态赋值 —— 只有一个写点

### 能量账本

所有能量变动都走 `AgentLedger`，带**原因记账**（`photosynthesis` / `graze` / `locomotion` /
`respiration` / `reproduce-loss` / `deploy` / `recycle-loss` / `carrion` …）。
每个 tick 校验：

```
injected − dissipated == Σ(活着的核 + 球 + 蓝细菌节点)
```

不平就以 exit 3 退出。这条恒等式是整个项目"生态数据可信"的根基。

### /state 二进制快照布局（v2）

```
偏移 0     magic 0x41 ("A")            u8
偏移 1     version = 2                u8
偏移 2     phase: 0运行/1结束/2暂停     u8
偏移 4     tick                       i32
偏移 8     worldW, worldH             f32 × 2
偏移 16    球数, 核数, 存活核数,        i32 × 4
           自由球数, 蓝细菌数, 连线数   i32 × 3
偏移 40    蓝细菌平均能量              f32
偏移 44    全局阳光 sun               f32  ← 前端明暗/HUD 数值来源
偏移 52    结束原因（定长 28B ASCII）  char[28]
偏移 80    球记录   25B × n           id,owner,x,y,energy,type,radius
           核记录  128B × n           32B 头 + 24 个基因 f32
           蓝细菌   20B × n           id,x,y,energy,hue
           连线      8B × n           idA,idB
```

---

## 命令行参数全表

| 参数 | 说明 | 默认 |
| --- | --- | --- |
| `--width W` / `--height H` / `--world-width` / `--world-height` | 环面（torus）世界尺寸 | 1024 × 1024 |
| `--ticks N` | 跑多少帧（不指定 = 永久运行） | 1000 |
| `--seed S` | 确定性随机种子 | 42 |
| `--nuclei N` | 初始核数量 | 12 |
| `--cyano-nodes N` | 初始蓝细菌节点数 | 512 |
| `--cyano-max-nodes N` | 蓝细菌节点数硬上限 | 4096 |
| `--sun-base F` | 阳光基线（0~1） | 0.5 |
| `--sun-amplitude F` | 昼夜正弦幅度 | 0.5 |
| `--policy NAME` | 决策策略（8 种） | rule |
| `--perceptron-seed S` / `--perceptron-scale F` | 感知机策略初始化 | 12345 / 0.1 |
| `--thermal-pref F` / `--thermal-w-thermal F` / `--thermal-w-caution F` | 趋温策略权重 | 0.5 / 1.2 / 0.8 |
| `--terrain-obstacles N` / `--terrain-waters N` / `--terrain-thermal N` | 生成地形斑块 | 0 / 0 / 0 |
| `--agent-mode N` | 行为模式 | 1 |
| `--audit` | 每帧能量审计 | off |
| `--stats PATH` / `--stats-interval N` | 趋势 CSV 输出 | — / 100 |
| `--trace PATH` / `--trace-interval N` | 轨迹 JSON 输出 | — / 1 |
| `--trace-genome` | 轨迹中包含 24 基因 | off |
| `--serve [port]` | HTTP 实时模式 | 8080 |
| `--no-open` | 实时模式下不自动开窗口 | off |
| `--render` / `--no-render` | 控制台字符渲染 | no-render |
| `--help` / `--version` | 帮助 / 版本 | — |

> 不带任何参数启动 = 实时服务模式 + 自动开窗口（双击场景）。

---

## HTTP API 全表（实时模式）

`build\agentsim.exe --serve 8080` 后，所有接口都是 `http://127.0.0.1:8080` + 下面的路径。

| 方法 | 路径 | 用途 |
| --- | --- | --- |
| `GET` | `/status` | 存活探测，返回 `{"ok":true,...}` |
| `GET` | `/state` | **二进制快照**（布局见上），前端主要数据源 |
| `POST` | `/env` | 改环境参数，行式 `axis.light=3` / `fine.sun_base_level=1.35` / `event.harvest=1` |
| `GET` | `/schema` | 参数元数据（113 个可调控键的 min/max/default/说明） |
| `POST` | `/control` | 播放控制：`cmd=pause` / `resume` / `step` |
| `POST` | `/run` | 开新局：`seed=` `nuclei=` `cyano_nodes=` `ticks=` `policy=` `max_fps=` |
| `GET` | `/saves` | 存档列表（JSON） |
| `POST` | `/save` | 存当前世界：`name=` |
| `POST` | `/load` | 读存档：`name=` |
| `POST` | `/rename` | 重命名存档 |
| `POST` | `/delete-save` | 删存档 |
| `GET` | `/timeline` | 时间轴检查点列表 |
| `POST` | `/seek` | 跳到某帧（从最近检查点恢复）|
| `GET` | `/packs` | 材质包列表 + 当前生效配色 |
| `POST` | `/set-packs` | 启用/禁用材质包：`enabled=p1,p2` |

**示例**：

```powershell
# 把光照拉到最高档
curl -X POST http://127.0.0.1:8080/env -d "axis.light=4"

# 开一局新世界，用 bandit 策略
curl -X POST http://127.0.0.1:8080/run -d "seed=7&nuclei=20&cyano_nodes=800&policy=bandit"

# 暂停
curl -X POST http://127.0.0.1:8080/control -d "cmd=pause"
```

---

## 构建门禁：8 道检查

`tools\check.ps1` 是全绿才允许合并的守门人：

| # | 检查 | 内容 |
| --- | --- | --- |
| 1 | `guard: decision includes` | 决策层不得依赖环境/sim/账本 |
| 1b | `guard: no std rng` | 全 src 禁用 `rand()` / `<random>` |
| 1c | `guard: behavior-is-only-writer` | 只有行为层能写实体状态 |
| 1d | `guard: decision has no rng` | 决策层禁用随机数 |
| 2 | `build` | `/W4 /WX` 零警告编译 |
| 3 | `unit_tests` | 395 条单测 / 20873 断言 |
| 3b | `frontend smoke` | 前端 245 条 DOM/解析/渲染断言 |
| 4 | `double-run determinism` | 同 seed 双跑输出逐字节一致 |
| 5 | `live-param × 5` | 5 个关键参数改值后输出必须变化（防死参数） |

---

## 目录结构

```text
adaptive-orbit-ecosystem/
├── src/                     现行版源码（v3.0.0）
│   ├── core/                Vec2(环面) / PCG32 / Config / Genome(24基因)
│   ├── environment/         蓝细菌场（光合/连线/分裂/遮光）/ 温度场 / 地形
│   ├── perception/          24 维感知
│   ├── decision/            8 种决策策略 + 接口
│   ├── behavior/            唯一写点：移动/耗能/产球/繁殖
│   ├── ledger/              能量账本（零和恒等式）
│   ├── sim/                 组合根：主循环 / 审计 / 快照 / 序列化
│   └── app/                 CLI / main / HTTP / 存档 / 时间轴 / 材质包 / 启动器
├── frontend/                Web 前端（原生 JS，零框架）
│   ├── js/                  renderer / main / live-client / charts / parser
│   ├── agentsim.html        单文件 bundle（约 259KB，双击即玩）
│   └── tools/               bundle.mjs 打包器 / smoke.mjs 冒烟测试
├── tests/unit/              395 条单测（30 个测试文件）
├── tools/                   build.ps1 / check.ps1 / gate.bat / rebuild.bat
│                            package.ps1 / analyze.mjs / sweep / evolve
├── docs/                    规格/契约/ADR/实验/日志（45 篇）
├── resourcepacks/           材质包示例
├── release/                 ★ 发布包（可执行 + 前端 + 材质包）
├── legacy/                  ★ 旧版源码（冻结，仅供参考，不参与构建）
│   └── ecocore-v3/          旧 EcoCore v3 全部源码与文档
├── LICENSE                  MIT
├── VERSION                  3.0.0（版本号单一真源）
├── CHANGELOG.md             版本历史与已知问题
└── CONTRIBUTING.md          贡献指南与铁律
```

---

## 文档索引

### 想理解设计

| 文档 | 内容 |
| --- | --- |
| [docs/V2-SPEC.md](docs/V2-SPEC.md) | 原始需求规格 |
| [docs/V2-SPEC-AMEND-1.md](docs/V2-SPEC-AMEND-1.md) | 规格修订（取消影响力/战斗等） |
| [docs/GOAL-ECOLOGY.md](docs/GOAL-ECOLOGY.md) | 生态目标的 6 条可测判据与测量口径 |
| [docs/GOAL.md](docs/GOAL.md) | 成功标准 S1–S25 全表 |
| [docs/ECOLOGY-DESIGN.md](docs/ECOLOGY-DESIGN.md) | 生态学设计依据（自遮光/避难所/营养循环） |
| [docs/DECISIONS/](docs/DECISIONS/) | 架构决策记录 ADR-001~005 |

### 想查契约与参数

| 文档 | 内容 |
| --- | --- |
| [docs/TRACE-CONTRACT.md](docs/TRACE-CONTRACT.md) | 轨迹 JSON v3 契约（冻结） |
| [docs/PARAM-MAPPING.md](docs/PARAM-MAPPING.md) | 113 个实时调控键 → Config 字段映射 |

### 想看证据与历史

| 文档 | 内容 |
| --- | --- |
| [docs/INDEX.md](docs/INDEX.md) | 每条成功标准的**可执行证据**与验证命令 |
| [docs/LOG.md](docs/LOG.md) | 31 轮开发日志（含性能优化、假设证伪的全过程） |
| [docs/experiments/](docs/experiments/) | 八策略对比、感知机进化、趋温策略实验数据 |
| [docs/reviews/](docs/reviews/) | 红队对抗审查报告 |
| [docs/accept/ACC-R11-acceptance.md](docs/accept/ACC-R11-acceptance.md) | R11 验收报告（四投诉定位 + 原版对比） |
| [docs/PLAYBOOK.md](docs/PLAYBOOK.md) | 多代理流水线工作法（可复用的提示词手册） |

---

## 版本控制与发布

### 版本号

- 单一真源：[`VERSION`](VERSION) 文件 + 二进制 `--version` 输出，两者必须一致
- 语义化版本：`MAJOR.MINOR.PATCH`
- 历史 tag `v1.0.0`～`v2.3.1` 属于**旧 EcoCore 代码线**；agentsim 首发为 **v3.0.0**
  （延续谱系的主版本号，避免与旧 tag 重名导致 `git checkout` 歧义）

### 重新生成发布包

```powershell
# 重建 release/ 目录（会自动重打前端 bundle）
powershell -NoProfile -ExecutionPolicy Bypass -File tools\package.ps1

# 同时生成 zip
powershell -NoProfile -ExecutionPolicy Bypass -File tools\package.ps1 -Zip
```

### 旧版源码去哪了

旧 EcoCore v3 的完整源码树（包括它自己的 docs / tests / tools / CMakeLists）已 `git mv` 到
[`legacy/ecocore-v3/`](legacy/ecocore-v3/)，并附 [legacy/README.md](legacy/README.md) 明确标注：
**冻结、只读参考、不参与任何构建**。仓库根的 `tools/build.ps1` 只编译 `src/`，两者完全隔离。

---

## 常见问题与排错

### 双击 agentsimw.exe 没反应 / 秒退

1. 确认 `release/` 下同时有 `agentsim.exe` 和 `agentsimw.exe`（启动器需要同目录的 exe）
2. 确认系统有 Edge / WebView2 运行时（Win10/11 默认有；若被卸载需要装 WebView2 Runtime）
3. 手动跑 `release\agentsim.exe --serve 8080` 看控制台报错（这个版本会显示窗口）

### 画面是黑的 / 没有实体

- 后端没连上时会回退到演示数据；检查 `agentsim.exe --serve` 是否在跑
- 打开浏览器开发者工具看 Console，`/state` 解析失败会打印具体原因
- 世界很大（1024²）而种群小的时候，实体在缩略视图里可能只有几个像素——按 `R` 复位视角，或滚轮放大

### 构建报 `LNK1104: 无法打开文件 unit_tests.exe`

这是**文件被占用**，不是代码错。通常是上一轮测试进程没退干净：

```powershell
taskkill /F /IM unit_tests.exe
taskkill /F /IM agentsim.exe
# 然后重新 build
```

也可能只是杀毒软件正在扫描刚链接出来的 exe，等几秒重试即可。

### 单测出现零星失败（但之前是全绿的）

先检查**是否有多个 unit_tests.exe 同时在跑**——测试在系统临时目录用固定前缀建目录
（`agentsim_rp_test_N`），并发跑会互相踩。杀干净后独占重跑一次。

### `total_energy` 不是 `36367.17200929`

说明默认参数下的行为被改动过。这是有意设计：这个数字是 v3.0.0 的金标准。
如果是有意变更（比如调平衡），请在 PR 里说明并同步更新文档；否则 `git diff src/` 找问题。

### 想让生态"更稳" / "更活跃"

不要改源码里的默认值，用实时调控轴（`/env` 或界面滑杆）动态试。
源码默认值代表一个**经过验证的基线**，改它会让金标准失效。

---

## 贡献与许可

见 [CONTRIBUTING.md](CONTRIBUTING.md)。核心要求：

1. `tools\check.ps1` 必须输出 `check PASSED`
2. 新增行为必须配单测 + 文档证据
3. 前端源码改动后必须重跑 `node frontend\tools\bundle.mjs`
4. 涉及行为/公式变更须说明位级回归影响

**许可**：[MIT](LICENSE) — 可自由使用、修改、商业使用、再分发，只需保留版权声明。

---

*维护者 `zx1414` · R11 交付 2026-11-02 · v3.0.0 开源发布 2026-11-05*