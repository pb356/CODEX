// Native Win32 UI-code smoke tests. Same-process messages; never hooks Explorer or a real desktop.
#define wWinMain DesktopBoxesUnusedEntry
#include "../src/main.cpp"
#undef wWinMain
#include <iostream>
void requireUi(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
void pumpFor(DWORD duration) {
    auto end = GetTickCount64() + duration;
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
int wmain() {
    App app;
    globalApp = &app;
    app.instance = GetModuleHandleW(nullptr);
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    GdiplusStartupInput input;
    GdiplusStartup(&app.gdip, &input, nullptr);
    int result = 1;
    try {
        wchar_t temp[32768];
        requireUi(GetTempPathW(32768, temp) > 0, "temp");
        app.dir = std::wstring(temp) + L"DesktopBoxesUI-" + std::to_wstring(GetCurrentProcessId()) +
                  L"-" + std::to_wstring(GetTickCount64());
        requireUi(CreateDirectoryW(app.dir.c_str(), nullptr) != FALSE, "unique test directory");
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
        CreateWindowExW(WS_EX_TOOLWINDOW, L"DesktopBoxes.Controller", L"UI Test", WS_POPUP, 0, 0, 0,
                        0, nullptr, nullptr, app.instance, &app);
        app.state.nextId = 3;
        box::Box a, b;
        a.id = 1;
        b.id = 2;
        b.x = 540;
        app.state.boxes = {a, b};
        app.createView(1);
        app.createView(2);
        auto v = app.views[0].get();
        auto other = app.views[1].get();
        requireUi(v->hwnd && other->hwnd, "windows created");
        app.state.animations = false;
        RECT before{}, neighbor{};
        GetWindowRect(v->hwnd, &before);
        GetWindowRect(other->hwnd, &neighbor);
        v->data().scroll = 0;
        v->toggle();
        RECT folded{};
        GetWindowRect(v->hwnd, &folded);
        requireUi(folded.bottom - folded.top == v->px(Title), "fold releases real window area");
        requireUi(v->data().h == 340, "expanded height retained");
        RECT after{};
        GetWindowRect(other->hwnd, &after);
        requireUi(EqualRect(&neighbor, &after), "other box geometry independent");
        v->toggle();
        GetWindowRect(v->hwnd, &after);
        requireUi(EqualRect(&before, &after), "expand restores geometry");
        std::cout << "PASS real HWND fold/expand and independent layout\n";
        std::wstring path = app.dir + L"\\temporary reference only.txt";
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                  CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        requireUi(file != INVALID_HANDLE_VALUE, "temporary file");
        DWORD written;
        WriteFile(file, "unchanged", 9, &written, nullptr);
        CloseHandle(file);
        auto sendDrop = [&](const std::wstring &p) {
            size_t bytes = sizeof(DROPFILES) + (p.size() + 2) * sizeof(wchar_t);
            HGLOBAL memory = GlobalAlloc(GHND, bytes);
            auto drop = (DROPFILES *)GlobalLock(memory);
            drop->pFiles = sizeof(DROPFILES);
            drop->fWide = TRUE;
            memcpy((BYTE *)drop + sizeof(DROPFILES), p.c_str(), (p.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(memory);
            SendMessageW(v->hwnd, WM_DROPFILES, (WPARAM)memory, 0);
        };
        sendDrop(path);
        requireUi(v->data().items.size() == 1 && v->data().items[0].path == canonical(path),
                  "real WM_DROPFILES adds reference");
        requireUi(box::transfer(v->data(), other->data(), 0), "cross-box transfer");
        requireUi(v->data().items.empty() && other->data().items.size() == 1, "ownership");
        std::cout << "PASS WM_DROPFILES reference and cross-box ownership\n";
        v->data().through = true;
        v->changed();
        requireUi((GetWindowLongPtrW(v->hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0,
                  "transparent style");
        requireUi(!v->inside && v->fade == 0, "pass-through disables effect");
        app.command(RestoreInput);
        requireUi(!v->data().through &&
                      !(GetWindowLongPtrW(v->hwnd, GWL_EXSTYLE) & WS_EX_TRANSPARENT),
                  "tray restore input command");
        std::cout << "PASS pass-through style and restoration (physical desktop input untested)\n";
        app.state.animations = true;
        v->toggle();
        pumpFor(400);
        GetWindowRect(v->hwnd, &folded);
        requireUi(!v->animating && folded.bottom - folded.top == v->px(Title),
                  "animated fold completes with real geometry");
        v->toggle();
        pumpFor(400);
        requireUi(!v->animating, "animated expansion completes");
        SendMessageW(v->hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(50, 70));
        pumpFor(300);
        SendMessageW(v->hwnd, WM_MOUSELEAVE, 0, 0);
        pumpFor(500);
        requireUi(v->fade == 0 && v->lastFrame == 0, "fade completes and timer stops");
        std::cout << "PASS animation completion and stopped fade timer\n";
        app.state.animations = false;
        SendMessageW(v->hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(50, 70));
        SendMessageW(v->hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(100, 100));
        requireUi(v->smooth.x == 50 && v->smooth.y == 70 && v->lastFrame == 0,
                  "reduced-motion glow stays static without timer");
        SendMessageW(v->hwnd, WM_MOUSELEAVE, 0, 0);
        for (int i = 0; i < 500; ++i) {
            v->toggle();
            v->toggle();
            SendMessageW(v->hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(50, 70));
            SendMessageW(v->hwnd, WM_MOUSELEAVE, 0, 0);
        }
        requireUi(!v->animating && v->lastFrame == 0, "no accumulated timer after rapid toggles");
        app.save();
        std::vector<uint8_t> bytes;
        requireUi(readBytes(app.dir + L"\\state.db", bytes), "UI save");
        auto state = box::decode(bytes);
        requireUi(state.boxes.size() == 2 && !state.boxes[0].collapsed &&
                      state.boxes[1].items.size() == 1,
                  "saved UI state");
        std::cout << "PASS 500 rapid fold pairs and saved state\n";
        // Actual temporary original is readable and renameable after all reference/UI operations.
        file = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        char actual[9];
        DWORD got;
        requireUi(file != INVALID_HANDLE_VALUE && ReadFile(file, actual, 9, &got, nullptr) &&
                      got == 9 && memcmp(actual, "unchanged", 9) == 0,
                  "original bytes");
        CloseHandle(file);
        app.monitor = std::make_unique<Monitor>(app.controller);
        app.monitor->update(app.state);
        auto waitStatus = [&](int status) {
            auto end = GetTickCount64() + 3000;
            while (GetTickCount64() < end) {
                pumpFor(50);
                auto it = other->health.find(canonical(path));
                if (it != other->health.end() && it->second == status)
                    return true;
            }
            return false;
        };
        requireUi(waitStatus(1), "background initial status");
        requireUi(MoveFileW(path.c_str(), (app.dir + L"\\renamed.txt").c_str()) != FALSE,
                  "original not held open");
        requireUi(waitStatus(2), "event-driven missing status after rename");
        requireUi(app.monitor->stop(), "watcher stops and cancels IO");
        app.monitor.reset();
        std::cout << "PASS event-driven file status, rename invalidation and watcher cleanup\n";
        DeleteFileW((app.dir + L"\\renamed.txt").c_str());
        std::cout << "PASS original content and released handles after UI operations\n";
        result = 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL UI smoke: " << e.what() << '\n';
    }
    if (app.monitor) {
        if (!app.monitor->stop())
            app.monitor.release();
        else
            app.monitor.reset();
    }
    for (auto &v : app.views)
        DestroyWindow(v->hwnd);
    app.views.clear();
    if (app.controller)
        DestroyWindow(app.controller);
    for (auto n : {L"state.db", L"state.bak", L"state.tmp"})
        DeleteFileW((app.dir + L"\\" + n).c_str());
    RemoveDirectoryW(app.dir.c_str());
    GdiplusShutdown(app.gdip);
    CoUninitialize();
    return result;
}
