# Gravity Desktop 创作时间线 / Development timeline

这份时间线区分原始开发记录与首次公开时间。不宣称历史版本可运行或通过当前产品验收。

This timeline distinguishes original development records from first public publication. Historical code is not claimed to work or pass current product acceptance.

## 原始 Git 历史 / Original Git history

以下独立分支保留原始提交 SHA、作者、作者日期、提交者日期及父关系。它们没有合入本仓库的 `main`；`main` 继续作为产品介绍及资料索引。

These independent branches preserve original commit identifiers, authors, author/committer dates, and parent relationships. They are not merged into `main`, which remains the product and archive index.

| 原始记录范围（UTC+08:00） / Original dates | 提交数 / Commits | 截止 SHA / Tip | 首次公开 / First public |
| --- | ---: | --- | --- |
| 2026-07-30 → 2026-08-08 | 78 | [`5a006bd7ae1ab4dcc7685c65b2813fe6acd8cff3`](https://github.com/GravityDesktop/GravityDesktop-Releases/commits/codex/history-2026-07-30-to-2026-08-08/) | 2026-10-22 |
| 2026-07-30 → 2026-08-20 | 182 | [`7de6cdb7ec485dd859e88cfcc150ca73ea6e614e`](https://github.com/GravityDesktop/GravityDesktop-Releases/commits/codex/history-through-2026-08-20/) | 2026-10-22 |

两个分支来自同一连续祖先链，78个提交包含在182个提交中，不是260个独立提交。[比较8月8日至8月20日的变化 / Compare August 8–20](https://github.com/GravityDesktop/GravityDesktop-Releases/compare/codex/history-2026-07-30-to-2026-08-08...codex/history-through-2026-08-20)。

Both branches belong to the same continuous ancestry: the 78 commits are included in the 182, not an additional set. Use the comparison link to inspect the later changes.

## 关键节点 / Milestones

| 原始 Git 日期（UTC+08:00） | 阶段 / Stage | 原始提交 / Original commit | 快照 / Snapshot |
| --- | --- | --- | --- |
| 2026-07-30 15:25:44 | 最早保留 Git 节点：视觉 Core、文件关系与布局 / Earliest retained Git snapshot | `41cdd57459de279cd3435707e473dbef57f298b4` | [6个文件 / 6 files](history/2026-07-30-first-retained/) |
| 2026-08-01 23:41:58 | 托管文件收集初版 / Initial managed-file collection | `c40e55f692565212ba4b94441a71e9a0e76e32cb` | [6个文件 / 6 files](history/2026-08-01-managed-collection/) |
| 2026-08-08 22:00:09 | 独立 Recovery 初版（0.1.0-alpha） / Initial independent Recovery | `5a006bd7ae1ab4dcc7685c65b2813fe6acd8cff3` | [12个文件 / 12 files](history/2026-08-08-independent-recovery/) |
| 2026-08-20 13:24:34 | English-first 本地化基线（版本头仍为0.1.0-alpha） / Localization baseline | `7de6cdb7ec485dd859e88cfcc150ca73ea6e614e` | [原始源码树 / Original tree](https://github.com/GravityDesktop/GravityDesktop-Releases/tree/codex/history-through-2026-08-20) |

前三个独立快照于2026-10-22首次公开。每个目录的 `source/` 保留原始文件字节，`PROVENANCE.json` 记录原始提交、树、日期及逐文件 SHA256/Git blob。原有[历史 Releases](https://github.com/GravityDesktop/GravityDesktop-Releases/releases)和手工上传的阶段源码 ZIP 保持不变。GitHub 自动生成的 tag 源码归档对应公开仓库的标签树，不应与阶段 ZIP 混淆。

The first three snapshots were first published on October 22, 2026. Their provenance files record the original commit/tree, dates, and per-file hashes. Existing historical releases and manually uploaded stage ZIPs remain unchanged. GitHub's automatically generated tag archives represent the tagged public repository tree, not the individual stage ZIPs.

## 更早的资料与日期边界 / Earlier evidence and date boundaries

本地保留的2026-07-25开发会话及构建回执，记录了当日项目开发与一次 Release x64 构建完成。另有早期资料文件与首个 Git 节点内容可对应。但尚未找到7月30日之前的原始 Git 提交或完整源码快照，因此没有补造7月25日提交，也不公开原始私人会话或运行日志。

Retained local development-session and build receipts record project work and a completed Release x64 build on July 25, 2026. Earlier document material also corresponds to the first Git snapshot. No pre-July-30 Git commit or complete source snapshot has been found, so no July 25 commit has been invented. Raw private sessions and logs are not published.

- Git日期是保留的元数据，不是外部认证的创作起始证明；本次公开提交及分支的公开日期使用实际日期。
- 历史分支只公开截至所列节点的祖先，不公开其后代。后续商业源码继续私有，1.0.20.35、.36、.37均不在范围内。
- 更晚节点需单独审查，不以补齐时间线为由跳过安全及权利核验。
- 本次历史源码构建与运行验收：**NOT RUN**。旧代码可能包含缺陷或真实文件操作，只能用隔离环境和合成数据研究。

Git dates are retained metadata, not externally certified creation dates. Publication dates are actual dates. Only the listed ancestor histories are published; later commercial source stays private. Later milestones require separate safety and rights review. Archival build/runtime acceptance is **NOT RUN**; use isolated environments and synthetic files.

## 安装与权利 / Installation and rights

正式安装与更新请使用 [Microsoft Store](https://apps.microsoft.com/store/detail/9P4Q9JK2DV8R?cid=DevShareMCLPCS)。这里不提供历史 EXE/MSIX，也不是降级渠道。未新增开源许可；原有版权及第三方许可声明继续适用。源码公开不自动授予商业再利用许可，也不改变第三方已有权利。

Install and update through Microsoft Store. No historical EXE/MSIX installers or downgrade channel are provided here. No new open-source license is granted. Existing copyright and third-party license terms continue to apply; public visibility does not itself grant commercial reuse rights or change existing third-party rights.
