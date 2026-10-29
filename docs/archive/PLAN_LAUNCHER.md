# 启动器 + 可视化 Mod —— 可行性评估与实施计划（v2）

> 修订要点（相对 v1）：
> ① 技术栈放宽——模拟核心保持 C++，启动器与可视化用 Web 前端（HTML/JS/Canvas），不限于 C++；
> ② 可视化升级为**逐球可见**（每球独立绘制 + 轨迹）；
> ③ 启动器新增**「我的存档」**功能；
> ④ Mod 支持**可调优先级**与冲突提示；
> ⑤ 新增**材质包**体系，与玩法包解耦（类比 Minecraft 的 mod / 资源包 / 世界存档三件套）。

---

## 1. 结论（可行性评估）

| 需求 | 现状基础 | 可行性 | 方案要点 |
|------|----------|--------|----------|
| 调节游戏参数 | `env_config.txt`（约 50 键，5 类）+ 命令行覆盖链已存在 | **高** | 参数页表单 → 写配置 → 带参启动；参数元数据做成单一 schema 表，UI 自动生成 |
| 管理玩法 mod + 可调优先级 | `docs/PLAN.md` Phase 0 已定义 ModAPI + registry，**尚未实施**（代码中无 EventBus/ModAPI） | **高** | 先落 Phase 0；`mods.list` 有序清单 = 注册顺序 = 事件派发顺序（PLAN.md 已规定插入序派发），拖拽排序即调优先级；冲突靠描述符提示，不做自动裁决 |
| 可视化 mod（逐球可见） | 渲染器是控制台字符版，无 IRenderer 抽象；`World` 只读访问器齐备；`main.cpp` 渲染点清晰 | **高** | `WebRenderer : IRenderer` + 本地 HTTP 服务器；每球 8 字节逐球流 + 浏览器轨迹缓冲渲染，5000 球 ~60FPS |
| 我的存档 | World 全为 POD 数据（balls/nuclei/prevOwner/rng）；`std::mt19937` 支持标准序列化（`operator<<` 往返） | **中高** | 版本化二进制 save.bin + meta + 浏览器回传缩略图；读档续跑与不中断运行**逐帧一致** |
| 材质包（资源包） | 可视化层在浏览器，前端资产天然可换皮 | **高** | `resourcepacks/<名>/` 文件夹：调色板 + 贴图 + 皮肤，行式配置，堆叠顺序覆盖，与玩法 mod 完全解耦 |

总评：**全部可行**。架构不变（单 exe 自托管 Web），功能扩展为 Minecraft 式三件套：**玩法 mod / 材质包 / 存档**，全部由同一个启动器页面管理。

---

## 2. 技术栈与形态（回应「不一定只能用 C++」）

- **模拟核心（World/物理/遗传/确定性/性能）必须 C++**：5000 球 60FPS 与同种子逐字节确定性是硬约束，也是本项目立身之本。
- **启动器 + 可视化 = Web 前端（HTML/JS/Canvas/WebGL，非 C++）**，由 C++ 内嵌的极简 HTTP 服务器承载。前端可自由使用 JS 生态（原生 Canvas 起步，将来可选 three.js 等），全部不进入 C++ 依赖。
- **桌面壳可后装**：`simulator.exe` 对外暴露的 HTTP API 是稳定接口——将来若想要 Electron/Tauri/Python 独立启动器（原生文件对话框、多版本管理等 Minecraft 启动器体验），引擎侧零改动即可接入。首期不做（见 §11）。
- 结论：C++ 只写「引擎 + 状态服务器 + 存档序列化」，其余全是 Web 技术。零第三方运行时依赖仍然成立（用户只需 exe + 浏览器）。

---

## 3. 目标架构

