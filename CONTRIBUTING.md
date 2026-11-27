# 参与 agentsim

想法、对照实验、扩展作品和源码改动都可以贡献。

| 有什么想分享 | 入口 |
| --- | --- |
| 生态机理、算法思路、观察结果或改进建议 | [GitHub Discussions](https://github.com/zx1414/adaptive-orbit-ecosystem/discussions) |
| 可下载的 Mod、决策算法或材质包 | [扩展投稿表](https://github.com/zx1414/adaptive-orbit-ecosystem/issues/new?template=extension.yml) |
| 游戏代码、文档或目录改动 | Fork 仓库后向 `main` 提交 Pull Request |

## 分享扩展

请写明扩展名称、作者、授权方式、已验证的游戏版本、安装步骤和源码或发布链接。分享对照实验时附上种子、场景、所用策略和观测帧段。社区目录在改动合并后更新。

| 类别 | 可提交内容 | 参考 |
| --- | --- | --- |
| 机制 Mod | 可装入 `mods/` 的场景文本。新增内核机制可以提交 C++ 改动 | [生态 Mod 开发](docs/MODS.md) |
| 决策算法 | 可装入 `models/` 的线性模型。也欢迎 Python 实验脚本和 C++ 策略改动 | [算法研究与模型接口](docs/RESEARCH.md) |
| 材质包 | 包含 `pack.txt` 和 `colors.txt` 的目录。可另附实机截图和保留目录结构的 ZIP | [材质包开发](docs/RESOURCE-PACKS.md) |

## 更新扩展目录

提交 Pull Request 时可参照 `docs/community/catalog.json` 的现有条目。每个条目使用独立的 `id`，类型为 `mechanism`、`decision` 或 `material`，并填写标题、说明、作者、扩展版本、已验证游戏版本、许可证、安装方法及源文件链接。下载路径可以指向 `docs/` 内的文件，也可以链接到公开的 HTTPS 发布地址。仓库内随页面发布的文件放在 `docs/community/extensions/`，目录中的相对链接从 `docs/` 起算。材质包 ZIP 应包含对应的材质包目录及两个文本文件。

在原版游戏中试装一次，并检查目录链接和下载内容。GitHub Pages 从 `main` 的 `docs/` 发布。 [社区扩展库](https://zx1414.github.io/adaptive-orbit-ecosystem/)
