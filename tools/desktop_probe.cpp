// Read-only Windows integration evidence. Does not hide icons, set wallpaper or send private
// messages.
#include "../src/platform.hpp"
#include <exdisp.h>
#include <iostream>
#include <servprov.h>
int wmain() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    std::cout << "DesktopBoxes read-only desktop probe 0.2.0\n";
    wchar_t build[64]{};
    DWORD bytes = sizeof(build);
    auto r = RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                          L"CurrentBuildNumber", RRF_RT_REG_SZ, nullptr, build, &bytes);
    std::cout << "OS build: " << (r == ERROR_SUCCESS ? desktop::utf8(build) : "unavailable")
              << "\n";
    std::cout << "Remote session: " << (GetSystemMetrics(SM_REMOTESESSION) != 0) << "\n";
    for (auto id : {FOLDERID_Desktop, FOLDERID_PublicDesktop}) {
        PWSTR path = nullptr;
        HRESULT hr = SHGetKnownFolderPath(id, 0, nullptr, &path);
        std::cout << "Known Folder resolved: " << SUCCEEDED(hr) << " (path omitted from report)\n";
        CoTaskMemFree(path);
    }
    IDesktopWallpaper *wallpaper = nullptr;
    HRESULT hr =
        CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&wallpaper));
    std::cout << "Public IDesktopWallpaper: 0x" << std::hex << uint32_t(hr) << std::dec << "\n";
    if (wallpaper) {
        UINT count = 0;
        hr = wallpaper->GetMonitorDevicePathCount(&count);
        std::cout << "Wallpaper monitors: "
                  << (SUCCEEDED(hr) ? std::to_string(count) : "unavailable") << "\n";
        wallpaper->Release();
    }
    IShellWindows *windows = nullptr;
    IDispatch *dispatch = nullptr;
    IServiceProvider *provider = nullptr;
    IShellBrowser *browser = nullptr;
    IShellView *view = nullptr;
    IFolderView2 *folder = nullptr;
    hr = CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&windows));
    VARIANT empty{};
    long shellHwnd = 0;
    if (SUCCEEDED(hr))
        hr = windows->FindWindowSW(&empty, &empty, SWC_DESKTOP, &shellHwnd, SWFO_NEEDDISPATCH,
                                   &dispatch);
    if (SUCCEEDED(hr) && dispatch)
        hr = dispatch->QueryInterface(IID_PPV_ARGS(&provider));
    else
        hr = E_NOINTERFACE;
    if (SUCCEEDED(hr))
        hr = provider->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&browser));
    if (SUCCEEDED(hr))
        hr = browser->QueryActiveShellView(&view);
    if (SUCCEEDED(hr))
        hr = view->QueryInterface(IID_PPV_ARGS(&folder));
    DWORD flags = 0;
    if (SUCCEEDED(hr))
        hr = folder->GetCurrentFolderFlags(&flags);
    std::cout << "Public desktop IFolderView2 query: 0x" << std::hex << uint32_t(hr) << std::dec
              << "\n";
    if (SUCCEEDED(hr))
        std::cout << "Desktop view FWF_NOICONS: " << ((flags & FWF_NOICONS) != 0) << "\n";
    if (folder)
        folder->Release();
    if (view)
        view->Release();
    if (browser)
        browser->Release();
    if (provider)
        provider->Release();
    if (dispatch)
        dispatch->Release();
    if (windows)
        windows->Release();
    std::cout << "No modifications performed. WorkerW embedding, icon restoration, Explorer "
                 "restart and physical desktop input remain untested.\n";
    CoUninitialize();
    return 0;
}
