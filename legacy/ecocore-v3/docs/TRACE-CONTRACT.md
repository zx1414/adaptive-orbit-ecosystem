# Trace JSON 契约

**当前版本：v3**（2026-11-01，R1-B 取消食物球后重定）。

## v3 变更摘要（相对 v2）

| 变更 | 说明 |
|---|---|
| `schemaVersion` / `meta.contractVersion` | 2 → **3**（两处必须同步） |
| series 行 | 删除 `food` 键；键序变为 `tick, nuclei, cyanoAlive, cyanoEnergy, totalEnergy, injected, dissipated, auditOk` |
| 帧内 `"f"` 槽位 | **语义复用**：从"食物元组数组"改为"蓝细菌连线拓扑" `[[idA,idB],...]`。键名保留 `f` 是刻意的——帧内键序 `tick, n, b, f, c` 不变，避免前端解析器大改；空拓扑 = 空数组 |
| 帧内 `"c"` 元组 | 5 项 → **6 项** `[id, x, y, energy, alive, hue]`（hue ∈ [0,360)），按 6 项解析，`alive` 不再是收尾布尔 |
| 死亡节点 | 容器里不再有死节点（饿死/被捕食在帧末压缩），前端"死亡节点"逻辑可删 |

## v3 完整结构

```json
{
  "schemaVersion": 3,
  "meta": { "contractVersion": 3, "seed": 42, "ticks": 20000, "policy": "rule",
            "framesCaptured": 1000, "framesTruncated": false },
  "bounds": { "minX": 0, "minY": 0, "maxX": 256, "maxY": 256 },
  "series": [
    { "tick": 1, "nuclei": 12, "cyanoAlive": 42, "cyanoEnergy": 812.5,
      "totalEnergy": 3662.2, "injected": 1200.0, "dissipated": 300.1, "auditOk": true }
  ],
  "frames": [
    {
      "tick": 50,
      "n": [[id, x, y, energy, cap, speed, alive], ...],
      "b": [[id, owner, x, y, energy, radius, type], ...],
      "f": [[idA, idB], ...],
      "c": [[id, x, y, energy, alive, hue], ...]
    }
  ]
}
```

### 字段语义

- `n` 核元组（7 项）：`id, x, y, energy, cap, speed, alive`
- `b` 球元组（7 项）：`id, owner, x, y, energy, radius, type`；`type` 0=Resource 1=Scout 2=Shield；
  `owner = 0` 表示自由球
- `f` **蓝细菌连线**（v3 语义）：`[idA, idB]`，按节点下标升序存，`idA < idB`
- `c` 蓝细菌元组（**6 项**）：`id, x, y, energy, alive, hue`

### 帧内键序固定
`tick, n, b, f, c` —— 解析器按此顺序读，不得依赖 JSON 对象键顺序（JSON 本身无序）。

### 数值格式
- 全部浮点 `%.6f`（NaN/Inf 塌缩为 `0.000000`）
- 实体顺序 = 容器下标顺序（双跑一致性红线，**不许排序**）

### 截断保护
- `frames` 受 `maxFrames`（默认 10000）与 `maxFrameBytes`（默认 64MB）双上限
- 触发时 meta 写 `framesTruncated: true` + `framesCaptured: N`；`series` 永不截断

---

# 历史（v2 及更早，已废弃，保留作参考）

# 轨迹 JSON 契约 v2（前端唯一数据来源）

> **这份文件是冻结契约。** C++ 侧（`src/app/TraceWriter.cpp`）按它写出，
> 前端（`frontend/`）按它读入。**两边独立迭代，谁都不许单方面改契约**；
> 要改必须先改这份文件并把 `schemaVersion` 加一。

## v2 变更记录（T29，2026-10-31）

1. `schemaVersion` **1 → 2**；`meta` 新增 `"contractVersion": 2`（与顶层
   `schemaVersion` 同步写入，冗余字段，前端认哪个都行）。
