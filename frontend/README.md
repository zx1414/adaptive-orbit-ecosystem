# EcoScope — EcoCore v3 前端（独立展示层）

零依赖、纯静态、双击即用的可视化前端，用于回放 `simulator.exe --html <path>` 输出的自包含轨迹。

## 隔离红线（必须遵守）

- 本目录 `frontend/` 是唯一允许新增/修改的位置；**不触碰** `src/`、`docs/`、`CMakeLists.txt`、`tools/`。
- 只消费 `src/app/HtmlTraceWriter.cpp` 写出的 `const TRACE = {...};` JSON 契约（或等价裸 JSON），
  不调用模拟器、不引入服务器、不引入任何第三方库/CDN。
- 主仓的 M4 C++ 改动与本目录互不影响；两边可以并行开发。

## 使用

- `frontend/index.html`：开发版（多文件，经典脚本，file:// 直接可开）。
- `frontend/EcoScope.html`：单文件打包版，由 `node frontend/tools/bundle.mjs` 生成，双击即用。
- 打开页面后：拖入轨迹文件，或点「内置示例」（内嵌真实模拟数据：seed 42 / M2.5 平衡参数 / 1200 tick）。

## 数据契约

```json
{
  "bounds": {"minX":0, "minY":0, "maxX":2000, "maxY":2000},
  "frames": [
    {"tick":0, "producers":30, "agents":12, "totalEnergy":1260,
     "p":[[x,y,energy,cap],...], "a":[[x,y,energy,cap],...]}
  ]
}
```

解析器 `js/trace-parser.js` 做完整校验：bounds 有效、tick 非负且递增、剔除 NaN/Inf 实体。

## 结构

```
frontend/
├── index.html          开发版入口
├── EcoScope.html       单文件成品（bundle 产物）
├── assets/styles.css   设计系统（暗色玻璃 + 极光背景）
├── js/
│   ├── demo-data.js    内置示例（由真实模拟生成，勿手改）
│   ├── trace-parser.js 契约解析/校验
│   ├── daylight.js     昼夜公式（镜像 src/core/Time.cpp）
│   ├── state.js        最小可观察状态
│   ├── charts.js       种群/能量 sparkline
│   ├── renderer.js     世界画布（相机/辉光/拖尾/昼夜背景）
│   └── main.js         装配：传输控制/拖放/快捷键/影院模式
├── build/              本地样例轨迹（真实模拟输出，不入库可删）
└── tools/bundle.mjs    单文件打包脚本
```

## 快捷键

空格 播放/暂停 · ←/→ 单帧 · Home/End 首尾 · 滚轮缩放 · 拖拽平移 · R 重置视角 · F 影院模式

## 重新生成内置示例

```powershell
& .\build-cmake\Release\simulator.exe --ticks 1200 --html-interval 5 --no-render `
  --html frontend\build\trace-balanced.html --seed 42 `
  --producer-max 1000 --agent-max 1000 --producer-competition 0.2 --producer-radius 400 `
  --producer-density-repro 0.02 --agent-harvest 0.5 --agent-reserve 10 --agent-half-saturation 20 `
  --agent-density-repro 0.01 --agent-density-radius 200 --agent-lifespan 3000
```

然后按 `js/demo-data.js` 头部注释的方式降采样重建（每 2 帧取 1，数值保留 1 位小数）。
