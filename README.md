# Gravity Desktop

用视觉 Core 组织文件、文件夹和快捷方式，让常用项目留在熟悉的位置。

Organize files, folders, and shortcuts around visual Cores. Keep everyday items in familiar places.

[Microsoft Store · 官方安装](https://apps.microsoft.com/store/detail/9P4Q9JK2DV8R?cid=DevShareMCLPCS) · [创作时间线 / Timeline](HISTORY.md) · [原始 Git 提交 / Original commits](https://github.com/GravityDesktop/GravityDesktop-Releases/commits/codex/history-through-2026-08-20/)

![Gravity Desktop organizes everyday items around visual Cores](media/screenshot-114533.png)

## 产品 / Product

- 以 Core 分组，保存组内项目的视觉位置与关系。
- 面向 Windows x64；管理放入 Core 的项目，不锁定所有 Windows 原生桌面图标。
- 正式安装与更新统一通过 Microsoft Store；当前可安装版本以商店页面为准。

Group related items around Cores and preserve their visual layout. For Windows x64. Gravity manages items placed in Cores, not every native Windows desktop icon. Install and update through Microsoft Store; consult the Store for the currently available version.

[观看演示 / Watch the demo](media/gravity-desktop-demo-20261005.mp4)

## 真实开发历史 / Real development history

这里保留早期源码快照，也提供经过审查的原始 Git 历史分支：可查看真实提交、父关系和逐次 Diff，而不只是本次集中上传的快照。

This repository preserves early source snapshots and audited original Git branches, including the original commits, parent relationships, and incremental diffs.

| 历史范围 / History | 提交 / Commits | 入口 / Browse |
| --- | ---: | --- |
| 2026-07-30 → 2026-08-08 · Core、托管收集、独立 Recovery | 78 | [历史分支 / Branch](https://github.com/GravityDesktop/GravityDesktop-Releases/tree/codex/history-2026-07-30-to-2026-08-08) |
| 2026-07-30 → 2026-08-20 · English-first 本地化基线 | 182 | [历史分支 / Branch](https://github.com/GravityDesktop/GravityDesktop-Releases/tree/codex/history-through-2026-08-20) |
| 2026-10-18 · 1.0.20.35 DefView 产品阶段 | 精选快照 / Curated snapshot | [源码与核验清单 / Source and provenance](history/1.0.20.35-defview/) |

前两个范围重叠，不能相加；.35是冻结工作树还原的精选源码，不是原始Git提交链。原始记录日期与首次公开日期分别记录在[时间线](HISTORY.md)。没有伪造或回填历史提交；Git 时间是原始元数据，不是第三方时间认证。

The first two ranges overlap and must not be added together. The .35 entry is a curated frozen-worktree reconstruction, not an original Git chain. Original record dates and first public publication dates are distinguished in the timeline. No historical commits or dates were fabricated; Git timestamps are retained metadata, not independent timestamp certification.

## 公开边界 / Publication boundaries

最新商业源码保持私有，1.0.20.36、.37 不在公开范围内；.35仅公开审查后的精选源码。这里的历史代码不是推荐使用的旧产品，不提供 EXE/MSIX 安装包。本次历史发布未做构建或运行验收（**NOT RUN**）；研究时使用隔离环境和合成数据，勿加载真实文件或 Safety Library。

Latest commercial source remains private, including 1.0.20.36 and .37; only the reviewed curated .35 source is published. Historical code is not a supported installation or downgrade route. No EXE/MSIX installers are distributed here. Build/runtime acceptance for this archival publication is **NOT RUN**; use isolated environments and synthetic data only.

公开源码不等于授予开源许可。本次未新增 MIT、GPL 等许可；原有版权及许可声明保持有效，第三方内容按各自许可处理。

Source visibility does not establish an open-source license. No new MIT, GPL, or other license is granted by this publication. Existing copyright and license notices remain applicable; third-party material retains its respective terms.

Copyright © 2026 Gravity Desktop.
