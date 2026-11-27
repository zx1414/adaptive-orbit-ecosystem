# 材质包开发

材质包是一套 Canvas 地图配色。它可以在开局菜单或参数页叠加启用。颜色修改随调色板刷新应用，运行中的生态机制继续按该局场景结算。

## 文件结构

在游戏目录的 `resourcepacks/` 下建立独立的目录。建议使用 ASCII 目录名与名称 ID，说明和作者可以写中文。两个文件均为 UTF-8 文本。

ZIP 版以解压后的目录为游戏目录。单文件版首次启动后使用 `%LOCALAPPDATA%\agentsim\portable\3.2.1\`，该位置无法写入时使用 EXE 旁的 `.agentsim\3.2.1\`。

```text
resourcepacks/
└── river_silt/
    ├── pack.txt
    └── colors.txt
```

`pack.txt` 定义显示信息。建议显式设置唯一的 `name`，缺省使用目录名。启用列表采用 `name`。为便于多个包组合，名称请使用不含逗号的稳定 ID。

```ini
name = river_silt
version = 1.0
author = 你的名字
desc = 暖灰水域和高对比器官
```

`colors.txt` 中每行设置一项颜色。分量为 0 至 255 的十进制整数，顺序为红、绿、蓝。

```ini
background = 224, 221, 199
ball_worker = 42, 118, 98
ball_scout = 79, 110, 165
ball_shield = 183, 109, 54
free_ball = 60, 83, 72
nucleus = 62, 78, 65
```

## 渲染器使用的键

| 键 | 地图用途 |
| --- | --- |
| `background` | 地图底色，昼夜效果可叠加 |
| `ball_worker` | 圆形资源球 |
| `ball_scout` | 菱形侦察球 |
| `ball_shield` | 六边形护盾球 |
| `free_ball` | 无主球的虚线描边 |
| `nucleus` | 核的辅助描边，主体仍按谱系色相着色 |

空行和以 `#` 开头的注释会跳过。省略的键或无法解析的行继承下层颜色。后加载的已启用包覆盖前面的同名键，最终值超出 RGB 范围时渲染器使用内置颜色。实体形状、尺寸和生态行为由现行渲染器及模拟内核决定。

## 试用和分发

重启游戏以发现新目录，在开局菜单展开「材质包」，勾选 `river_silt` 后开局。游戏将启用顺序写入 `resourcepacks.list`，该文件每行一个名称。分发时打包自己的材质包目录，使用者将它放进 `resourcepacks/`。

项目附带的 `classic_flat/` 可以作为模板。检查效果时，可打开或关闭昼夜天空来确认底色，再对比各种器官和自由球的可读性。 [示例材质包](../resourcepacks/classic_flat)
