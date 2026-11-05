# ACC-R11 验收报告（红队 · 只读）

- 对象：D:\ScuStu\Q\agentsim（当前实现）；对照：D:\ScuStu\Q\_ref-v231（tag v2.3.1 原版源码，只读）
- 结论速览：四投诉全部实锤且全部可修；对照原版存在 1 项显示层硬伤（阳光数值/明暗）、3 项玩法级缺失（影响力经济、玩法 mod、--config 注入）与若干劣化点。
- 本报告不修改任何代码/构建产物，仅此一份输出。

## P0（必须修）

### P0-1 ①帧率低：三层瓶颈叠加，前端轮询 100ms 是最大显示硬伤
- 问题：实时模式画面更新上限被前端锁死在约 10fps，与「本来就这么卡」无关——后端 60fps 与浏览器 60Hz 之间被 100ms 轮询掐断。
- 证据：
  - frontend/js/live-client.js:300 `intervalMs: 100`（轮询默认）；frontend/js/live-client.js:413 每次轮询后 `setTimeout(pollOnce, S.intervalMs)`——帧只能 10 帧/秒到达。
  - frontend/js/main.js:1340 enterLiveMode 硬编码 `intervalMs: 100`；main.js:296 live 模式 rAF 直接 return（画面完全由轮询驱动）。
  - frontend/js/main.js:45 `FPS_BASE=24` 只作用于离线回放/演示步进（main.js:299），与实时卡顿无关，勿误修。
  - src/app/main.cpp:1125-1133 serve 模式默认 60fps tick 节流（合理上限，保留）。
  - 真实算力瓶颈（世界满载时）：src/sim/Simulation.cpp:425 socialAccelerations 为 O(n²) 核对；src/core/Config.h:75 maxNuclei=1200（1200² 约 144 万对/帧）；蓝细菌 linkTransfer 也是 O(n²)（src/environment/CyanoNode.cpp:386-411，cap 4096 见 Config.h:314）。项目自留实测注释 src/sim/Simulation.cpp:448：「节点数暴涨导致 O(n²) 的 linkTransfer 把 4000 tick 拖到 61 秒」。
- 建议修法：① live-client/main.js 的 intervalMs 降到 16~33（live-client.js:436 已支持 16ms 下限，一行改动即把显示上限提到 30~60fps）；② 中期把 socialAccelerations 与 linkTransfer 换空间哈希网格（原版有现成实现，见 TOP10-1）；③ 保持后端 60fps 节流与 renderInterval 快照节流（main.cpp:1113-1119）不动。

### P0-2 ②蓝细菌连线跨全屏：后端拓扑是环面短边，前端画成了直连长线
- 问题：连线渲染未做环面 wrap——跨界节点对（如 x=1020 与 x=5，世界宽 1024）被画成横穿全屏的直线；用户期望「左边出去、右边连进来」。
- 证据：
  - 后端正确：src/environment/CyanoNode.cpp:386-474 linkTransfer 全程用 torusDistance 建边（环面距离），拓扑本身没有跨屏问题。
  - 前端缺陷：frontend/js/renderer.js:270-279 drawCyanoNetwork 对每条 link 直接 `moveTo(s2x(a)) → lineTo(s2x(b))`，无任何 ±worldW/±worldH 平移分段；renderer.js:275-276 的剔除只过滤「两端都出视口」的情况，穿屏长线反而被保留。
  - 实体同病：drawBalls（renderer.js:340）与 drawNuclei（renderer.js:436）只画单一位置，无跨界镜像（边缘实体缺半边视野）；detectInteractions（renderer.js:410-415）只对 x 做 wrap 距离，漏了 y。
- 建议修法：画线前检测 |x1-x2|>worldW/2（或 y 向），把一端平移 ±worldW（±worldH）后画两条短边（落到屏幕内的那条自然可见，另一条剔除）；顺手给 ball/nuclei 加「靠近边缘小于半径时补画一份镜像」与 y 向 wrap 距离。

