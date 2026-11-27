# 算法研究与模型接口

使用 Python 可以逐步驱动游戏使用的模拟内核。训练环境按个体 ID 返回观测和结算，实验脚本可以实现任意策略与奖励。游戏菜单可运行导出的 24 输入、2 输出线性模型。

## 训练示例

需要 Python 3.10+，示例使用标准库。在免安装包的解压目录运行下列命令，从源码构建时将 `--exe` 改为 `build/agentsim.exe`。

```powershell
python tools/train_navigation.py --exe .\agentsim.exe --mod nutrient_diffusion --output models/nearest_food.txt
```

重启游戏，选择「养分扩散」和 `nearest_food`。示例用最近食物方向监督训练一个线性层。批跑时可使用 `--policy model.nearest_food` 选择同一个模型。

模型为 UTF-8 文本，首行为 `perceptron 2 24`，其后两行分别是对应输出的偏置与 24 个权重。可用 `tools/agentsim_env.py` 中的 `export_perceptron()` 导出。名称取文件名，规则与场景 ID 相同，目录为 `models/`。开启新局时解析出的权重会随存档保存。

推理先计算两个线性输出，分别截取到 [-1, 1]，然后把非零方向向量归一化。原始输出之和大于零时冲刺意愿为 0.5，其余情况为 0。使用孢子规则时，模型适配器会在最近食物进入约 85% 摄食半径后请求停下。

## 用自己的算法驱动环境

把实验脚本放进 `tools/`，或将其加入 Python 模块搜索路径。

```python
from agentsim_env import AgentSimEnv

with AgentSimEnv("agentsim.exe") as env:
    frame = env.reset(42, "nutrient_diffusion", ticks=300, audit=True)
    while not (frame["terminated"] or frame["truncated"]):
        if not frame["audit_ok"]:
            raise RuntimeError("Simulation audit failed")
        actions = {}
        for agent in frame["observations"]:
            if agent["food_ids"][0] is None:
                actions[agent["id"]] = (1.0, 0.0, 0.35, 0.0)
                continue
            close = agent["food_distances"][0] <= 0.85 * agent["bite_radius"]
            actions[agent["id"]] = (*agent["sensors"][:2], 0.0 if close else 0.65, 0.0)
        frame = env.step(actions)
```

- `reset(seed, scenario, ticks=…, nuclei=…, cyano=…, audit=…)` 启动一个独立回合。
- `observations` 是同一帧快照。每项包含字符串 `id`、核的能量、24 维 `sensors`、前三个食物通道的 `food_ids`、世界单位距离 `food_distances`、是否为孢子的 `food_is_spore`，以及摄食半径 `bite_radius`。没有对应目标时 ID 为 `null`。
- `step({id: (dx, dy, speed, boost)})` 提交导航动作。方向分量在 -1 至 1，速度比例和冲刺意愿在 0 至 1。省略的个体使用空动作。
- `outcomes` 报告上一帧个体的能量前后值、实际摄食组织量、吸收能量和存活状态。新生个体由 `birth_ids` 给出。
- `world` 分别报告核、成熟节点和孢子数，也提供总能量、养分、残骸与累计能量账。核种群灭绝时 `terminated` 为真，达到帧上限时 `truncated` 为真。

可以根据结算结果选择奖励，也可保持动作不变做场景对照。建议固定种子、初始数量、场景和帧数，随后跨多个种子比较。自定义 C++ 策略可在构造 `Simulation` 时注入 `IDecisionPolicy`。 [数据与时序](ARCHITECTURE.md)

## 感知向量 v1

| 下标 | 含义 |
| --- | --- |
| 0–5 | 按距离取前三个食物候选的相对方向，x、y 交替排列 |
| 6–8 | 同一批候选的归一化接近度 |
| 9 | 最近食物的归一化距离 |
| 10–12 | 自身速度的 x、y 分量和大小，均除以 `1 + speed` |
| 13 | 到最近坐标卷绕缝的归一化距离 |
| 14–15 | 读通道噪声幅度、保留值 0 |
| 16–17 | 温度失真度、归一化温度采样 |
| 18–20 | 最近邻核的归一化距离、单位方向 x 与 y |
| 21 | 核自身能量占容量的比例 |
| 22–23 | 可施力球数量除以 8、最近可施力球的归一化距离 |

食物候选的缺失槽用 `food_ids` 中的 `null` 标识。距离和方向可能带感知噪声。缺少邻核时对应三个值为 `[1, 0, 0]`。缺少可施力球时末两维为 `[0, 1]`。
