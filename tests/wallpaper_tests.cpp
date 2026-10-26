#define wWinMain DesktopBoxesPreviewUnusedEntry
#include "../src/wallpaper_preview.cpp"
#undef wWinMain
#include "../src/preview_launcher.hpp"
#include <iostream>
void demand(bool value, const char *why) {
    if (!value)
        throw std::runtime_error(why);
}
void runFor(DWORD ms) {
    auto end = GetTickCount64() + ms;
    MSG msg;
    while (GetTickCount64() < end) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message != WM_QUIT) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        Sleep(5);
    }
}
int wmain(int argc, wchar_t **argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput gi;
    Gdiplus::GdiplusStartup(&token, &gi, nullptr);
    int result = 1;
    {
        Preview p;
        try {
            wchar_t path[32768];
            GetModuleFileNameW(nullptr, path, 32768);
            std::wstring module(path);
            p.assetDirectory =
                module.substr(0, module.find_last_of(L"\\/")) + L"\\assets\\wallpaper";
            p.fallback = std::make_unique<Gdiplus::Bitmap>(
                (p.assetDirectory + L"\\approved-static.png").c_str());
            demand(SUCCEEDED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &p.factory)),
                   "Direct2D factory");
            demand(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                              CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&p.wic))),
                   "WIC factory");
            auto instance = GetModuleHandleW(nullptr);
            WNDCLASSEXW wc{};
            wc.cbSize = sizeof(wc);
            wc.hInstance = instance;
            wc.lpfnWndProc = proc;
            wc.lpszClassName = L"DesktopBoxes.WallpaperTest";
            wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
            RegisterClassExW(&wc);
            HWND h = CreateWindowExW(0, wc.lpszClassName,
                                     L"Wine 10 兼容层 · 独立叶片运动审阅 · 非 Windows 10 桌面验收",
                                     WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 80, 70, 1280, 830,
                                     nullptr, nullptr, instance, &p);
            demand(h != nullptr, "native preview window");
            ShowWindow(h, SW_SHOWNORMAL);
            SetForegroundWindow(h);
            SetCursorPos(1450, 900);
            runFor(700);
            demand(!p.failed && p.target && p.images[3] && p.renderedFrames > 1,
                   "offline layers actually decode and render");
            demand(p.interval == 67, "default energy interval");
            std::cout << "PASS offline Direct2D/WIC leaf layers render in native preview; default "
                         "capped energy timer\n";
            SendMessageW(p.mode, CB_SETCURSEL, 0, 0);
            SendMessageW(h, WM_COMMAND, MAKEWPARAM(1, CBN_SELCHANGE), (LPARAM)p.mode);
            runFor(100);
            auto frames = p.renderedFrames;
            double time = p.clock.seconds(GetTickCount64());
            runFor(250);
            demand(p.interval == 0 && p.renderedFrames == frames &&
                       p.clock.seconds(GetTickCount64()) == time,
                   "static stops frame rendering and clock");
            std::cout << "PASS static mode stops both frame timer and motion clock\n";
            SendMessageW(p.mode, CB_SETCURSEL, 2, 0);
            SendMessageW(h, WM_COMMAND, MAKEWPARAM(1, CBN_SELCHANGE), (LPARAM)p.mode);
            demand(p.interval == 34, "standard capped interval");
            runFor(150);
            p.pause(playback::Reason::Locked, true);
            p.pause(playback::Reason::Sleeping, true);
            p.pause(playback::Reason::Sleeping, false);
            frames = p.renderedFrames;
            runFor(200);
            demand(p.interval == 0 && p.renderedFrames == frames,
                   "wake cannot resume a locked preview");
            p.pause(playback::Reason::Locked, false);
            runFor(200);
            demand(p.interval == 34 && p.renderedFrames > frames,
                   "final pause reason cleared resumes");
            ShowWindow(h, SW_HIDE);
            runFor(100);
            frames = p.renderedFrames;
            runFor(200);
            demand(p.interval == 0 && p.renderedFrames == frames, "hidden preview stops rendering");
            ShowWindow(h, SW_SHOWNORMAL);
            runFor(200);
            std::cout << "PASS interleaved pause reasons, hidden stop and event-driven resume\n";
            if (argc > 1 && std::wstring(argv[1]) == L"--capture") {
                SetWindowTextW(h, L"Wine 10 兼容层 · 分层运动预览（不是动态桌面）");
                std::cout << "CAPTURE READY: native preview playing for 20 seconds\n" << std::flush;
                runFor(20000);
            }
            p.failed = true;
            p.sync();
            InvalidateRect(h, nullptr, FALSE);
            runFor(100);
            demand(p.interval == 0 && p.fallback->GetLastStatus() == Gdiplus::Ok,
                   "render failure static fallback");
            DestroyWindow(h);
            demand(p.hooks.empty() && p.interval == 0, "hooks and timers released");
            std::cout << "PASS drawing failure static fallback and window-event/timer cleanup\n";
            HANDLE child = nullptr;
            {
                PreviewLauncher launcher;
                demand(launcher.open(), "launch app-owned native helper");
                child = OpenProcess(SYNCHRONIZE, FALSE, launcher.pid());
                demand(child != nullptr, "child process handle");
            }
            demand(WaitForSingleObject(child, 3000) == WAIT_OBJECT_0,
                   "job object closes helper on owner exit");
            CloseHandle(child);
            std::cout << "PASS app-owned preview process terminates when its job owner closes\n";
            result = 0;
        } catch (const std::exception &e) {
            std::cerr << "FAIL wallpaper: " << e.what() << "\n";
            if (IsWindow(p.hwnd))
                DestroyWindow(p.hwnd);
        }
    }
    Gdiplus::GdiplusShutdown(token);
    CoUninitialize();
    return result;
}
