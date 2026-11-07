# ecocore-v231-runnable — 旧版 v2.3.1 完整可运行副本（冻结，仅供参考）

这是旧 EcoCore v2.3.1 时代的**完整原始工作目录副本**，从开发机原样移入。
与本目录上一级 `legacy/ecocore-v3/`（git 管理的源码快照）相比，本副本额外保留：

- `mods/` — 旧版 C++ 模组源码（ball_loss_relief、climate_event、cyano_producer 等 10 个）
- `scripts/` — 旧版 Python 聚类/追随实验脚本（依赖 numpy）
- `web/` — 旧版网页前端
- `env_config.txt` / `env_config_balanced.txt` — 旧版运行配置（ball_loss_cost=1.5 标定档）
- `resourcepacks/`、`CMakeLists.txt`、`custom_nuclei.txt`、`mods.list.example`

**状态：冻结 / 只读参考。不参与本仓库任何构建。**
旧版基线在 git 历史的 tag `v1.0.0` ~ `v2.3.1`、`legacy-v2.3.1` 里，可直接 git show <tag>:<path> 查看。
新版（四层管线 agentsim）见仓库根 README.md。
