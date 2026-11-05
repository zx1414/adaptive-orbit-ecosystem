# SWEEP —— 生态判据分析器与参数扫描工具使用说明

> 任务卡 R2-D 交付。两个工具配合使用：`analyze.mjs` 回答"这组参数下的生态达不达标"，
> `sweep.ps1` 把这个问题批量地问一遍并汇总成 CSV。
> 判据定义: `docs/GOAL-ECOLOGY.md` §一（6 条硬判据）。
> 调参方法论: `docs/ECOLOGY-DESIGN.md`「调参方法论（不许手调）」——**用本工具的数据定常数，不许试错手感**。

## 一、analyze.mjs —— 6 条判据自动判定

```powershell
node tools/analyze.mjs <trace.json> [--out report.md] [--json [path]]
                      [--baseline <trace2.json>] [--genome <genome.json>]
                      [--out-genome <path>] [--help] [--version]
```

输入是 `agentsim --trace` 导出的 JSON（契约 v3；v2 也兼容读取）：

```powershell
# 1. 跑一次模拟导出 trace
.uildagentsim.exe --no-render --ticks 5000 --seed 42 --trace build/t.json --trace-interval 20

# 2. 出报告
node tools/analyze.mjs build/t.json --out build/report.md
```

### 判据表

| # | 判据 | 判定式 | 数据来源 | 本轮状态 |
|---|------|--------|----------|----------|
| 1 | 能量紧平衡 | \|slope(totalEnergy)\| × ticks / mean(total) < 0.15 | series | ✅ 可算 |
| 2 | 种群波动 | 末 20% tick 的核数 CV ∈ [0.05, 0.6] 且末态核数 > 0 | series | ✅ 可算 |
| 3 | 基因多样性 | 24 参数中 ≥18 个末态 std > 初态 std × 0.3 | genome | ⛔ SKIP（需 --trace-genome） |
| 4 | hue 发散 | hue 圆标准差 > 30°（圆统计） | genome | ⛔ SKIP（需 --trace-genome） |
| 5 | 捕食者-猎物 | corr(nuclei, cyano) < -0.2 或互相关 lag 1..200 有过零 | series | ✅ 可算 |
| 6 | 调控有效 | 两组运行的 cyanoPreference / maxSpeed 平均变化 > 10% | genome ×2 | ⛔ SKIP（需 genome + baseline） |

### 降级规则（重要）

- trace 里**没有 genome 数据**时，判据 3/4/6 输出 **SKIP（不是 FAIL）**，报告里会明确写
  "需要主代理接线 --trace-genome"。SKIP 是"数据缺失"，不是"生态失败"。
- 判据 6 需要第二次运行做对照：`--baseline <trace2>`。典型用法是
  "light 轴调满 vs 默认" 或 "pressure 轴调满 vs 默认" 两个 trace 各跑一次。
- v1/v2 旧文件没有 `cyanoAlive` 键时判据 5 SKIP（无从比较）。

### genome 数据从哪来（判据 3/4/6 的钥匙）

analyze.mjs 按以下优先级找 genome，找到即自动启用判据 3/4/6：

1. 显式 `--genome <file>`；
2. trace 同名旁车文件 `<trace>.genome.json`（格式见下）；
3. trace frames 内嵌键 `g`（v3 契约：`[[id, p0..p23, hue], ...]`，26 个数/核，
   参数顺序 = `src/core/Genome.h` 字段声明序，hue 在最后）。

内嵌 `g` 出现时自动写出旁车文件（`--out-genome` 可改路径），供二次分析免重跑。
旁车文件格式：`{schemaVersion:1, genomeTrace:true, samples:[{tick, genomes:[{id, hue, params:[24]}]}]}`。

> **需要主代理接线: --trace-genome**（2026-11-01 时点主项目尚未实现该导出；
> 导出格式主代理已定稿为帧键 g=26 项/核，本工具已按该格式实现，等数据到位即可出判据 3/4/6。）

