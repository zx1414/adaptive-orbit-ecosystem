# 对齐清单（旧版 v2.3.1 → agentsim）

状态：⬜ 未开始 / 🟡 进行中 / ✅ 完成 / ❌ 明确不做

## 用户点名不做的
- ❌ 控制台字符渲染（ConsoleRenderer / gridCols / gridRows / clearScreen / pauseOnExit）
- ❌ 玩法 mod 系统（mod_api / mods_registry / mods.list / Mod 页）

## 核心模拟
| 项 | 旧版 | 新版 | 轮次 |
|---|---|---|---|
| 24 参数基因组 + 变异（含 4 社会参数 + 2 施力参数） | NucleusParams | ✅ 模块已交付 | R1-A |
| 遗传色相 hue（独立 RNG） | Nucleus.hue + hueRng_ | 🟡 | R1-A |
| 三类球差异化（Shield 降代谢+分担施力代价） | BallType 3 类（Shield 仅速度差） | ⬜ 要超越旧版 | R2 |
| 球的力场模型（径向弹簧+切向+排斥+跟随耦合） | updateBallDynamics | ⬜ | R2 |
| 球归属与脱附惩罚 | updateOwnership + ballLossCost | ⬜ | R2 |
| ~~核战斗~~ | 用户取消 | ❌ 不做 | — |
| 核对核斥力/聚拢/硬分离（**无能量转移**） | nucleusMovement 部分 | ⬜ | R2 |
| 核繁殖 + 变异 + 色相遗传 | reproduce | ⬜ | R2 |
| 觅食（朝球移动） | forageK/forageRadius | ⬜ | R2 |
| 随机游走 | wanderK/wanderTurnRate | ⬜ | R2 |
| 蓝细菌光合/代谢/上限 | Cyanobacteria | 🟡 | R1-B |
| 蓝细菌连线传输 | linkTransfer | ✅ 已实现 | — |
| 蓝细菌分裂繁殖 | reproduction* | ✅ 已实现 | — |
| 蓝细菌产球 | ballSpawn* | ✅ 已实现 | — |
| 蓝细菌被捕食 + 转化率 4 模式 | cyanoConversionRate | 🟡 | R1-B |
| 蓝细菌节点色相 | cyanoHueJitter | 🟡 | R1-B |
| 蓝细菌连线对外可见 | 旧版快照里有 | ✅ links() + trace f 槽位 | R1-B |
| 取消食物球 | — | ✅ 已合并（trace v3） | R1-B |
| 阳光模型（昼夜/梯度/噪声/周期） | SunModel | 🟡 | R1-B |

## 实时调控
| 项 | 旧版 | 新版 | 轮次 |
|---|---|---|---|
| 四轴五档 | EnvControl::axes | ✅ 已接线（RuntimeEnv 109 键） | R1-C/R3-B |
| 细调参数键 | fine.* + ParamSchema | 🟡 | R1-C |
| 事件卡 4 种 | harvest/winter/plague/bloom | 🟡 | R1-C |
| ~~影响力（上限/开局/恢复/费用）~~ | 用户取消 | ❌ 不做 | — |
| 预设 preset | EnvPresetDef | 🟡 | R1-C |
| 蓝细菌在线开关 | cyano=on/off | 🟡 | R1-C |
| 干预走 ledger（免费但审计仍须成立） | 无（旧版没审计） | ⬜ | R3 |
| 施力：指定球 + 花能量加速度 | applyForce（随机触发，占位） | ⬜ 要变成决策层点名 | R2 |
| 决策层调制遗传参数 ±40% | 无 | ⬜ 新设计 | R2 |
| 参数表 113 键 | ParamSchema | ✅ 已交付 | R1-C |
| key → SimConfig 映射 | 旧版直接改 WorldConfig | ⬜ | R3 |

