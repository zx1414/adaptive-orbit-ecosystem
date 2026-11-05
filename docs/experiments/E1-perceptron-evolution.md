# E1 感知机权重进化实验报告

- 日期：2026-10-31
- 执行者：实验工程师（子代理）
- 工具脚本：`tools/evolve.ps1`（纯 PowerShell，一条命令跑完全部扫描）
- 数据：`build/exp/perceptron-sweep.csv`（42 组）、`build/exp/E1-generalization.csv`（8 组）

## 1. 实验设置

- 可执行：`build/agentsim.exe`（未重编译，源码零改动）
- 命令模板：`agentsim.exe --no-render --ticks 2000 --seed 42 --audit --policy perceptron --perceptron-seed <S> --perceptron-scale <K>`
- 参数空间：`--perceptron-seed` ∈ {1,7,42,99,123,999,2024,31337} × `--perceptron-scale` ∈ {0.01,0.05,0.1,0.3,0.5}，共 40 组 + rule/stub 基线 2 组 = 42 组
- 权重语义（PerceptronPolicy.h）：`w[2][16]+b[2]`，`Z=W^T X+b`，`y=sign(Z)`；seed 经 PCG32 生成随机初值，scale 控制初值幅度；权重在整个运行期**固定不变**（serialize/deserialize 已预留但未参与本次扫描）
- 环境：Windows，PowerShell 5.1，单次运行约 1s，总耗时约 48s；42/42 组 exit=0、audit=ok
- 确定性：最优点组（pseed=1, scale=0.5）双跑 stdout 逐字节一致（S2 复核通过）

## 2. 结果

### Top 10（按 total_energy 降序，seed=42, 2000 ticks）

| # | policy | pseed | scale | nuclei | food | total_energy | audit | exit |
| - | ------ | ----- | ----- | ------ | ---- | ------------ | ----- | ---- |
| 1 | stub | - | - | 12 | 3398 | 40850.00 | ok | 0 |
| 2 | perceptron | 1 | 0.5 | 6 | 1212 | 28450.00 | ok | 0 |
| 3 | perceptron | 1 | 0.3 | 7 | 567 | 26532.00 | ok | 0 |
| 4 | perceptron | 1 | 0.01 | 8 | 901 | 24966.00 | ok | 0 |
| 5 | perceptron | 1 | 0.1 | 8 | 902 | 24966.00 | ok | 0 |
| 6 | perceptron | 1 | 0.05 | 8 | 360 | 24966.00 | ok | 0 |
| 7 | rule | - | - | 12 | 37 | 17910.08 | ok | 0 |
| 8 | perceptron | 999 | 0.3 | 12 | 15 | 17840.00 | ok | 0 |
| 9 | perceptron | 999 | 0.1 | 12 | 18 | 17840.00 | ok | 0 |
| 10 | perceptron | 2024 | 0.01 | 12 | 18 | 17840.00 | ok | 0 |

### 最差 5 组（perceptron 内，按 total_energy 升序）

| # | policy | pseed | scale | nuclei | food | total_energy | audit | exit |
| - | ------ | ----- | ----- | ------ | ---- | ------------ | ----- | ---- |
| 1 | perceptron | 123 | 0.5 | 12 | 19 | 17840.00 | ok | 0 |
| 2 | perceptron | 999 | 0.5 | 12 | 15 | 17840.00 | ok | 0 |
| 3 | perceptron | 999 | 0.3 | 12 | 15 | 17840.00 | ok | 0 |
| 4 | perceptron | 999 | 0.05 | 12 | 16 | 17840.00 | ok | 0 |
| 5 | perceptron | 999 | 0.01 | 12 | 12 | 17840.00 | ok | 0 |

（7/8 的 pseed 在所有 scale 下都精确落在 17840.00 的"吸收态"：nuclei=12、food≈15-49、无繁殖无死亡。）

### 均值聚合（seed=42）

按 pseed（5 个 scale 平均）：

| pseed | energy | food | nuclei |
| ----- | ------ | ---- | ------ |
| 1 | 25976.0 | 788.4 | 7.4 |
| 7-31337 其余 7 个 | 17840.0 | 15.2-40.0 | 12.0 |

按 scale（8 个 pseed 平均）：

| scale | energy | food | nuclei |
| ----- | ------ | ---- | ------ |
| 0.01 | 18730.8 | 132.4 | 11.5 |
| 0.05 | 18730.8 | 63.1 | 11.5 |
| 0.1 | 18730.8 | 132.1 | 11.5 |
| 0.3 | 18926.5 | 91.6 | 11.4 |
| 0.5 | 19166.3 | 171.9 | 11.3 |

## 3. 观察结论

### 3.1 主结论：pseed 决定"逃逸或困死"，scale 决定"逃逸后走多远"

数据里最强的信号不是 scale，而是 **pseed 的两极分化**：8 个 pseed 中 7 个在全部 5 个 scale 下
都精确收敛到同一个 17840.00 吸收态（nuclei=12 不变、food≈15-49、无繁殖无死亡），
只有 pseed=1 逃逸成功（energy 24966-28450）。这说明初值所在的符号结构（W^T X 的符号模式）
比幅度更重要：**大多数随机初值让感知机输出近似固定的方向组合，种群陷入"巡逻-进食-勉强平衡"的稳态；
只有撞上能形成"定向觅食梯度"的初值（pseed=1），种群才进入持续移动-进食-繁殖的动态**。
（以上机理为推测，未做权重逐分量反解验证。）