### 退出码

| 码 | 含义 |
|----|------|
| 0 | 无 FAIL（SKIP 不影响） |
| 1 | 至少一条判据 FAIL |
| 2 | 用法 / IO / 解析错误 |

### 输出内容（report.md）

1. **判据总表**（6 条，PASS/FAIL/SKIP + 实测值 + 阈值 + 说明）
2. **时间序列**：核数 / 蓝细菌数 / 总能量三线图（内联 SVG，零依赖）
3. **能量收支**表：斜率、注入、耗散、净变化、auditOk 违规帧数
4. **种群波动**细节（判据 2 的末 20% 窗口图）
5. **捕食者-猎物相位**（判据 5 的互相关曲线）
6. **基因多样性**：24 参数各自的初态/末态 std、保持率、逐条迷你折线（判据 3）
7. **hue 圆标准差**随时间（判据 4）
8. **末态散点** maxSpeed × energyThreshold，点色 = hue（判据 4b 的"分群"检查材料）
9. **策略聚类**：k-means(k=3) 主基因标准化聚类，各簇大小与中心
10. **调控对比**（判据 6，给了 --baseline 时）
11. **方法说明**（每个统计量的口径，防止"指标定义打架"）

`--json` 输出同样内容的机器可读版（sweep.ps1 就是用它取 verdict 的）。

## 二、sweep.ps1 —— 参数扫描

```powershell
# 单参数扫描
.	oolssweep.ps1 -Param "nuclei" -Values 8,12,24 -Ticks 20000 -Seeds 42,7,123

# 二维网格
.	oolssweep.ps1 -Grid @{ "nuclei"=8,12; "food"=32,64 } -Ticks 10000

# 指定 exe（默认 buildagentsim.exe；可用主项目或其它 worktree 的 exe）
.	oolssweep.ps1 -Param "nuclei" -Values 8,16 -Exe "D:ScuStuQagentsimuildagentsim.exe"
```

### 参数

| 参数 | 说明 |
|------|------|
| `-Param <名> -Values <列表>` | 单参数扫描 |
| `-Grid @{ k=v1,v2; ... }` | 多参数笛卡尔积 |
| `-Ticks <N>` | 每次运行 tick 数（默认 20000） |
| `-Seeds <列表>` | 每组参数跑哪些种子（默认 42） |
| `-Exe <路径>` | 覆盖默认 exe |
| `-KeepArtifacts` | 保留每次运行的 trace/stats/verdict 中间文件 |
| `-ExtraArgs <数组>` | 附加固定 CLI 参数（如 `--policy bandit`） |

执行方式：**串行**（不并行，避免干扰计时与内存）。每组组合 × 每种子跑一次
agentsim + 一次 analyze.mjs（取 `--json` 的判据汇总做 verdict）。

### 当前可扫描的参数（CLI 白名单）

sweep 只会扫**当前构建里真实生效**的参数（宁可少跑也不静默假跑）：

| schema 键 | CLI 通道 | 状态 |
|-----------|----------|------|
| `nuclei` | `--nuclei` | ✅ 可扫 |
| `food` / `balls` | `--food` | ✅ 可扫（自由球启动库存） |
| `policy` | `--policy` | ✅ 可扫 |
| `width` / `height` | `--width`/`--height` | ✅ 可扫 |
| `seed` | `-Seeds` 参数 | ✅ 走种子列表 |
| `sun_base_level` `cyano_nodes` `cyano_photosynthesis_rate` `basal_cost` 等 | **无** | ⛔ 未接线，传了会警告并跳过 |

**需要主代理接线**：ParamSchema 里的生态键（sun 系、cyano 系、pressure 系）接上 CLI
后，在 sweep.ps1 的 `$script:Wired` 表加一行映射即可，其余逻辑不用动。

### CSV 列

`<参数各列>,seed,nuclei_final,total_energy_final,energy_slope,cyano_final,nuclei_cv,alive,exit_code,verdict`

