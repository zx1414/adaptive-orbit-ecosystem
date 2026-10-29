# M4 最小可视化：自包含 HTML 轨迹

> 状态：已完成。`tools/m4_acceptance.ps1` 20 项全 PASS；HTML 帧 JSON/Canvas/无外部 URL；
> 帧满 2000 时降采样并保留末帧，--html 路径在启动阶段 fail-fast。

目标：用 WorldView 生成一个可直接双击打开的单文件 HTML，回放生产者/Agent 的时空轨迹；
不引入服务器、第三方库或旧 v2 快照协议。

## 1. 范围

- 新增 app::HtmlTraceWriter：只读 WorldView，按固定间隔采样帧，运行结束写单个 HTML。
- CLI：`--html <path>`，`--html-interval <n>`（默认 100，必须 >0）。
- 兼容 `--load`：从读档 tick 开始采样；`--save` 与 HTML 可同时使用。
- 失败（无法写文件）退出码 4；旧参数与退出码不变。

## 2. 数据与页面

每帧：

    { tick, producers, agents, totalEnergy,
      p: [[x,y,energy,cap], ...], a: [[x,y,energy,cap], ...] }

- 坐标按 WorldView bounds 归一化；数值保留有限性。
- 帧数上限 2000；超过时停止采样并在页面提示。
- HTML 内嵌 JSON、canvas 绘图、播放/暂停/滑块、当前 tick/种群/总能量。
- 生产者绿色、Agent 橙色，亮度/半径随 energy/cap；背景按 tick 昼夜相位变化。
- 单文件 UTF-8，无外部请求。

## 3. 代码

- `src/app/HtmlTraceWriter.h/.cpp`：capture(view)、write()、ok()。
- `src/app/Cli.*`：htmlPath/htmlInterval。
- `src/app/main.cpp`：循环内按 interval capture；结束 write；错误码 4。
- CMake：app 源文件加入 HtmlTraceWriter.cpp（simulator 目标）。
- 不修改 sim/world 的接口；不引入第三方依赖。

## 4. 验收

- 2000 tick 生成 HTML；文件包含 `const TRACE =`、`frames`、`canvas`，帧数 >=2 且 tick 递增。
- 打开页面可播放；无外部 URL/脚本。
- `--html` 与 `--load` 组合可用；写入失败退出码 4（启动即探测）。
- 2500 tick / interval 1：帧数 <=2000、末帧 tick=2500、stdout 标记 downsampled=1。
- `--load --stats`：CSV 首行为载入 tick（非 interval 对齐时也写出）。
- `tools/m4_acceptance.ps1` 自动化检查；全量 tools/check.ps1 仍通过。