```
simulator.exe（单 exe，双模式）
├─ 模拟模式（现状路径，行为不变）
└─ 启动器模式（--serve）
    ├─ HttpServer（WinSock，仅绑定 127.0.0.1:8765，端口可配/自动避让）
    │     GET /             → 启动器页面（web/ 目录下 index.html）
    │     GET /schema       → 参数 schema JSON（由 C++ 参数表生成）
    │     GET /state        → 最新一帧快照（二进制 ArrayBuffer：逐球 + 核 + 统计）
    │     GET /mods         → 玩法 mod 清单（名称/描述/开关/顺序/冲突标记）
    │     GET /packs        → 材质包清单 + 堆叠顺序
    │     GET /saves        → 存档列表（meta 摘要）
    │     POST /control     → {"cmd":"start|pause|resume|step|stop"}
    │     POST /run         → {params 全集, seed, balls, nuclei, frames} 启动模拟
    │     POST /save-config → 写回 env_config.txt（设为默认）
    │     POST /set-mods    → 写回 mods.list（有序清单）
    │     POST /set-packs   → 写回 packs.list（堆叠顺序）
    │     POST /save        → {"name"} 落盘 save.bin + meta
    │     POST /load        → {"name"} 恢复世界（停在暂停态）
    │     POST /save-thumb  → 浏览器 canvas 截图（PNG base64）存入存档
    │     POST /delete-save → 删除存档
    └─ 启动器 UI（四页，浏览器渲染）
          ├─ 运行页：快速设置（seed/balls/nuclei/frames）+ 启停/暂停/单步 + 实时 canvas（逐球）
          ├─ 参数页：约 50 参数按 5 类分组编辑 + 保存默认/恢复默认（schema 驱动）
          ├─ Mod 页：两个标签——「玩法包」（开关/拖拽排序/冲突提示）+「材质包」（堆叠/换皮）
          └─ 存档页：存档列表（缩略图/帧号/种子/参数摘要）+ 载入/删除/重命名
```

### 数据流（关键设计）

```
模拟主循环（主线程）                    HTTP 工作线程
World::step() ×N
  └─ 每 render_interval 帧：
      WebRenderer 构建快照 ──mutex──▶ 最新快照缓冲（原子替换）
      · 小球 → 逐球：x,y(16bit) + 类型(2bit) + 归属核id(15bit) = 8B/球
      · 核   → 位置/能量/关键遗传参数（逐点）
      · 统计 → 种群/平均能量/各类型球占比
                                       浏览器 fetch /state（~30Hz 轮询）
                                         → canvas 轨迹缓冲渲染（逐球 + 拖尾）
```

---

## 4. 可视化设计：逐球可见（v2 核心变化）

- **数据量**：每球 8 字节（x、y 各 uint16，世界 2000×2000；类型 2bit；ownerId 15bit，-1=自由球）。5000 球 = **40KB/帧**，30Hz 轮询 = 1.2MB/s，本地回环无压力；C++ 侧一帧一次 memcpy 即可。
- **渲染 v1：Canvas 2D ImageData 轨迹缓冲**——世界坐标映射到视口像素，每球写 2~4 像素点；每帧整图 alpha×0.9 渐隐 → 天然产生运动拖尾。5000 球每帧 ≈ 6 万次 uint8 写入 + 一次 putImageData，60FPS 无压力。纯原生 Canvas，无 WebGL 门槛。
- **配色语义**（默认材质，材质包可覆盖）：
  - 球型着色：护盾=蓝 / 资源=绿 / 侦察=橙；
  - 归属色相：ownerId → 黄金角散列映射色相（同核球群同色，自由球灰白）→ **一眼看出球群归属与争夺**；
  - 核：大圆点 + 能量环 + 悬停显示 22 参数摘要。
- **交互**：缩放/平移（拉近看单个球、拉远看全貌）、暂停/单步、速度调节、点击核查看参数。
- **可视化效果预期**：环绕轨道、球群拖尾、追击/逃跑、繁殖分裂、能量环消长——全部肉眼可辨。
- **v2 可选**：WebGL 实例化点精灵（发光/贴图），仅前端改动。
- 热力网格降级为**可选叠加层**（统计模式），不再是唯一视图。

---

## 5. 我的存档

