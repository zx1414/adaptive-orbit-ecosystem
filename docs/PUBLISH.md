# GitHub 合作开发发布指令（v1.1.0）

> 本文档是一份**可直接交给 AI 助手执行的指令**，也可人工按步骤执行。
> 目标：只把**合作开发的必要内容**上传到 GitHub，并发布一份可直接游玩的 release。

---

## 1. 上传清单（白名单）

上传这些：

| 内容 | 说明 |
|------|------|
| `src/` | 全部源码（引擎/启动器/HTTP/存档/参数表） |
| `mods/` | 示例 mod（合作开发的起点） |
| `web/` | 启动器前端（index.html + app.js） |
| `resourcepacks/` | 示例材质包 |
| `docs/` | 计划与 mod 开发指南 |
| `scripts/` | 批量聚类/回归测试脚本（Python） |
| `CMakeLists.txt`、`README.md`、`.gitignore` | 构建与文档 |
| `env_config.txt`、`custom_nuclei.txt` | 配置示例 |
| `启动器.bat` | 双击入口 |

**不上传**（已由 `.gitignore` 排除，或属个人数据）：

- `build/`（构建产物）、`*.exe`（二进制，放 Releases）、`*.csv`（运行输出）、`output/`（旧分析数据）
- `saves/`（个人存档）、`mods.list` / `packs.list`（个人启用配置）
- `release/`（发布打包目录）

若发现误入暂存区，用 `git rm -r --cached <路径>` 移出后重新提交。

---

## 2. 执行指令（复制给 AI 或逐行执行）

```bash
# 0) 确认环境（应看到 origin = https://github.com/zx1414/adaptive-orbit-ecosystem.git）
git remote -v && git status --short

# 1) 提交本次版本（v2.2.0 白名单，含 tests/ 基线）
git add src mods web resourcepacks docs scripts tests CMakeLists.txt README.md CHANGELOG.md \
        .gitignore env_config.txt env_config_balanced.txt custom_nuclei.txt mods.list.example 启动器.bat
git commit -m "v2.2.0: 全元素可见 + 四轴快调/细调 + 影响力/事件卡 + 蓝细菌存档 v2"

# 2) 打版本标签
git tag -a v2.2.0 -m "v2.2.0: 全元素可见 + 实时环境调控 + 存档 v2，Windows 可直接游玩"

# 3) 推送（分支 + 标签）
git push origin HEAD
git push origin v2.2.0
```

推送需要 GitHub 登录凭证；若提示认证失败，先执行 `git config --global credential.helper manager`
并手动 push 一次以唤起浏览器登录，再重试。历史版本命令见 §3–§6。

---

## 3. 发布 Release v1.1.0（网页操作，约 1 分钟）

1. 打开 `https://github.com/zx1414/adaptive-orbit-ecosystem/releases/new`；
2. 「Choose a tag」输入 `v1.1.0`（已推送，选择即可）；标题 `v1.1.0`；
3. 描述粘贴：
   > 双击 simulator.exe 进入启动器：参数调节 / Mod 管理（优先级+冲突提示）/ 我的存档（读档续玩逐帧一致）/ 材质包 / 浏览器逐球实时可视化。控制台模式：`simulator.exe --console`。
4. 「Attach binaries」上传本仓库根目录的 `release/自适应核生态模拟器-v1.1.0.zip`；
5. 点「Publish release」完成。

zip 生成方式（已在本地生成，重新打包用）：

```powershell
Compress-Archive -Path release\v1.1.0\* -DestinationPath release\自适应核生态模拟器-v1.1.0.zip -Force
```

---

## 4. 发版版本号备忘（下次发版改三处）

1. `src/main.cpp` 的 `kVersion`（`--version` 输出）；
2. `web/index.html` 顶栏版本号；
3. 本文档与 README 中的版本说明。

---

## 5. v2.0.0 发布记录

发布日期：本轮。内容：**蓝细菌生产者 mod + 前端布局优化**。