### P0-3 ③看不到当前光照数值：sun 已解析到 frame.live.sun，UI 无处显示
- 问题：数据链路已通到最后一米，纯 UI 缺失。
- 证据：
  - frontend/js/live-client.js:127 `var sun = dv.getFloat32(44, LE)`（/state 头 offset 44）；live-client.js:291 已装入 `live: { ..., sun: sun }`。
  - frontend/js/main.js 全文 8 处 sun 全是参数滑杆键名（main.js:558、822-824），updateHud（main.js:256-268）与 updateLive（main.js:240-251）均未读 frame.live.sun；frontend/index.html 无 st-sun 类元素（/sun|光照|阳光/ 仅命中寒冬事件按钮 title）。
  - 原版对照：原版状态栏常驻显示——_ref-v231/web/index.html:670 `<span>阳光 <b id="st-sun">0.00</b></span>`；_ref-v231/web/app.js:875 每帧 `el('st-sun').textContent = sim.sunLevel.toFixed(2)`。
- 建议修法：updateHud 末尾追加 `' · 阳光 ' + (frame.live && isFinite(frame.live.sun) ? frame.live.sun.toFixed(2) : '--')`（或状态卡加一枚 metric）。一行级改动。

### P0-4 ④画面无明暗变化：MINIMAL 平涂背景把 sun 完全短路，且前端昼夜周期与后端不一致
- 问题：昼夜/光照轴没有任何视觉反馈。
- 证据：
  - frontend/js/renderer.js:61 `MINIMAL = true` 默认开；renderer.js:152-163 drawSky 的 MINIMAL 分支只平涂 `palette.background` 后直接 return——sun 值不参与背景亮度（dayness 仍算出来给网格 alpha 用，renderer.js:161、218，网格 0.05~0.11 的微差肉眼几乎不可见）。
  - 非 MINIMAL 的渐变/光晕（renderer.js:164-179）是前端硬编码 `DAYNIGHT_PERIOD=200`（renderer.js:68）的时间函数，而后端真实昼夜周期是 `sunPeriodFrames=628`（src/core/Config.h:37）——即使开满特效，前端天色相位与模拟光照相位也对不上。
  - 后端 /state 每帧都在发真实 sun（src/app/main.cpp:654-664 serveSunEstimate + main.cpp:1115 写入快照头），前端拿来即用。
- 建议修法：MINIMAL 分支改为按 frame.live.sun 调背景亮度（如在平涂上叠一层 `rgba(255,240,200, k*(sun-0.5))` 半透明白昼罩 + `rgba(0,0,20, k*(0.5-sun))` 夜幕罩，零渐变保持极简风）；或把平涂 HSL 亮度乘 `0.55+0.45*sun`。同时弃用前端 200 帧硬编码相位，统一以快照 sun 为唯一光照源。

## P1（应该修）

### P1-1 玩法缺失：影响力经济整体未实现，环境调控零成本
- 问题：原版四轴/细调/事件卡全部消耗影响力且随帧回蓝，形成「干预有代价」的核心玩法；新版明确自曝「无影响力——apply 全部免费」，玩家可无脑寒冬+爆发刷，策略性劣化。
- 证据：src/app/EnvControl.cpp:10「修正 1：无影响力——apply 全部免费」、:486「无影响力可恢复」、:498「无 influence；interventions/peakAlive/frames 直接顶上」。原版：_ref-v231/web/app.js:1623（影响力 cur/max 显示）、:1655（每帧回蓝提示）、:1765-1786（事件卡按费用禁用/提示）、:1913-1921（细调整批扣费）；_ref-v231/README.md:70（快调 1 点/细调 3 点/事件卡无冷却）。
- 建议修法：EnvControl 加 influence 池（cur/max/regenPerFrame），POST /env 与事件卡按档位扣费、余额不足拒绝并返回剩余值；前端 /env 已有渲染位（main.js describeEnv）。

### P1-2 玩法缺失：10 个玩法 mod 无一移植（共生/行会/季节/气候/溢出/失球宽限等）
- 问题：原版以 mod 形式补齐生态平衡与长期玩法（防种群崩溃、四季节奏、随机气候），新版零移植，世界退化为纯捕食动力学。
- 证据：_ref-v231/mods/ 下 10 个玩法 mod，头部注释即机制说明——symbiosis_bond.cpp:1-7（核间共生，修正 20→1 崩溃）、niche_guild.cpp:1-6（行会互惠）、season_cycle.cpp:6-12（四季轮换）、climate_event.cpp:9-13（800 帧随机气候）、energy_spill.cpp:1-7（溢出再分配）、rare_ball_subsidy.cpp:11-17（稀有球保育）、ball_loss_relief.cpp:1-8（失球宽限）；配套 mods_registry.cpp 与 /mods 路由（_ref-v231/src/LauncherApp.cpp 3 处 /mods）。新版：src/app/main.cpp 路由表（781-971）无 /mods，全仓无玩法 mod 实现。
- 建议修法：不必照搬动态加载——先把 season_cycle、energy_spill（或 symbiosis_bond）、climate_event 三个做成 Config 内置开关（改动小、收益最大），台账全走现有 ledger 零和约束。

