# tests

v2.3.1 回归与红队验收入口。

## 确定性基线

`tests/baseline/`：

- `env_off.txt` / `env_on.txt`：关闭/开启蓝细菌的固定参数（seed42、200 球、12 核、5000 帧；固定 `forage_radius=300`，用于隔离默认值变化）。
- `v2.1.0-baseline.txt`：v2.1.0 产物的 CSV SHA256 清单（历史参考，勿再作为当前门槛）。
- `v2.3.0-baseline.txt` / `v2.3.1-baseline.txt`：v2.3.0/v2.3.1 产物清单；v2.3.1 默认参数变为 `forage_radius=800`，但基线文件显式固定 300，因此哈希与 v2.3.0 一致，便于隔离回归。
- 注意：v2.3.0 起取消了“场上球数不低于初始值”的保底补球，并调大蓝细菌子代散布距离，off/on 哈希与 v2.1.0 基线不同，这是预期的行为变更。

复跑（在临时目录，避免污染仓库）：

```powershell
$exe = '..\simulator.exe'
& $exe --console --no-render --env env_off.txt --seed 42 --balls 200 --nuclei 12 `
       --frames 5000 --sample-interval 5000 --max-fps 0
# 对 balls_005000.csv / nuclei_005000.csv / survivors.csv 做 SHA256 对比
```

同一版本、同参数重复运行必须逐字节一致；v2.3.1 的基线见 `v2.3.1-baseline.txt`。

## 红队脚本

- `tests/qa/lib_sim.ps1`：启动器 HTTP/快照/存档辅助函数。
- `tests/qa/_smoke.ps1`：环境接口、快照、存档冒烟。
- `tests/qa/red_visual/`：Node + CDP/PNG 可见性与布局检查（需 Edge）。
- 生成物放 `tests/artifacts/`（已 gitignore，不入库）。

## 启动器接口速查

- `GET /env`、`POST /env`：四轴/细调/事件/预设/蓝细菌开关。
- `GET /timeline`：时间轴检查点列表与当前/最大帧。
- `POST /seek`：`frame=N`，定位到历史帧（会暂停）。
- `POST /branch`：`frames=N` + schema key=value，从当前帧分支新世界线。
- `GET /state`：快照 v2（80B 头；球 6B、核 32B、节点 24B、连线 8B）。
- `POST /run`、`POST /control`、`POST /save`、`POST /load`、`POST /set-mods`、`POST /set-packs`。