打包产物：`_agent\自适应核生态模拟器-v2.0.0.zip`（403 KB，解压即可用）。
打包源目录：`_agent\release_v2\`。

### 与 v1.1.0 的差异

- 新增 mod `cyanobacteria`（实现 `mods/cyano_producer.cpp`）：
  自养生产者群落，光合产能 / 连接网络能量流动 / 概率产球与繁殖 /
  核捕食节点（转化率随机动性下降）。
  **启用需两步**：配置 `cyano_nodes > 0`，且 exe 同目录 `mods.list` 含一行 `cyanobacteria`。
- 前端：HUD 四分组（模拟 / 球型 / 能量 / 蓝细菌）、图例可折叠、窄屏防重叠。
- 版本号三处已同步为 2.0.0。

### 打包注意（已踩坑）

- **包内不要放 `env_config.txt`**。仓库根的该文件含非默认值
  （`ball_loss_cost = 1.5`，默认 5.0），会让模拟结果偏离基线，
  导致与 v1.1.0 的逐字节回归失败。包内改用程序内建默认；
  平衡配置以 `env_config_balanced.txt` 单独提供。
- **`mods` 文件名不得与 `src` 同名**（Windows 大小写不敏感）：
  `mods/cyanobacteria.cpp` 会覆盖 `src/Cyanobacteria.cpp` 的 obj，
  造成一片 LNK2019。已改名为 `cyano_producer.cpp`。
- 构建脚本 `_agent\integration\build.cmd` 已改为显式列出 mods 文件，
  勿回退成 `mods\*.cpp` glob（会重新引入上述覆盖）。

上传 Release 时描述可写：

> v2.0.0：新增蓝细菌生产者 mod（光合产能 / 菌丝网络 / 核捕食的能量取舍），
> 前端 HUD 分组与图例折叠优化。关闭蓝细菌时与 v1.1.0 行为逐字节一致。

---

## 6. v2.1.0 发布记录（前端缺陷修复 + UI 重做）

发布日期：本轮。内容：**修复「只有核能显示」+ 前端 UI/交互重做 + 启动器内核版本对齐**。
打包产物：`_agent\自适应核生态模拟器-v2.1.0.zip`；打包源目录：`_agent\release_v210\`；
打包脚本：`_agent\qa\package_v210.ps1`（可重复执行）。

### 6.1 修复：启用材质包后小球/蓝细菌全部隐身

根因（详见 `_agent/qa/ROOT_CAUSE.md`）：`web/app.js` 的 `parseColors()` 从
`resourcepacks/<包>/colors.txt` 解析出的键名是 **snake_case**（`ball_shield`、
`free_ball`、`cyano_node`…），而 `applyPaletteToRenderer()` 读的是 **camelCase**
（`p.ballShield`、`p.freeBall`…）。于是启用任意 `mode = type_color` 的材质包时
`typeModeColors` 始终是 `[0,0,0]`，`stampBalls()` 把 `0`（alpha=0，全透明）写进像素缓冲
→ 球 100% 隐身；核走 `ctx.arc/stroke` 直接绘制，所以「只有核还能看见」。
单段键名（`background`、`nucleus`）两种写法同形，因此背景与核配色是生效的——
「尺寸/背景生效、球色失效」正是这条根因的指纹。

修复：新增 `pickArr(p, camel, snake)` 兼容层 + `clampByte` 越界夹取；两种键名都取；
并加**两道兜底**：①`mode==='type_color'` 时若某类型色仍为 0，回落 `ownerColors` 第一组；
②`stampBalls()` 里 `col === 0` 时回落 `freeColorWord`。任何路径都不会再写出透明球。

> 回归教训：`node --check`、DOM id 审计、CSV 逐字节回归**都不经过「浏览器里真的画出来没有」**。
> 前端改动必须用无头浏览器实拍验证。本轮建立的截图工具链：
> `_agent/qa/shoot.ps1`（无头 Edge 截图）、`_agent/qa/replica.js`（按 app.js 数学离线重算像素）。

### 6.2 修复：画布被拉伸（核画成椭圆）

原先 canvas 位图固定 1280×720、CSS `width/height:100%` 铺满 stage，非 16:9 窗口下
圆形的核被拉成椭圆。改为 `resizeCanvas()` 跟随 `#stage` 实际尺寸重建位图
（等比、单边 ≤1600、总像素 ≤1.6M、幂等），`W/H/trail/buf` 改为可重建；
宽高比与容器一致 ⇒ 核恒为正圆，指针映射公式不变仍然准确。

### 6.3 UI 与操作便捷性

- 暗色设计令牌统一；HUD 改 2×2 等宽卡片 + `tabular-nums`；控制条分「播放 / 数据 / 视图」三组；
  `select#speed` 暗色化；tab 胶囊化；徽章语义色 + 状态圆点；`退出` 危险色。
- 图例默认收起（`<details>`），展开含完整快捷键说明；快捷键拆固定两行避免孤字。
- **侧栏可折叠**（`#btn-sidebar`，状态存 `localStorage`），折叠后 `resizeCanvas()` 立即放大画面。
- **快捷键**：空格 暂停/继续 · N 单步 · S 保存 · R 重置视图 · Esc 返回菜单 · 1/2/3/4 切页 · +/− 缩放；
  输入框/按钮聚焦时智能让行；三处可发现的提示（按钮 title、图例、侧栏）。
- 双击画布重置视图；控制条 `− 倍率 +` 缩放控件；暂停角标；快速设置值记忆（`localStorage`）；
  未启动空状态引导文案；窄屏（≤900px）HUD 单列紧凑模式。