### P1-3 参数「注而未接」：UI 可调但行为层不消费（虚假参数）
- 问题：参数页能拖动若干键，实际运行时被行为层常量短路——比原版「可调即生效」劣化。
- 证据：src/core/Config.h:172 `speedCostExponent` 注释自曝「现行为层无指数项」、:173 `reproductionCostRatio`「现繁殖成本是行为层常量」、:177 `damping`「现无阻尼项」。原版全部接入模型（_ref-v231/README.md:163-171 球阻尼/积分、:245-247 代谢 v^β 超线性、:249-253 繁殖比）。参数规模对照：ref env_config.txt 实键 124 个 vs 新版 ParamSchema.cpp 114 项注册。
- 建议修法：把 R3-A 注释列出的三个「待消费」键接进 AgentBehavior/Simulation（代谢指数项、繁殖成本比、核阻尼），接不全的键从参数页隐藏，避免「调了没用」的验收印象。

### P1-4 劣化：同核小球吸引关闭，球群视觉与「吸引-排列-吸收」核心观感消失
- 问题：原版默认开启同核吸引让归属球聚成群（核心视觉签名）；新版默认 0=关闭。
- 证据：src/core/Config.h:226 `sameOwnerAttractRadius = 0.0（0 = 关闭）`；原版 _ref-v231/src/World.h:94-95 `ballSameOwnerAttractRadius = 40.0` + `attractK = 0.15`，README.md:157-161 数学模型明确该力。
- 建议修法：Config 默认改回 40/0.15（力已实现，仅默认值关死）。

### P1-5 玩法缺失：--config 自定义核注入（22/24 字段 CSV 开局）
- 问题：原版支持从文件注入自定义核开局（调试/关卡式玩法入口）；新版无。
- 证据：_ref-v231/README.md:106 `--config <file>` 注入自定义核（22 字段/行）；新版 src/app/Cli.cpp 无 --config（0 处）。
- 建议修法：批跑解析已读写基因组序列化（Simulation.cpp:1380 附近 serWriteF64(g.*)），补一个 CSV→addNucleus 通道即可，POST /run 可加 config= 参数。

### P1-6 劣化：模拟结束原因（灭绝/达成）不可见
- 问题：原版快照头带 28 字节 finishReason，前端能显示「为什么结束」；新版 phase 只有 0/1/2，玩家看不到死因。
- 证据：_ref-v231/src/WebRenderer.cpp:229-232（快照 36-64B 写 finishReason，28 字节定长）；新版 src/app/main.cpp:629 只写 `paused ? 2 : 0`，且 main.cpp:645 自留 28 字节保留区（「扩展字段区」）正好可以放。
- 建议修法：保留区写入定长 reason（"extinct"/"max_ticks"/"manual"），前端 live-client 解析后在状态卡显示。

### P1-7 性能防线缺失：连线无抽稀降级，节点 cap 提到 4096 后前端 stroke 可能爆帧
- 问题：原版连线超过 4000 条自动 4 取 1 抽稀并 HUD 标注；新版节点上限 R10 已提到 4096（Config.h:314）却无任何前端降级，与 P0-1 叠加。
- 证据：_ref-v231/web/app.js:27-28 `CY_LINK_DECIMATE_AT = 4000` + LINK_THINNING_FLAG、:735-754 抽稀绘制与计数；新版 renderer.js:264-280 每条 link 全画，无上限。
- 建议修法：照抄抽稀阈值 + HUD 标注「连线已抽稀」；同时把 renderer.js 每帧 byId 对象重建（renderer.js:265-266）换 Map 复用。

### P1-8 帧数/规模入口限制过紧：实时帧窗口 600、菜单帧数上限 600
- 问题：原版默认总帧数 100000 可长跑观测演化；新版菜单 frames 上限 600（且 live 滚动窗口仅 600 帧），长程演化玩法不可达。
- 证据：frontend/js/main.js:1209 `intOf('menuFrames', 300, 20, 600)`；原版 _ref-v231/README.md:90 `--frames 默认 100000`。appendLiveFrame 滚动窗口 600（main.js:1274）可保留（显示用），但入口上限应放开。
- 建议修法：菜单上限提到至少 100000（后端 serve 本就是永久模式，main.cpp:1017 runForever），系列图表采样已按 tick 推进不受影响。