- **目录结构**：`saves/<存档名>/save.bin + meta.txt + thumb.png`（类比 Minecraft 每世界一文件夹）。
- **save.bin（版本化二进制）**：magic + 格式版本 + config/mod 清单哈希 + `rng` 状态（`std::mt19937` 标准 `operator<<` 序列化往返）+ `frame_` + 全部 balls（位置/速度/类型/归属 + prevOwner）+ 全部 nuclei（22 参数 + 能量/年龄/游走角/稳定 id）+ `nextNucleusId_`。SpatialGrid 不存，读档时重建。
- **meta.txt**：行式 key=value（名称/时间/帧号/种子/参数摘要/启用 mod 列表）——C++ 与 JS 都能解析。
- **thumb.png**：浏览器把当前画面 `canvas.toDataURL` → POST /save-thumb（C++ 不写 PNG 编码器）。
- **流程**：暂停 → 点保存 → C++ 落盘 → 前端截图回传缩略图；载入：POST /load → 重建 World + 重建 grid → 停在暂停态供继续；删除/重命名走 UI。
- **mod 状态**：ModAPI 扩展 saveState/loadState 钩子（首期可为空实现，接口先占位）。
- **验收硬指标**：**save → load → 继续运行 与 不中断直跑逐帧完全一致**（rng 状态完整恢复）；坏文件/版本不符时友好报错而非崩溃。

---

## 6. Mod 优先级与冲突（玩法包）

- **优先级 = 加载顺序**：`mods.list` 有序行式清单 = 注册顺序 = EventBus 派发顺序（PLAN.md 已规定「emit 按插入顺序遍历」）→ Mod 页**拖拽排序**即调优先级，确定性由固定顺序保证。
- **mod 描述符**：registry 中每 mod 附带 `{name, 描述, 默认开关, touches[](影响的配置键/EnergyReason), incompatible[]}`。
- **冲突提示（轻量，不做自动裁决）**：启用集合中 `touches` 相交 → 启动前黄条警告「可能冲突，请调整优先级」；`incompatible` 命中 → 红条。**用户排序为最终裁定**（自动解决冲突属复杂问题，明确不做）。
- **每 mod 独立参数文件**：`mods/<name>.conf`（key=value，复用现有行式解析器）→ 从根上杜绝全局键打架。
- 玩法包 = C++ 编译内注册（manifest 开关）；DLL 动态加载仍为可选后期（见 §11）。

---

## 7. 材质包（资源包）

- **目录结构**：`resourcepacks/<名字>/` 内：
  - `pack.txt`：行式清单（name/desc/version/author）；
  - `colors.txt`：行式调色板（三类球色、核色、自由球色、背景色）；
  - `sprites/`：可选 PNG（核/球贴图，仅浏览器用）；
  - `theme.css`：可选页面皮肤。
- **行式配置而非 JSON**：C++ 与 JS 共用同一解析思路；PNG/CSS 仅浏览器消费。
- **堆叠**：多个包按 `packs.list` 顺序叠加，**后者覆盖前者**（Minecraft 资源包栈逻辑）；Mod 页第二个标签管理，拖拽排序。
- **可选**：控制台字符渲染也读 `colors.txt` 换肤（成本低，非必须）。
- **与玩法包完全解耦**：玩法 = C++（改规则），材质 = 前端资产（改观感）。

---

## 8. 分阶段实施

### Phase 0（1 单位，前置）：Mod 基础架构 —— 直接执行 docs/PLAN.md

按 PLAN.md 全量落地（EventBus / EnergySystem / ModAPI / mods_registry / IRenderer / 事件发射 / README 指南 + 验收清单）。可视化依赖其中 Step 5（IRenderer）插槽；Mod 管理依赖 Step 1~4。回归保障（不注册 mod 时同种子输出一致）必须全绿。

### Phase A（2.5 单位）：可视化 mod —— 逐球可见

