# 桌面归纳盒

Windows 10 x64 原生 C++/Win32 桌面应用开发预览。仅记录文件引用，不搬动原文件；中文界面、托盘管理、独立折叠、整框穿透和低强度星空光晕，无联网／登录／额外运行时需求。

**当前状态：可以交叉构建原生 EXE 和离线安装器，尚未完成 Windows 10 实机验收，不能视为可投入生产的软件。**

- [中文使用说明与限制](docs/使用说明.md)
- [测试报告与未验证清单](docs/测试报告.md)
- [依赖、安全与许可声明](docs/第三方声明.md)
- [完整需求与对应实现](docs/需求对应.md)

## 构建

Linux：运行 `tools/bootstrap-linux.sh` 准备用户目录内的交叉工具，再运行 `tools/test.sh` 和 `tools/build-linux.sh`。构建结果在 `dist/`，不需要启动服务或开放端口。

Windows 开发机：Visual Studio 2022 C++ 桌面工具、Windows SDK、CMake ≥3.20、NSIS 3.11（makensis 在 PATH），执行 `tools/build-windows.ps1`。MSVC 使用 /MT；终端用户不需要开发工具或额外运行库。Windows 原生构建路线尚未在此 Linux 环境验证。云快照中可选 Wine 兼容检查通过 `DISPLAY=:91 tools/run-wine-tests.sh` 运行，需先启动隔离 Xvfb；不代替 Windows 实机验收。

生成 `DesktopBoxes-0.1.0-x64-Setup.exe`、独立 `DesktopBoxes.exe` 和 `SHA256SUMS.txt`。安装器当前未签名，不提供绕过安全防护的方法。

## 设计边界

每个筐是独立、非置顶、非自动激活的分层工具窗口，无全屏输入层、全局鼠标钩子、Explorer 注入或私有 WorkerW 嵌入。因此与原生桌面层级不完全等价，显示桌面后可能需要托盘恢复。外部重命名不猜测新路径，失效时由用户重新定位。

模型和有界配置格式在 `src/model.hpp`；Windows 持久化与事件式文件状态监视在 `src/platform.hpp`；界面、输入、托盘与光晕在 `src/main.cpp`；当前用户安装／卸载脚本在 `installer/setup.nsi`。

测试仅使用临时文件。未通过或未运行的验收不能由模型测试、Wine 测试或交叉编译替代。详见测试报告。