2. 每帧新增 `"c": [[id, x, y, energy, alive], ...]`——**蓝细菌元组**，
   5 项，格式与食物 `f` 完全一致。帧内键序：`tick, n, b, f, c`。
   元组顺序 = `Simulation::cyanoNodes()` 容器下标（双跑一致性红线，不许排序）；
   死节点也导出（`alive=false`，与食物同语义——前端可见"死亡"这一事实）。
   坐标已是环面内坐标（`CyanoField` 内部 wrap），前端无需再折。
3. `series` 每行新增 `"cyanoAlive": N`（存活蓝细菌数）与
   `"cyanoEnergy": F`（存活蓝细菌能量总和）。追加键位于 `food` 之后、
   `totalEnergy` 之前；`alive/energy` 只算 alive 节点，与账本 `sumLiving` 口径一致。
4. **v1 兼容语义**：`c` 为空数组时前端回退渲染食物即可（前端 parser 已容错）。
   v1 文件（无 `c` 键、无 `contractVersion`、series 无 cyano*）按旧方式读。

## 顶层结构

```json
{
  "schemaVersion": 2,
  "meta": {
    "seed": 42,
    "ticks": 1200,
    "world": { "width": 256.0, "height": 256.0 },
    "policy": "rule",
    "generator": "agentsim",
    "contractVersion": 2,
    "framesCaptured": 1200,
    "framesTruncated": false
  },
  "bounds": { "minX": 0.0, "minY": 0.0, "maxX": 256.0, "maxY": 256.0 },
  "series": [
    { "tick": 0, "nuclei": 12, "food": 64, "cyanoAlive": 32, "cyanoEnergy": 0.0,
      "totalEnergy": 1800.5, "injected": 1800.5, "dissipated": 0.0, "auditOk": true }
  ],
  "frames": [
    {
      "tick": 0,
      "n": [[id, x, y, energy, cap, speed, alive], ...],
      "b": [[id, ownerId, x, y, energy, radius, type], ...],
      "f": [[id, x, y, energy, alive], ...],
      "c": [[id, x, y, energy, alive], ...]
    }
  ]
}
```

## 字段说明

| 字段 | 类型 | 说明 |
| --- | --- | --- |
| `schemaVersion` | int | **v2 起固定 2**。前端不认的版本要**明确报错**，不要静默降级 |
| `meta.contractVersion` | int | 与 `schemaVersion` 同步（冗余，2） |
| `meta.world` | object | 环面尺寸，前端用来画 wrap 接缝 |
| `bounds` | object | 世界包围盒。因为世界是环面，**它就是 [0,w]×[0,h]**，不随实体位置变化 |
| `series` | array | **每帧一条**，供折线图用。数据量小，前端不必降采样 |
| `series[].cyanoAlive` | int | 本 tick 存活蓝细菌数（v2 新增） |
| `series[].cyanoEnergy` | float | 存活蓝细菌能量总和（v2 新增） |
| `frames` | array | 空间快照，供画布用。可**降采样**（`--trace-interval N`） |

### 实体元组（**定长数组，不是对象**，为了体积）

- **核** `n = [id, x, y, energy, cap, speed, alive]` — 7 项
- **球** `b = [id, ownerId, x, y, energy, radius, type]` — 7 项，
  `type`：`0=Resource, 1=Scout, 2=Shield`；`ownerId=0` 表示**自由球**
- **食物** `f = [id, x, y, energy, alive]` — 5 项
- **蓝细菌** `c = [id, x, y, energy, alive]` — 5 项（v2 新增，格式同食物）；
  `id` 从 1 起递增，与食物/核的 id 空间各自独立，`c` 元组只出现在 `c` 数组里，
  不会与 `f` 混淆

**为什么用定长数组**：一条 1200 帧 × 200 实体的轨迹，用对象约 2.4 MB，用数组约 0.6 MB。
双击打开的 HTML 里塞得下。

