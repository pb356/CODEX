Unicode True
Target amd64-unicode
!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"
!include "WinVer.nsh"
Name "桌面归纳盒 0.2.0（预览版）"
OutFile "../dist/DesktopBoxes-0.2.0-x64-Setup.exe"
InstallDir "$LOCALAPPDATA\Programs\DesktopBoxes"
RequestExecutionLevel user
SetCompressor /SOLID lzma
BrandingText "桌面归纳盒 · 当前用户离线安装"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\DesktopBoxes.exe"
!define MUI_FINISHPAGE_RUN_TEXT "启动桌面归纳盒（请先使用临时文件验收）"
!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "SimpChinese"
Function .onInit
  ${IfNot} ${AtLeastWin10}
    MessageBox MB_OK|MB_ICONSTOP "此版本需要 Windows 10 或更新版本。"
    Abort
  ${EndIf}
  System::Call 'kernel32::OpenMutexW(i 0x100000, i 0, w "Local\DesktopBoxes.UserInstance") p.r0'
  ${If} $0 != 0
    System::Call 'kernel32::CloseHandle(p r0)'
    MessageBox MB_OK|MB_ICONEXCLAMATION "请先从托盘退出桌面归纳盒，再进行安装或升级。"
    Abort
  ${EndIf}
  ${IfNot} ${RunningX64}
    MessageBox MB_OK|MB_ICONSTOP "此安装包仅支持 Windows x64。"
    Abort
  ${EndIf}
FunctionEnd
Section "当前用户安装"
 SetShellVarContext current
 SetOutPath "$INSTDIR"
 File "../build/DesktopBoxes.exe"
 File "../build/WallpaperPreview.exe"
 File "../build/DesktopIntegrationProbe.exe"
 File /oname=使用说明.txt "../docs/使用说明.md"
 File /oname=测试报告.txt "../docs/测试报告.md"
 File /oname=第三方声明.txt "../docs/第三方声明.md"
 File /oname=安全检查.txt "../docs/security/README.md"
 SetOutPath "$INSTDIR\assets\wallpaper"
 File "../assets/wallpaper/approved-static.png"
 File "../assets/wallpaper/background.png"
 File "../assets/wallpaper/anthurium.png"
 File "../assets/wallpaper/monstera.png"
 File "../assets/wallpaper/banana.png"
 File "../assets/wallpaper/provenance.json"
 SetOutPath "$INSTDIR\licenses"
 File "../licenses/gcc-mingw-w64-copyright"
 File "../licenses/mingw-w64-copyright"
 File "../licenses/nsis-copyright"
 SetOutPath "$INSTDIR"
 WriteUninstaller "$INSTDIR\Uninstall.exe"
 CreateDirectory "$SMPROGRAMS\桌面归纳盒"
 CreateShortcut "$SMPROGRAMS\桌面归纳盒\桌面归纳盒.lnk" "$INSTDIR\DesktopBoxes.exe"
 CreateShortcut "$SMPROGRAMS\桌面归纳盒\卸载.lnk" "$INSTDIR\Uninstall.exe"
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DesktopBoxes" "DisplayName" "桌面归纳盒"
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DesktopBoxes" "DisplayVersion" "0.2.0"
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DesktopBoxes" "UninstallString" '$\"$INSTDIR\Uninstall.exe$\"'
 WriteRegStr HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DesktopBoxes" "InstallLocation" "$INSTDIR"
 WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DesktopBoxes" "NoModify" 1
 WriteRegDWORD HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DesktopBoxes" "NoRepair" 1
SectionEnd
Function un.onInit
 System::Call 'kernel32::OpenMutexW(i 0x100000, i 0, w "Local\DesktopBoxes.UserInstance") p.r0'
 ${If} $0 != 0
   System::Call 'kernel32::CloseHandle(p r0)'
   MessageBox MB_OK|MB_ICONEXCLAMATION "请先从托盘退出桌面归纳盒，再卸载。"
   Abort
 ${EndIf}
FunctionEnd
Section "Uninstall"
 SetShellVarContext current
 DeleteRegValue HKCU "Software\Microsoft\Windows\CurrentVersion\Run" "DesktopBoxes"
 DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Uninstall\DesktopBoxes"
 Delete "$SMPROGRAMS\桌面归纳盒\桌面归纳盒.lnk"
 Delete "$SMPROGRAMS\桌面归纳盒\卸载.lnk"
 RMDir "$SMPROGRAMS\桌面归纳盒"
 Delete "$INSTDIR\licenses\gcc-mingw-w64-copyright"
 Delete "$INSTDIR\licenses\mingw-w64-copyright"
 Delete "$INSTDIR\licenses\nsis-copyright"
 RMDir "$INSTDIR\licenses"
 Delete "$INSTDIR\DesktopBoxes.exe"
 Delete "$INSTDIR\WallpaperPreview.exe"
 Delete "$INSTDIR\DesktopIntegrationProbe.exe"
 Delete "$INSTDIR\assets\wallpaper\approved-static.png"
 Delete "$INSTDIR\assets\wallpaper\background.png"
 Delete "$INSTDIR\assets\wallpaper\anthurium.png"
 Delete "$INSTDIR\assets\wallpaper\monstera.png"
 Delete "$INSTDIR\assets\wallpaper\banana.png"
 Delete "$INSTDIR\assets\wallpaper\provenance.json"
 RMDir "$INSTDIR\assets\wallpaper"
 RMDir "$INSTDIR\assets"
 Delete "$INSTDIR\使用说明.txt"
 Delete "$INSTDIR\测试报告.txt"
 Delete "$INSTDIR\第三方声明.txt"
 Delete "$INSTDIR\安全检查.txt"
 Delete "$INSTDIR\Uninstall.exe"
 RMDir "$INSTDIR"
 IfSilent retainConfig
 MessageBox MB_YESNO|MB_ICONQUESTION|MB_DEFBUTTON2 "是否同时删除软件配置（路径引用与分类）？原文件不会被删除。选“否”保留配置。" IDNO retainConfig
 Delete "$LOCALAPPDATA\DesktopBoxes\state.db"
 Delete "$LOCALAPPDATA\DesktopBoxes\state.bak"
 Delete "$LOCALAPPDATA\DesktopBoxes\state.tmp"
 Delete "$LOCALAPPDATA\DesktopBoxes\state.corrupt"
 Delete "$LOCALAPPDATA\DesktopBoxes\state-v0.1.db"
 RMDir "$LOCALAPPDATA\DesktopBoxes"
 retainConfig:
SectionEnd
