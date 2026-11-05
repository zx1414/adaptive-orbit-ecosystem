# agentsim — 自适应环绕核生态模拟与生存游戏

**一个会自己"长"出生态的模拟器。** 阳光照下来，蓝细菌光合生长、互相连线分配能量、分裂繁殖；
带着可遗传基因的核去觅食、移动、繁衍、变异；小球绕核旋转，把能量存在球里。

**没有剧本，没有预设平衡。** 种群数量、谱系分化、策略兴衰，全部由能量收支和遗传漂变自己决定。
你的角色是调光照、调生产者，然后看这个生态如何回应你。

<img src="docs/images/ui-run.png" alt="agentsim 运行界面：核（按血统着色）、蓝细菌连线网络、小球、右侧实时状态与曲线、底部四轴调控条" width="100%">

<sub>▲ 实际运行界面。大圆=核（颜色=血统 hue）· 小点+绿线=蓝细菌与其能量网络 · 小形状=小球器官 · 右侧=实时状态与生态曲线 · 底部=四轴调控 + 事件卡</sub>

**[⬇ 下载 v3.0.0（Windows x64 免安装，567KB）](https://github.com/zx1414/adaptive-orbit-ecosystem/releases/download/v3.0.0/agentsim-3.0.0-win64.zip)** — 解压后双击 `agentsimw.exe` 即玩

---

## 状态与链接

| | |
| --- | --- |
| **版本** | [`3.0.0`](VERSION) — 首个开源发布 |
| **许可证** | [MIT](LICENSE) — 可商用、可修改、可闭源分发 |
| **平台** | Windows 10 / 11 x64 |
| **测试** | 395 单测 / 20873 断言 / 0 失败 · 8 道门禁 |
| **依赖** | 零第三方库（C++17 标准库 + Win32）；前端原生 JS |
| **仓库** | https://github.com/zx1414/adaptive-orbit-ecosystem |
| **下载** | [GitHub Releases](https://github.com/zx1414/adaptive-orbit-ecosystem/releases/tag/v3.0.0) — 免安装 zip |

---

## 我该看哪里？（按身份选路径）

| 你是… | 从这里开始 |
| --- | --- |
| **只想玩一下** | [立即开始玩](#立即开始玩不用编译) → 双击 `release/agentsimw.exe` |
| **想调参 / 做实验** | [游戏怎么玩](#游戏里能看到什么-怎么玩) + [命令行](#跑一个生态实验命令行) + [参数全表](#命令行参数全表) |
| **想读懂代码** | [架构与数据流](#架构与数据流) → [目录结构](#目录结构) → [`src/`](src/) |
| **想改代码 / 提 PR** | [从源码构建](#从源码构建) → [构建门禁](#构建门禁8-道检查) → [CONTRIBUTING.md](CONTRIBUTING.md) |
| **想接前端 / 写工具** | [HTTP API 全表](#http-api-全表实时模式) + [`/state` 二进制布局](#state-二进制快照布局v2) + [TRACE-CONTRACT](docs/TRACE-CONTRACT.md) |
| **想知道"生态学上成不成立"** | [生态判据](#生态判据它到底健康吗) + [GOAL-ECOLOGY](docs/GOAL-ECOLOGY.md) + [实验报告](docs/experiments/) |
| **想接手维护** | [CHANGELOG](CHANGELOG.md) → [docs/LOG.md](docs/LOG.md) → [docs/PLAYBOOK.md](docs/PLAYBOOK.md) |

---

## 目录

- [30 秒看懂这个项目](#30-秒看懂这个项目)
- [立即开始玩（不用编译）](#立即开始玩不用编译)
- [游戏里能看到什么 / 怎么玩](#游戏里能看到什么-怎么玩)
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

## 30 秒看懂这个项目

### 它在模拟什么

```text
        ☀ 阳光（昼夜 628 帧循环，可实时调）
             │  光合作用 —— 全系统唯一的能量入口
             ▼
        ● 蓝细菌 ──── 连线 ──── ● 蓝细菌
             │                     ↑ 生产者层：光合 / 互传能量 / 分裂 / 被吃
             │                     ↑ 密度高会互相遮光 → 天然负反馈，不会无限爆炸
             │  捕食 graze
             ▼
        ◉ 核（消费者 + 唯一决策体）
         ╱  │  ╲
       ○   ○   ○   ← 小球（器官）：资源 / 侦察 / 护盾，绕核旋转，能量存在球内
```

### 三句话讲清它的设计取舍

1. **能量是唯一硬约束。** 每 tick 校验 `注入 − 耗散 == 所有活着的实体的能量总和`。
   账不平就立即以 exit 3 崩掉。所以"生态好不好"不是调参调出来的观感，是算出来的结果。
2. **四层单向管线。** `环境 → 感知 → 决策 → 行为 → 账本`。决策层看不到世界、不掷骰子；
   只有行为层能改实体状态。这条约束由 `tools/check.ps1` **静态扫源码**强制执行，违反即构建失败。
3. **完全确定性。** 同一个 seed 跑两次，输出逐字节相同。所以任何"生态演变"都能精确复现、
   精确前后对比——这是能拿它做实验而不是做玩具的前提。

---

## 立即开始玩（不用编译）

### 第 1 步：启动

打开 [`release/`](release/) 文件夹，**双击 `agentsimw.exe`**。

| 会发生什么 | 说明 |
| --- | --- |
| 不弹黑框控制台 | 启动器是无窗口的 Win32 程序 |
| 直接开一个独立应用窗口 | 走 Edge `--app` 模式，没有浏览器标签栏 / 地址栏 |
| 已有实例时不再开新窗口 | 单实例保护：会复用已有窗口并让本进程退出 |
| 关闭窗口 = 退出 | 后台 `agentsim.exe` 随之结束 |

**系统要求**：Windows 10/11 x64，自带 Edge / WebView2 运行时（系统默认就有）。
整个 `release/` 文件夹可以拷到任何 Windows 机器直接跑，**绿色免安装**。

> ⚠️ 不要单独移动 `agentsim.exe` 或 `agentsimw.exe` 中的某一个——启动器需要同目录的 exe。
> `release/frontend/` 也必须与两个 exe 保持同级。

### 第 2 步：开一局

<img src="docs/images/ui-menu.png" alt="agentsim 菜单页：种子、初始核数、蓝细菌节点数、帧数、帧率上限、决策策略" width="100%">

<sub>▲ 菜单页。填种子、初始核数、蓝细菌节点数、帧数、帧率上限、决策策略，然后点「开始模拟」。</sub>

1. 各项参数都有合理默认值，**可以直接点「开始模拟」**
2. 想换风格就改「决策策略」下拉（8 种，见[下文](#8-种决策策略)）
3. 开跑后自动连上实时模拟，画面开始动

### 第 3 步：观察与调控

运行界面分四块，从外到内：

| 位置 | 是什么 | 你能做什么 |
| --- | --- | --- |
| **中间画布** | 生态本体 | 滚轮缩放、拖拽平移、悬停核看基因、点核跟随 |
| **右侧状态栏** | 实时数字 + 5 条曲线 | 读种群/能量/审计/谱系多样性 |
| **底部四轴** | 光照 / 蓝细菌 / 环境压力 / 生产者，各 0~4 五档 | **拖动立即生效，免费无限次** |
| **底部时间轴** | 播放 / 暂停 / 单步 / 倍速 / 循环 | 控制时间推进 |

**画面里每个元素代表什么**：

| 你看到 | 含义 |
| --- | --- |
| 大圆，颜色各异 | **核**。颜色 = hue = **血统**，同色系即近亲 |
| 大圆中间的灰点 | 交互指示：靠近食物/球时会闪白 |
| 小圆点 + 半透明绿线 | **蓝细菌** + 它们的能量传输网络 |
| 圆形 / 菱形 / 六边形的小东西 | **小球**：资源球 / 侦察球 / 护盾球 |
| 空心 + 虚线外圈的小球 | **自由球**（无主，可以被任何核捡走） |
| HUD 上的「阳光 x.xx」 | 当前光照强度（0~1），画面明暗跟着它变 |
| 右上角「能量审计 PASS」 | 每帧恒等式校验通过 |

---

## 游戏里能看到什么 / 怎么玩

### 这游戏的"目标"是什么

**没有通关条件。** 它是个**生态沙盘**：你可以把生态逼到崩溃边缘再拉回来，
也可以观察不同决策策略下种群命运的彻底分化。

真正好玩的地方在于**机制之间的耦合**：改一个参数，影响会沿食物链传导好几层，
而且常常不是你想的方向——这才是"自涌现"的意思。

### 几个值得试的实验

1. **饿死它们**：光照拉到最低档，看核群如何在几十帧内萎缩、最后是否灭绝
   （状态栏会显示「结束原因：灭绝」）
2. **策略对照**：同一个 seed，分别用 `rule` 和 `bandit` 各跑一遍，核数曲线完全不同
3. **看谱系分化**：跑够长时间，盯右侧「谱系多样性（hue 圆标准差）」曲线——
   数值越大说明生态位分化越明显
4. **分叉实验**：存一个档 → 改一个参数 → 继续跑，比较两条世界线的走向
5. **手动干预**：用底部「事件卡」（寒冬 / 繁华 / 收获 / 瘟疫）砸一个冲击进去，看恢复力

### 界面快捷键

| 键 | 作用 |
| --- | --- |
| `空格` | 播放 / 暂停 |
| `←` `→` | 单步（按住 `Shift` 一次 10 帧） |
| `R` | 视角复位（回到全图） |
| `F` | 影院模式（隐藏所有面板，纯看生态） |
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
| Node.js 18+ | **仅**前端 smoke / 打包 / 生态分析脚本需要；跑模拟器本身不需要 |

### 构建（3 条命令）

```powershell
git clone https://github.com/zx1414/adaptive-orbit-ecosystem.git
cd adaptive-orbit-ecosystem

# 一次构建出三个产物：
#   agentsim.exe   控制台/CLI 主程序
#   agentsimw.exe  无控制台启动器（双击用）
#   unit_tests.exe 单元测试
powershell -NoProfile -ExecutionPolicy Bypass -File tools\build.ps1
```

**预期输出**（最后几行）：

```text
vcvars: C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat
flags: /nologo /std:c++17 /W4 /WX /EHsc /permissive- /utf-8 /I src /DAGENTSIM_HAS_GENOME=1
cl agentsim.exe ...
cl unit_tests.exe ...
cl agentsimw.exe ...
build OK
```

编译用 `/W4 /WX`（**警告即错误**），所以能编过就等于零警告。

### 验证构建是否正确

```powershell
# 1) 版本号
build\agentsim.exe --version
```
```text
agentsim 3.0.0
```

```powershell
# 2) 金标准回归：这个数字必须一模一样
build\agentsim.exe --no-render --ticks 2000 --seed 42
```
```text
done tick=2000 nuclei=3 cyano=930 total_energy=36367.17200929 audit=ok policy=rule
                                              ^^^^^^^^^^^^^^^^^^^^^ 这就是金标准
```

> **`total_energy` 不是 `36367.17200929` 说明默认参数下的行为被改动了。**
> 这是有意设计——它让"无意中改了生态"这件事无处藏身。若非有意为之，请查 `git diff src/`。

### 完整门禁

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\check.ps1
```
```text
...（前略）
[PASS] live-param: --thermal-w-caution
check PASSED          ← 这就是通过标志
```

约 5–6 分钟（395 条单测 + 前端 smoke + 双跑确定性 + 5 个参数探针）。

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
     ^^^^帧数  ^^核数  ^^^蓝细菌数  ^^^^^^^^总能量    ^^^^审计  ^^^^所用策略
```

`audit=ok` 表示全程能量账都是平的。若变成 `audit=fail`，进程会以 **exit 3** 退出。

### 8 种决策策略

同样世界、同样 seed（`--ticks 1000 --seed 42`），换策略会产生**实质不同**的生态命运。
下面是**实测**结果（2026-11-05，v3.0.0 二进制）：

| 策略 | 1000 帧后核数 | 状态 |
| --- | --- | --- |
| `foragebank` | **13** | 存活（最繁盛） |
| `thermal` | 9 | 存活 |
| `stub` | 7 | 存活（几乎不动却活得不错） |
| `hysteresis` | 6 | 存活 |
| `bandit` | 6 | 存活 |
| `riskaverse` | 6 | 存活 |
| `rule` | 3 | 存活（默认策略，最保守） |
| `perceptron` | **灭绝** | 个体全灭 → 进程 exit 5 |

> 这就是"策略真的在起作用"的直接证据：**同一世界、同一随机种子，只因决策方式不同，
> 结局从"13 个核繁荣"到"全灭退出"**。`perceptron` 灭绝不是 bug——它展示了
> 一个未被优化的随机初始化网络在能量约束下必然饿死，正是自然选择该淘汰的类型。

各策略的决策风格：

| 策略 | 决策风格 |
| --- | --- |
| `rule` | 规则驱动：朝最近食物走，饿急了才考虑繁殖（**默认**，基线） |
| `stub` | 空策略：几乎不动，用于对照实验 |
| `perceptron` | 单层感知机：权重构造期随机初始化，可随进化漂移 |
| `thermal` | 趋温策略：按温度适宜度 + 失真规避打分 |
| `hysteresis` | 迟滞策略：带状态记忆，避免来回抖动 |
| `foragebank` | 觅食-储蓄双目标：在"现在吃"和"存着"之间权衡 |
| `bandit` | 多臂老虎机：探索 / 利用权衡 |
| `riskaverse` | 风险规避：偏好安全但收益低的路径 |

### 导出轨迹（给分析脚本用）

```powershell
build\agentsim.exe --no-render --ticks 20000 --seed 42 `
  --trace build\t.json --trace-interval 30 --trace-genome
node tools\analyze.mjs build\t.json
```

`--trace-genome` 会把每个核的 24 个基因一起导出（文件大很多），
分析脚本的判据 3/4（基因多样性、谱系分化）需要它。

---

## 生态判据：它到底"健康"吗

`tools/analyze.mjs` 对轨迹自动判定 6 条生态判据。测量口径：取轨迹**最后 30%** 窗口，
并带**稳态门**——若种群仍在趋势中（未达稳态），判 `DEFER` 而不是硬判 PASS/FAIL。

| # | 判据 | 通过条件 | 当前状态（seed 42） |
| --- | --- | --- | --- |
| 1 | 能量收支紧平衡 | 注入−耗散−存量 相对误差 < 0.15 | ✅ 通过（0.1484） |
| 2 | 种群在承载力附近波动 | 不爆炸、不灭绝 | ✅ 通过 |
| 3 | 遗传多样性不塌缩 | 各基因维度 CV 高于阈值 | ✅ 通过 |
| 4 | 谱系 hue 分化 | 色相圆标准差 > 30° | ❌ **未达标**（实测 9.9°） |
| 5 | 捕食者-猎物振荡 | 窗口内相关性与过零点符合 | ✅ 通过（r=0.018, lag=169） |
| 6 | 调控有效性 | 改光照轴后基因偏好漂移 > 10% | ✅ 通过（+55%） |

> **已知缺口**：判据 4 未达标是当前版本最大的问题。
> 用户已明确「稳态参数后续实现」，因此 v3.0.0 按**框架完整性**发布，
> 判据 4 的标定列入下个版本路线图（见 [CHANGELOG](CHANGELOG.md)）。

---

## 架构与数据流

### 四层单向管线

```text
┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐
│   环境        │───▶│    感知       │───▶│    决策       │───▶│    行为       │
│ Environment  │    │ Perception   │    │ Decision     │    │ Behavior     │
│              │    │              │    │              │    │ (唯一写点)    │
│ 蓝细菌 / 光照 │    │ 24 维观察     │    │ 8 种策略      │    │ 移动 / 吃 /   │
│ 温度 / 地形   │    │ 只读、无 RNG  │    │ 不读世界      │    │ 繁殖 / 产球   │
└──────────────┘    └──────────────┘    └──────────────┘    └──────┬───────┘
       ▲                                                          │
       │                                                          ▼
       └──────────────────────────────────────────────── ┌──────────────┐
                                                          │    账本       │
                                                          │   Ledger     │
                                                          │  零和恒等式   │
                                                          └──────────────┘
```

**三条不可违反的红线**（由 `tools/check.ps1` 静态检查，违反即构建失败）：

| # | 红线 | 为什么 |
| --- | --- | --- |
| 1 | `src/decision` 不能 include `environment/`、`sim/`、`ledger/` | 决策层看不到世界，才能保持"纯函数式决策" |
| 2 | `src/perception` 不能 include `decision/` | 感知层不能调策略，才能保证"观察 ≠ 行动" |
| 3 | 除 `src/behavior` 外，任何文件不能给核/球状态赋值 | 只有一个写点，才可能审计能量去向 |

### 能量账本

所有能量变动都走 `AgentLedger`，并带**原因记账**：

`photosynthesis` · `graze` · `locomotion` · `respiration` · `reproduce-loss` ·
`deploy` · `recycle-loss` · `carrion` · `detach` · `force`

每个 tick 校验：

```text
injected − dissipated == Σ(活着的核 + 球 + 蓝细菌节点)
```

不平就以 **exit 3** 退出。这条恒等式是整个项目"生态数据可信"的根基。

### `/state` 二进制快照布局（v2）

实时模式下前端 33ms 拉一次，这是前端唯一的数据源：

```text
偏移  0   magic 0x41 ("A")          u8
偏移  1   version = 2                u8
偏移  2   phase: 0运行/1结束/2暂停     u8
偏移  3   保留                       u8
偏移  4   tick                       i32
偏移  8   worldW, worldH             f32 × 2
偏移 16   ballCount                  i32
偏移 20   nucleusCount               i32
偏移 24   aliveNuclei                i32
偏移 28   freeBallCount              i32
偏移 32   cyanoCount                 i32
偏移 36   linkCount                  i32
偏移 40   cyanoAvgEnergy             f32
偏移 44   全局阳光 sun               f32   ← 前端明暗 + HUD「阳光 x.xx」的来源
偏移 48   保留                       i32
偏移 52   finishReason（定长 28B）    char[28]  "extinct" / "max_ticks" / "paused" / "running"
偏移 80   球记录    25B × n          id u32, owner u32, x f32, y f32, energy f32, type u8, radius f32
          核记录   128B × n          32B 头（id, x, y, energy, cap, speed, hue, alive）+ 24 基因 f32
          蓝细菌    20B × n          id u32, x f32, y f32, energy f32, hue f32
          连线       8B × n          idA u32, idB u32
```

解析权威实现见 [`frontend/js/live-client.js`](frontend/js/live-client.js)（带 stride 自检，版本感知）。

---

## 命令行参数全表

| 参数 | 说明 | 默认 |
| --- | --- | --- |
| `--width W` / `--height H` | 环面世界尺寸 | 1024 × 1024 |
| `--world-width W` / `--world-height H` | 同上（别名） | — |
| `--ticks N` | 跑多少帧（不指定 = 永久运行） | 1000 |
| `--seed S` | 确定性随机种子 | 42 |
| `--nuclei N` | 初始核数量 | 12 |
| `--cyano-nodes N` | 初始蓝细菌节点数 | 512 |
| `--cyano-max-nodes N` | 蓝细菌节点数硬上限（性能护栏） | 4096 |
| `--sun-base F` | 阳光基线（0~1） | 0.5 |
| `--sun-amplitude F` | 昼夜正弦幅度 | 0.5 |
| `--policy NAME` | 决策策略（8 种，见上） | `rule` |
| `--perceptron-seed S` / `--perceptron-scale F` | 感知机策略初始化 | 12345 / 0.1 |
| `--thermal-pref F` | 趋温策略最适温度 | 0.5 |
| `--thermal-w-thermal F` / `--thermal-w-caution F` | 趋温策略两项权重 | 1.2 / 0.8 |
| `--terrain-obstacles N` | 生成障碍斑块数 | 0 |
| `--terrain-waters N` | 生成水域斑块数 | 0 |
| `--terrain-thermal N` | 生成温度区斑块数 | 0 |
| `--agent-mode N` | 行为模式 | 1 |
| `--audit` | 每帧能量审计（建议总是开） | off |
| `--stats PATH` | 趋势 CSV 输出路径 | — |
| `--stats-interval N` | 趋势采样间隔 | 100 |
| `--trace PATH` | 轨迹 JSON 输出路径 | — |
| `--trace-interval N` | 轨迹采样间隔 | 1 |
| `--trace-genome` | 轨迹中包含 24 基因（体积大） | off |
| `--serve [port]` | HTTP 实时模式 | 8080 |
| `--no-open` | 实时模式下不自动开窗口 | off |
| `--render` / `--no-render` | 控制台字符渲染 | `--no-render` |
| `--help` / `--version` | 帮助 / 版本 | — |

> **不带任何参数启动 = 实时服务模式 + 自动开窗口**（这就是双击场景）。

---

## HTTP API 全表（实时模式）

`build\agentsim.exe --serve 8080` 之后，全部接口都在 `http://127.0.0.1:8080` 下：

| 方法 | 路径 | 用途 |
| --- | --- | --- |
| `GET` | `/status` | 存活探测 → `{"ok":true,...}` |
| `GET` | `/state` | **二进制快照**（布局见上），前端主数据源 |
| `POST` | `/env` | 改环境参数。行式：`axis.light=3` / `fine.sun_base_level=1.35` / `event.harvest=1` |
| `GET` | `/schema` | 参数元数据（113 个可调控键的 min/max/default/说明） |
| `POST` | `/control` | 播放控制：`cmd=pause` / `cmd=resume` / `cmd=step` |
| `POST` | `/run` | 开新局：`seed=` `nuclei=` `cyano_nodes=` `ticks=` `policy=` `max_fps=` |
| `GET` | `/saves` | 存档列表（JSON） |
| `POST` | `/save` | 存当前世界：`name=` |
| `POST` | `/load` | 读存档：`name=` |
| `POST` | `/rename` | 重命名存档 |
| `POST` | `/delete-save` | 删存档 |
| `GET` | `/timeline` | 时间轴检查点列表 |
| `POST` | `/seek` | 跳到某帧（从最近检查点恢复） |
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

| # | 检查项 | 内容 |
| --- | --- | --- |
| 1 | `guard: decision includes` | 决策层不得依赖环境 / sim / 账本 |
| 1b | `guard: no std rng` | 全 `src/` 禁用 `rand()` / `<random>` / `random_device` |
| 1c | `guard: behavior-is-only-writer` | 只有行为层能写实体状态（含数组下标形态） |
| 1d | `guard: decision has no rng` | 决策层禁用随机数 |
| 2 | `build` | `/W4 /WX` 零警告编译 |
| 3 | `unit_tests` | 395 条单测 / 20873 断言 |
| 3b | `frontend smoke` | 前端 245 条 DOM / 解析 / 渲染断言 |
| 4 | `double-run determinism` | 同 seed 双跑输出逐字节一致 |
| 5 | `live-param × 5` | 5 个关键参数改值后输出必须变化（防死参数） |

---

## 目录结构

```text
adaptive-orbit-ecosystem/
├── src/                     现行版源码（v3.0.0）
│   ├── core/                Vec2(环面) / PCG32 / Config / Genome(24 基因)
│   ├── environment/         蓝细菌场（光合/连线/分裂/自遮光）/ 温度场 / 地形
│   ├── perception/          24 维感知（侦察球决定感知半径）
│   ├── decision/            8 种决策策略 + IDecisionPolicy 接口
│   ├── behavior/            唯一写点：移动 / 耗能 / 产球 / 繁殖
│   ├── ledger/              能量账本（零和恒等式 + 按原因记账）
│   ├── sim/                 组合根：主循环 / 审计 / 快照 / 存档序列化
│   └── app/                 CLI / main / HTTP / 存档 / 时间轴 / 材质包 / 启动器
│
├── frontend/                Web 前端（原生 JS，零框架）
│   ├── js/                  renderer / main / live-client / charts / parser
│   ├── agentsim.html        单文件 bundle（约 259KB，双击即玩）
│   └── tools/               bundle.mjs 打包器 / smoke.mjs 冒烟测试
│
├── tests/unit/              395 条单测（30 个测试文件）
├── tools/                   build.ps1 / check.ps1 / gate.bat / rebuild.bat
│                            package.ps1 / analyze.mjs / sweep / evolve
├── docs/                    规格 / 契约 / ADR / 实验 / 日志（45 篇）
│   └── images/              README 用的界面截图
├── resourcepacks/           材质包示例（classic_flat）
│
├── release/                 ★ 发布包（拷走就能玩）
│   ├── agentsim.exe             CLI / 服务主程序
│   ├── agentsimw.exe            无控制台启动器（双击这个）
│   ├── frontend/agentsim.html   单文件前端
│   └── README.txt               包内说明
│
├── legacy/                  ★ 旧版源码（冻结，仅供参考，不参与构建）
│   ├── README.md                为什么在这、能不能用
│   └── ecocore-v3/              旧 EcoCore v3 完整源码树
│
├── LICENSE                  MIT
├── VERSION                  3.0.0（版本号单一真源）
├── CHANGELOG.md             版本历史与已知问题
├── CONTRIBUTING.md          贡献指南与五条铁律
└── README.md                本文件
```

---

## 文档索引

### 想理解设计

| 文档 | 内容 |
| --- | --- |
| [docs/V2-SPEC.md](docs/V2-SPEC.md) | 原始需求规格 |
| [docs/V2-SPEC-AMEND-1.md](docs/V2-SPEC-AMEND-1.md) | 规格修订（取消影响力经济 / 核战斗等） |
| [docs/GOAL-ECOLOGY.md](docs/GOAL-ECOLOGY.md) | 生态目标的 6 条可测判据与测量口径 |
| [docs/ECOLOGY-DESIGN.md](docs/ECOLOGY-DESIGN.md) | 生态学设计依据（自遮光 / 捕食避难所 / 营养循环） |
| [docs/GOAL.md](docs/GOAL.md) | 成功标准 S1–S25 全表 |
| [docs/DECISIONS/](docs/DECISIONS/) | 架构决策记录 ADR-001 ~ 005 |

### 想查契约与参数

| 文档 | 内容 |
| --- | --- |
| [docs/TRACE-CONTRACT.md](docs/TRACE-CONTRACT.md) | 轨迹 JSON v3 契约（冻结） |
| [docs/PARAM-MAPPING.md](docs/PARAM-MAPPING.md) | 113 个实时调控键 → Config 字段映射 |

### 想看证据与历史

| 文档 | 内容 |
| --- | --- |
| [docs/INDEX.md](docs/INDEX.md) | 每条成功标准的**可执行证据**与验证命令 |
| [docs/LOG.md](docs/LOG.md) | 31 轮开发日志（含性能优化、假设证伪全过程） |
| [docs/experiments/](docs/experiments/) | 八策略对比 / 感知机进化 / 趋温策略实验数据 |
| [docs/reviews/](docs/reviews/) | 红队对抗审查报告 |
| [docs/accept/ACC-R11-acceptance.md](docs/accept/ACC-R11-acceptance.md) | R11 验收报告（四项问题定位 + 原版对比） |
| [docs/PLAYBOOK.md](docs/PLAYBOOK.md) | 多代理流水线工作法（可复用提示词手册） |
| [CHANGELOG.md](CHANGELOG.md) | 版本历史与已知问题 |

---

## 版本控制与发布

### 版本号规则

- **单一真源**：[`VERSION`](VERSION) 文件 + 二进制 `--version` 输出，两者必须一致
- 语义化版本：`MAJOR.MINOR.PATCH`
- 历史 tag `v1.0.0`～`v2.3.1` 属于**旧 EcoCore 代码线**（源码在 `legacy/`）；
  agentsim 首发取 **v3.0.0**（延续谱系的下一个主版本），避免与旧 tag 重名导致 `git checkout` 歧义

### 重新生成发布包

```powershell
# 重建 release/（会自动重打前端 bundle）
powershell -NoProfile -ExecutionPolicy Bypass -File tools\package.ps1

# 同时生成 zip
powershell -NoProfile -ExecutionPolicy Bypass -File tools\package.ps1 -Zip
```

### 旧版源码去哪了

旧 EcoCore v3 的完整源码树（含它自己的 docs / tests / tools / CMakeLists）已移到
[`legacy/ecocore-v3/`](legacy/ecocore-v3/)，并在 [legacy/README.md](legacy/README.md) 明确标注：

> **冻结 · 只读参考 · 不参与任何构建**

仓库根的 `tools/build.ps1` 只编译 `src/`，与 `legacy/` 完全隔离。旧版生态模型
（静止生产者 + 独立 Agent 追食物）已被 v3.0.0 的四层管线取代，其平衡数值文档已作废。

---

## 常见问题与排错

### 双击 agentsimw.exe 没反应 / 秒退

1. 确认 `release/` 下**同时**有 `agentsim.exe` 与 `agentsimw.exe`（启动器需要同目录的 exe）
2. 确认系统有 Edge / WebView2 运行时（Win10/11 默认有；若被卸载需装 WebView2 Runtime）
3. 手动运行 `release\agentsim.exe --serve 8080` 看控制台报错

### 画面是黑的 / 看不到实体

- 后端没连上时会回退到演示数据；检查 `agentsim.exe --serve` 是否在跑
- 世界很大（1024²）而种群很小时，实体在缩略视图里只有几个像素
  → 按 `R` 复位视角，或滚轮放大
- 打开浏览器开发者工具看 Console，`/state` 解析失败会打印具体原因

### 构建报 `LNK1104: 无法打开文件 unit_tests.exe`

这是**文件被占用**，不是代码错。通常是上一轮测试进程没退干净：

```powershell
taskkill /F /IM unit_tests.exe
taskkill /F /IM agentsim.exe
tools\rebuild.bat
```

也可能只是杀毒软件正在扫描刚链接出来的 exe，等几秒重试即可。

### 单测出现零星失败（但之前是全绿的）

先检查**是否有多个 `unit_tests.exe` 同时在跑**——测试会在系统临时目录建固定前缀目录，
并发运行会互相踩。杀干净后独占重跑一次：

```powershell
taskkill /F /IM unit_tests.exe
build\unit_tests.exe
```

### `total_energy` 不是 `36367.17200929`

说明默认参数下的行为被改动过。这是**有意设计**：这个数字是 v3.0.0 的金标准。
若是有意变更（比如调平衡），请在 PR 里说明并同步更新文档；否则用 `git diff src/` 找问题。

### 想让生态「更稳」/「更活跃」

**不要改源码里的默认值**，用实时调控轴（`/env` 或界面滑杆）动态试。
源码默认值代表一个经过验证的基线，改它会让金标准失效。

---

## 贡献与许可

见 [CONTRIBUTING.md](CONTRIBUTING.md)。核心要求：

1. `tools\check.ps1` 必须输出 `check PASSED`
2. 新增行为必须配单测 + 文档证据
3. 前端源码改动后必须重跑 `node frontend\tools\bundle.mjs`
4. 涉及行为 / 公式变更须说明位级回归影响

**许可**：[MIT](LICENSE) — 可自由使用、修改、商业使用、再分发，只需保留版权声明。

---

*维护者 [zx1414](https://github.com/zx1414) · R11 交付 2026-11-02 · v3.0.0 开源发布 2026-11-05*