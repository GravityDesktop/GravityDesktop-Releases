# Gravity Desktop 创作时间线 / Development timeline

此页记录能够从现存源码和 Git 记录中核对的开发阶段，不宣称每个阶段均可运行或通过产品验收。

This page documents stages supported by retained source and Git records. It does not claim that every snapshot works or has passed product acceptance.

## 第一批已公开源码 / First published source batch

| 原始 Git 记录日期（UTC+08:00） / Original Git date | 阶段 / Stage | 原始提交 / Original commit | 查看源码 / Source | 本次公开日期 / Published |
| --- | --- | --- | --- | --- |
| 2026-07-30 15:25:44 | 最早保留快照：视觉 Core、文件关系与布局 / Earliest retained visual Core, relationship and layout snapshot | `41cdd57459de279cd3435707e473dbef57f298b4` | [6个文件 / 6 files](history/2026-07-30-first-retained/) | 2026-10-22 |
| 2026-08-01 23:41:58 | 托管文件收集初版 / Initial managed-file collection | `c40e55f692565212ba4b94441a71e9a0e76e32cb` | [6个文件 / 6 files](history/2026-08-01-managed-collection/) | 2026-10-22 |
| 2026-08-08 22:00:09 | 独立 Recovery 初版（0.1.0-alpha） / Initial independent Recovery (0.1.0-alpha) | `5a006bd7ae1ab4dcc7685c65b2813fe6acd8cff3` | [12个文件 / 12 files](history/2026-08-08-independent-recovery/) | 2026-10-22 |

每个目录的 `source/` 保存对应提交中的原始文件；`PROVENANCE.json` 保存提交、树、原始作者/提交日期，以及逐文件 SHA256 和 Git blob 标识。对应 [GitHub Releases](https://github.com/GravityDesktop/GravityDesktop-Releases/releases) 提供各阶段的独立源码 ZIP。

Each `source/` directory preserves the original files for its snapshot. `PROVENANCE.json` records the original commit/tree, author/committer dates, per-file SHA256 and Git blob identifiers. Matching historical releases provide separate source ZIP archives.

## 日期与资料边界 / Date and evidence boundaries

- 7月30日是目前找到的**最早保留 Git 快照**，不是经外部认证的创作起始日期。原始 Git 时间是记录元数据；公开提交与 Release 使用真实公开时间，不回填历史日期。
- 已保留的最早两个快照没有独立版本头，因此按日期命名，不替它们编造版本号。
- 不复制私有仓库的 `.git` 历史，只导出本批选定的源码文件；不包含当前 .37 或 .35/.36 源码快照、凭据、签名证书、个人状态、安全库原件、运行日志或安装包。
- 本批构建 / 运行验证：**NOT RUN**。历史版本可能操作真实文件，必须与日用程序及真实数据隔离。
- 原文件保持字节一致，包括换行和编码；历史 README 里的操作说明和功能声明只代表当时记录，不是当前安全保证。
- 后续版本仅在真实资料存在且完成公开内容检查后补充；缺失版本不虚构，.35 本轮不发布。

July 30 is the earliest retained Git snapshot found, not an independently certified creation-start date. Git dates are metadata; public commits/releases retain their actual publication dates. The first two snapshots have no separate version header and are identified by date, not invented version numbers. Only the selected snapshots are exported; private repository history and current production source are not transferred. Build/runtime testing for this archival batch is **NOT RUN**. Keep historical code away from production data. Original file bytes, including line endings and encoding, are preserved; historical README claims are not current safety guarantees.

## 后续资料清单（未公开） / Retained later milestones (not published here)

已找到 2026-08-20 的 0.1.0-alpha 本地化节点，以及 1.0.15.0、1.0.16.0、1.0.17.0、1.0.18.0、1.0.19.0、1.0.20.0、1.0.20.4 的 Git 节点。本地另有 .31 至 .34 的冻结构建源码材料；其中 **1.0.20.34 是 .35 之前的产品候选**，并非独立的干净 Git 版本标签，公开前需按冻结清单重建并核对。

Retained material also includes the August 20 localization checkpoint, Git milestones from 1.0.15.0 through 1.0.20.4, and frozen build-source material for .31–.34. These are an inventory, not a claim that their source has already been published. In particular, .34 is the product candidate preceding .35; its frozen dirty-worktree source must be reconstructed and verified before publication.

## 使用与版权 / Use and copyright

正式使用请从 [Microsoft Store](https://apps.microsoft.com/store/detail/9P4Q9JK2DV8R?cid=DevShareMCLPCS) 安装。这里没有旧 EXE/MSIX 下载，不是降级渠道。原有版权声明保留，本次未新增开源许可证。

Install the supported product from Microsoft Store. These archives contain no EXE/MSIX downloads and are not a downgrade channel. Original copyright notices remain in place; no new open-source license is added by this archival publication.