1. **A1** IRenderer 插槽：`Renderer` → `ConsoleRenderer : IRenderer`，`main` 持有 `unique_ptr<IRenderer>`（PLAN.md Step 5）。
2. **A2** 极简 HTTP 服务器（`src/HttpServer.h/.cpp`，WinSock，~400 行）：仅 127.0.0.1；GET 静态 + /state、少量 POST；Content-Length 固定、无 keep-alive 复杂逻辑；端口 8765 占用自动 +1。
3. **A3** 帧快照（`src/WebRenderer.h/.cpp`）：实现 IRenderer，每 render_interval 帧构建逐球 8B 流 + 核点 + 统计，写入互斥保护的快照缓冲（只读 World，不碰 rng、不改状态）。
4. **A4** 前端（`web/index.html` + `web/app.js`，运行时从 exe 旁 `web/` 目录读取，开发期直改直刷）：ImageData 轨迹缓冲渲染 + 缩放平移 + 暂停/单步/速度 + 悬停看核参数。
5. **A5** 接线：`--serve` 模式启动服务器 + `ShellExecute` 打开浏览器；模拟结束回菜单态，`survivors.csv` 照常写出。
6. **验收**：浏览器逐球轨迹实时可见（环绕/归属/争斗肉眼可辨）；5000 球/500 核模拟 ≥60FPS 不降；同种子无渲染路径输出不变。

### Phase B（2 单位）：启动器 —— 参数页 + 运行控制

1. **B1** 参数 schema 单一来源（`src/ParamSchema.h/.cpp`）：`WorldConfig` 约 50 字段的元数据（键/类型/范围/默认/分组/中文描述）收敛为一张 C++ 表，驱动 UI 生成（GET /schema）、JSON↔WorldConfig 转换（POST /run）、env_config.txt 写出。`EnvConfig::load` 保持现状为权威解析器，加**键覆盖测试**防漂移。
2. **B2** 参数页 UI：5 类分组 + 范围校验 + 「恢复默认」；「保存为默认」写回 `env_config.txt`（带注释、与现状格式兼容）。
3. **B3** 运行控制：POST /run → 服务器写临时 `run_config.txt` → **同进程**重建 World 运行（单 exe 无子进程管理）；/control 实现 暂停/继续/单步/停止；运行中参数只读，结束回菜单。
4. **B4** 运行时 HUD：种群曲线、平均能量、核数量、三类球占比、能量分布直方图——全部来自快照统计。
5. **验收**：改任意参数启动可见行为差异；保存默认重启保持；同种子命令行/页面启动结果一致。

### Phase C（1.5 单位）：Mod 中心 —— 玩法包 + 优先级

1. **C1** mod 描述符 + `mods.list` 有序清单 + 「读清单 → 固定顺序注册」。
2. **C2** Mod 页（玩法包标签）：列表/开关/拖拽排序/重启生效 + 冲突黄红条（touches/incompatible）。
3. **C3** `mods/<name>.conf` 独立参数文件。
4. **验收**：periodic_drain 开关行为可复现；调换两 mod 顺序结果变化、同序同种子逐字节一致；冲突提示正确出现。

### Phase D（1.5 单位）：我的存档

1. **D1** 版本化二进制序列化 + rng `operator<<` 往返 + 读档重建 SpatialGrid。
2. **D2** meta.txt + 缩略图上传 + 存档页（列表/载入/删除/重命名）。
3. **D3** ModAPI saveState/loadState 钩子（占位）。
4. **验收**：save→load→continue 与直跑逐帧一致；坏文件/版本不符友好报错；缩略图正确显示。

### Phase E（1 单位）：材质包

1. **E1** 包扫描 + `packs.list` 堆叠顺序。
2. **E2** 前端加载 colors/sprites/theme；Mod 页「材质包」标签。
3. **E3**（可选）控制台渲染读 colors.txt 换肤。
4. **验收**：装两个包拖换顺序视觉变化；无包时默认观感不变。

**顺序：0 → A → B → C → D → E**（D/E 可互换；E 仅依赖 A）。

---

## 9. 关键技术决策（v2 更新）

