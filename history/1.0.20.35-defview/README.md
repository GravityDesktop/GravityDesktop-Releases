# Gravity Desktop 1.0.20.35 — 历史源码 / Historical source

原始构建：2026-10-18；首次公开：2026-10-23。

Original build: October 18, 2026. First public publication: October 23, 2026.

## 这个节点记录了什么 / What this milestone records

DefView 子窗口产品架构阶段的 Win+D 闪烁优化：在严格身份、拓扑和输入绑定核验通过时，复用同一 DefView 下的场景窗口，避免仅外层 root 变化就整组重建。正常、无状态变化的自动引用检查减少提示；真正的 Explorer/DefView 替换仍保留完整恢复路径。

A DefView-child product milestone addressing unnecessary reconstruction on desktop transitions. The source conditionally reuses scene windows when host identity, topology, and input binding checks pass, keeps healthy unchanged automatic reference checks quiet, and retains full reconstruction for a genuinely replaced Explorer/DefView host.

这不是对所有Windows环境、无闪烁或无缺陷的保证。历史版本早于后续布局、韩语及正式资源键迁移维护，不应视为当前推荐安装版本。

This is not a guarantee of defect-free or flicker-free behavior across Windows environments. It predates later layout, Korean-language, and resource-key localization maintenance and is not the current recommended installation.

## 来源与完整性 / Provenance and integrity

- 冻结构建身份：`120-035-DEFVIEW-PRODUCT-CANDIDATE-R4`，原渠道为 `LOCALTEST`。
- 基础Git提交：`060f44a398b34777687163c0f3c8125f595d8ea9`，当时还有未提交修改；**没有独立原始.35提交可供直接推送**。
- 按当时保留的补丁、未跟踪源码和冻结清单还原，523个冻结文件逐一核验。公开其中210个源码、资源及必要依赖文件，字节未修改；逐文件SHA256见 [PROVENANCE.json](PROVENANCE.json)。
- 这是一份有出处的精选源码快照，**不是完整原始Git树或完整开发现场**。未包含私人调查报告、日志、用户/VM数据、签名材料、安装脚本、第三方测试/模糊测试压缩语料及非必要平台资料。
- 第三方依赖是裁剪后的源码子集，保留各自许可及版权声明；未宣称是完整上游发行包。

The original build was based on a dirty worktree, not an independent clean .35 Git commit. Reconstruction used the retained patch, untracked source, and frozen manifest; all 523 frozen entries were checked. This curated publication includes 210 byte-identical files, with per-file SHA256 in the provenance file. It excludes private evidence, user/VM data, signing material, installer scripts, third-party test/fuzz archives, and optional platform material. Dependency subsets retain their original notices and are not presented as full upstream distributions.

## 使用边界 / Use boundaries

这是源码归档，不是安装包。提供 Visual Studio 工程、程序/Recovery 源码、六份当时的语言资源及静态ZIP依赖源码。**本次精选源码发布的重新构建和GUI验收：NOT RUN**；不承诺开箱即编译。工程中的原始构建路径和配置按当时保留，可能需要本地工具链适配。

This is a source archive, not an installer. It contains Visual Studio projects, application/Recovery source, the six language resources from that build, and static ZIP dependency source. **Rebuild and GUI acceptance for this curated publication: NOT RUN.** Build paths/configuration are preserved and may require local toolchain adaptation.

不要连接真实Core配置、真实用户文件或Safety Library。研究请使用隔离环境和合成数据。正常安装与更新使用 [Microsoft Store](https://apps.microsoft.com/store/detail/9P4Q9JK2DV8R?cid=DevShareMCLPCS)。

Do not connect real Core configuration, user files, or an active Safety Library. Use an isolated environment and synthetic data. Install and update the supported product through Microsoft Store.

本次公开不新增开源许可；原有版权及第三方许可继续适用。`.36/.37`源码及私有仓库祖先链未随本快照公开。

No new open-source license is granted. Existing copyright and third-party licenses remain applicable. This snapshot does not publish .36/.37 source or the private repository's ancestry.