- `energy_slope`：`--stats` 导出的总能量序列的线性回归斜率（每 tick）
- `nuclei_cv`：末 20% tick 核数的标准差/均值（与判据 2 同口径）
- `exit_code`：0=正常 5=灭绝 -1=参数未接线 -2=运行异常；2/3/4=参数错/审计失败/IO
- `verdict`：`PASS`（判据无 FAIL）/ `FAIL`（有 FAIL）/ `RUN_ERROR` / `SKIP_PARAM_NOT_WIRED` / `ANALYZE_ERROR`

**verdict 语义警告**：PASS 只表示"当前判据集内无 FAIL"。判据 3/4/6 因缺 genome 被
SKIP 时，PASS 的含金量有限（只覆盖了 1/2/5 三条）。比较参数请读 CSV 的数值列，
不要只看 verdict 词。

跑完控制台会打印汇总表 + 推荐组合（排序：FAIL 少 → PASS 多 → |slope| 小）。

## 三、"参数 → 生态效果"经验表

> 数据来源：2026-11-01 用本工具实测（v2/v3 主干）。**当前所有组合判据 1/2/5 全 FAIL**
> ——瓶颈是机制（kReproCap=24 硬上限、无自遮光），不是参数值。这张表的用途是
> 机制修好之后，记录"哪个参数往哪边推、生态往哪边走"。

| 参数 | 扫描范围 | 生态效果（实测） | 数据来源 |
|------|----------|------------------|----------|
| `nuclei` | 8 → 24 | 末态核数全部收敛到 24（kReproCap 锁死），初始值只影响到达时间；3000 tick 内 8/16 起始都回到 24 | sweep 20261101-020316 |
| `nuclei`（短跑 500 tick） | 8, 12 | 瞬态 CV 0.03~0.06：还在爬坡期，判据 2 偶然 PASS（cv=0.0568@seed7）是瞬态假象——**判据要在稳态窗口判，别用短跑数据** | sweep 20261101-020331 |
| `food`（启动库存） | 待测 | | |
| `policy` | 待测 | | |
| `width`×`height` | 待测 | | |
| `sun_base_level` | ⛔ 待接线 | | |
| `cyano_photosynthesis_rate` | ⛔ 待接线 | | |
| `basal_cost` / `respiration_base` | ⛔ 待接线 | | |
| `cyano_metabolism_rate` | ⛔ 待接线 | | |

### 已知生态结论（写 R3 机制单前的基线）

- 三种子 20000 tick：核数恒 24、总能量 12.4k~12.8k、蓝细菌 64 顶格 ——
  `analyze.mjs` 判据 1（斜率 0.52~0.77）/ 2（cv=0）/ 5（r=+0.86~0.96）全 FAIL。
  数据实锤了 `docs/ECOLOGY-DESIGN.md` 的诊断：**没有负反馈，只有常数**。
- 蓝细菌与核数**正相关**（r≈0.96）而不是捕食者-猎物负相关：
  因为捕食 25% 随机触发与种群解耦，蓝细菌吃不动、只受光驱动。
  R3 的"捕食强度基因化 + 处理时间"落地后，这条应转负——它是最灵敏的机制风向标。

## 四、常见问题

- **报告里 SVG 打不开？** 内联 SVG 直接嵌在 markdown 里，GitHub/VSCode 预览可渲染；
  纯文本编辑器看不到图就用 `--json` 拿数值。
- **双跑一致性**：本工具只读数据不产生随机数，同 trace 分析结果逐字节一致。
- **中文乱码**：sweep.ps1 需要带 UTF-8 BOM（仓库现状即带）；输出重定向时脚本已设
  `[Console]::OutputEncoding = UTF8`。
- **analyze 报"期望 schemaVersion 2 或 3"**：文件太老（v1）或损坏；工具会继续解析，
  缺失字段对应判据 SKIP。