| 决策点 | 选择 | 理由 |
|--------|------|------|
| 技术栈 | 引擎 C++ + 启动器/可视化 Web 前端（HTML/JS） | 性能与确定性靠 C++；UI 用 Web 生态，桌面壳可后装 |
| 可视化粒度 | **逐球 8B 流 + ImageData 轨迹缓冲**；热力图降为可选叠加 | 满足「看到各个球」；5000 球 60FPS 可达成 |
| 帧传输 | 二进制 ArrayBuffer，30Hz 轮询 | 40KB/帧，本地回环无压力 |
| 模拟运行位置 | 启动器进程内（同进程重建 World） | 单 exe 无子进程管理；快照共享内存 |
| 前端资源 | exe 旁 `web/` 目录 | 开发期直改直刷；发布时可打包 |
| mod 优先级 | `mods.list` 有序清单 = 注册/派发顺序；冲突只提示不自动裁决 | 确定性 + 用户最终裁定 |
| 存档 | `saves/<名>/` 三件套；二进制版本化 + rng 标准序列化 | 读档续跑逐帧一致；缩略图由浏览器回传 |
| 材质包 | 行式配置 + 前端资产栈；与玩法包解耦 | C++/JS 共用解析；换皮不碰引擎 |

---

## 10. 风险与对策（v2 更新）

| 风险 | 等级 | 对策 |
|------|------|------|
| 手写 HTTP 服务器健壮性 | 中 | 接口最小化；只绑 127.0.0.1；Content-Length 固定；客户端异常只关连接不崩进程 |
| 逐球帧带宽/渲染 | 低 | 8B/球二进制 + ImageData 整图一次 putImageData；5000 球实测余量充足 |
| 存档格式漂移（结构体改动） | 中 | magic + 格式版本号；每阶段回归测试覆盖读写往返；版本不符友好报错 |
| rng 序列化往返不精确 | 低 | 用标准 `operator<<` 序列化 + 「读档续跑与直跑逐帧一致」验收兜底 |
| 确定性回归（新增代码污染 rng/迭代顺序） | 中 | 快照/HTTP/UI 全部只读 World；每阶段同种子基线对比 |
| 参数表与 EnvConfig 漂移 | 低 | B1 键覆盖测试纳入验收 |
| 前端复杂度上升 | 低 | app.js 模块化；材质包为纯前端资产，不碰引擎 |
| 端口冲突/浏览器未自动打开 | 低 | 端口自动递增重试；失败打印 URL 手动访问 |

---

## 11. 明确不做（本期，防范围蔓延）

- 不做 DLL 动态 mod（可选后期）；不做 mod **自动**冲突解决（只提示）；不做 mod 在线商店/下载。
- 不做存档云同步、跨版本自动迁移（仅提示版本不符）。
- 不做 WebGL 发光特效（v2 可选）；材质包 v1 不含音频。
- 不做独立桌面启动器壳（Electron/Tauri/Python）——HTTP API 已为其预留，随时可后装。
- 不引入 C++ 第三方库；不改 22 遗传参数含义与 CSV 列顺序；不改 `env_config.txt` 键语义与加载优先级。

---

## 12. 验收总表（端到端场景）

- [ ] 双击 `simulator.exe` → 浏览器自动打开启动器。
- [ ] 运行页：**逐球轨迹实时可见**（环绕/归属/争斗可辨），缩放平移、暂停/单步/停止、结束回菜单、`survivors.csv` 正常写出。
- [ ] 参数页：改 5 个不同类别参数启动行为差异可见；保存默认重启保持；恢复默认生效。
- [ ] Mod 页：玩法包开关/拖拽排序/冲突黄红条；材质包堆叠换皮。
- [ ] 存档页：保存/载入/删除/重命名；**载入续跑与直跑逐帧一致**；缩略图正确显示。
- [ ] 命令行模式（`--render`/`--no-render`）零回归；`g++ -Wall -Wextra` 零警告；5000 球/500 核 ≥60FPS 不降。

---

## 13. 工作量预估与顺序

| 阶段 | 内容 | 相对工作量 |
|------|------|-----------|
| Phase 0 | EventBus/EnergySystem/ModAPI/IRenderer（PLAN.md 现成规格） | 1 |
| Phase A | 可视化 mod（逐球 + HTTP 服务器 + 前端） | 2.5 |
| Phase B | 启动器：参数页 + 运行控制 + HUD | 2 |
| Phase C | Mod 中心：玩法包 + 优先级 + 冲突提示 | 1.5 |
| Phase D | 我的存档 | 1.5 |
| Phase E | 材质包 | 1 |

