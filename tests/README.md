# tests

v2.2.0 回归与红队验收入口。

## 确定性基线

`tests/baseline/`：

- `env_off.txt` / `env_on.txt`：关闭/开启蓝细菌的固定参数（seed42、200 球、12 核、5000 帧）。
- `v2.1.0-baseline.txt`：v2.1.0 产物的 CSV SHA256 清单，相对路径键为 `off/...`、`on/...`。

复跑（在临时目录，避免污染仓库）：

```powershell
$exe = '..\_agent\integration\build\simulator.exe'
& $exe --console --no-render --env env_off.txt --seed 42 --balls 200 --nuclei 12 `
       --frames 5000 --sample-interval 5000 --max-fps 0
# 对 balls_005000.csv / nuclei_005000.csv / survivors.csv 做 SHA256 对比
```

无玩家操作时，关闭与开启蓝细菌的输出都必须与 v2.1.0 基线逐字节一致。

## 红队脚本

- `tests/qa/lib_sim.ps1`：启动器 HTTP/快照/存档辅助函数。
- `tests/qa/_smoke.ps1`：环境接口、快照、存档冒烟。
- `tests/qa/red_visual/`：Node + CDP/PNG 可见性与布局检查（需 Edge）。
- 生成物放 `tests/artifacts/`（已 gitignore，不入库）。

## 启动器接口速查

- `GET /env`、`POST /env`：四轴/细调/事件/预设/蓝细菌开关。
- `GET /state`：快照 v2（80B 头；球 6B、核 32B、节点 24B、连线 8B）。
- `POST /run`、`POST /control`、`POST /save`、`POST /load`、`POST /set-mods`、`POST /set-packs`。