## 服务与前端
| 项 | 旧版 | 新版 | 轮次 |
|---|---|---|---|
| WinSock HTTP 服务器 | HttpServer (399 行) | ✅ `--serve` 已接（5 路由） | R1-C/R3-B |
| GET /status /state /env /schema | 有 | 🟡 定义 + R3 接线 | R1-C/R3 |
| GET /saves /packs /timeline | 有 | ⬜ | R4 |
| POST /run /control /env /seek /branch /save /load | 有 | ⬜ | R4 |
| 二进制快照 /state | 80 字节头 + 逐球/核/蓝细菌/连线 | ✅ 头80B+球25B+核32B+蓝细菌20B+连线8B | R3-B |
| 菜单页 | 有 | ✅ | R1-D |
| 运行页（实时渲染） | 有 | ✅ 实时/演示双源 | R1-D/R4-D |
| 参数页 | 有 | ✅ | R1-D |
| 存档页 | 有 | ✅ 假数据（R4 接真） | R1-D |
| 环境调控条 | 有 | ✅ 无影响力版 | R1-D |
| 时间轴控件 | 有 | ✅ | R1-D |
| 核色相渲染 | 有 | ✅ | R1-D |
| ~~轨迹拖尾~~ | 有 | ❌ 用户取消 | — |
| 施力脉冲（画在球上，不画核-球连线） | 无 | ⬜ 新设计 | R1-D/R2 |
| 不画球—核连线 | 用户明确要求 | 🟡 | R1-D |
| 蓝细菌连线渲染 | 有 | ✅ 线在节点下层 | R1-D |
| 三类球形状区分 | 有 | ✅ 圆/菱形/六边形 | R1-D |
| 悬停查看核基因 | 有 | ✅ 24 参数分组 | R1-D |
| 昼夜背景 | 有 | ✅ 已实现 | — |
| 缩放平移 | 有 | ✅ 已实现 | — |

## 存档
| 项 | 旧版 | 新版 | 轮次 |
|---|---|---|---|
| 二进制存档 save.bin | SnapshotStore (475 行) | ⬜ | R4 |
| 时间轴 200 帧检查点 | 有 | ⬜ | R4 |
| 拖动/输入帧号回看 | 有 | ⬜ | R4 |
| 世界线分支 branch | 有 | ⬜ | R4 |
| 存档缩略图 thumb.png | 有 | ⬜ | R4 |
| 自动存档开关 | 有 | ⬜ | R4 |

## 材质包
| 项 | 旧版 | 新版 | 轮次 |
|---|---|---|---|
| pack.txt + colors.txt | resourcepacks/ | ⬜ | R5 |
| 堆叠顺序 | 有 | ⬜ | R5 |
| 内置示例包 | classic_flat | ⬜ | R5 |

## 迭代计划
- **R1**（进行中）：A 基因组 / B 取消食物+蓝细菌 / C HTTP+EnvControl / D 前端 UI
- **R2**：接线 Genome→Nucleus+繁殖+色相；三类球差异化；核战斗；球力场模型
- **R3**：接线 EnvControl→SimConfig + 事件走 ledger + --serve + /state 快照
- **R4**：存档 + 时间轴 + 分支 + 前端实时化
- **R5**：材质包 + 参数页真接线 + 收尾

## 实时服务（R3-B 已交付，2026-11-01 08:00）

`agentsim --serve [port]` 提供：

| 路由 | 说明 |
|---|---|
| `GET /status` | 运行状态 + tick |
| `GET /env` | 四轴档位 + 事件 + 预设 + 干预计数 |
| `GET /schema` | 116 个参数元数据 |
| `POST /env` | 行式 key=value，实时改参数（免费无限） |
| `GET /state` | 二进制快照（80B 头 + 球25B/核32B/蓝细菌20B/连线8B） |
| `GET /` | frontend/ 静态文件 |

事件全走 ledger：harvest(injectPrimary, 2000封顶) / winter(dissipate) / plague(dissipate) / bloom(addCyanoNodes)。
每事件执行后 `auditOk()` 检查，失败 exit 3。

**RuntimeEnv**：109 个键接线（106 schema 键 + cyano_enabled + 2 别名），
5 个键明确拒绝（width/height/seed/ticks/regrow_food），未知键进 unwiredKeys 去重。

## 前端实时化（R4-D 已交付，2026-11-01 09:20）

- `live-client.js`（402 行）：`/state` 二进制解析，与 `main.cpp` 的 `buildStateSnapshot` 逐字段对拍
- `main.js` 双源：启动时 probe `/status`，在线走实时轮询（100ms，600 帧滚动窗口），失败回退演示数据
- 调控直通 `POST /env`（`axis.<id>=N` / `event.<id>=1` / `preset.<id>=1` / `cyano=on|off`）
- 播放控制 `POST /control`（`cmd=pause|resume|step`），旧后端 404 时优雅降级
- `file://` 双击仍可玩（probe 永不 reject，fetch 缺失即走演示路径）
- smoke 156 checks（含手写二进制夹具逐字段断言 + fetch stub 端到端）

