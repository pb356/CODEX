# 三个开源项目的源码研究与本项目优化

研究日期：2026-10-26。仅研究用户指定的 **Desktop Frames+、Lively Wallpaper、WPF UI**；截图中划掉的其他项目不纳入。已实际读取下列仓库源码及许可证，不以 README 功能表作为已实现证明。固定提交、已读文件和文件 SHA256 见 [reviewed-sources.json](reviewed-sources.json)。

| 项目 | 核对的提交 | 许可证原文 | 适用范围 |
| --- | --- | --- | --- |
| [Desktop Frames+](https://github.com/limbo666/DesktopFramesPlus) | `a7d09fcbf067ba98e6894489b45502d548d98e6e` | [MIT](https://github.com/limbo666/DesktopFramesPlus/blob/a7d09fcbf067ba98e6894489b45502d548d98e6e/License.md)；包含 BirdyFences 来源说明 | 独立折叠、目录视图、布局保存、低干扰窗口交互 |
| [Lively Wallpaper](https://github.com/rocksdanister/lively) | `25a0a5f4ed1988ff42117d3a17feb321062237fe` | [GPL-3.0](https://github.com/rocksdanister/lively/blob/25a0a5f4ed1988ff42117d3a17feb321062237fe/LICENSE) | 播放暂停、遮挡判断、会话恢复、动态桌面兼容边界 |
| [WPF UI](https://github.com/lepoco/wpfui) | `ffebacd61058170cf63864b7d5aa730cffff848a` | [MIT](https://github.com/lepoco/wpfui/blob/ffebacd61058170cf63864b7d5aa730cffff848a/LICENSE) | 主题变量、字号层级、设置页分组、焦点与无障碍 |

Desktop Frames+ 的 GitHub API 曾将许可证识别为 Other；以实际 License.md 为准，确认为 MIT。

## 已用于本次改动

### Desktop Frames+：目录事件必须触发可靠的状态核对

实际阅读 `PortalFrameManager.cs` 中的顶层 FileSystemWatcher、事件合并、Error/溢出核对与 generation 检查，以及 `LayoutStore.cs` 的临时写入／previous 备份；阅读 `FrameManager.cs` 的 IsRolled/UnrolledHeight 和 `NonActivatingWindow.cs` 的交互切换。

本项目 `inbox.hpp` 只监听用户桌面／公共桌面顶层，先注册通知再补扫，避免首次枚举与监听之间漏收文件。批量事件合并，溢出做顶层补扫；目录不可用时保留已知引用并报告未更新。`async.hpp` 合并同一键的后台任务及结果，结果队列满时暂停取新任务，消费后继续，避免静默丢失已接受结果。搜索结果按版本拒绝旧结果。

`model.hpp` 保留每框展开尺寸、折叠状态和滚动位置；`platform.hpp` 增加 v0.1 原始配置的独立迁移备份。原子写入与上一版备份继续保留。文件引用不创建 `.lnk`，不真实移动文件，不采用上游的自动整理、自动执行规则或桌面鼠标钩子。

### Lively：暂停理由独立，恢复不能覆盖其他暂停条件

实际阅读 `Core/Suspend/Playback.cs` 的会话／电源／远程暂停与可见窗口评估、`PlaybackDeferrer.cs`、`WindowUtil.cs` 及 `WinDesktopCore.cs` 的 WorkerW 和 Explorer 失效处理。

本项目新增独立的 `playback_policy.hpp`：锁屏、断开会话、休眠、隐藏、遮挡、远程、节能与故障分别保留。唤醒只清除休眠理由，不能错误取消锁屏暂停。普通筐会话恢复后补扫收件。悬停层用原生区域相减判断多个不透明窗口共同遮挡，不截图，不安装鼠标钩子。

`WallpaperPreview.exe` 是离线 Direct2D/WIC 原生审阅窗口，使用独立 PNG 叶片和固定连接点；默认节能上限 15 fps，标准上限 30 fps，静态模式无帧定时器。运动时钟在暂停期间冻结，恢复不跳过暂停时间。运行故障停止动态并回退已确认静态图。由主程序打开时受 Job Object 管理，主程序退出或崩溃后不遗留渲染子进程。

**兼容边界：**Lively 的动态桌面代码发送 Progman `0x052C` 并使用 WorkerW/SetParent，这是未公开的 Shell 行为。阅读成熟项目并不等于当前程序通过 Windows 10 验收。本版本没有接入这一后端，不以全屏置顶窗口冒充壁纸。收纳筐仍是非置顶工具窗口，Win+D 后可能需从托盘恢复。桌面整洁模式也未开放；原生图标恢复通过验证前不发布为完成。

### WPF UI：将视觉变量、语义与系统可访问性统一

实际阅读 `Variables.xaml`、`Typography.xaml`、`SystemThemeWatcher.cs`、`ToggleSwitch.cs` 及 Gallery `SettingsPage.xaml`。借鉴分组、主次文本、主题切换和焦点语义，由 Win32 原生控件与 GDI+ 独立实现；没有引入 WPF UI 程序集或新增 .NET 运行时。

本项目统一 14 DIP 圆角、42 DIP 标题、16 DIP 留白、14/13/12 字号层级与雾白／松影／烟蓝主题。设置分为全局与每筐，外观／特效／布局三页，百分比滑条、图标档位和系统颜色选择器代替开发参数。实时预览可取消，不影响文件引用；保留旧版自定义图标／字体数值。增加系统高对比度配色响应并停止装饰光感，保留清晰焦点。文件列表按可见项绘制，MSAA 虚拟子元素提供按钮名称与项目路径。

## 代码与授权边界

这三个仓库仅作为设计与行为研究资料。本次没有复制、改编或链接它们的源码、二进制、图标、字体或壁纸；没有把 Lively 作为捆绑播放器。将来若使用上游代码，应重新逐文件核对许可与归属，MIT 保留相应声明，GPL 代码的衍生／分发须满足其许可，不能仅通过署名替代。

用户已经将艺术方向改为海报／平面设计，优先于早期附件中的“实拍／写实摄影”方向；已确认 03、04、06、08 与静态稿。正式预览采用本次生成素材，外部参考图片不作为安装素材。

## 验证与尚缺条件

代码运行证据在 [测试报告](../../测试报告.md)，原生窗口截图和运动记录在 [界面评审](../../review/README.md)。Wine 兼容层证据不等于 Windows 验收。

缺少真正 Windows 10 x64 桌面环境，以下不报通过：干净机器离线安装、实际桌面穿透／Win+D、多屏缩放、Explorer 重启、图标接管恢复、动态桌面挂载、Office／设计软件连续保存、CPU／GPU／内存目标及 8 小时稳定性。`DesktopIntegrationProbe.exe` 仅通过公开接口读取能力，不更改桌面状态；能力可查询仍不能代替恢复与交互测试。
