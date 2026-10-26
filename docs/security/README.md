# 依赖已知漏洞检查 · 2026-10-26

本次已成功读取 Debian 官方 [Security Tracker JSON](https://security-tracker.debian.org/tracker/data/json)，补做此前 403 阻碍的查询。匹配快照 SHA256、实际工具链来源版本与原始记录摘要见 [dependency-review-20261026.json](dependency-review-20261026.json)。未把未解析记录视作安全，也未将源码包匹配当作完整二进制审计。

| 来源包／记录 | 当前判断 |
| --- | --- |
| GCC 14：CVE-2026-102010，二叉堆 priority queue 的 erase_if | Tracker 对 trixie 标 open。本项目源码未使用这一扩展容器／函数；未完成静态运行库所有可达路径的独立核验，不能仅据源码检索宣布不受影响。 |
| GCC 14：CVE-2026-95619，对齐分配大输入溢出 | Tracker 标 open。程序有配置、路径、图像及队列边界，未主动使用对齐 new；仍需核验最终静态运行库和可达输入，未宣布不受影响。 |
| gcc-mingw-w64：CVE-2016-4973，libssp | Tracker 标 open/unimportant。PE 不依赖外部 libssp；这不单独证明静态链接及编译防护均无风险。 |
| mingw-w64：CVE-2018-5392，旧版 ASLR 兼容问题 | 当前 mingw-w64 为 12.0.0，不是描述中的 5.0.4；仍列出 tracker open/unimportant 记录。链接启用 ASLR/NX/high-entropy，真正 Windows 地址随机化尚未验证。 |
| NSIS：CVE-2026-42171，SYSTEM 上下文与 Low IL 临时目录 | 已通过 Debian 官方签名与包哈希获取并升级到 **NSIS 3.12-1**；tracker sid 对该条标 resolved，fixed_version 为 3.12-1。最终安装包重新构建，常规安装继续 `RequestExecutionLevel user`。这条来源包问题已按修复版本处理，仍不代替最终安装器的 Windows 安全与安装验收。 |
| binutils-mingw-w64 | 当前匹配未发现 trixie 未解决条目；不表示通用 binutils 包、所有构建输入及所有漏洞来源均无风险。 |

没有引入三个参考项目的框架或播放器依赖；MIT/GPL 研究边界见 [源码研究](../references/open-source/README.md)。安装器与 PE 未签名，未完成 Windows Defender、完整 SBOM、多来源 CVE 核对和独立可利用性审查。当前只交付开发预览，不作“零漏洞”或正式发布保证。

本次不修改系统防护、不添加联网更新器，也不要求用户以高权限运行。最终正式构建应在修复适用问题后重建并重新记录安装包哈希。