## 生态判据最新快照（2026-11-01 08:25，seed 42 × 5000 tick）

```
判据 1 能量紧平衡    FAIL  0.5336     ← R3-A 自遮光负责修复
判据 2 种群波动      PASS  0.2469
判据 3 基因多样性    PASS  22/24
判据 4 hue 发散      PASS  32.1°      ← 感知 24 维接线修复后从 17.8° 升上来
判据 5 捕食者-猎物    FAIL  r=+0.4985  ← R3-A 自遮光负责修复
判据 6 调控有效      SKIP （需要 --baseline 对比）
```

## 生态判据快照（R3-A 交付后，2026-11-01 11:50）

三种子 5000 tick（shadeK=0.20 / grazeRefuge=15 / respirationBase=0.03 / biteFraction=0.15）：
```
            seed42          seed7           seed123
判据1 FAIL   0.7072          0.5146          0.7446
判据2 PASS   cv=0.138 f=102   cv=0.158 f=94   cv=0.229 f=39
判据3 PASS   21/24           23/24           24/24
判据4 FAIL   5.5°            16.3°           58.6°  (PASS)
判据5 FAIL   r=+0.656        r=+0.669        r=+0.815
``
对照 R3-B 基线（无自遮光）：cyano 64→44~87 浮动（**脱离护栏**）、
nuclei 300→39~102、能量 25228→3338~3872。

### 未达成的原因（R3-A 报告 §6，机制层）
1. **crit1 两难**：支持 12 核创始人活过瓶颈的注入速率，在稳态必然过供。
   解法（R4 候选）：初始库存能量化，把"创始人窗口"与"稳态注入"解耦。
2. **crit5 统计陷阱**：r=+0.66~0.82 由共同增长趋势主导（非平稳序列 lag-0 皮尔逊）；
   refuge 把 cyano 钉在地板，生产端失去振荡幅度。
3. **maxSpeed 单调选择压**：自遮光让食物稀缺，慢速严格最优，塌向 Clamp 下限 5。
   offset=300 实测更糟（群落破碎加速局域灭绝）。需要收益函数改造（迁移回报）。

### R3-A 的新增机制（全部保留）
- 自遮光（shadeK=0.20，搭载 linkTransfer 邻居循环，退化分支保护）
- **捕食避难所 grazeRefugeEnergy=15**：无避难所时实测 boom-bust 灭绝（154核→cyano 0→全崩）；
  这是 R3-A 最重要的新增机制
- 营养循环真实转移（fertilize + carrion 流水闭合断言，1e-9 容差）
- maxNodes 64→256（护栏降级），kGrazeBiteFraction 基因化


## R8 验收反馈对齐（2026-11-01 14:05）
| 用户要求 | 判定 | 落点 |
| --- | --- | --- |
| exe 打开立即关闭 → 可被前端拉起 | 根因=无参批跑即退（main.cpp:1019） | T49 R8-A |
| 场景相对球运动半径太小，加大 4 倍 | 256²→512²（面积语义，理由见 LOG） | T53 主代理合并后 |
| 全实体极简材质 | MINIMAL 开关 + palette 覆盖色值 | T51 R8-D |
| 核比球大、中央灰点、交互闪白 | 核 2.5×，灰点 30%，邻近闪烁 | T51/T52 R8-D |
| 悬停参数卡 + 点击持久挂载可关 | /state v2 基因 + 卡片 DOM | T50/T52 A+D |


## R8 验收闭环（2026-11-01 17:40）
| 用户要求 | 状态 |
| --- | --- |
| exe 双击秒退 → 前端拉起 | ✅ argc==1 默认 serve + ShellExecuteA（--no-open 可关）；批跑契约零影响 |
| 场景加大 4 倍 | ✅ 512²（colonies/cyano/maxNuclei 密度等比联动；6000t 419 核） |
| 全实体极简材质 | ✅ MINIMAL 默认开；palette 换色不改风格 |
| 核>球、中央灰点、交互闪白 | ✅ 核 2.5×；灰点 30%；邻近闪烁 800ms |
| 悬停参数卡 + 点击钉住 | ✅ /state v2 真基因数据；多卡并存、×关闭、死亡自动解钉 |
