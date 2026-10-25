# 桌面归纳盒下载（Windows 10 x64 开发预览）

本分支包含原生源码、离线安装器、源码压缩包和 SHA256 校验文件。预览构建未签名，尚未完成真实 Windows 10 桌面与性能验收。

- [下载离线安装包](https://github.com/pb356/CODEX/raw/refs/heads/delivery/desktop-boxes-v0.1.0-20261026/dist/DesktopBoxes-0.1.0-x64-Setup.exe)
- [下载源码与构建脚本](https://github.com/pb356/CODEX/raw/refs/heads/delivery/desktop-boxes-v0.1.0-20261026/dist/DesktopBoxes-0.1.0-source.zip)
- [查看 SHA256](dist/SHA256SUMS.txt)
- [测试报告与已知限制](docs/测试报告.md)
- [中文使用说明](docs/使用说明.md)

如果浏览器无法直接下载，打开上方 dist 目录，选择文件，再点击文件页右上角的“Download raw file”（下载原始文件）按钮。私有仓库需要先登录有权限的 GitHub 账号。

14 组核心测试通过；Windows 持久化及 7 组 Win32 UI 冒烟检查已在 Wine 兼容环境通过。Windows 实机安装、物理桌面穿透、多屏缩放、资源占用、8 小时稳定性及完整漏洞核查仍未验证。应用使用普通非置顶工具窗口，显示桌面后可能需要从托盘恢复。
