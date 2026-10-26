#include "async.hpp"
#include "design.hpp"
#include "inbox.hpp"
#include "platform.hpp"
#include "playback_policy.hpp"
#include "preview_launcher.hpp"
#include <dwmapi.h>
#include <oleacc.h>
#include <sstream>
#include <windowsx.h>
using namespace desktop;
using namespace Gdiplus;
namespace {
constexpr int Title = 42, Gap = 12;
constexpr UINT TrayMsg = WM_APP + 1, SaveTimer = 10, FrameTimer = 11;
enum Command {
    NewBox = 100,
    Settings,
    DeleteBox,
    Collapse,
    Lock,
    Through,
    SortName,
    SortType,
    Refresh,
    RemoveItem,
    OpenItem,
    Relocate,
    Search,
    AllCollapse,
    AllExpand,
    HideAll,
    ShowAll,
    Quit,
    Effects,
    Animations,
    Startup,
    RestoreInput,
    Help,
    NewInbox,
    PauseInbox,
    OpenDesktop,
    InboxAll,
    InboxUnclassified,
    ViewNew,
    GlobalSettings,
    WallpaperSettings,
    RestoreDesktopIcons,
    OpenComputer,
    OpenRecycle,
    SortManual
};
struct App;
struct View {
    App *app;
    uint32_t id;
    HWND hwnd = nullptr;
    UINT dpi = 96;
    int actualH = 0;
    bool inside = false, tracking = false, dragging = false, resizing = false, animating = false,
         effectFailed = false;
    POINT down{}, mouse{}, smooth{};
    int pressed = -1;
    float fade = 0;
    ULONGLONG lastFrame = 0, leaveAt = 0, animStart = 0, occlusionAt = 0;
    int fromH = 0, targetH = 0, fromY = 0, targetY = 0;
    bool occluded = false;
    std::string query, cachedQuery;
    bool filterDirty = true, loading = false, focused = false;
    int selected = -1, hovered = -1, focusPart = 0, dropCount = 0, dropFeedback = 0;
    int dragScrollDirection = 0, thumbOffset = 0;
    bool scrollDragging = false;
    float scrollRemainder = 0, softX = 0, softY = 0;
    uint64_t revision = 0;
    HWND tooltip = nullptr;
    std::shared_ptr<View *> life = std::make_shared<View *>(this);
    bool dropRegistered = false;
    std::string jumpPath;
    std::deque<std::string> importQueue;
    box::PathSet importKeys;
    uint64_t importRevision = 0;
    unsigned imported = 0, importFailed = 0;
    std::wstring importDetails;
    void pumpImports();
    std::wstring notice;
    ULONGLONG noticeUntil = 0;

    std::vector<int> filtered;
    std::vector<std::wstring> labels, types;
    HDC surfaceDC = nullptr;
    HBITMAP surfaceBitmap = nullptr;
    void *surfaceBits = nullptr;
    int surfaceW = 0, surfaceH = 0;
    std::unique_ptr<SolidBrush> starBrush;
    std::unique_ptr<Bitmap> glowBitmap;
    std::unique_ptr<ImageAttributes> glowAttributes;
    uint64_t glowKey = 0;
    void clearSurface() {
        if (surfaceDC)
            DeleteDC(surfaceDC);
        if (surfaceBitmap)
            DeleteObject(surfaceBitmap);
        surfaceDC = nullptr;
        surfaceBitmap = nullptr;
        surfaceBits = nullptr;
    }
    ~View() {
        *life = nullptr;
        if (IsWindow(hwnd))
            DestroyWindow(hwnd);
        clearSurface();
    }

    std::map<std::string, int> health;
    std::map<std::string, std::shared_ptr<SharedIcon>> actualIcons;
    std::map<std::string, std::unique_ptr<Bitmap>> iconBitmaps;
    std::map<std::string, ULONGLONG> iconAges;
    std::set<std::string> requestedIcons;
    std::wstring tooltipBuffer;
    void tooltipText(POINT p);
    void ensureVisible(int item);
    RECT scrollThumb();
    void consumeWheel(int delta);
    void acceptPaths(std::vector<std::string> paths);
    void requestProjection();
    int contentY() const;
    void flash(std::wstring text);

    box::Box &data();
    int px(int x) const {
        return MulDiv(x, dpi, 96);
    }
    int dip(int x) const {
        return MulDiv(x, 96, dpi);
    }
    bool pendingIconRequests = false;
    void draw();
    void fit();
    void toggle();
    void timer();
    void stop();
    void menu(POINT p, int item = -1);
    void open(int index);
    void drop(HDROP d);
    void changed(bool monitor = false);
    const std::vector<int> &visible();
    int hit(POINT p);
    void render();
    void updateStyle();
    void setTimer();
};
struct App {
    HINSTANCE instance;
    HWND controller = nullptr;
    box::State state;
    std::vector<std::unique_ptr<View>> views;
    std::unique_ptr<Monitor> monitor;
    std::unique_ptr<AsyncQueue<ProjectionJob, ProjectionResult>> projections;
    std::unique_ptr<AsyncQueue<IconJob, IconResult>> iconLoader;
    std::unique_ptr<InboxWatcher> inbox;
    std::unique_ptr<PreviewLauncher> wallpaperPreview;
    uint64_t inboxRevision = 0;
    bool inboxHealthy = false, pendingSave = false;
    playback::Gate sessionGate;
    std::vector<std::wstring> desktopRoots;