- 版本号两处同步为 **2.1.0**（`src/main.cpp` 的 `kVersion` + `web/index.html` 顶栏）。

### 6.4 启动器内核对齐（重要）

此前仓库根 `simulator.exe` 是旧的 **v1.1.0** 二进制（快照 v1，64B 头 / 28B 核记录），
而 `web/app.js` 只接受快照 v2 —— 双击 `启动器.bat` 会调到它，结果是**画面全黑、连核都没有**。
本轮已用 v2.1.0 构建覆盖根目录 exe，旧文件留档为 `simulator_v1.1.0_old.exe`。

### 6.5 本轮回归证据

- 构建：0 error / 39 warning（与 v2.0.0 基线同类，C4099×38 + C4996×1，无新增警告种类）。
- `#13 关闭等价`：`balls_005000.csv` / `nuclei_005000.csv` / `survivors.csv`
  与冻结基线**逐字节一致**（`E0EB7319…` / `BD13B8BE…` / `BD13B8BE…`）。
- `--schema-check` 通过；`src/` 仅 `main.cpp` 的版本字符串改动（1 行）。
- 前端：`node --check` exit 0；`_agent/qa/ui_check.js` 128 PASS、`fix_check.js` 56 PASS、
  `struct_check.js` 7 PASS；无头 Edge 实拍覆盖 1400×900 / 1024×700 / 800×600、
  菜单 / 运行 / 暂停 / 结束四态、图例展开、侧栏折叠、参数·Mod·存档页。截图存于 `_agent/qa/shot/`。

### 6.6 打包注意（沿用 + 新增）

- 包内仍**不放** `env_config.txt`（仓库根那份含 `ball_loss_cost = 1.5` 等非默认值）。
- 包内**新增 `packs.list`**（默认 `big_beads`），装好即是最新配色；可在 Mod 页切换/关闭。
- 包内不放 `survivors.csv`（测试残留）与 `mods.list`（蓝细菌默认关闭，需要时复制
  `mods.list.example`）。

---

## 7. v2.2.0 发布记录（全元素可见 + 实时环境调控）

发布日期：本轮。内容：**修复全元素显示 + 四轴快调/细调 + 影响力/事件卡 + 蓝细菌存档 v2**。
打包产物：`_agent\自适应核生态模拟器-v2.2.0.zip`；打包脚本：`_agent\qa\package_v220.ps1`。

### 7.1 显示修复

- 蓝细菌连线默认 `rgba(...,0.7)` + 线宽 1.5；材质包新增 `cyano_link_alpha`（0.35~1）。
- 节点半径夹 [1.6,6]、深色描边；`flat` 首帧显式填充色；球/核低亮度提亮与 halo。
- 新增 `window.__simDebug` 渲染自检与 HUD 折叠行。
- 存档 v2 保存蓝细菌节点/连接/nextNodeId/色相 RNG/环境状态；读档不再丢失元素。

### 7.2 实时环境调控

- 四轴五档快调 + 细调抽屉（11 个 ParamSchema 键）+ 影响力预算 + 4 张事件卡。
- 新接口 `GET /env`、`POST /env`；运行/暂停 ≤1 帧生效，菜单写入下次启动配置。
- `climate_event` 改为自动事件适配器，与事件卡共用 `World::applyEnvEvent`，不重复结算。
- 详见 `docs/ENV_CONTROLS.md`。

### 7.3 发布/打包注意

- 包内仍不放个人 `env_config.txt`；默认演示由启动器“启用蓝细菌/推荐生态 mod”复选框保证。
- 根 `env_config.txt` 已调整为演示平衡值（蓝细菌 150、球 300、核 12、食物补充 0.01）。

---

## 8. v2.2.1 发布记录（图例动态化 + 球型 + 手动事件无冷却）

发布日期：本轮。打包产物：`_agent\自适应环绕核生态模拟器-v2.2.1.zip`；
打包脚本：`_agent\qa\package_v221.ps1`。

- 图例色块改为按当前材质包/快照生成，三类有主球增加形状区分（方/菱/十字）。
- 手动事件取消冷却，只保留影响力消耗；旧档冷却字段读档清零。
- 快照 v2 / 存档 NSMS v2 不变；确定性基线 7 个 CSV 哈希与 v2.1.0 一致。
- 包内不放个人 `env_config.txt`，与 v2.2.0 相同。
- 版本号三处：`src/main.cpp`、`web/index.html`、本文档/README。
- 回归：无改动时关/开蓝细菌 5000 帧 CSV 与 v2.1.0 基线逐字节一致；`--schema-check` 通过；
  `tests/baseline/v2.1.0-baseline.txt` 保存基线哈希。
