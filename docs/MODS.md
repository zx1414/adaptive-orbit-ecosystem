# 生态 Mod 开发

一个场景 Mod 是一份 UTF-8 文本，放在游戏目录的 `mods/`。它定义开局参数和当前内核提供的机制组合。游戏启动时扫描目录，菜单列出可用场景，并显示格式有误的文件。

ZIP 版以解压后的目录为游戏目录。单文件版首次启动后使用 `%LOCALAPPDATA%\agentsim\portable\3.2.1\`，该位置无法写入时使用 EXE 旁的 `.agentsim\3.2.1\`。

## 创建场景

将下列内容保存为 `mods/shallow_water.txt`，重启服务，在开局菜单选择「浅水养分扩散」。

```ini
version=1
name=浅水养分扩散
day_cycle=on
initial_nutrient_multiplier=1.5
nutrient_diffusion=0.08
```

| 键 | 值 | 作用 |
| --- | --- | --- |
| `version` | 必填，当前为 `1` | 文件格式版本 |
| `name` | 必填，1 至 120 字节 | 菜单显示名称 |
| `day_cycle` | `on` 或 `off`，默认 `on` | 是否结算自然昼夜振荡。光照档和寒冬仍会改变光照基线 |
| `initial_nutrient_multiplier` | 0 至 20，默认 1 | 乘到开局养分预算上 |
| `nutrient_diffusion` | 0 至 1，默认 0 | 每帧分解后，各区同步把该比例的可吸收养分分给环面邻区 |

文件名去掉 `.txt` 后是稳定场景 ID。使用 1 至 48 个小写英文字母、数字、下划线或连字符，`standard` 由内核保留。每行一个 `key=value`，空行及以 `#` 开头的行会跳过，文件上限为 16 KiB。未知键、重复键或越界数值会让该文件加载失败。

`mods/` 中的三个示例各展示一类变化。恒定光照关闭昼夜振荡，富营养水域将初始养分加倍，养分扩散增加区域间的养分流动。扩散以各区旧库存为输入，残骸留在原位置，进出量在同一帧结算。

## 对照运行

可以用相同的种子和初始实体数对照标准生态与自己的场景。下载版在解压目录运行以下命令，从源码构建时把可执行文件替换为 `build/agentsim.exe`。

```powershell
.\agentsim.exe --mod standard --seed 42 --nuclei 12 --cyano-nodes 48 --ticks 500 --audit
.\agentsim.exe --mod shallow_water --seed 42 --nuclei 12 --cyano-nodes 48 --ticks 500 --audit
```

`--extensions-dir <目录>` 可以指定其他扩展根目录。该参数同时定位 `mods/` 和 `models/`。指定时使用该目录，缺省时优先找可执行文件所在目录或其上级包含的 `mods/`，再回退到当前工作目录。

在实时界面观察种群、可吸收养分和残骸。改变扩散系数时，先固定其他场景参数，再比较多个种子的局部供给和种群走势。存档记录生效的场景 ID、格式版本及实际参数，载入后使用这些值。

## 在 C++ 中增加机制

当前场景格式通过数据选择现行内核的机制。新增机制时，从对应层的输入和结算开始，例如养分运输由 `NutrientField` 处理，`Simulation` 负责确定调用时序。场景参数由 `ExtensionCatalog` 验证，进入 `ScenarioConfig`，并经 `SaveStore` 与 `Timeline` 持久化。修改配置格式时同步考虑旧档默认值和检查点重放。 [模拟管线](ARCHITECTURE.md)