合计约 **9.5 单位**。顺序：**0 → A → B → C → D → E**，每阶段完成即编译 + 跑验收，再进下一阶段。

---

## 14. 实施状态（更新于 2026-08-17）

- [x] **Phase 0 完成**：EventBus / EnergySystem / ModAPI / mods_registry / IRenderer / ConsoleRenderer / 事件发射 / README Mod 指南。验收：g++ `-Wall -Wextra` 零警告；不注册 mod 时 survivors/trend/nuclei 快照与基线**逐字节一致**；示例 mod 开关可复现（PLAGUE 收支出现）；`--energy-summary` 正常；满负载（5000 球/500 核，ball_loss_cost=0）60.2 FPS ≥60。
- [x] **Phase A 完成**：HttpServer（WinSock，GET 静态 + /state + POST /control）、WebRenderer（逐球 6B 快照 + 核点 + 统计，快照格式见 `src/WebRenderer.h`）、前端 `web/`（ImageData 轨迹缓冲逐球渲染 + 缩放/平移/暂停/单步/速度/悬停/结束覆盖层）、`--serve`/`--port`/`--no-browser` 接线 + 浏览器自动打开。验收：端点 200；暂停帧号稳定、单步推进；serve 模式 survivors.csv 与基线逐字节一致；结束原因 UTF-8 正确显示；退出指令干净关停。
- [x] **启动器成为主体**：双击 `simulator.exe`（无参数）直接进入启动器；控制台模式改为 `--console`（带参数时默认控制台，批量脚本不受影响）。`启动器.bat` 亦可用。
- [x] **Phase B 完成**：ParamSchema 单一来源表（80 参数：WorldConfig + ParamRanges 全部字段，offsetof 驱动；`--schema-check` 键覆盖测试通过）；GET /schema（含 env 当前值）；POST /run（key=value 行，env 为基准 + 覆盖）；POST /save-config（schema 生成 env_config.txt）；LauncherApp 状态机（菜单→运行→结束→回菜单/退出，ModAPI 生命周期绑定 World 修复了回调悬空崩溃）；运行页快速设置 + 控制条 + HUD；参数页 5 组折叠表单 + 恢复默认 + 保存默认。
- [x] **Phase C 完成（后端 + UI）**：mods_registry 元数据化（ModInfo：touches/incompatible）；mods.list 有序清单 = 优先级 = 事件派发顺序；GET /mods 冲突检查（黄条警告/红条互斥）；POST /set-mods；Mod 页（勾选启用 + ↑↓ 调序 + 本地冲突提示）；双示例 mod（periodic_drain / plague_strike，touches 均含 PLAGUE 演示冲突）。验收：同序两次输出一致（104 核）、换序结果不同（90 核）。
- [x] **Phase D 完成**：World 版本化二进制序列化（magic NSMS + 版本 + schema 配置段 + rng `operator<<` 往返 + 球/核/遗传参数全量，读档重建 SpatialGrid）；`saves/<名>/`（save.bin + meta.txt + thumb.png，缩略图由浏览器截屏回传 base64）；存档页（缩略图/载入/删除/重命名，中文名经 URL 百分号解码）；mod saveState/loadState 占位钩子；WinFs 工具层（UTF-8 → Win32 W-API，修复 GBK argv 路径下 std::filesystem 抛"非法字节序列"的崩溃）。验收：**存→续 vs 存→载→续 vs 直跑 三路 survivors 逐字节一致**；坏文件/版本不符友好报错。
- [x] **Phase E 完成**：resourcepacks/<包>/（pack.txt + colors.txt，行式调色板）；packs.list 堆叠顺序（后者覆盖前者）；GET /packs、POST /set-packs、/rp/ 静态路由；Mod 页「材质包」标签（勾选/↑↓/即时换肤）；渲染器支持 type_color 与 owner_hue 两种模式 + 背景/自由球/核芯配色；内置示例包 classic_flat。
