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

# 1) 提交合作开发内容
git add src mods web resourcepacks docs scripts CMakeLists.txt README.md .gitignore env_config.txt custom_nuclei.txt 启动器.bat
git commit -m "v1.1.0: 启动器成为主体（参数页/Mod中心/我的存档/材质包）+ mod 基础架构与逐球可视化"

# 2) 打版本标签
git tag -a v1.1.0 -m "v1.1.0: 启动器 + Mod + 存档 + 材质包，Windows 可直接游玩"

# 3) 推送（分支 + 标签）
git push origin HEAD
git push origin v1.1.0
```

推送需要 GitHub 登录凭证；若提示认证失败，先执行 `git config --global credential.helper manager`
并手动 push 一次以唤起浏览器登录，再重试。

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