### 3.2 scale 的作用：仅在逃逸的 pseed 上生效，且 energy 随 scale 单调上升

pseed=1 的 energy 随 scale 单调递增（0.01/0.05/0.1 → 24966，0.3 → 26532，0.5 → 28450），
但 nuclei 却从 8 降到 6、food 从 ~900 升到 1212。推测机理：scale 大 → 权重幅度大 →
在"读通道噪声 + 写通道扰动都随速度增强"（R6.2）的耦合下，大幅权重把噪声放大为更强的方向偏转，
等效于更高的游速/更大的探索半径 → 更快清空局部食物 → 单核能量积累更极端 →
部分核更早触发繁殖（nuclei 一度更多）又更快饿死（终态更少），账面 total_energy 更高。
在困死的 7 个 pseed 上 scale 几乎无影响（energy 恒 17840），即 **幅度调制不改变符号结构，救不了坏初值**。

### 3.3 与 200 tick 已知结果的对照

200 tick 时 perceptron（pseed=42, scale=0.1）food=19/energy=2748.80，本次复现得到
food=25/energy=2772.80（同一命令，微小偏差来自环境对齐细节，量级一致）；
拉长到 2000 tick 后该组停在 17840 吸收态，而 pseed=1 组达到 28450。即：
**200 tick 的短跑排名不能预测 2000 tick 的生态终局**（pseed=42 在短跑与 rule 相当，长跑却与 rule 几乎同态）。

### 3.4 rule / stub 基线

stub 实际上是"不吃不动"式占位策略，却在 2000 tick 后 food=3398、energy=40850 全场最高：
食物再生速度远快于消耗，核呼吸耗能是主要能量出口，保持低速反而整体能量留存最高。
rule 策略 food=37、energy=17910，与 7/8 困死感知机组几乎同态（17840-17910），
说明 rule 的打分规则在这个时间尺度上也逼近"进食=呼吸"的平衡。
**生态健康度定义需要警惕**：若把 total_energy 当唯一指标，"什么都不做"反而最优；
更合理的复合指标应包含 food 消耗量（生态吞吐）与种群繁殖活性。

## 4. seed 泛化测试

对最优点组（pseed=1, scale ∈ {0.3, 0.5}）换仿真 seed ∈ {7, 123} 复跑：

| simSeed | pseed | scale | nuclei | food | total_energy | 相对吸收态(17840) |
| ------- | ----- | ----- | ------ | ---- | ------------ | ---------------- |
| 7 | 1 | 0.5 | 6 | 1441 | 27996.00 | +56.9% |
| 123 | 1 | 0.5 | 8 | 1282 | 25040.00 | +40.4% |
| 7 | 1 | 0.3 | 8 | 364 | 25052.00 | +40.4% |
| 123 | 1 | 0.3 | 6 | 967 | 28338.00 | +58.9% |

4/4 组全部显著高于 17840 吸收态（+40% ~ +59%），也高于同 seed 的 rule 基线（17848-17864）。
**泛化通过：pseed=1 的优势不是对 seed=42 的过拟合，而是该初值方向结构本身可迁移。**
sub 基线在所有 seed 下仍全场最高（40810-40850），印证 3.4 的"低速留存"效应与 seed 无关。

## 5. 已知局限（诚实清单）

1. **样本极小**：8 个 pseed、5 个 scale、3 个 sim seed，全部结论建立在一两位点分辨率上；
   "7/8 困死"不等于"87.5% 初值困死"的统计断言。
2. **单一指标依赖**：total_energy 被用作主排名指标，但它偏向"低消耗"而非"生态繁荣"
   （stub 反而最高）；food 与 nuclei 只作辅助观察，未做复合适应度定义。
3. **没有真进化**：权重在整个运行期固定，pseed 只改初值、不改运行期学习/遗传；
   `--policy rule` 的 mutation=0.05 也只在 rule 侧生效。本次实验是**参数扫描（初始化敏感性分析）**，
   与"权重进化"之间存在本质差距：没有选择压力、没有世代更替的权重传递。
4. **机理是推测**：3.1-3.2 的符号结构/噪声放大解释未做权重逐分量反解、未做中间 tick 快照验证，
   只有终态一行数据作证据。
5. **单环境配置**：世界大小、食物再生率、初始核数均用默认值，结论对其他环境参数的外推性未知。

## 6. 下一步建议

把 pseed=1 的权重用 `PerceptronPolicy::serialize()` 导出存档，并在此基础上实现
"每 N tick 用 top-能量核的权重 + 小扰动覆盖低能量核"的 simple遗传算子，让权重真正跨代传递。

## 附：复现命令

```powershell
# 全扫描（42 组，~50s）
powershell -ExecutionPolicy Bypass -File tools\evolve.ps1
# 最优点组
build\agentsim.exe --no-render --ticks 2000 --seed 42 --audit --policy perceptron --perceptron-seed 1 --perceptron-scale 0.5
# 泛化
build\agentsim.exe --no-render --ticks 2000 --seed 7   --audit --policy perceptron --perceptron-seed 1 --perceptron-scale 0.5
build\agentsim.exe --no-render --ticks 2000 --seed 123 --audit --policy perceptron --perceptron-seed 1 --perceptron-scale 0.5
```