    std::wstring dir;
    bool hidden = false, suspended = false, saveBlocked = false, saveWarned = false,
         quitting = false, modal = false;
    ULONG_PTR gdip = 0;
    HANDLE singleton = nullptr;
    UINT taskbarCreated = 0;
    void save() {
        if (saveBlocked)
            return;
        if (modal || modalDepth) {
            pendingSave = true;
            return;
        }
        try {
            if (writeConfig(state, dir)) {
                saveWarned = false;
                return;
            }
        } catch (...) {
        }
        if (!saveWarned) {
            saveWarned = true;
            message(controller,
                    L"配置保存失败。当前更改尚未可靠保存，请检查当前用户 AppData "
                    L"的剩余空间与写入权限。",
                    MB_OK | MB_ICONWARNING);
        }
    }
    void dirty(bool update = false) {
        SetTimer(controller, SaveTimer, 600, nullptr);
        if (update && monitor)
            monitor->update(state);
        if (update)
            invalidateInbox();
    }
    View *view(uint32_t id) {
        for (auto &v : views)
            if (v->id == id)
                return v.get();
        return nullptr;
    }
    box::Box *inboxData() {
        for (auto &b : state.boxes)
            if (b.kind == box::Kind::Inbox)
                return &b;
        return nullptr;
    }
    void updateInbox(bool fresh = true);
    void createInbox();
    void invalidateInbox();
    void pauseFor(playback::Reason reason, bool enabled);
    void createView(uint32_t id);
    void create();
    void all(bool collapse);
    void visibility(bool show);
    void tray();
    void command(UINT cmd);
    void load();
    void redraw() {
        for (auto &v : views) {
            v->updateStyle();
            v->draw();
        }
    }
};
App *globalApp = nullptr;
box::Box &View::data() {
    for (auto &b : app->state.boxes)
        if (b.id == id)
            return b;
    throw std::runtime_error("box missing");
}
bool motionAllowed(App *a) {
    BOOL enabled = TRUE;
    SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &enabled, 0);
    return enabled && a->state.animations && !design::highContrast();
}
bool startupEnabled() {
    HKEY k;
    DWORD type = 0, n = 0;
    bool yes = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                      KEY_QUERY_VALUE, &k) == ERROR_SUCCESS) {
        yes = RegQueryValueExW(k, L"DesktopBoxes", nullptr, &type, nullptr, &n) == ERROR_SUCCESS &&
              type == REG_SZ;
        RegCloseKey(k);
    }
    return yes;
}
void toggleStartup(HWND owner) {
    HKEY k;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                        nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) {
        message(owner, L"无法修改当前用户启动项。");
        return;
    }
    LONG r;
    if (startupEnabled())
        r = RegDeleteValueW(k, L"DesktopBoxes");
    else {
        wchar_t path[32768];
        DWORD n = GetModuleFileNameW(nullptr, path, 32768);
        std::wstring value = L"\"" + std::wstring(path, n) + L"\"";
        r = RegSetValueExW(k, L"DesktopBoxes", 0, REG_SZ, (BYTE *)value.c_str(),
                           (DWORD)((value.size() + 1) * 2));
    }
    RegCloseKey(k);
    if (r != ERROR_SUCCESS)
        message(owner, L"启动项修改失败。");
}
struct Form {
    std::wstring title;
    std::vector<std::wstring> labels, values;
    std::vector<HWND> edits;
    bool accepted = false;
    std::wstring action = L"确定";
};
INT_PTR CALLBACK formProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto f = (Form *)GetWindowLongPtrW(h, DWLP_USER);
    if (m == WM_INITDIALOG) {
        f = (Form *)l;
        SetWindowLongPtrW(h, DWLP_USER, l);
        SetWindowTextW(h, f->title.c_str());
        HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
        int y = 14;
        for (size_t i = 0; i < f->labels.size(); ++i) {
            HWND label = CreateWindowW(L"STATIC", f->labels[i].c_str(), WS_CHILD | WS_VISIBLE, 12,
                                       y + 3, 215, 23, h, nullptr, nullptr, nullptr);
            SendMessageW(label, WM_SETFONT, (WPARAM)font, TRUE);
            HWND e = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", f->values[i].c_str(),
                                     WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 230, y,
                                     215, 25, h, (HMENU)(INT_PTR)(1000 + i), nullptr, nullptr);
            SendMessageW(e, WM_SETFONT, (WPARAM)font, TRUE);
            SendMessageW(e, EM_SETLIMITTEXT, 160, 0);
            f->edits.push_back(e);
            y += 31;
        }
        CreateWindowW(L"BUTTON", f->action.c_str(),
                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, 258, y + 8, 88, 28, h,
                      (HMENU)IDOK, nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 355, y + 8, 88, 28, h,
                      (HMENU)IDCANCEL, nullptr, nullptr);
        RECT r{0, 0, 460, y + 50};
        AdjustWindowRectEx(&r, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE, WS_EX_DLGMODALFRAME);
        POINT p;
        GetCursorPos(&p);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &mi);
        int ww = r.right - r.left, hh = r.bottom - r.top;
        SetWindowPos(h, HWND_TOP, mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - ww) / 2,
                     mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - hh) / 2, ww, hh, 0);
        return TRUE;
    }
    if (m == WM_COMMAND && LOWORD(w) == IDOK) {
        for (size_t i = 0; i < f->edits.size(); ++i) {
            wchar_t b[161];
            GetWindowTextW(f->edits[i], b, 161);
            f->values[i] = b;
        }
        f->accepted = true;
        EndDialog(h, IDOK);
        return TRUE;
    }
    if (m == WM_COMMAND && LOWORD(w) == IDCANCEL) {
        EndDialog(h, IDCANCEL);
        return TRUE;
    }
    if (m == WM_CLOSE) {
        EndDialog(h, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}
bool form(HWND owner, Form &f) {
    struct {
        DLGTEMPLATE d;
        WORD menu, cls, title;
    } t{};
    t.d.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    t.d.dwExtendedStyle = WS_EX_DLGMODALFRAME;
    t.d.cx = 320;
    t.d.cy = 220;
    globalApp->modal = true;
    std::vector<HWND> disabled;
    for (auto &v : globalApp->views)
        if (IsWindowEnabled(v->hwnd)) {
            disabled.push_back(v->hwnd);
            EnableWindow(v->hwnd, FALSE);
        }
    DialogBoxIndirectParamW(globalApp->instance, &t.d, owner, formProc, (LPARAM)&f);
    for (auto h : disabled)
        if (IsWindow(h))
            EnableWindow(h, TRUE);
    globalApp->modal = false;
    if (globalApp->pendingSave) {
        globalApp->pendingSave = false;
        globalApp->dirty();
    }
    return f.accepted;
}
void settings(View *v);
bool editSettings(App *app, View *view, box::Box &initial, bool creating = false);
void globalSettings(App *app);
void showImportDetails(View *view);
#include "settings.inl"
void View::requestProjection() {
    filterDirty = false;
    cachedQuery = query;
    loading = true;
    ++revision;
    selected = -1;
    hovered = -1;
    filtered.clear();
    labels.clear();
    types.clear();
    ProjectionJob job;
    job.id = id;
    job.revision = revision;
    job.snapshot = data();
    job.query = query;
    if (data().kind == box::Kind::Inbox && !data().inboxAll)
        for (auto &b : app->state.boxes)
            if (b.kind == box::Kind::Normal)
                for (auto &item : b.items)
                    job.excluded.insert(box::pathKey(item.path));
    if (app->projections) {
        if (!app->projections->submit(std::to_string(id), std::move(job)))
            loading = false;
    } else {
        auto r = project(job);
        filtered = std::move(r.indices);
        labels = std::move(r.labels);
        types = std::move(r.types);
        loading = false;
    }
}
const std::vector<int> &View::visible() {
    if (filterDirty || cachedQuery != query)
        requestProjection();
    return filtered;
}
int View::contentY() const {
    return design::contentTop(const_cast<View *>(this)->data());
}
void View::flash(std::wstring text) {
    notice = std::move(text);
    noticeUntil = GetTickCount64() + 4500;
    SetTimer(hwnd, 14, 4500, nullptr);
    draw();
}
int View::hit(POINT p) {
    if (data().collapsed || dip(p.y) < contentY())
        return -1;
    int x = dip(p.x), y = dip(p.y) - contentY() - 12;
    if (x < Gap || y < 0)
        return -1;
    RECT r;
    GetClientRect(hwnd, &r);
    int cell = design::cellWidth(data());
    int cols = std::max(1, (dip(r.right) - 2 * Gap) / cell);
    int col = (x - Gap) / cell, row = y / design::cellHeight(data()) + data().scroll;
    if (col >= cols)
        return -1;
    int pos = row * cols + col;
    const auto &a = visible();
    return pos >= 0 && pos < (int)a.size() ? a[pos] : -1;
}
void View::fit() {
    auto &b = data();
    RECT rect;
    GetWindowRect(hwnd, &rect);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST), &mi);
    auto w = mi.rcWork;
    int width = std::min(px(b.w), int(w.right - w.left)),
        height = b.collapsed ? px(Title) : std::min(px(b.h), int(w.bottom - w.top));
    b.x = std::clamp(b.x, (int)w.left, (int)w.right - width);
    b.y = std::clamp(b.y, (int)w.top, (int)w.bottom - height);
    actualH = height;
    SetWindowPos(hwnd, nullptr, b.x, b.y, width, height, SWP_NOACTIVATE | SWP_NOZORDER);
}
void View::updateStyle() {
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    if (data().through)
        ex |= WS_EX_TRANSPARENT;
    else
        ex &= ~WS_EX_TRANSPARENT;
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    if (data().through || !data().glow || !app->state.effects) {
        inside = false;
        fade = 0;
        if (!animating)
            stop();
    }
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOZORDER | SWP_FRAMECHANGED);
}
void View::changed(bool monitor) {
    filterDirty = true;
    updateStyle();
    draw();
    app->dirty(monitor);
}
void View::stop() {
    KillTimer(hwnd, FrameTimer);
    tracking = false;
    lastFrame = 0;
}
void View::setTimer() {
    if (lastFrame)
        return;
    lastFrame = GetTickCount64();
    SetTimer(hwnd, FrameTimer, 34, nullptr);
}
void View::toggle() {
    auto &b = data();
    if (animating) {
        animating = false;
    }
    b.collapsed = !b.collapsed;
    RECT r;
    GetWindowRect(hwnd, &r);
    fromH = r.bottom - r.top;
    fromY = r.top;
    targetH = b.collapsed ? px(Title) : px(b.h);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi);
    targetH = std::min(targetH, (int)(mi.rcWork.bottom - mi.rcWork.top));
    b.y = std::clamp(b.y, (int)mi.rcWork.top, (int)mi.rcWork.bottom - targetH);
    targetY = b.y;
    if (motionAllowed(app) && !app->hidden && !app->suspended) {
        animStart = GetTickCount64();
        animating = true;
        setTimer();
    } else {
        actualH = targetH;
        SetWindowPos(hwnd, nullptr, b.x, b.y, r.right - r.left, targetH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        draw();
    }
    app->dirty();
}
struct CoverCheck {
    HWND self;
    RECT r;
    bool covered = false;
    HRGN remaining = nullptr;
};
BOOL CALLBACK coverEnum(HWND h, LPARAM p) {
    auto c = (CoverCheck *)p;
    if (h == c->self)
        return FALSE;
    if (!IsWindowVisible(h) || IsIconic(h))
        return TRUE;
    if (GetWindowLongPtrW(h, GWL_EXSTYLE) & (WS_EX_LAYERED | WS_EX_TRANSPARENT))
        return TRUE;
    DWORD cloaked = 0;
    DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked)
        return TRUE;
    RECT r;
    if (GetWindowRect(h, &r)) {
        HRGN region = CreateRectRgn(0, 0, 0, 0);
        if (!region)
            return TRUE;
        int kind = GetWindowRgn(h, region);
        if (kind == ERROR) {
            RECT frame = r;
            if (SUCCEEDED(
                    DwmGetWindowAttribute(h, DWMWA_EXTENDED_FRAME_BOUNDS, &frame, sizeof(frame))))
                r = frame;
            SetRectRgn(region, r.left, r.top, r.right, r.bottom);
        } else
            OffsetRgn(region, r.left, r.top);
        int left = c->remaining ? CombineRgn(c->remaining, c->remaining, region, RGN_DIFF) : ERROR;
        DeleteObject(region);
        if (left == NULLREGION) {
            c->covered = true;
            return FALSE;
        }
    }
    return TRUE;
}
void View::timer() {
    auto now = GetTickCount64();
    float dt = std::min(0.1f, float(now - lastFrame) / 1000);
    lastFrame = now;
    if (app->suspended || app->hidden || (data().through && !animating) || !IsWindowVisible(hwnd)) {
        inside = false;
        fade = 0;
        animating = false;
        fit();
        stop();
        return;
    }
    if (animating) {
        float t = std::min(1.f, float(now - animStart) / 200);
        float ease = 1 - (1 - t) * (1 - t);
        actualH = int(fromH + (targetH - fromH) * ease);
        RECT r;
        GetWindowRect(hwnd, &r);
        int y = int(fromY + (targetY - fromY) * ease);
        SetWindowPos(hwnd, nullptr, data().x, y, r.right - r.left, actualH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        if (t >= 1)
            animating = false;
    }
    if (now - occlusionAt > 250) {
        CoverCheck c{hwnd, {}};
        GetWindowRect(hwnd, &c.r);
        c.remaining = CreateRoundRectRgn(c.r.left, c.r.top, c.r.right, c.r.bottom, px(28), px(28));
        if (c.remaining) {
            EnumWindows(coverEnum, (LPARAM)&c);
            DeleteObject(c.remaining);
        }
        occluded = c.covered;
        occlusionAt = now;
    }
    if (occluded) {
        inside = false;
        fade = 0;
        draw();
        if (!animating)
            stop();
        return;
    }
    if (inside)
        fade = std::min(1.f, fade + dt / 0.18f);
    else
        fade = std::max(0.f, fade - dt / 0.30f);
    bool settledX = design::settle(softX, float(dip(mouse.x)), dt),
         settledY = design::settle(softY, float(dip(mouse.y)), dt);
    smooth.x = px((int)softX);
    smooth.y = px((int)softY);
    draw();
    bool staticDone = inside && fade >= 1 && data().effect != 2 && settledX && settledY;
    if (!animating && ((!inside && fade <= 0) || !motionAllowed(app) || staticDone))
        stop();
}
void View::draw() {
    if (app->suspended || app->hidden)
        return;
    try {
        render();
    } catch (...) {
        effectFailed = true;
        fade = 0;
        inside = false;
        stop();
        // Retry once without the effect; never spin a failing WM_PAINT loop.
        try {
            render();
        } catch (...) {
        }
    }
}
#include "render.inl"
void View::open(int index) {
    auto &b = data();
    if (index < 0 || index >= (int)b.items.size())
        return;
    auto path = wide(b.items[index].path);
    SHELLEXECUTEINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_FLAG_NO_UI | SEE_MASK_ASYNCOK;
    info.hwnd = hwnd;
    info.lpVerb = L"open";
    info.lpFile = path.c_str();
    info.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&info))
        message(hwnd,
                L"系统未能打开此入口。原文件可能已移走、离线，或没有关联程序。可右键重新定位；软件"
                L"不会搜索全盘。",
                MB_OK | MB_ICONWARNING);
}
void View::acceptPaths(std::vector<std::string> paths) {
    if (data().kind == box::Kind::Inbox) {
        flash(L"收件箱会自动显示真实桌面的项目");
        return;
    }
    if (importQueue.empty()) {
        importKeys = box::itemKeys(data());
        importRevision = revision;
        imported = 0;
        importFailed = 0;
        importDetails.clear();
    }
    for (auto &p : paths)
        importQueue.push_back(std::move(p));
    pumpImports();
    if (!importQueue.empty()) {
        notice = L"正在加入项目…";
        noticeUntil = GetTickCount64() + 600000;
        SetTimer(hwnd, 16, 10, nullptr);
        draw();
    }
}
void View::pumpImports() {
    if (importRevision != revision) {
        importKeys = box::itemKeys(data());
        importRevision = revision;
    }
    ULONGLONG start = GetTickCount64();
    unsigned count = 0;
    while (!importQueue.empty() && count++ < 128 && GetTickCount64() - start < 6) {
        auto p = std::move(importQueue.front());
        importQueue.pop_front();
        if (!box::validPath(p)) {
            ++importFailed;
            importDetails += L"路径格式无效\r\n";
            continue;
        }
        if (!importKeys.insert(box::pathKey(p)).second) {
            ++importFailed;
            importDetails += L"已在此收纳筐中：" + wide(p) + L"\r\n";
            continue;
        }
        data().items.push_back({std::move(p)});
        ++imported;
    }
    if (importQueue.empty()) {
        KillTimer(hwnd, 16);
        importKeys.clear();
        if (imported)
            changed(true);
        if (importFailed)
            flash(L"已添加 " + std::to_wstring(imported) + L" 项，" +
                  std::to_wstring(importFailed) + L" 项未添加 · 点击查看");
        else if (imported)
            flash(L"已添加 " + std::to_wstring(imported) + L" 项");
    }
}
void View::drop(HDROP d) {
    std::vector<std::string> paths;
    UINT count = DragQueryFileW(d, 0xffffffff, nullptr, 0);
    size_t totalBytes = 0;
    for (UINT k = 0; k < count; ++k) {
        UINT n = DragQueryFileW(d, k, nullptr, 0);
        if (!n || n > 32760)
            continue;
        totalBytes += (n + 1) * sizeof(wchar_t);
        if (totalBytes > box::MaxConfig)
            break;
        std::wstring p(n + 1, 0);
        if (DragQueryFileW(d, k, p.data(), n + 1) != n)
            continue;
        p.resize(n);
        try {
            paths.push_back(canonical(p));
        } catch (...) {
            paths.push_back({});
        }
    }
    DragFinish(d);
    acceptPaths(std::move(paths));
}
void View::menu(POINT p, int item) {
    if (app->modal || modalDepth)
        return;
    HMENU m = CreatePopupMenu();
    const auto snapshot = data();
    const auto &b = snapshot;
    if (item >= 0) {
        AppendMenuW(m, MF_STRING, OpenItem, L"打开原文件");
        if (b.kind == box::Kind::Normal) {
            AppendMenuW(m, MF_STRING, Relocate, L"重新定位引用…");
            AppendMenuW(m, MF_STRING, RemoveItem, L"移除引用（保留原文件）");
        }
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    }
    if (b.kind == box::Kind::Inbox) {
        AppendMenuW(m, MF_STRING, InboxUnclassified, L"只看未分类");
        AppendMenuW(m, MF_STRING, InboxAll, L"查看全部桌面文件");
        AppendMenuW(m, MF_STRING, ViewNew, L"查看新收件");
    }
    AppendMenuW(m, MF_STRING, NewBox, L"新建收纳筐…");
    AppendMenuW(m, MF_STRING, Settings, L"本筐外观与光晕设置…");
    AppendMenuW(m, MF_STRING, Collapse, b.collapsed ? L"展开" : L"折叠");
    AppendMenuW(m, MF_STRING | (b.locked ? MF_CHECKED : 0), Lock, L"锁定位置与尺寸");
    AppendMenuW(m, MF_STRING | (b.through ? MF_CHECKED : 0), Through,
                L"整框点击穿透（从托盘恢复）");
    AppendMenuW(m, MF_STRING, Search, L"搜索已添加的名称…");
    AppendMenuW(m, MF_STRING, SortManual, L"手动排列");
    AppendMenuW(m, MF_STRING, SortName, L"按名称排序");
    AppendMenuW(m, MF_STRING, SortType, L"按类型排序");
    AppendMenuW(m, MF_STRING, Refresh, L"刷新引用状态");
    AppendMenuW(m, MF_STRING, DeleteBox, L"删除此筐（保留原文件）…");
    SetForegroundWindow(app->controller);
    auto cmd =
        TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, app->controller, nullptr);
    DestroyMenu(m);
    PostMessageW(app->controller, WM_NULL, 0, 0);
    auto &current = data();
    switch (cmd) {
    case InboxAll:
        current.inboxAll = true;
        changed();
        break;
    case InboxUnclassified:
        current.inboxAll = false;
        changed();
        break;
    case ViewNew:
        current.inboxAll = true;
        for (auto &i : current.items)
            if (i.fresh && jumpPath.empty())
                jumpPath = i.path;
        for (auto &i : current.items)
            i.fresh = false;
        changed();
        app->updateInbox();
        break;
    case NewBox:
        app->create();
        break;
    case Settings:
        settings(this);
        break;
    case Collapse:
        toggle();
        break;
    case Lock:
        current.locked = !current.locked;
        changed();
        break;
    case Through:
        current.through = !current.through;
        changed();
        break;
    case OpenItem:
        open(item);
        break;
    case RemoveItem:
        if (item >= 0 && item < (int)current.items.size()) {
            current.items.erase(current.items.begin() + item);
            changed(true);
        }
        break;
    case Relocate:
        if (item >= 0 && item < (int)current.items.size()) {
            Form f{L"重新定位（只修改引用）",
                   {L"原文件或文件夹的完整路径"},
                   {wide(current.items[item].path)}};
            if (form(hwnd, f)) {
                try {
                    auto path = canonical(f.values[0]);
                    bool duplicate = false;
                    for (size_t i = 0; i < current.items.size(); ++i)
                        if (i != (size_t)item && box::samePath(current.items[i].path, path))
                            duplicate = true;
                    if (duplicate)
                        throw std::runtime_error("duplicate");
                    current.items[item].path = path;
                    changed(true);
                } catch (...) {
                    message(hwnd, L"路径无效或已在此收纳筐中。");
                }
            }
        }
        break;
    case Search: {
        Form f{L"搜索已添加的项目（留空显示全部）", {L"项目名称包含"}, {wide(query)}};
        if (form(hwnd, f)) {
            query = utf8(f.values[0]);
            current.scroll = 0;
            draw();
        }
    } break;
    case SortName:
    case SortType:
    case SortManual:
        current.sort = cmd == SortName ? 1 : cmd == SortType ? 2 : 0;
        changed();
        break;
    case Refresh:
        if (data().kind == box::Kind::Inbox)
            app->updateInbox();
        if (app->monitor)
            app->monitor->update(app->state);
        break;
    case DeleteBox:
        if (ask(hwnd, L"只删除此收纳筐和分类记录，不删除原文件。是否继续？", L"删除收纳筐",
                MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
            PostMessageW(app->controller, WM_APP + 5, id, 0);
        }
        break;
    }
}
#include "accessibility.inl"
#include "drop_target.inl"
LRESULT CALLBACK boxProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    View *v = (View *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        v = (View *)((CREATESTRUCTW *)l)->lpCreateParams;
        v->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)v);
    }
    if (!v)
        return DefWindowProcW(h, m, w, l);
    try {
        switch (m) {
        case WM_GETOBJECT:
            if ((LONG)l == OBJID_CLIENT) {
                auto accessible = new BoxAccessible(v->life);
                LRESULT result = LresultFromObject(IID_IAccessible, w, accessible);
                accessible->Release();
                return result;
            }
            break;
        case WM_CREATE:
            DragAcceptFiles(h, TRUE);
            return 0;
        case WM_NCCALCSIZE:
            return 0; // Keep native resize support without a visible non-client border.
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ERASEBKGND:
            return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(h, &ps);
            EndPaint(h, &ps);
            v->draw();
            return 0;
        }
        case WM_NCHITTEST: {
            if (v->data().through)
                return HTTRANSPARENT;
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            ScreenToClient(h, &p);
            RECT r;
            GetClientRect(h, &r);
            if (!design::roundedContains(v->dip(p.x), v->dip(p.y), v->dip(r.right),
                                         v->dip(r.bottom)))
                return HTTRANSPARENT;
            int edge = v->px(6);
            if (!v->data().locked && !v->data().collapsed) {
                bool left = p.x < edge, right = p.x >= r.right - edge, top = p.y < edge,
                     bottom = p.y >= r.bottom - edge;
                if (top && left)
                    return HTTOPLEFT;
                if (top && right)
                    return HTTOPRIGHT;
                if (bottom && left)
                    return HTBOTTOMLEFT;
                if (bottom && right)
                    return HTBOTTOMRIGHT;
                if (left)
                    return HTLEFT;
                if (right)
                    return HTRIGHT;
                if (top)
                    return HTTOP;
                if (bottom)
                    return HTBOTTOM;
            }
            return HTCLIENT;
        }
        case WM_GETMINMAXINFO: {
            auto p = (MINMAXINFO *)l;
            p->ptMinTrackSize = {v->px(240),
                                 v->px((v->data().collapsed || v->animating) ? Title : 120)};
            p->ptMaxTrackSize = {v->px(8192), v->px(8192)};
            return 0;
        }
        case WM_ENTERSIZEMOVE:
            if (v->animating) {
                v->animating = false;
                v->fit();
            }
            v->resizing = true;
            v->animating = false;
            return 0;
        case WM_SIZING:
        case WM_MOVING:
            return DefWindowProcW(h, m, w, l);
        case WM_EXITSIZEMOVE: {
            v->resizing = false;
            auto &b = v->data();
            RECT r;
            GetWindowRect(h, &r);
            b.x = r.left;
            b.y = r.top;
            if (!b.collapsed) {
                b.w = std::clamp(v->dip(r.right - r.left), 240, 8192);
                b.h = std::clamp(v->dip(r.bottom - r.top), 120, 8192);
            }
            for (auto &other : v->app->views)
                if (other.get() != v) {
                    RECT o;
                    GetWindowRect(other->hwnd, &o);
                    if (abs(b.x - o.right) < v->px(12))
                        b.x = o.right;
                    if (abs(b.y - o.bottom) < v->px(12))
                        b.y = o.bottom;
                    if (abs(b.x - o.left) < v->px(12))
                        b.x = o.left;
                    if (abs(b.y - o.top) < v->px(12))
                        b.y = o.top;
                }
            v->fit();
            v->changed();
            return 0;
        }
        case WM_WINDOWPOSCHANGED:
            if (v->resizing) {
                auto &b = v->data();
                RECT r;
                GetWindowRect(h, &r);
                if (!b.collapsed) {
                    b.w = std::clamp(v->dip(r.right - r.left), 240, 8192);
                    b.h = std::clamp(v->dip(r.bottom - r.top), 120, 8192);
                }
                v->draw();
            }
            return DefWindowProcW(h, m, w, l);
        case WM_DPICHANGED: {
            v->dpi = HIWORD(w);
            auto p = (RECT *)l;
            v->data().x = p->left;
            v->data().y = p->top;
            v->fit();
            v->draw();
            v->app->dirty();
            return 0;
        }
        case WM_DISPLAYCHANGE:
            v->fit();
            v->draw();
            v->app->dirty();
            return 0;
        case WM_TIMER:
            if (w == 14) {
                KillTimer(h, 14);
                v->notice.clear();
                v->draw();
                return 0;
            }
            if (w == 16) {
                v->pumpImports();
                return 0;
            }
            if (w == 15) {
                if (v->dragScrollDirection) {
                    int old = v->data().scroll;
                    v->data().scroll = std::max(0, old + v->dragScrollDirection);
                    v->draw();
                    if (v->data().scroll == old) {
                        v->dragScrollDirection = 0;
                        KillTimer(h, 15);
                    }
                }
                return 0;
            }
            if (w == FrameTimer)
                v->timer();
            return 0;
        case WM_MOUSEMOVE: {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            if (v->scrollDragging) {
                RECT client;
                GetClientRect(h, &client);
                RECT thumb = v->scrollThumb();
                int top = v->px(v->contentY() + 8),
                    travel = std::max(
                        1, int(client.bottom - v->px(10) - top - (thumb.bottom - thumb.top)));
                int cols = std::max(1, (v->dip(client.right) - 2 * Gap) /
                                           design::cellWidth(v->data())),
                    rows = std::max(1, (v->dip(client.bottom) - v->contentY() - 12) /
                                           design::cellHeight(v->data()));
                int maximum = std::max(0, ((int)v->filtered.size() + cols - 1) / cols - rows);
                v->data().scroll =
                    std::clamp(MulDiv(p.y - v->thumbOffset - top, maximum, travel), 0, maximum);
                v->draw();
                v->app->dirty();
                return 0;
            }
            v->tooltipText(p);
            int hover = v->hit(p);
            if (hover != v->hovered) {
                v->hovered = hover;
                v->draw();
            }
            if (GetCapture() == h && v->pressed >= -2 &&
                (abs(p.x - v->down.x) > GetSystemMetrics(SM_CXDRAG) ||
                 abs(p.y - v->down.y) > GetSystemMetrics(SM_CYDRAG))) {
                if (v->pressed == -2 && !v->data().locked) {
                    v->pressed = -1;
                    ReleaseCapture();
                    POINT screen = p;
                    ClientToScreen(h, &screen);
                    SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(screen.x, screen.y));
                    return 0;
                }
                if (v->pressed >= 0) {
                    v->dragging = true;
                    POINT screen = p;
                    ClientToScreen(h, &screen);
                    HWND target = WindowFromPoint(screen);
                    for (auto &box : v->app->views) {
                        box->dragScrollDirection = 0;
                        KillTimer(box->hwnd, 15);
                        box->dropFeedback = 0;
                        if (box->hwnd == target && !box->data().collapsed && !box->data().through) {
                            POINT at = screen;
                            ScreenToClient(target, &at);
                            RECT r;
                            GetClientRect(target, &r);
                            box->dropFeedback = 1;
                            box->dragScrollDirection = at.y < box->px(box->contentY() + 26) ? -1
                                                       : at.y > r.bottom - box->px(26)      ? 1
                                                                                            : 0;
                            if (box->dragScrollDirection)
                                SetTimer(target, 15, 50, nullptr);
                        }
                        if (IsWindowVisible(box->hwnd))
                            box->draw();
                    }
                }
            }
            if (!v->tracking) {
                TRACKMOUSEEVENT t{sizeof(t), TME_LEAVE, h, 0};
                TrackMouseEvent(&t);
                v->tracking = true;
            }
            v->mouse = p;
            if (!v->inside) {
                v->inside = true;
                v->smooth = p;
                v->softX = float(v->dip(p.x));
                v->softY = float(v->dip(p.y));
            }
            if (v->data().glow && v->app->state.effects && !v->effectFailed && !v->data().through &&
                !v->app->suspended) {
                if (motionAllowed(v->app))
                    v->setTimer();
                else if (v->fade == 0) {
                    v->fade = 1;
                    v->smooth = p;
                    v->softX = float(v->dip(p.x));
                    v->softY = float(v->dip(p.y));
                    v->draw();
                }
            }
            return 0;
        }
        case WM_MOUSELEAVE:
            v->hovered = -1;
            v->tracking = false;
            v->inside = false;
            if (motionAllowed(v->app) && v->fade > 0)
                v->setTimer();
            else {
                v->fade = 0;
                v->draw();
            }
            return 0;
        case WM_LBUTTONDOWN: {
            v->down = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            SetForegroundWindow(h);
            SetFocus(h);
            v->focused = true;
            RECT client;
            GetClientRect(h, &client);
            RECT thumb = v->scrollThumb();
            if (thumb.right > thumb.left && v->down.x >= client.right - v->px(18) &&
                v->down.y >= v->px(v->contentY())) {
                v->scrollDragging = true;
                v->thumbOffset = PtInRect(&thumb, v->down) ? v->down.y - thumb.top
                                                           : (thumb.bottom - thumb.top) / 2;
                v->pressed = -1;
                SetCapture(h);
                SendMessageW(h, WM_MOUSEMOVE, w, l);
                return 0;
            }
            v->pressed = v->dip(v->down.y) < Title ? -2 : v->hit(v->down);
            v->dragging = false;
            if (v->pressed >= 0) {
                v->selected = v->pressed;
                v->focusPart = 3;
            } else if (v->dip(v->down.y) < Title)
                v->focusPart = v->dip(v->down.x) > v->dip(client.right) - 44 ? 2 : 1;
            SetCapture(h);
            v->draw();
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            if (v->scrollDragging) {
                v->scrollDragging = false;
                ReleaseCapture();
                return 0;
            }
            for (auto &box : v->app->views) {
                box->dragScrollDirection = 0;
                box->dropFeedback = 0;
                KillTimer(box->hwnd, 15);
                if (IsWindowVisible(box->hwnd))
                    box->draw();
            }
            int selected = v->pressed;
            bool drag = v->dragging;
            v->pressed = -1;
            v->dragging = false;
            ReleaseCapture();
            if (drag && selected >= 0) {
                POINT screen = p;
                ClientToScreen(h, &screen);
                HWND dest = WindowFromPoint(screen);
                for (auto &target : v->app->views)
                    if (target->hwnd == dest && !target->data().through &&
                        !target->data().collapsed) {
                        if (target.get() == v) {
                            int to = v->hit(p);
                            auto &items = v->data().items;
                            if (to >= 0 && to != selected && selected < (int)items.size()) {
                                auto moved = items[selected];
                                items.erase(items.begin() + selected);
                                items.insert(items.begin() + to, moved);
                                v->data().sort = 0;
                                v->changed();
                                if (v->data().kind == box::Kind::Inbox)
                                    v->app->updateInbox();
                            }
                        } else if (box::transfer(v->data(), target->data(), selected)) {
                            v->changed(true);
                            target->changed();
                            if (v->data().kind == box::Kind::Inbox ||
                                target->data().kind == box::Kind::Inbox)
                                v->app->updateInbox();
                        } else
                            v->flash(L"无法归入目标筐，原分类保留");
                        break;
                    }
                return 0;
            }
            if (v->data().kind == box::Kind::Inbox && v->dip(p.y) >= Title &&
                v->dip(p.y) < v->contentY() && abs(p.x - v->down.x) < GetSystemMetrics(SM_CXDRAG)) {
                int x = v->dip(p.x);
                if (x < 96)
                    v->data().inboxAll = false;
                else if (x < 180)
                    v->data().inboxAll = true;
                else {
                    v->data().inboxAll = true;
                    for (auto &item : v->data().items)
                        if (item.fresh && v->jumpPath.empty())
                            v->jumpPath = item.path;
                    for (auto &item : v->data().items)
                        item.fresh = false;
                    v->app->updateInbox();
                }
                v->changed();
                return 0;
            }
            RECT full;
            GetClientRect(h, &full);
            if (!v->importDetails.empty() && GetTickCount64() < v->noticeUntil &&
                p.y > full.bottom - v->px(34)) {
                showImportDetails(v);
                return 0;
            }
            if (selected == -2 && abs(p.x - v->down.x) < GetSystemMetrics(SM_CXDRAG) &&
                abs(p.y - v->down.y) < GetSystemMetrics(SM_CYDRAG)) {
                RECT r;
                GetClientRect(h, &r);
                if (v->dip(p.x) > v->dip(r.right) - 44) {
                    ClientToScreen(h, &p);
                    v->menu(p);
                } else if (v->dip(p.x) > v->dip(r.right) - 88)
                    v->toggle();
            }
            return 0;
        }
        case WM_LBUTTONDBLCLK: {
            ReleaseCapture();
            v->pressed = -1;
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            RECT r;
            GetClientRect(h, &r);
            if (v->dip(p.y) < Title && v->dip(p.x) < v->dip(r.right) - 88)
                v->toggle();
            else if (v->dip(p.y) >= Title)
                v->open(v->hit(p));
            return 0;
        }
        case WM_CAPTURECHANGED:
            v->scrollDragging = false;
            for (auto &box : v->app->views) {
                box->dragScrollDirection = 0;
                box->dropFeedback = 0;
                KillTimer(box->hwnd, 15);
                if (IsWindowVisible(box->hwnd))
                    box->draw();
            }
            v->pressed = -1;
            v->dragging = false;
            return 0;
        case WM_CONTEXTMENU: {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
            if (p.x == -1)
                GetCursorPos(&p);
            POINT client = p;
            ScreenToClient(h, &client);
            v->menu(p, v->hit(client));
            return 0;
        }
        case WM_MOUSEWHEEL: {
            POINT cursor;
            GetCursorPos(&cursor);
            ScreenToClient(h, &cursor);
            RECT client;
            GetClientRect(h, &client);
            if (!v->data().through &&
                design::roundedContains(v->dip(cursor.x), v->dip(cursor.y), v->dip(client.right),
                                        v->dip(client.bottom))) {
                v->consumeWheel(GET_WHEEL_DELTA_WPARAM(w));
                return 0;
            }
            return DefWindowProcW(h, m, w, l);
        }
        case WM_SETFOCUS:
            v->focused = true;
            if (!v->focusPart)
                v->focusPart = 1;
            v->draw();
            return 0;
        case WM_KILLFOCUS:
            v->focused = false;
            v->draw();
            return 0;
        case WM_GETDLGCODE:
            return DLGC_WANTARROWS | DLGC_WANTTAB | DLGC_WANTCHARS;
        case WM_KEYDOWN: {
            auto &list = v->visible();
            if (w == VK_TAB) {
                int max = v->data().collapsed || list.empty() ? 2 : 3;
                v->focusPart =
                    1 + (v->focusPart - 1 + (GetKeyState(VK_SHIFT) < 0 ? -1 : 1) + max) % max;
                if (v->focusPart == 3 && v->selected < 0)
                    v->selected = list.front();
                v->draw();
                return 0;
            }
            if (w == VK_ESCAPE) {
                v->selected = -1;
                v->draw();
                return 0;
            }
            if (w == 'F' && GetKeyState(VK_CONTROL) < 0) {
                Form f{L"搜索已添加的项目", {L"名称包含"}, {wide(v->query)}};
                if (form(h, f)) {
                    v->query = utf8(f.values[0]);
                    v->filterDirty = true;
                    v->data().scroll = 0;
                    v->draw();
                }
                return 0;
            }
            if (w == VK_RETURN || w == VK_SPACE) {
                if (v->focusPart == 1)
                    v->toggle();
                else if (v->focusPart == 2) {
                    RECT r;
                    GetWindowRect(h, &r);
                    POINT p{r.right - v->px(24), r.top + v->px(Title)};
                    v->menu(p);
                } else if (w == VK_RETURN && v->selected >= 0)
                    v->open(v->selected);
                return 0;
            }
            if (w == VK_DELETE && v->selected >= 0 && v->data().kind == box::Kind::Normal) {
                auto &items = v->data().items;
                if (v->selected < (int)items.size()) {
                    items.erase(items.begin() + v->selected);
                    v->selected = -1;
                    v->changed(true);
                }
                return 0;
            }
            if (v->focusPart == 3 && !list.empty()) {
                RECT r;
                GetClientRect(h, &r);
                int cols = std::max(1, (v->dip(r.right) - 2 * Gap) / design::cellWidth(v->data()));
                auto it = std::find(list.begin(), list.end(), v->selected);
                int pos = it == list.end() ? 0 : int(it - list.begin());
                int step = w == VK_LEFT    ? -1
                           : w == VK_RIGHT ? 1
                           : w == VK_UP    ? -cols
                           : w == VK_DOWN  ? cols
                                           : 0;
                if (w == VK_HOME)
                    pos = 0;
                else if (w == VK_END)
                    pos = (int)list.size() - 1;
                else if (w == VK_PRIOR)
                    pos -= cols * std::max(1, (v->dip(r.bottom) - v->contentY()) /
                                                  design::cellHeight(v->data()));
                else if (w == VK_NEXT)
                    pos += cols * std::max(1, (v->dip(r.bottom) - v->contentY()) /
                                                  design::cellHeight(v->data()));
                else
                    pos += step;
                pos = std::clamp(pos, 0, (int)list.size() - 1);
                v->selected = list[pos];
                v->ensureVisible(v->selected);
                return 0;
            }
            return 0;
        }
        case WM_SETCURSOR: {
            int hit = LOWORD(l);
            LPCWSTR cursor = hit == HTLEFT || hit == HTRIGHT            ? IDC_SIZEWE
                             : hit == HTTOP || hit == HTBOTTOM          ? IDC_SIZENS
                             : hit == HTTOPLEFT || hit == HTBOTTOMRIGHT ? IDC_SIZENWSE
                             : hit == HTTOPRIGHT || hit == HTBOTTOMLEFT ? IDC_SIZENESW
                                                                        : IDC_ARROW;
            SetCursor(LoadCursorW(nullptr, cursor));
            return TRUE;
        }
        case WM_DROPFILES:
            if (v->data().through || v->data().collapsed) {
                DragFinish((HDROP)w);
                return 0;
            }
            v->drop((HDROP)w);
            return 0;
        case WM_SHOWWINDOW:
            if (!w) {
                v->inside = false;
                v->fade = 0;
                v->animating = false;
                v->stop();
            }
            return 0;
        case WM_CLOSE:
            v->app->visibility(false);
            return 0;
        case WM_DESTROY:
            if (v->dropRegistered) {
                RevokeDragDrop(h);
                v->dropRegistered = false;
            }
            *v->life = nullptr;
            KillTimer(h, 14);
            KillTimer(h, 15);
            KillTimer(h, 16);
            DragAcceptFiles(h, FALSE);
            v->stop();
            return 0;
        }
    } catch (...) {
        v->effectFailed = true;
        v->inside = false;
        v->fade = 0;
        v->stop();
    }
    return DefWindowProcW(h, m, w, l);
}
void App::createView(uint32_t id) {
    auto v = std::make_unique<View>();
    v->app = this;
    v->id = id;
    auto &b = v->data();
    POINT p{b.x, b.y};
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &mi);
    HDC dc = GetDC(nullptr);
    v->dpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(nullptr, dc);
    HWND h = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                             L"DesktopBoxes.Box", wide(b.name).c_str(),
                             WS_POPUP | WS_CLIPSIBLINGS | WS_THICKFRAME, b.x, b.y, v->px(b.w),
                             v->px(b.collapsed ? Title : b.h), nullptr, nullptr, instance, v.get());
    if (!h)
        throw std::runtime_error("window");
    using DpiFn = UINT(WINAPI *)(HWND);
    auto dpiFn = (DpiFn)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
    if (dpiFn)
        v->dpi = dpiFn(h);
    {
        auto target = new DropReceiver(v->life);
        v->dropRegistered = SUCCEEDED(RegisterDragDrop(h, target));
        target->Release();
    }
    v->fit();
    v->updateStyle();
    v->draw();
    v->tooltip = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, h, nullptr, instance, nullptr);
    TOOLINFOW tool{};
    tool.cbSize = sizeof(tool);
    tool.uFlags = TTF_SUBCLASS;
    tool.hwnd = h;
    tool.uId = 1;
    GetClientRect(h, &tool.rect);
    tool.lpszText = (LPWSTR)L" ";
    SendMessageW(v->tooltip, TTM_ADDTOOLW, 0, (LPARAM)&tool);
    SendMessageW(v->tooltip, TTM_SETMAXTIPWIDTH, 0, v->px(420));
    ShowWindow(h, SW_SHOWNOACTIVATE);
    views.push_back(std::move(v));
}
void App::create() {
    if (saveBlocked) {
        message(controller, L"配置损坏尚未处理。请先备份并恢复配置，再创建收纳筐。");
        return;
    }
    if (state.nextId == UINT32_MAX) {
        message(controller, L"配置中的编号空间已耗尽，无法新建收纳筐。");
        return;
    }
    if (state.boxes.size() >= box::MaxBoxes) {
        message(controller, L"最多支持 50 个收纳筐。");
        return;
    }
    box::Box b;
    if (!editSettings(this, nullptr, b, true))
        return;
    b.id = state.nextId++;
    POINT p;
    GetCursorPos(&p);
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(MonitorFromPoint(p, MONITOR_DEFAULTTONEAREST), &mi);
    b.x = mi.rcWork.left + 30;
    b.y = mi.rcWork.top + 30;
    for (int i = 0; i < 15; ++i) {
        bool collision = false;
        for (auto &v : views) {
            RECT r;
            GetWindowRect(v->hwnd, &r);
            RECT candidate{b.x, b.y, b.x + b.w, b.y + b.h}, intersection;
            if (IntersectRect(&intersection, &r, &candidate)) {
                collision = true;
                break;
            }
        }
        if (!collision)
            break;
        b.x += 26;
        b.y += 26;
    }
    state.boxes.push_back(b);
    createView(b.id);
    if (hidden)
        ShowWindow(views.back()->hwnd, SW_HIDE);
    dirty(true);
}
void App::all(bool collapse) {
    for (auto &v : views)
        if (v->data().collapsed != collapse)
            v->toggle();
    dirty();
}
void App::visibility(bool show) {
    hidden = !show;
    for (auto &v : views) {
        ShowWindow(v->hwnd, show ? SW_SHOWNOACTIVATE : SW_HIDE);
        if (show) {
            v->fit();
            v->draw();
        }
    }
    if (show && monitor)
        monitor->update(state);
}
void App::tray() {
    NOTIFYICONDATAW n{};
    n.cbSize = sizeof(n);
    n.hWnd = controller;
    n.uID = 1;
    n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    n.uCallbackMessage = TrayMsg;
    n.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
    wcscpy_s(n.szTip, L"桌面归纳盒 · 右键管理，退出不影响原文件");
    Shell_NotifyIconW(NIM_ADD, &n);
    n.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &n);
}
#include "features.inl"
void App::pauseFor(playback::Reason reason, bool enabled) {
    sessionGate.set(reason, enabled);
    bool wasPaused = suspended;
    suspended = sessionGate.paused();
    if (suspended) {
        for (auto &v : views) {
            v->inside = false;
            v->fade = 0;
            v->animating = false;
            v->fit();
            v->stop();
        }
    } else if (wasPaused) {
        redraw();
        if (monitor)
            monitor->update(state);
        updateInbox(); // Supplement notifications lost during suspend/disconnection.
    }
}
void App::command(UINT cmd) {
    if (modal || modalDepth)
        return;
    switch (cmd) {
    case NewInbox:
        createInbox();
        break;
    case GlobalSettings:
        globalSettings(this);
        break;
    case PauseInbox:
        state.inboxPaused = !state.inboxPaused;
        updateInbox();
        redraw();
        dirty();
        break;
    case OpenDesktop:
        openKnownFolder(controller, FOLDERID_Desktop, true);
        break;
    case OpenRecycle:
        openKnownFolder(controller, FOLDERID_RecycleBinFolder);
        break;
    case OpenComputer:
        openKnownFolder(controller, FOLDERID_ComputerFolder);
        break;
    case RestoreDesktopIcons:
        message(controller,
                L"桌面右键 → 查看 → "
                L"显示桌面图标。\n\n本版本未启用自动隐藏；整洁模式将在恢复验证通过后开放。");
        break;
    case NewBox:
        create();
        break;
    case AllCollapse:
        all(true);
        break;
    case AllExpand:
        all(false);
        break;
    case HideAll:
        visibility(false);
        break;
    case ShowAll:
        visibility(true);
        break;
    case Effects:
        state.effects = !state.effects;
        redraw();
        dirty();
        break;
    case Animations:
        state.animations = !state.animations;
        for (auto &v : views) {
            v->inside = false;
            v->fade = 0;
            v->animating = false;
            v->stop();
            v->fit();
            v->draw();
        }
        dirty();
        break;
    case Startup:
        toggleStartup(controller);
        break;
    case RestoreInput:
        for (auto &v : views) {
            v->data().through = false;
            v->changed();
        }
        break;
    case Settings: {
        if (views.empty())
            create();
        else {
            POINT p;
            GetCursorPos(&p);
            HMENU m = CreatePopupMenu();
            for (size_t i = 0; i < views.size(); ++i)
                AppendMenuW(m, MF_STRING, 10000 + i, wide(views[i]->data().name).c_str());
            auto c = TrackPopupMenu(m, TPM_RETURNCMD, p.x, p.y, 0, controller, nullptr);
            DestroyMenu(m);
            if (c >= 10000 && (size_t)c < 10000 + views.size())
                settings(views[c - 10000].get());
        }
    } break;
    case Help:
        message(
            controller,
            L"桌面归纳盒 0.2.0 "
            L"预览版\n\n拖入只保存路径，不复制或移动原文件。双击入口由系统打开原文件。\n右键可设置"
            L"、搜索、排序或移除引用。拖动图标可在本软件内调整排列、框间分类。\n整框穿透后请从托盘"
            L"恢复交互。锁定位置不影响打开。\n\n使用普通非置顶工具窗口；未使用 Explorer "
            L"私有嵌入接口。显示桌面可能隐藏窗口，可从托盘“显示全部”恢复。\n未完成 Windows "
            L"实机与性能验收，请先用临时文件试用。\n配置：%LOCALAPPDATA%\\DesktopBoxes");
        break;
    case Quit:
        if (std::any_of(views.begin(), views.end(),
                        [](auto &v) { return !v->importQueue.empty(); })) {
            if (ask(controller,
                    L"仍有项目等待添加。现在退出将保留已添加的引用，未处理项目需要下次重新拖入。原"
                    L"文件不受影响。\n\n是否现在退出？",
                    L"桌面归纳盒", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES)
                break;
        }
        quitting = true;
        wallpaperPreview.reset();
        save();
        inbox.reset();
        projections.reset();
        iconLoader.reset();
        for (auto &v : views)
            DestroyWindow(v->hwnd);
        if (monitor && !monitor->stop()) {
            monitor.release();
        } else
            monitor.reset();
        {
            NOTIFYICONDATAW n{};
            n.cbSize = sizeof(n);
            n.hWnd = controller;
            n.uID = 1;
            Shell_NotifyIconW(NIM_DELETE, &n);
        }
        DestroyWindow(controller);
        break;
    }
}
void App::load() {
    dir = configDir();
    auto target = dir + L"\\state.db";
    if (GetFileAttributesW(target.c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND)
            saveBlocked = true;
        return;
    }
    std::vector<uint8_t> a;
    try {
        if (!readBytes(target, a))
            throw std::runtime_error("read");
        state = box::decode(a);
        if (!preserveLegacyConfig(dir, a)) {
            saveBlocked = true;
            message(controller,
                    L"旧版配置已读取，但迁移备份未能安全建立。为保留原记录，本次暂停配置写入。请检"
                    L"查配置目录权限及 state-v0.1.db；原文件不受影响。",
                    MB_OK | MB_ICONWARNING);
        }
        for (auto &b : state.boxes) {
            wide(b.name);
            for (auto &i : b.items)
                wide(i.path);
        }
        return;
    } catch (...) {
    }
    box::State backup;
    bool valid = false;
    try {
        if (readBytes(dir + L"\\state.bak", a)) {
            backup = box::decode(a);
            valid = true;
        }
    } catch (...) {
    }
    saveBlocked = true;
    if (valid && ask(controller,
                     L"主配置损坏。是否从上一次备份恢复？原配置将保留为 "
                     L"state.corrupt（已有同名保留时不会覆盖）。",
                     L"配置恢复", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES) {
        if (MoveFileExW(target.c_str(), (dir + L"\\state.corrupt").c_str(),
                        MOVEFILE_WRITE_THROUGH)) {
            state = std::move(backup);
            saveBlocked = false;
            save();
        } else
            message(controller, L"无法保留损坏配置，已停止写入。请退出后手动备份配置目录。");
    } else
        message(controller,
                L"配置无法加载，已禁止自动覆盖。请退出后备份配置目录，并恢复 state.bak "
                L"或将损坏文件移出该目录。原文件不受影响。",
                MB_OK | MB_ICONWARNING);
}
LRESULT CALLBACK controlProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    App *a = (App *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        a = (App *)((CREATESTRUCTW *)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)a);
        a->controller = h;
    }
    if (!a)
        return DefWindowProcW(h, m, w, l);
    try {
        if (m == a->taskbarCreated && a->taskbarCreated) {
            a->tray();
            return 0;
        }
        switch (m) {
        case TrayMsg: {
            if (a->modal || modalDepth)
                return 0;
            UINT event = LOWORD(l);
            if (event == WM_LBUTTONDBLCLK) {
                a->visibility(a->hidden);
                return 0;
            }
            if (event != WM_CONTEXTMENU && event != WM_RBUTTONUP && event != NIN_SELECT)
                return 0;
            POINT p;
            GetCursorPos(&p);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, NewBox, L"新建收纳筐…");
            AppendMenuW(menu, MF_STRING, GlobalSettings, L"软件设置…");
            AppendMenuW(menu, MF_STRING, NewInbox,
                        a->inboxData() ? L"显示桌面收件箱" : L"启用桌面收件箱");
            if (a->inboxData())
                AppendMenuW(menu, MF_STRING | (a->state.inboxPaused ? MF_CHECKED : 0), PauseInbox,
                            L"暂停自动收件");
            AppendMenuW(menu, MF_STRING, OpenDesktop, L"打开真实桌面");
            AppendMenuW(menu, MF_STRING, OpenComputer, L"此电脑");
            AppendMenuW(menu, MF_STRING, OpenRecycle, L"回收站");
            AppendMenuW(menu, MF_STRING, RestoreDesktopIcons, L"恢复原生桌面图标…");
            AppendMenuW(menu, MF_STRING, Settings, L"设置收纳筐…");
            AppendMenuW(menu, MF_STRING, AllCollapse, L"全部折叠");
            AppendMenuW(menu, MF_STRING, AllExpand, L"全部展开");
            AppendMenuW(menu, MF_STRING, a->hidden ? ShowAll : HideAll,
                        a->hidden ? L"显示全部" : L"隐藏全部");
            AppendMenuW(menu, MF_STRING, ShowAll, L"显示全部（显示桌面后恢复）");
            AppendMenuW(menu, MF_STRING, RestoreInput, L"恢复所有筐的鼠标交互");
            AppendMenuW(menu, MF_STRING | (a->state.effects ? MF_CHECKED : 0), Effects,
                        L"启用悬停光感");
            AppendMenuW(menu, MF_STRING | (a->state.animations ? MF_CHECKED : 0), Animations,
                        L"启用动画（关闭后静态光晕）");
            AppendMenuW(menu, MF_STRING | (startupEnabled() ? MF_CHECKED : 0), Startup,
                        L"当前用户开机启动");
            AppendMenuW(menu, MF_STRING, Help, L"使用说明与限制");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, Quit, L"退出");
            SetForegroundWindow(h);
            auto cmd =
                TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, p.x, p.y, 0, h, nullptr);
            DestroyMenu(menu);
            PostMessageW(h, WM_NULL, 0, 0);
            a->command(cmd);
            return 0;
        }
        case WM_COMMAND:
            a->command(LOWORD(w));
            return 0;
        case WM_TIMER:
            if (w == SaveTimer) {
                KillTimer(h, SaveTimer);
                a->save();
            }
            return 0;
        case WM_APP + 5: {
            auto id = (uint32_t)w;
            if (auto v = a->view(id)) {
                v->stop();
                DestroyWindow(v->hwnd);
            }
            a->views.erase(std::remove_if(a->views.begin(), a->views.end(),
                                          [&](auto &v) { return v->id == id; }),
                           a->views.end());
            a->state.boxes.erase(std::remove_if(a->state.boxes.begin(), a->state.boxes.end(),
                                                [&](auto &b) { return b.id == id; }),
                                 a->state.boxes.end());
            a->updateInbox();
            a->dirty(true);
            return 0;
        }
        case WM_APP + 6:
            if (a->projections) {
                for (auto &r : a->projections->take())
                    if (auto v = a->view(r.id)) {
                        if (r.revision != v->revision)
                            continue;
                        v->filtered = std::move(r.indices);
                        v->labels = std::move(r.labels);
                        v->types = std::move(r.types);
                        v->loading = false;
                        if (!v->jumpPath.empty()) {
                            for (size_t k = 0; k < v->data().items.size(); ++k)
                                if (box::samePath(v->data().items[k].path, v->jumpPath)) {
                                    v->selected = (int)k;
                                    v->ensureVisible((int)k);
                                    break;
                                }
                            v->jumpPath.clear();
                        }
                        v->draw();
                    }
            }
            return 0;
        case WM_APP + 7:
            if (a->iconLoader) {
                std::set<View *> repaint;
                for (auto &r : a->iconLoader->take())
                    for (auto &v : a->views)
                        if (v->requestedIcons.erase(r.path)) {
                            if (v->actualIcons.size() >= 96) {
                                auto oldest = std::min_element(
                                    v->iconAges.begin(), v->iconAges.end(),
                                    [](auto &x, auto &y) { return x.second < y.second; });
                                if (oldest != v->iconAges.end()) {
                                    v->actualIcons.erase(oldest->first);
                                    v->iconBitmaps.erase(oldest->first);
                                    v->iconAges.erase(oldest);
                                }
                            }
                            v->actualIcons[r.path] = r.image;
                            v->iconAges[r.path] = GetTickCount64();
                            repaint.insert(v.get());
                        }
                for (auto &v : a->views)
                    if (v->pendingIconRequests) {
                        v->pendingIconRequests = false;
                        repaint.insert(v.get());
                    }
                for (auto v : repaint)
                    if (IsWindowVisible(v->hwnd))
                        v->draw();
            }
            return 0;
        case WM_APP + 8:
            if (a->inbox) {
                auto r = a->inbox->take();
                if (r.revision != a->inboxRevision)
                    return 0;
                a->inboxHealthy = r.healthy;
                a->desktopRoots = std::move(r.roots);
                if (auto b = a->inboxData()) {
                    bool different = b->items.size() != r.items.size();
                    if (!different)
                        for (size_t i = 0; i < r.items.size(); ++i)
                            if (b->items[i].path != r.items[i].path ||
                                b->items[i].fresh != r.items[i].fresh) {
                                different = true;
                                break;
                            }
                    if (different) {
                        b->items = std::move(r.items);
                        if (auto v = a->view(b->id))
                            v->changed();
                    }
                    if (!r.healthy)
                        if (auto v = a->view(b->id))
                            v->flash(r.problem);
                }
            }
            return 0;
        case WM_APP + 4:
            if (a->monitor) {
                auto results = a->monitor->take();
                for (auto &v : a->views) {
                    v->health.clear();
                }
                for (auto &result : results)
                    if (auto v = a->view(result.id))
                        v->health[result.path] = result.status;
                for (auto &v : a->views)
                    if (IsWindowVisible(v->hwnd))
                        v->draw();
            }
            return 0;
        case WM_WTSSESSION_CHANGE:
            if (w == WTS_SESSION_LOCK || w == WTS_SESSION_UNLOCK)
                a->pauseFor(playback::Reason::Locked, w == WTS_SESSION_LOCK);
            else if (w == WTS_CONSOLE_DISCONNECT || w == WTS_REMOTE_DISCONNECT)
                a->pauseFor(playback::Reason::Disconnected, true);
            else if (w == WTS_CONSOLE_CONNECT || w == WTS_REMOTE_CONNECT)
                a->pauseFor(playback::Reason::Disconnected, false);
            return 0;
        case WM_POWERBROADCAST:
            if (w == PBT_APMSUSPEND) {
                a->pauseFor(playback::Reason::Sleeping, true);
            } else if (w == PBT_APMRESUMEAUTOMATIC || w == PBT_APMRESUMESUSPEND) {
                a->pauseFor(playback::Reason::Sleeping, false);
            }
            return TRUE;
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE:
            for (auto &v : a->views) {
                v->inside = false;
                v->fade = 0;
                v->animating = false;
                v->stop();
                v->fit();
                v->draw();
            }
            return 0;
        case WM_QUERYENDSESSION:
            a->save();
            return TRUE;
        case WM_ENDSESSION:
            if (w)
                a->command(Quit);
            return 0;
        case WM_CLOSE:
            a->command(Quit);
            return 0;
        case WM_DESTROY:
            WTSUnRegisterSessionNotification(h);
            PostQuitMessage(0);
            return 0;
        }
    } catch (...) {
        message(h, L"操作未完成。请检查输入与配置权限；原文件不会被移动或删除。",
                MB_OK | MB_ICONWARNING);
    }
    return DefWindowProcW(h, m, w, l);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    using AwarenessFn = BOOL(WINAPI *)(DPI_AWARENESS_CONTEXT);
    auto awarenessFn = (AwarenessFn)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"),
                                                           "SetProcessDpiAwarenessContext");
    if (awarenessFn)
        awarenessFn(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    else
        SetProcessDPIAware();
    OleInitialize(nullptr);
    INITCOMMONCONTROLSEX common{sizeof(common),
                                ICC_WIN95_CLASSES | ICC_TAB_CLASSES | ICC_BAR_CLASSES};
    InitCommonControlsEx(&common);
    App app;
    int exitCode = 0;
    app.instance = instance;
    globalApp = &app;
    app.singleton = CreateMutexW(nullptr, FALSE, L"Local\\DesktopBoxes.UserInstance");
    if (!app.singleton || GetLastError() == ERROR_ALREADY_EXISTS) {
        message(nullptr, L"桌面归纳盒已在此会话运行，请从系统托盘操作。");
        if (app.singleton)
            CloseHandle(app.singleton);
        return 0;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        CloseHandle(app.singleton);
        return 1;
    }
    GdiplusStartupInput input;
    if (GdiplusStartup(&app.gdip, &input, nullptr) != Ok) {
        CoUninitialize();
        CloseHandle(app.singleton);
        return 1;
    }
    try {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = controlProc;
        wc.lpszClassName = L"DesktopBoxes.Controller";
        RegisterClassExW(&wc);
        wc.lpfnWndProc = boxProc;
        wc.style = CS_DBLCLKS;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = L"DesktopBoxes.Box";
        RegisterClassExW(&wc);
        app.taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
        CreateWindowExW(WS_EX_TOOLWINDOW, L"DesktopBoxes.Controller", L"桌面归纳盒", WS_POPUP, 0, 0,
                        0, 0, nullptr, nullptr, instance, &app);
        if (!app.controller)
            throw std::runtime_error("controller");
        WTSRegisterSessionNotification(app.controller, NOTIFY_FOR_THIS_SESSION);
        app.load();
        app.tray();
        app.projections = std::make_unique<AsyncQueue<ProjectionJob, ProjectionResult>>(
            app.controller, WM_APP + 6, project);
        app.iconLoader = std::make_unique<AsyncQueue<IconJob, IconResult>>(app.controller,
                                                                           WM_APP + 7, systemIcon);
        app.inbox = std::make_unique<InboxWatcher>(app.controller);
        for (auto &b : app.state.boxes)
            app.createView(b.id);
        app.monitor = std::make_unique<Monitor>(app.controller);
        app.monitor->update(app.state);
        app.updateInbox();
        if (app.state.boxes.empty() && !app.saveBlocked) {
            message(
                app.controller,
                L"欢迎使用桌面归纳盒。\n\n软件只保存您主动拖入的文件引用，不扫描或整理现有桌面。原"
                L"文件保留在原路径。\n下一步可创建第一个收纳筐；取消后也可以从托盘创建。\n\n本版本"
                L"尚待 Windows "
                L"实机验收，请先使用临时文件。\n普通非置顶窗口的桌面行为限制见托盘“使用说明”。");
            app.create();
        }
        MSG msg;
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!app.quitting)
            app.command(Quit);
    } catch (...) {
        exitCode = 1;
        message(nullptr, L"程序初始化失败。请检查当前用户配置目录；原文件不受影响。",
                MB_OK | MB_ICONERROR);
        if (app.monitor && !app.monitor->stop())
            app.monitor.release();
        app.monitor.reset();
    }
    app.inbox.reset();
    app.projections.reset();
    app.iconLoader.reset();
    app.views.clear();
    GdiplusShutdown(app.gdip);
    OleUninitialize();
    CoUninitialize();
    CloseHandle(app.singleton);
    ExitProcess(exitCode);
    return 0;
}
