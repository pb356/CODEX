// Actual Win32 workers and UI exercised only with private temporary fixtures.
#define wWinMain DesktopBoxesUnusedEntry
#include "../src/main.cpp"
#undef wWinMain
#include <filesystem>
#include <iostream>
namespace fs = std::filesystem;
void checkFeature(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
void pumpFeature() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message != WM_QUIT) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
}
template <class Fn> void eventually(Fn fn, const char *why, DWORD timeout = 7000) {
    auto end = GetTickCount64() + timeout;
    do {
        pumpFeature();
        if (fn())
            return;
        Sleep(10);
    } while (GetTickCount64() < end);
    throw std::runtime_error(why);
}
void tempFile(const std::wstring &p, const char *contents = "reference-safe") {
    HANDLE file = CreateFileW(p.c_str(), GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    checkFeature(file != INVALID_HANDLE_VALUE, "create private fixture");
    DWORD n;
    bool ok = WriteFile(file, contents, (DWORD)strlen(contents), &n, nullptr) != FALSE;
    CloseHandle(file);
    checkFeature(ok, "write private fixture");
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
    std::wstring dir;
    int result = 1;
    try {
        wchar_t temp[MAX_PATH];
        checkFeature(GetTempPathW(MAX_PATH, temp) > 0, "temp path");
        dir = std::wstring(temp) + L"DesktopBoxesFeatures-" +
              std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        checkFeature(CreateDirectoryW(dir.c_str(), nullptr) != FALSE, "private directory");
        app.dir = dir;
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
        CreateWindowExW(WS_EX_TOOLWINDOW, L"DesktopBoxes.Controller", L"Feature fixture", WS_POPUP,
                        0, 0, 0, 0, nullptr, nullptr, app.instance, &app);

        { // Don't consume notifications: simulate a UI thread temporarily busy in a native dialog.
            AsyncQueue<int, int> queue(app.controller, WM_APP + 60, [](const int &n) { return n; });
            for (int k = 0; k < 128; ++k)
                checkFeature(queue.submit(std::to_string(k), k), "accepted initial worker job");
            Sleep(200);
            for (int k = 128; k < 256; ++k)
                checkFeature(queue.submit(std::to_string(k), k), "accepted queued worker job");
            Sleep(200);
            std::set<int> got;
            eventually(
                [&] {
                    for (auto n : queue.take())
                        got.insert(n);
                    return got.size() == 256;
                },
                "worker must not discard accepted results");
            checkFeature(*got.begin() == 0 && *got.rbegin() == 255, "all bounded worker results");
            checkFeature(queue.stop(), "worker cancellation");
        }
        std::cout << "PASS bounded worker backpressure delivers 256 accepted results without "
                     "silent loss\n";

        box::Box b;
        b.id = 1;
        b.w = 400;
        b.h = 340;
        for (int k = 0; k < 11001; ++k)
            b.items.push_back({"C:\\PrivateFixture\\" + std::to_string(k) + ".txt"});
        ProjectionJob projection;
        projection.id = 1;
        projection.revision = 3;
        projection.snapshot = b;
        projection.query = "11000";
        auto p = project(projection);
        checkFeature(p.indices.size() == 1 && p.indices[0] == 11000,
                     "search covers offscreen last reference");
        projection.query.clear();
        projection.snapshot.sort = 1;
        p = project(projection);
        checkFeature(p.indices.size() == 11001 && p.labels.size() == 11001,
                     "sort covers entire model");
        checkFeature(p.labels[p.indices.front()] == L"0.txt", "name sorting");
        for (auto count : {10u, 200u, 1000u, 10000u}) {
            ProjectionJob sizeCase;
            sizeCase.snapshot = b;
            sizeCase.snapshot.items.resize(count);
            sizeCase.query = std::to_string(count - 1) + ".txt";
            auto result = project(sizeCase);
            checkFeature(std::find(result.indices.begin(), result.indices.end(), int(count - 1)) !=
                             result.indices.end(),
                         "size checkpoint search reaches final item");
            sizeCase.query.clear();
            sizeCase.snapshot.sort = 2;
            result = project(sizeCase);
            checkFeature(result.indices.size() == count, "size checkpoint sorts full list");
        }
        app.state.nextId = 2;
        app.state.boxes = {b};
        app.state.animations = false;
        app.createView(1);
        auto v = app.view(1);
        checkFeature(v && v->hwnd, "native virtualized box");
        app.projections = std::make_unique<AsyncQueue<ProjectionJob, ProjectionResult>>(
            app.controller, WM_APP + 6, project);
        v->filterDirty = true;
        v->requestProjection();
        eventually([&] { return !v->loading && v->filtered.size() == 11001; },
                   "background projection reaches native view");
        RECT size{};
        GetWindowRect(v->hwnd, &size);
        v->focusPart = 3;
        SendMessageW(v->hwnd, WM_KEYDOWN, VK_END, 0);
        checkFeature(v->selected == 11000 && v->data().scroll > 0,
                     "keyboard reaches last reference");
        SendMessageW(v->hwnd, WM_KEYDOWN, VK_HOME, 0);
        checkFeature(v->selected == 0 && v->data().scroll == 0,
                     "keyboard returns to first reference");
        v->query = "11000";
        v->requestProjection();
        v->query = "10999";
        v->requestProjection();
        eventually(
            [&] { return !v->loading && v->filtered.size() == 1 && v->filtered[0] == 10999; },
            "latest query wins over stale result");
        RECT after{};
        GetWindowRect(v->hwnd, &after);
        checkFeature(EqualRect(&size, &after), "large dataset never grows box");
        std::cout << "PASS 11001 references: full search/sort, stale result rejection, first/last "
                     "navigation and fixed geometry\n";

        app.pauseFor(playback::Reason::Locked, true);
        app.pauseFor(playback::Reason::Sleeping, true);
        app.pauseFor(playback::Reason::Sleeping, false);
        checkFeature(app.suspended && v->lastFrame == 0, "wake leaves locked UI timers stopped");
        app.pauseFor(playback::Reason::Locked, false);
        checkFeature(!app.suspended, "unlock clears final reason");
        std::cout
            << "PASS Win32 lifecycle: suspend/resume cannot override a concurrent lock pause\n";

        { // Two opaque windows together, neither alone, fully cover the target.
            HWND left = CreateWindowW(L"STATIC", L"", WS_POPUP | WS_VISIBLE, 10, 10, 100, 100,
                                      nullptr, nullptr, app.instance, nullptr);
            HWND right = CreateWindowW(L"STATIC", L"", WS_POPUP | WS_VISIBLE, 110, 10, 100, 100,
                                       nullptr, nullptr, app.instance, nullptr);
            CoverCheck c{nullptr, {10, 10, 210, 110}, false, CreateRectRgn(10, 10, 210, 110)};
            coverEnum(left, (LPARAM)&c);
            checkFeature(!c.covered, "partial coverage stays active");
            coverEnum(right, (LPARAM)&c);
            checkFeature(c.covered, "combined coverage recognized");
            DeleteObject(c.remaining);
            DestroyWindow(left);
            DestroyWindow(right);
        }
        std::cout
            << "PASS combined opaque-window coverage uses native regions without screenshots\n";

        v->fade = 1;
        v->softX = 90;
        v->softY = 100;
        v->draw();
        checkFeature(v->glowBitmap != nullptr, "reused glow surface created by actual renderer");
        Gdiplus::Color center;
        checkFeature(v->glowBitmap->GetPixel(63, 63, &center) == Gdiplus::Ok &&
                         center.GetAlpha() <= 30,
                     "decorative glow alpha bounded to at most twelve percent");
        v->fade = 0;
        v->inside = false;
        v->stop();
        std::cout << "PASS rendered glow uses a low-alpha surface rather than an opaque bright "
                     "gradient\n";

        { // Exercise actual native settings controls, not a mirrored form model.
            v->data().icon = 36;
            v->data().font = 15;
            v->data().opacity = 173;
            v->data().scroll = 7;
            settings_ui::Panel panel;
            panel.app = &app;
            panel.view = v;
            panel.before = v->data();
            panel.draft = v->data();
            struct DialogTemplate {
                DLGTEMPLATE d;
                WORD menu = 0, cls = 0, title = 0;
            } t{};
            t.d.style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
            t.d.cx = 360;
            t.d.cy = 400;
            HWND h = CreateDialogIndirectParamW(app.instance, &t.d, app.controller,
                                                settings_ui::dialogProc, (LPARAM)&panel);
            checkFeature(h && panel.get(settings_ui::Name), "native settings initialized");
            SetWindowTextW(panel.get(settings_ui::Name), L"仅改名称");
            panel.preview();
            checkFeature(v->data().name == "仅改名称" && v->data().icon == 36 &&
                             v->data().font == 15 && v->data().opacity == 173,
                         "editing name preserves custom appearance values");
            checkFeature(
                SendMessageW(panel.get(settings_ui::Opacity), TBM_GETRANGEMIN, 0, 0) == 0 &&
                    SendMessageW(panel.get(settings_ui::Opacity), TBM_GETRANGEMAX, 0, 0) == 100,
                "user facing percentage");
            panel.page = 1;
            panel.build();
            checkFeature(panel.get(settings_ui::Effect0) && panel.get(settings_ui::Effect2),
                         "effect page controls");
            panel.page = 2;
            panel.build();
            checkFeature(panel.get(settings_ui::Locked), "layout page controls");
            DestroyWindow(h);
            box::appearance(v->data(), panel.before);
            v->data().scroll = panel.before.scroll;
            checkFeature(v->data().items.size() == 11001 && v->data().scroll == 7,
                         "appearance rollback leaves references and scroll");
            auto accessible = new BoxAccessible(v->life);
            VARIANT child{};
            child.vt = VT_I4;
            child.lVal = 1;
            BSTR name = nullptr;
            checkFeature(accessible->get_accName(child, &name) == S_OK && name &&
                             SysStringLen(name) > 0,
                         "fold button accessible name");
            SysFreeString(name);
            accessible->Release();
        }
        std::cout << "PASS native settings tabs, percentage control, custom-value preservation, "
                     "appearance rollback and accessible button\n";

        { // Both injected roots are private fixtures. Never watches the user's real desktop.
            auto user = dir + L"\\user", pub = dir + L"\\public";
            CreateDirectoryW(user.c_str(), nullptr);
            CreateDirectoryW(pub.c_str(), nullptr);
            tempFile(user + L"\\existing.txt");
            tempFile(user + L"\\legitimate-zero.txt", "");
            tempFile(user + L"\\saving.tmp");
            tempFile(user + L"\\~$locked.docx");
            tempFile(pub + L"\\public.lnk", "not executed");
            CreateDirectoryW((user + L"\\folder.tmp").c_str(), nullptr);
            tempFile(user + L"\\folder.tmp\\nested.txt");
            InboxWatcher watcher(app.controller);
            InboxRequest request;
            request.enabled = true;
            request.revision = 1;
            request.markNewOnScan = false;
            request.testRoots = {user, pub};
            watcher.update(request);
            InboxSnapshot latest;
            auto waitCount = [&](size_t count) {
                eventually(
                    [&] {
                        auto s = watcher.take();
                        if (!s.roots.empty())
                            latest = std::move(s);
                        return latest.revision == request.revision && latest.healthy &&
                               latest.items.size() == count;
                    },
                    "inbox fixture count");
            };
            waitCount(4);
            checkFeature(std::none_of(latest.items.begin(), latest.items.end(),
                                      [](auto &i) { return i.fresh; }),
                         "first inventory no new marker");
            tempFile(user + L"\\arriving.crdownload");
            Sleep(750);
            pumpFeature();
            checkFeature(MoveFileW((user + L"\\arriving.crdownload").c_str(),
                                   (user + L"\\finished.txt").c_str()) != FALSE,
                         "temp final rename");
            waitCount(5);
            checkFeature(std::any_of(latest.items.begin(), latest.items.end(),
                                     [](auto &i) {
                                         return box::nameOf(i.path) == "finished.txt" && i.fresh;
                                     }),
                         "final name new entry");
            tempFile(user + L"\\existing.txt", "overwritten externally");
            Sleep(800);
            pumpFeature();
            request.prior = latest.items;
            request.revision++;
            request.paused = true;
            watcher.update(request);
            Sleep(200);
            tempFile(user + L"\\while-paused.txt");
            request.paused = false;
            request.markNewOnScan = true;
            request.revision++;
            watcher.update(request);
            waitCount(6);
            // A manual refresh uses the same top-level reconciliation as notification overflow
            // recovery.
            request.prior = latest.items;
            request.revision++;
            watcher.update(request);
            waitCount(6);
            checkFeature(watcher.stop(), "inbox cancels directory IO");
            HANDLE f = CreateFileW((user + L"\\existing.txt").c_str(), GENERIC_READ,
                                   FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                                   OPEN_EXISTING, 0, nullptr);
            char bytes[64]{};
            DWORD n = 0;
            checkFeature(f != INVALID_HANDLE_VALUE &&
                             ReadFile(f, bytes, sizeof(bytes), &n, nullptr),
                         "read unchanged external result");
            CloseHandle(f);
            checkFeature(std::string(bytes, n) == "overwritten externally",
                         "watcher never edits file");
        }
        std::cout << "PASS desktop inbox private roots: existing/public/zero-byte/folder, temp "
                     "filtering, rename, replacement, pause/resume and bounded rescan\n";
        result = 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL features: " << e.what() << "\n";
    }
    app.inbox.reset();
    app.projections.reset();
    app.iconLoader.reset();
    app.monitor.reset();
    app.views.clear();
    if (app.controller)
        DestroyWindow(app.controller);
    GdiplusShutdown(app.gdip);
    OleUninitialize();
    CoUninitialize();
    if (result == 0 && !dir.empty())
        fs::remove_all(dir);
    return result;
}