## 硬性要求（C++ 侧）

1. `--trace <path>` 写出上述 JSON；`--trace-interval N` 控制 `frames` 的降采样间隔
   （`series` 始终每帧一条，不降采样）。
2. 所有浮点**保留 6 位小数**（`%.6f`），保证双跑逐字节一致（S2 红线）。
3. **不得输出 NaN / Inf**：写之前统一钳制，NaN → 0。
4. 数组顺序**固定**：核按 `nuclei_` 下标、球按 `n.balls` 下标、食物按 `foods_` 下标、
   蓝细菌按 `cyano_.nodes()` 下标。顺序影响双跑一致性，不许按任何运行时属性排序。
5. 顶层键顺序固定：`schemaVersion, meta, bounds, series, frames`；
   帧内键顺序固定：`tick, n, b, f, c`。

## 硬性要求（前端侧）

1. **零依赖**：不引用任何 CDN、npm 包、外部字体。所有 CSS/JS 内联或同目录相对路径。
2. **`file://` 可运行**：不许用 `fetch()` 读本地文件（浏览器会 CORS 拦掉），
   数据用 `<script src="js/trace-data.js">` 形式载入，或让用户拖拽文件进来用 `FileReader` 读。
3. **单个 HTML 可交付**：提供 `frontend/index.html`（开发版，多文件）+ 一个打包产物
   （单文件，双击即用）。打包用 `node frontend/tools/bundle.mjs`，产物自带全部数据和代码。
4. 解析必须**校验** `schemaVersion`、剔除 NaN/Inf 实体、容忍 `frames` 为空。
5. **v2 新增**：`schemaVersion===2` 时读 `frames[].c` 渲染第一营养级；
   `c` 为空数组时回退渲染食物（v1 兼容语义，parser 已容错）；
   series 缺 `cyanoAlive`/`cyanoEnergy` 时按 0 处理（容忍 v1 文件）。

## 旧版功能对照（S25 判据）

旧版 `EcoScope` 有、新版也**必须**有的功能：

| # | 功能 | 旧版实现位置 |
| --- | --- | --- |
| 1 | 播放 / 暂停 / 单帧步进 / 跳到首尾 | `js/main.js` transport |
| 2 | 时间轴拖动 + 播放速度 0.5×–8× + 循环 | `index.html` `#timeline` `#speed` `#opt-loop` |
| 3 | 实时指标（生产者数 / 核数 / 总能量 / 光照） | `data-stat` |
| 4 | 种群折线图 + 总能量折线图 | `js/charts.js` |
| 5 | 画布渲染：辉光、拖尾、体型缩放 | `js/renderer.js` |
| 6 | 昼夜背景（随时间变化的底色） | `js/daylight.js` |
| 7 | 滚轮缩放 / 拖拽平移 / R 重置视角 | `js/renderer.js` 相机 |
| 8 | 影院模式（隐藏所有 UI） | `F` 键 |
| 9 | 拖拽载入轨迹文件 | `#dropzone` |
| 10 | 快捷键（空格、←/→、Home/End） | `js/main.js` |
| 11 | 空状态引导 | `#empty-state` |
| 12 | 暗色玻璃 + 极光背景的视觉风格 | `assets/styles.css` |

**新增（旧版没有，新版要有）**：

| # | 功能 | 理由 |
| --- | --- | --- |
| 13 | 核与球的**轨道可视化**（球绕核的连线） | 本项目核心是"环绕核"，旧版没有球 |
| 14 | `auditOk` 状态灯 | 审计恒等式是本项目红线，前端要能一眼看出 |
| 15 | 自由球与从属球用不同颜色 | S20 自由球是新增语义 |
| 16 | 环面接缝提示（实体跨边界时不闪断） | Torus 拓扑的视觉正确性 |
| 17 | 蓝细菌（第一营养级）画布渲染 + 种群折线 | v2 契约 `c` 数组与 series cyano* 字段（T29） |
