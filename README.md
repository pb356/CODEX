# 桌面归纳盒 0.2.0

Windows 10 x64 原生 C++/Win32 开发预览。仅维护文件引用，中文、离线、当前用户安装，无 Python、Node 或 .NET 运行时要求。

**未通过真正 Windows 10 实机验收。动态桌面挂载与原生图标隐藏尚未开放；分层运动窗口是预览。**

- [下载离线安装包（未签名）](https://github.com/pb356/CODEX/raw/refs/heads/delivery/desktop-boxes-v0.2.0-20261026/dist/DesktopBoxes-0.2.0-x64-Setup.exe)
- [下载源码、素材、脚本与测试证据](https://github.com/pb356/CODEX/raw/refs/heads/delivery/desktop-boxes-v0.2.0-20261026/dist/DesktopBoxes-0.2.0-source.zip)
- [三个开源项目研究与实际优化](docs/references/open-source/README.md)
- [原生界面截图与分层运动预览](docs/review/README.md)
- [中文使用说明](docs/使用说明.md) · [测试报告](docs/测试报告.md) · [需求对应与缺口](docs/需求对应.md) · [许可与安全](docs/第三方声明.md)

## 本轮变化

桌面收件箱为主动启用的只读顶层库存，支持未分类／全部桌面、暂停与补扫。网格只绘制可见项，搜索排序在后台覆盖全部引用，系统图标按需加载，不设固定每框配额。统一三主题、简洁标题与原生设置页，实时预览可取消，支持键盘与 MSAA；折叠真正缩短窗口、保留各框状态，穿透可从托盘恢复。

结合 Desktop Frames+、Lively Wallpaper、WPF UI 的源码研究，修正监听注册间隙、后台结果丢失、交叠会话暂停与过亮光斑；增加独立迁移备份、组合遮挡和系统高对比度。三个项目未作为代码或二进制依赖引入。

已确认的海报风格采用本次生成的离线静态背景与三独立叶片，提供 Direct2D/WIC 原生运动审阅窗口。普通筐仍是非置顶工具窗口；Win+D 后可能需托盘恢复。不能把预览或 Wine 结果当作 Windows 桌面集成已完成。

## 构建与验证

Linux 云环境：`tools/bootstrap-linux.sh` 准备用户目录内交叉工具；`tools/test.sh` 运行 ASan/UBSan 核心测试；`tools/build-linux.sh` 构建主程序、预览、只读探针、测试和 NSIS 离线安装器。`python3 tools/package-source.py` 打包源码及当前证据。无需服务或开放端口。

Windows 开发机：VS 2022 C++ 桌面工具／SDK、CMake ≥3.20、NSIS 3.12；运行 `tools/build-windows.ps1`。MSVC 使用 /MT，终端用户无需这些开发工具。此 Windows 原生构建路线尚未在云 Linux 环境实机验证。

可选兼容测试：已准备的 Wine 10 + 隔离 Xvfb 上运行 `DISPLAY=:91 tools/run-wine-tests.sh`。不能针对用户活动桌面执行测试截图；所有文件操作用临时目录。兼容层的只读探针不支持完整 Explorer 桌面接口，不报 Windows 验收通过。

源码按模型、后台任务、收件监听、窗口交互、渲染、设置、无障碍及预览拆分。安装包含主程序与随附离线素材，独立复制主 EXE 不含完整预览内容。程序与安装器未签名，遵守系统安全提示，不提供绕过措施。
