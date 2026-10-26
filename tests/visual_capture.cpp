// Capture actual native controls on an isolated test display. Never use a user's live desktop.
#define wWinMain DesktopBoxesUnusedEntry
#include "../src/main.cpp"
#undef wWinMain
#include <filesystem>
#include <iostream>
void capturePump(DWORD ms) {
    auto end = GetTickCount64() + ms;
    MSG msg;
    while (GetTickCount64() < end) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message != WM_QUIT) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }
        }
        Sleep(10);
    }
}
void saveCapture(const wchar_t *name) {
    int w = GetSystemMetrics(SM_CXSCREEN), h = GetSystemMetrics(SM_CYSCREEN);
    HDC screen = GetDC(nullptr), copy = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateCompatibleBitmap(screen, w, h);
    auto old = SelectObject(copy, bitmap);
    BitBlt(copy, 0, 0, w, h, screen, 0, 0, SRCCOPY | CAPTUREBLT);
    SelectObject(copy, old);
    Gdiplus::Bitmap image(bitmap, nullptr);
    CLSID png{0x557cf406, 0x1a04, 0x11d3, {0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e}};
    auto status = image.Save(name, &png, nullptr);
    DeleteObject(bitmap);
    DeleteDC(copy);
    ReleaseDC(nullptr, screen);
    if (status != Gdiplus::Ok) {
        std::cerr << "PNG capture GDI+ status " << status << "\n";
        throw std::runtime_error("capture save");
    }
}
LRESULT CALLBACK backdrop(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_PAINT) {
        PAINTSTRUCT paint;
        auto dc = BeginPaint(h, &paint);
        Gdiplus::Graphics g(dc);
        Gdiplus::Bitmap art(L"assets\\wallpaper\\approved-static.png");
        RECT r;
        GetClientRect(h, &r);
        g.DrawImage(&art, 0, 0, r.right, r.bottom);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
        Gdiplus::Font f(L"Noto Sans CJK SC", 16);
        Gdiplus::SolidBrush ink(Gdiplus::Color(255, 45, 57, 47));
        g.DrawString(L"Wine 10 / Debian 13 · 原生窗口兼容层截图，非 Windows 10 实机", -1, &f,
                     Gdiplus::PointF(24, 24), &ink);
        EndPaint(h, &paint);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
int wmain() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    OleInitialize(nullptr);
    INITCOMMONCONTROLSEX common{sizeof(common),
                                ICC_WIN95_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&common);
    App app;
    globalApp = &app;
    app.instance = GetModuleHandleW(nullptr);
    GdiplusStartupInput gi;
    GdiplusStartup(&app.gdip, &gi, nullptr);
    int result = 1;
    HWND background = nullptr;
    std::wstring dir;
    try {
        wchar_t temp[MAX_PATH];
        GetTempPathW(MAX_PATH, temp);
        dir = std::wstring(temp) + L"DesktopBoxesVisual-" + std::to_wstring(GetCurrentProcessId());
        CreateDirectoryW(dir.c_str(), nullptr);
        app.dir = dir;
        std::filesystem::create_directories(L"docs\\review");
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = app.instance;
        wc.lpfnWndProc = controlProc;
        wc.lpszClassName = L"DesktopBoxes.Controller";
        RegisterClassExW(&wc);
        wc.lpfnWndProc = boxProc;
        wc.style = CS_DBLCLKS;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"DesktopBoxes.Box";
        RegisterClassExW(&wc);
        wc.lpfnWndProc = backdrop;
        wc.lpszClassName = L"DesktopBoxes.VisualBackdrop";
        RegisterClassExW(&wc);
        background = CreateWindowExW(WS_EX_NOACTIVATE, wc.lpszClassName,
                                     L"Compatibility review fixture", WS_POPUP | WS_VISIBLE, 0, 0,
                                     1600, 1000, nullptr, nullptr, app.instance, nullptr);
        SetWindowPos(background, HWND_BOTTOM, 0, 0, 1600, 1000, SWP_NOACTIVATE);
        CreateWindowExW(WS_EX_TOOLWINDOW, L"DesktopBoxes.Controller", L"Capture fixture", WS_POPUP,
                        0, 0, 0, 0, nullptr, nullptr, app.instance, &app);
        app.projections = std::make_unique<AsyncQueue<ProjectionJob, ProjectionResult>>(
            app.controller, WM_APP + 6, project);
        app.iconLoader = std::make_unique<AsyncQueue<IconJob, IconResult>>(app.controller,
                                                                           WM_APP + 7, systemIcon);
        app.state.nextId = 6;
        std::vector<std::string> names{"进行中项目", "设计软件", "植物素材", "方案汇报",
                                       "临时文件"};
        for (int k = 0; k < 5; ++k) {
            box::Box b;
            b.id = k + 1;
            b.name = names[k];
            b.x = 760 + (k % 2) * 405;
            b.y = 100 + (k / 2) * 288;
            b.w = 385;
            b.h = 265;
            design::theme(b, k == 1 ? 0 : k == 3 ? 2 : 1);
            for (int n = 0; n < 40; ++n) {
                auto path = dir + L"\\" + wide(names[k]) + L"-" + std::to_wstring(n) + L".txt";
                HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                          nullptr, CREATE_NEW, 0, nullptr);
                if (file != INVALID_HANDLE_VALUE)
                    CloseHandle(file);
                b.items.push_back({canonical(path)});
            }
            app.state.boxes.push_back(std::move(b));
        }
        for (auto &b : app.state.boxes)
            app.createView(b.id);
        SetCursorPos(30, 960);
        capturePump(2200);
        saveCapture(L"docs\\review\\01-boxes-wine.png");
        auto v = app.views.front().get();
        SendMessageW(v->hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(120, 100));
        capturePump(600);
        v->selected = 2;
        v->draw();
        saveCapture(L"docs\\review\\02-hover-selection-wine.png");
        SendMessageW(v->hwnd, WM_MOUSELEAVE, 0, 0);
        capturePump(500);
        v->toggle();
        capturePump(500);
        saveCapture(L"docs\\review\\03-collapsed-wine.png");
        v->toggle();
        capturePump(500);
        settings_ui::Panel panel;
        panel.app = &app;
        panel.view = v;
        panel.before = v->data();
        panel.draft = v->data();
        struct T {
            DLGTEMPLATE d;
            WORD menu, cls, title;
        } t{};
        t.d.style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
        t.d.cx = 360;
        t.d.cy = 400;
        HWND dialog = CreateDialogIndirectParamW(app.instance, &t.d, app.controller,
                                                 settings_ui::dialogProc, (LPARAM)&panel);
        ShowWindow(dialog, SW_SHOW);
        SetForegroundWindow(dialog);
        capturePump(1200);
        saveCapture(L"docs\\review\\04-settings-appearance-wine.png");
        panel.page = 1;
        TabCtrl_SetCurSel(panel.tab, 1);
        panel.build();
        capturePump(1000);
        saveCapture(L"docs\\review\\05-settings-effects-wine.png");
        DestroyWindow(dialog);
        box::appearance(v->data(), panel.before);
        v->draw();
        settings_ui::Panel global;
        global.app = &app;
        global.global = true;
        global.effects = true;
        global.animations = true;
        global.page = 2;
        HWND g = CreateDialogIndirectParamW(app.instance, &t.d, app.controller,
                                            settings_ui::dialogProc, (LPARAM)&global);
        ShowWindow(g, SW_SHOW);
        TabCtrl_SetCurSel(global.tab, 2);
        SetForegroundWindow(g);
        capturePump(1000);
        saveCapture(L"docs\\review\\06-settings-wallpaper-wine.png");
        capturePump(1000);
        DestroyWindow(g);
        result = 0;
        std::cout << "PASS captured six native UI states on Wine isolated display; not Windows "
                     "acceptance\n";
    } catch (const std::exception &e) {
        std::cerr << "FAIL visual fixture " << e.what() << "\n";
    }
    app.projections.reset();
    app.iconLoader.reset();
    app.views.clear();
    if (app.controller)
        DestroyWindow(app.controller);
    if (background)
        DestroyWindow(background);
    GdiplusShutdown(app.gdip);
    OleUninitialize();
    CoUninitialize();
    if (result == 0 && !dir.empty())
        std::filesystem::remove_all(dir);
    return result;
}