## P2（可选）

### P2-1 控制台字符渲染模式未移植
- 原版：_ref-v231/README.md:59-63 `--console` 字符渲染（n/N/s/w/c 图例）+ src/ConsoleRenderer.cpp。玩法价值低（已有 Web 实时渲染），可在 README 标注「不移植」了结此项。
### P2-2 「变动世界线」（rebranch）未实现
- 原版：_ref-v231/README.md:73 载入后保留世界状态调参开新局；_ref-v231/src/World.h:220-222 `rebranch(cfg, runFrames)`。新版 Timeline.cpp 仅 1 处提及、无 /rebranch 路由（main.cpp:781-971）、前端 0 处。检查点已齐（GET /timeline + POST /seek），补一个「从检查点分叉」接口即可。
### P2-3 材质包连线透明度键
- 原版 colors.txt 支持 cyano_link_alpha(0.35~1)（_ref-v231/web/app.js:173-177）；新版 classic_flat/colors.txt 无此键，renderer.js:267 连线 alpha 硬编码。小改动，做连线 wrap 时顺带。
### P2-4 悬停/跟随交互补全
- 新版已支持悬停看 24 基因（index.html:94、117），但原版另有「点按核跟随 + 双击画布重置」（_ref-v231/web/app.js:254）；新版仅菜单提示跟随（index.html:94）。确认跟随已实现则关闭此项。

## 原版优点 TOP10

1. **空间哈希网格**（SpatialGrid，cell 60）：_ref-v231/src/SpatialGrid.cpp:1、World.h:42 `gridCellSize=60`——这是 O(n²) 的正解。移植：把 socialAccelerations 与 linkTransfer 的全对扫描换成 cell 桶邻域查询，P0-1 的算力瓶颈随之消解。
2. **状态栏阳光实时数值**：_ref-v231/web/index.html:670 + app.js:875。移植：updateHud 加一行读 frame.live.sun（P0-3）。
3. **轨迹拖尾 fadeTrail**：_ref-v231/web/app.js:35-39（FADE=14、u32 位图）、:589、:533（160 万像素上限护栏）。移植：离屏 canvas 每帧 destination-out 半透明擦除再叠加实体层，注意照抄像素上限防大窗口拖死帧率。
4. **影响力经济（快调/细调/事件卡扣费+回蓝）**：_ref-v231/web/app.js:1623-1655、:1765-1786、:1913-1921。移植：EnvControl 加池与扣费拒绝（P1-1），前端 UI 骨架新版已有。
5. **玩法 mod 集（10 个，含防崩溃平衡器）**：_ref-v231/mods/symbiosis_bond.cpp:1-7、season_cycle.cpp:6-12、energy_spill.cpp:1-7、climate_event.cpp:9-13 等。移植：先内置 3 个高价值开关（P1-2），机制注释即规格书。
6. **/mods 路由 + 冲突警告 + 优先级排序**：_ref-v231/README.md:72、src/mods_registry.cpp。移植：做 P1-2 时同步暴露 GET /mods（清单+冲突黄条），避免玩法包互相踩机制。
7. **快照内 finishReason 定长字段**：_ref-v231/src/WebRenderer.cpp:229-232。移植：写入新版快照头 28 字节保留区（main.cpp:645 现成空位，P1-6）。
8. **连线抽稀 + 绘制/计数 HUD 自检**：_ref-v231/web/app.js:27-28、:735-754、:873-892（drawn/counts 对账）。移植：照抄阈值与标注，顺带在前端 HUD 暴露「画了多少/实际多少」防渲染静默失效。
9. **同核球群聚集力默认开启**：_ref-v231/src/World.h:94-95（40/0.15）。移植：改 Config.h:226 默认值即恢复「吸引-排列-吸收」观感（P1-4）。
10. **变动世界线 rebranch**：_ref-v231/README.md:73、World.h:220-222。移植：在现有 Timeline 检查点上补「从 checkpoint 开新局」接口，存档页加按钮（P2-2）。

---
附：四投诉与修复对应关系——①→P0-1（轮询一行 + 网格中期）；②→P0-2；③→P0-3；④→P0-4。「原版有的都要实现」总差距 = P1-1/2/3/4/5/6/8 + P2 清单，其中 P1-1（影响力）与 P1-2（玩法 mod）是玩法视角最大缺口。
