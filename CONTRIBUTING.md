# 参与 agentsim

想法、对照实验、扩展作品和源码改动都可以贡献。

| 有什么想分享 | 入口 |
| --- | --- |
| 生态机理、算法思路、观察结果或改进建议 | [GitHub Discussions](https://github.com/zx1414/adaptive-orbit-ecosystem/discussions) |
| 可下载的 Mod、决策算法或材质包 | [自行发布扩展](https://github.com/zx1414/adaptive-orbit-ecosystem/issues/new?template=extension.yml) |
| 游戏代码、文档或目录改动 | Fork 仓库后向 `main` 提交 Pull Request |

## 分享扩展

登录 GitHub，填写发布表单。在「下载地址」上传 ZIP，或粘贴访客可以访问的 HTTPS 下载链接。写明用途、授权、已试用的游戏版本和安装步骤。提交的发布帖会自动进入扩展库的「玩家发布」区，访客可直接打开作品并下载公开文件。

发布后可在 GitHub 编辑原帖，更新文件和说明。关闭发布帖会将它从扩展库的玩家列表移除。分享对照实验时，可附上种子、场景、策略和观察帧段。 [浏览玩家作品](https://zx1414.github.io/adaptive-orbit-ecosystem/#players)

| 类别 | 可提交内容 | 参考 |
| --- | --- | --- |
| 机制 Mod | 可装入 `mods/` 的场景文本。新增内核机制可以提交 C++ 改动 | [生态 Mod 开发](docs/MODS.md) |
| 决策算法 | 可装入 `models/` 的线性模型。也欢迎 Python 实验脚本和 C++ 策略改动 | [算法研究与模型接口](docs/RESEARCH.md) |
| 材质包 | 包含 `pack.txt` 和 `colors.txt` 的目录。可另附实机截图和保留目录结构的 ZIP | [材质包开发](docs/RESOURCE-PACKS.md) |

## 更新官方示例

提交 Pull Request 时可参照 `docs/community/catalog.json` 的现有条目。每个条目使用独立的 `id`，类型为 `mechanism`、`decision` 或 `material`，并填写标题、说明、作者、扩展版本、已验证游戏版本、许可证、安装方法及源文件链接。下载路径可以指向 `docs/` 内的文件，也可以链接到公开的 HTTPS 发布地址。仓库内随页面发布的文件放在 `docs/community/extensions/`，目录中的相对链接从 `docs/` 起算。材质包 ZIP 应包含对应的材质包目录及两个文本文件。

在原版游戏中试装一次，并检查目录链接和下载内容。GitHub Pages 从 `main` 的 `docs/` 发布。 [社区扩展库](https://zx1414.github.io/adaptive-orbit-ecosystem/)
