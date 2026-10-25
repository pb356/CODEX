#include "platform.hpp"
#include <dwmapi.h>
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
    Help
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
    bool filterDirty = true;
    std::vector<int> filtered;
    std::vector<std::wstring> labels, types;
    HDC surfaceDC = nullptr;
    HBITMAP surfaceBitmap = nullptr;
    void *surfaceBits = nullptr;
    int surfaceW = 0, surfaceH = 0;
    std::unique_ptr<SolidBrush> starBrush;
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
        clearSurface();
    }

    std::map<std::string, int> health;
    std::map<std::wstring, std::unique_ptr<Bitmap>> icons;
    box::Box &data();
    int px(int x) const {
        return MulDiv(x, dpi, 96);
    }
    int dip(int x) const {
        return MulDiv(x, 96, dpi);
    }
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
    std::wstring dir;
    bool hidden = false, suspended = false, saveBlocked = false, saveWarned = false,
         quitting = false, modal = false;
    ULONG_PTR gdip = 0;
    HANDLE singleton = nullptr;
    UINT taskbarCreated = 0;
    void save() {
        if (saveBlocked)
            return;
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
    }
    View *view(uint32_t id) {
        for (auto &v : views)
            if (v->id == id)
                return v.get();
        return nullptr;
    }
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
    return enabled && a->state.animations;
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
    return f.accepted;
}
uint32_t number(const std::wstring &s, int lo, int hi) {
    size_t n = 0;
    auto v = std::stoul(s, &n);
    if (n != s.size() || v < (unsigned)lo || v > (unsigned)hi)
        throw std::runtime_error("range");
    return (uint32_t)v;
}
uint32_t color(const std::wstring &s) {
    size_t n = 0;
    auto x = s;
    if (!x.empty() && x[0] == '#')
        x.erase(0, 1);
    auto v = std::stoul(x, &n, 16);
    if (n != 6 || v > 0xffffff)
        throw std::runtime_error("color");
    return (uint32_t)v;
}
std::wstring hex(uint32_t n) {
    wchar_t b[10];
    swprintf(b, 10, L"#%06X", n);
    return b;
}
void settings(View *v) {
    auto b = v->data();
    Form f{L"收纳筐设置（数字项请按范围填写）",
           {L"名称（1–80 字）", L"背景颜色 #RRGGBB", L"背景不透明度 16–245", L"文字颜色 #RRGGBB",
            L"字号 10–24", L"图标大小 16–64", L"光晕颜色 #RRGGBB", L"光晕亮度 0–60",
            L"光晕范围 40–300", L"星点数量 0–24", L"本筐光晕 0=关 / 1=开",
            L"预设 0=自定义 1=轻柔 2=标准", L"整框穿透 0=关 / 1=开"},
           {wide(b.name), hex(b.bg), std::to_wstring(b.opacity), hex(b.fg), std::to_wstring(b.font),
            std::to_wstring(b.icon), hex(b.glowColor), std::to_wstring(b.brightness),
            std::to_wstring(b.radius), std::to_wstring(b.stars), b.glow ? L"1" : L"0", L"0",
            b.through ? L"1" : L"0"}};
    if (!form(v->hwnd, f))
        return;
    try {
        if (f.values[0].empty() || f.values[0].size() > 80)
            throw std::runtime_error("name");
        b.name = utf8(f.values[0]);
        b.bg = color(f.values[1]);
        b.opacity = number(f.values[2], 16, 245);
        b.fg = color(f.values[3]);
        b.font = number(f.values[4], 10, 24);
        b.icon = number(f.values[5], 16, 64);
        b.glowColor = color(f.values[6]);
        b.brightness = number(f.values[7], 0, 60);
        b.radius = number(f.values[8], 40, 300);
        b.stars = number(f.values[9], 0, 24);
        b.glow = number(f.values[10], 0, 1) != 0;
        auto preset = number(f.values[11], 0, 2);
        b.through = number(f.values[12], 0, 1) != 0;
        if (preset == 1) {
            b.brightness = 24;
            b.radius = 140;
            b.stars = 8;
        }
        if (preset == 2) {
            b.brightness = 36;
            b.radius = 170;
            b.stars = 16;
        }
        v->data() = std::move(b);
        v->changed();
    } catch (...) {
        message(v->hwnd, L"设置未保存：请检查名称、颜色格式和数值范围。", MB_OK | MB_ICONWARNING);
    }
}
const std::vector<int> &View::visible() {
    if (!filterDirty && cachedQuery == query && labels.size() == data().items.size())
        return filtered;
    filtered.clear();
    labels.clear();
    types.clear();
    cachedQuery = query;
    filterDirty = false;
    auto q = wide(query);
    for (auto &c : q)
        c = (wchar_t)towlower(c);
    for (size_t i = 0; i < data().items.size(); ++i) {
        auto &item = data().items[i];
        auto name = wide(box::nameOf(item.path));
        auto lower = name;
        for (auto &c : lower)
            c = (wchar_t)towlower(c);
        if (q.empty() || lower.find(q) != std::wstring::npos)
            filtered.push_back((int)i);
        auto dot = name.find_last_of(L'.');
        auto ext = dot == std::wstring::npos ? L"file" : name.substr(dot);
        if (ext.size() > 20)
            ext = L"file";
        for (auto &c : ext)
            c = (wchar_t)towlower(c);
        auto status = health.find(item.path);
        if (status != health.end() && status->second == 4)
            ext = L"folder";
        types.push_back(ext);
        if (status != health.end()) {
            if (status->second == 2)
                name = L"[失效] " + name;
            if (status->second == 3)
                name = L"[离线] " + name;
        }
        labels.push_back(std::move(name));
    }
    return filtered;
}
int View::hit(POINT p) {
    if (data().collapsed || dip(p.y) < Title)
        return -1;
    int x = dip(p.x), y = dip(p.y) - Title - 8;
    if (x < Gap || y < 0)
        return -1;
    RECT r;
    GetClientRect(hwnd, &r);
    int cols = std::max(1, (dip(r.right) - Gap) / 100);
    int col = (x - Gap) / 100, row = y / (int(data().icon) + 48) + data().scroll;
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
};
BOOL CALLBACK coverEnum(HWND h, LPARAM p) {
    auto c = (CoverCheck *)p;
    if (h == c->self)
        return FALSE;
    if (!IsWindowVisible(h) || IsIconic(h))
        return TRUE;
    if (GetWindowLongPtrW(h, GWL_EXSTYLE) & WS_EX_LAYERED)
        return TRUE;
    DWORD cloaked = 0;
    DwmGetWindowAttribute(h, DWMWA_CLOAKED, &cloaked, sizeof(cloaked));
    if (cloaked)
        return TRUE;
    RECT r;
    if (GetWindowRect(h, &r) && r.left <= c->r.left && r.top <= c->r.top && r.right >= c->r.right &&
        r.bottom >= c->r.bottom) {
        HRGN region = CreateRectRgn(0, 0, 0, 0);
        int kind = GetWindowRgn(h, region);
        DeleteObject(region);
        if (kind == ERROR || kind == SIMPLEREGION) {
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
        EnumWindows(coverEnum, (LPARAM)&c);
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
        fade = std::max(0.f, fade - dt / 0.25f);
    smooth.x += (LONG)((mouse.x - smooth.x) * std::min(1.f, dt * 9));
    smooth.y += (LONG)((mouse.y - smooth.y) * std::min(1.f, dt * 9));
    draw();
    if (!animating && ((!inside && fade <= 0) || !motionAllowed(app)))
        stop();
}
void View::draw() {
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
void View::render() {
    auto &b = data();
    RECT rect;
    GetWindowRect(hwnd, &rect);
    int w = rect.right - rect.left, h = rect.bottom - rect.top;
    if (w < 1 || h < 1)
        return;
    struct ScreenDC {
        HDC dc = GetDC(nullptr);
        ~ScreenDC() {
            if (dc)
                ReleaseDC(nullptr, dc);
        }
    } screenOwner;
    HDC screen = screenOwner.dc;
    if (!screen)
        return;
    if (!surfaceDC || surfaceW != w || surfaceH != h) {
        clearSurface();
        surfaceW = w;
        surfaceH = h;
        BITMAPINFO bi{};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = w;
        bi.bmiHeader.biHeight = -h;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        bi.bmiHeader.biCompression = BI_RGB;
        surfaceDC = CreateCompatibleDC(screen);
        surfaceBitmap = CreateDIBSection(screen, &bi, DIB_RGB_COLORS, &surfaceBits, nullptr, 0);
        if (!surfaceDC || !surfaceBitmap) {
            clearSurface();
            return;
        }
        SelectObject(surfaceDC, surfaceBitmap);
    }
    HDC dc = surfaceDC;
    void *bits = surfaceBits;
    {
        Bitmap surface(w, h, w * 4, PixelFormat32bppPARGB, (BYTE *)bits);
        Graphics g(&surface);
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.Clear(Color((BYTE)b.opacity, (BYTE)(b.bg >> 16), (BYTE)(b.bg >> 8), (BYTE)b.bg));
        g.SetCompositingMode(CompositingModeSourceOver);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.ScaleTransform(dpi / 96.f, dpi / 96.f);
        g.SetClip(RectF(0, 0, float(dip(w)), float(b.collapsed ? Title : dip(h))));
        if (fade > 0 && app->state.effects && b.glow && !effectFailed && !b.through) {
            float x = float(dip(smooth.x)), y = float(dip(smooth.y)),
                  radius = float(b.collapsed ? std::min(75u, b.radius) : b.radius);
            GraphicsPath path;
            path.AddEllipse(x - radius, y - radius, radius * 2, radius * 2);
            PathGradientBrush brush(&path);
            brush.SetCenterColor(Color((BYTE)(b.brightness * fade), (BYTE)(b.glowColor >> 16),
                                       (BYTE)(b.glowColor >> 8), (BYTE)b.glowColor));
            Color edge(0, 0, 0, 0);
            int count = 1;
            brush.SetSurroundColors(&edge, &count);
            g.FillPath(&brush, &path);
            float t = motionAllowed(app) ? GetTickCount64() / 1000.f : 0;
            if (!starBrush)
                starBrush = std::make_unique<SolidBrush>(Color(0, 0, 0, 0));
            for (uint32_t i = 0; i < b.stars; ++i) {
                float sx = float((i * 97 + 31) % std::max(1, dip(w))),
                      sy = float((i * 59 + 19) % std::max(1, b.collapsed ? Title : dip(h)));
                float bright = 0.7f + 0.15f * std::sin(t * 0.8f + i);
                starBrush->SetColor(
                    Color((BYTE)(b.brightness * 1.5f * fade * bright), 190, 198, 245));
                g.FillEllipse(starBrush.get(), sx, sy, 1.5f, 1.5f);
            }
        }
        SolidBrush title(Color(40, 255, 255, 255));
        g.FillRectangle(&title, 0, 0, dip(w), Title);
        SolidBrush text(Color(255, (BYTE)(b.fg >> 16), (BYTE)(b.fg >> 8), (BYTE)b.fg));
        FontFamily family(L"Microsoft YaHei UI");
        const FontFamily *chosenFamily =
            family.GetLastStatus() == Ok ? &family : FontFamily::GenericSansSerif();
        Font font(chosenFamily, (REAL)b.font, FontStyleRegular, UnitPixel);
        Font small(chosenFamily, 11, FontStyleRegular, UnitPixel);
        StringFormat fmt;
        fmt.SetTrimming(StringTrimmingEllipsisCharacter);
        fmt.SetFormatFlags(StringFormatFlagsNoWrap);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        auto heading = wide(b.name) + L"  (" + std::to_wstring(b.items.size()) + L")";
        g.DrawString(heading.c_str(), -1, &font, RectF(12, 11, float(dip(w) - 94), 24), &fmt,
                     &text);
        g.DrawString(b.collapsed ? L"＋" : L"－", -1, &font, PointF(float(dip(w) - 72), 10), &text);
        g.DrawString(L"⋯", -1, &font, PointF(float(dip(w) - 34), 10), &text);
        if (!b.collapsed) {
            g.SetClip(RectF(0, Title, float(dip(w)), float(std::max(0, dip(h) - Title))));
            const auto &a = visible();
            int cols = std::max(1, (dip(w) - Gap) / 100), cell = int(b.icon) + 48;
            int rows = std::max(1, (dip(h) - Title - 8) / cell);
            int maxScroll = std::max(0, ((int)a.size() + cols - 1) / cols - rows);
            b.scroll = std::min(b.scroll, maxScroll);
            int start = b.scroll * cols, stop = std::min((int)a.size(), start + (rows + 1) * cols);
            for (int pos = start; pos < stop; ++pos) {
                int index = a[pos];
                float x = float(Gap + (pos % cols) * 100),
                      y = float(Title + 8 + (pos / cols - b.scroll) * cell);
                const auto &ext = types[index];
                if (!icons.count(ext) && icons.size() < 64) {
                    SHFILEINFOW info{};
                    SHGetFileInfoW(
                        ext.c_str(),
                        ext == L"folder" ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL, &info,
                        sizeof(info), SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES);
                    if (info.hIcon) {
                        auto img = std::make_unique<Bitmap>(info.hIcon);
                        DestroyIcon(info.hIcon);
                        icons[ext] = std::move(img);
                    }
                }
                auto icon = icons.find(ext);
                if (icon != icons.end())
                    g.DrawImage(icon->second.get(),
                                RectF(x + (88 - b.icon) / 2, y, float(b.icon), float(b.icon)));
                else {
                    SolidBrush fallback(Color(210, 180, 195, 220));
                    g.FillRectangle(&fallback, x + 28, y, 32.f, 32.f);
                }
                const auto &name = labels[index];
                StringFormat label;
                label.SetTrimming(StringTrimmingEllipsisCharacter);
                label.SetAlignment(StringAlignmentCenter);
                g.DrawString(name.c_str(), -1, &small, RectF(x, y + b.icon + 4, 88, 36), &label,
                             &text);
            }
            if (a.empty()) {
                auto s = query.empty() ? L"拖入文件、文件夹或快捷方式\n只保存引用，不移动原文件"
                                       : L"没有匹配的已添加项目";
                g.DrawString(s, -1, &small, RectF(18, Title + 28, float(dip(w) - 36), 75), nullptr,
                             &text);
            }
            if (maxScroll) {
                SolidBrush track(Color(120, 180, 195, 220));
                float usable = float(dip(h) - Title - 12),
                      thumb = std::max(16.f, usable * rows / (maxScroll + rows));
                g.FillRectangle(&track, float(dip(w) - 5),
                                Title + 6 + (usable - thumb) * b.scroll / maxScroll, 2.f, thumb);
            }
        }
    }
    POINT dst{rect.left, rect.top}, src{0, 0};
    SIZE size{w, h};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    BOOL ok = UpdateLayeredWindow(hwnd, screen, &dst, &size, dc, &src, 0, &blend, ULW_ALPHA);

    if (!ok) {
        effectFailed = true;
        stop();
    }
}
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
void View::drop(HDROP d) {
    UINT count = DragQueryFileW(d, 0xffffffff, nullptr, 0);
    size_t total = 0;
    for (auto &b : app->state.boxes)
        total += b.items.size();
    std::wstring failures;
    unsigned failed = 0, added = 0;
    UINT limit = std::min(count, 2000u);
    for (UINT i = 0; i < limit; ++i) {
        UINT n = DragQueryFileW(d, i, nullptr, 0);
        try {
            if (!n || n > 32760 || total >= box::MaxTotal)
                throw std::runtime_error("limit");
            std::wstring path(n + 1, 0);
            if (DragQueryFileW(d, i, path.data(), n + 1) != n)
                throw std::runtime_error("drop");
            path.resize(n);
            auto p = canonical(path);
            bool duplicate = false;
            for (auto &item : data().items)
                if (box::samePath(item.path, p)) {
                    duplicate = true;
                    break;
                }
            if (duplicate) {
                ++failed;
                if (failed <= 8)
                    failures += L"\n已在此收纳筐中：" + wide(box::nameOf(p));
                continue;
            }
            if (!box::add(data(), p))
                throw std::runtime_error("limit");
            ++added;
            ++total;
        } catch (...) {
            ++failed;
            if (failed <= 8)
                failures += L"\n第 " + std::to_wstring(i + 1) + L" 项：无效路径或超过数量限制";
        }
    }
    if (count > limit) {
        failed += count - limit;
        failures += L"\n批量添加上限为 2000 项。";
    }
    DragFinish(d);
    if (added)
        changed(true);
    if (failed)
        message(hwnd,
                L"已添加 " + std::to_wstring(added) + L" 项；未添加 " + std::to_wstring(failed) +
                    L" 项。" + failures,
                MB_OK | MB_ICONINFORMATION);
}
void View::menu(POINT p, int item) {
    if (app->modal || modalDepth)
        return;
    HMENU m = CreatePopupMenu();
    const auto snapshot = data();
    const auto &b = snapshot;
    if (item >= 0) {
        AppendMenuW(m, MF_STRING, OpenItem, L"打开原文件");
        AppendMenuW(m, MF_STRING, Relocate, L"重新定位引用…");
        AppendMenuW(m, MF_STRING, RemoveItem, L"移除引用（保留原文件）");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    }
    AppendMenuW(m, MF_STRING, NewBox, L"新建收纳筐…");
    AppendMenuW(m, MF_STRING, Settings, L"本筐外观与光晕设置…");
    AppendMenuW(m, MF_STRING, Collapse, b.collapsed ? L"展开" : L"折叠");
    AppendMenuW(m, MF_STRING | (b.locked ? MF_CHECKED : 0), Lock, L"锁定位置与尺寸");
    AppendMenuW(m, MF_STRING | (b.through ? MF_CHECKED : 0), Through,
                L"整框点击穿透（从托盘恢复）");
    AppendMenuW(m, MF_STRING, Search, L"搜索已添加的名称…");
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
        std::stable_sort(
            current.items.begin(), current.items.end(),
            [&](const box::Item &x, const box::Item &y) {
                auto a = wide(box::nameOf(x.path)), c = wide(box::nameOf(y.path));
                if (cmd == SortType) {
                    auto ext = [](std::wstring s) {
                        auto p = s.find_last_of(L'.');
                        return p == std::wstring::npos ? L"" : s.substr(p);
                    };
                    auto ea = ext(a), ec = ext(c);
                    int cmp = CompareStringOrdinal(ea.c_str(), -1, ec.c_str(), -1, TRUE);
                    if (cmp != CSTR_EQUAL)
                        return cmp == CSTR_LESS_THAN;
                }
                return CompareStringOrdinal(a.c_str(), -1, c.c_str(), -1, TRUE) == CSTR_LESS_THAN;
            });
        current.scroll = 0;
        changed();
        break;
    case Refresh:
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
            if (w == FrameTimer)
                v->timer();
            return 0;
        case WM_MOUSEMOVE: {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
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
                if (v->pressed >= 0)
                    v->dragging = true;
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
            }
            if (v->data().glow && v->app->state.effects && !v->effectFailed && !v->data().through &&
                !v->app->suspended) {
                if (motionAllowed(v->app))
                    v->setTimer();
                else if (v->fade == 0) {
                    v->fade = 1;
                    v->smooth = p;
                    v->draw();
                }
            }
            return 0;
        }
        case WM_MOUSELEAVE:
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
            v->pressed = v->dip(v->down.y) < Title ? -2 : v->hit(v->down);
            v->dragging = false;
            SetCapture(h);
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
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
                                v->changed();
                            }
                        } else if (box::transfer(v->data(), target->data(), selected)) {
                            v->changed(true);
                            target->changed();
                        } else
                            message(h, L"已在此收纳筐中或已达到数量上限；原分类保留。");
                        break;
                    }
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
        case WM_MOUSEWHEEL:
            if (!v->data().collapsed) {
                int delta = GET_WHEEL_DELTA_WPARAM(w);
                v->data().scroll = std::max(0, v->data().scroll - (delta > 0 ? 1 : -1));
                v->draw();
                v->app->dirty();
            }
            return 0;
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
    v->fit();
    v->updateStyle();
    v->draw();
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
    Form f{state.boxes.empty() ? L"创建第一个收纳筐" : L"创建收纳筐",
           {L"名称（可自行填写）", L"颜色 #RRGGBB", L"背景不透明度 16–245"},
           {L"进行中项目", L"#26324A", L"190"}};
    f.action = state.boxes.empty() ? L"创建第一个收纳筐" : L"创建收纳筐";
    if (!form(controller, f))
        return;
    box::Box b;
    try {
        if (f.values[0].empty() || f.values[0].size() > 80)
            throw std::runtime_error("name");
        b.name = utf8(f.values[0]);
        b.bg = color(f.values[1]);
        b.opacity = number(f.values[2], 16, 245);
    } catch (...) {
        message(controller, L"名称或颜色、透明度格式无效。请重新创建。");
        return;
    }
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
void App::command(UINT cmd) {
    if (modal || modalDepth)
        return;
    switch (cmd) {
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
            L"桌面归纳盒 0.1.0 "
            L"预览版\n\n拖入只保存路径，不复制或移动原文件。双击入口由系统打开原文件。\n右键可设置"
            L"、搜索、排序或移除引用。拖动图标可在本软件内调整排列、框间分类。\n整框穿透后请从托盘"
            L"恢复交互。锁定位置不影响打开。\n\n使用普通非置顶工具窗口；未使用 Explorer "
            L"私有嵌入接口。显示桌面可能隐藏窗口，可从托盘“显示全部”恢复。\n未完成 Windows "
            L"实机与性能验收，请先用临时文件试用。\n配置：%LOCALAPPDATA%\\DesktopBoxes");
        break;
    case Quit:
        quitting = true;
        save();
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
            AppendMenuW(menu, MF_STRING, Settings, L"设置收纳筐…");
            AppendMenuW(menu, MF_STRING, AllCollapse, L"全部折叠");
            AppendMenuW(menu, MF_STRING, AllExpand, L"全部展开");
            AppendMenuW(menu, MF_STRING, a->hidden ? ShowAll : HideAll,
                        a->hidden ? L"显示全部" : L"隐藏全部");
            AppendMenuW(menu, MF_STRING, ShowAll, L"显示全部（显示桌面后恢复）");
            AppendMenuW(menu, MF_STRING, RestoreInput, L"恢复所有筐的鼠标交互");
            AppendMenuW(menu, MF_STRING | (a->state.effects ? MF_CHECKED : 0), Effects,
                        L"启用星空光晕");
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
            a->dirty(true);
            return 0;
        }
        case WM_APP + 4:
            if (a->monitor) {
                auto results = a->monitor->take();
                for (auto &v : a->views) {
                    v->health.clear();
                    v->filterDirty = true;
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
            if (w == WTS_SESSION_LOCK || w == WTS_CONSOLE_DISCONNECT ||
                w == WTS_REMOTE_DISCONNECT) {
                a->suspended = true;
                for (auto &v : a->views) {
                    v->inside = false;
                    v->fade = 0;
                    v->animating = false;
                    v->fit();
                    v->stop();
                }
            } else if (w == WTS_SESSION_UNLOCK || w == WTS_CONSOLE_CONNECT ||
                       w == WTS_REMOTE_CONNECT) {
                a->suspended = false;
                a->redraw();
            }
            return 0;
        case WM_POWERBROADCAST:
            if (w == PBT_APMSUSPEND) {
                a->suspended = true;
                for (auto &v : a->views) {
                    v->inside = false;
                    v->fade = 0;
                    v->stop();
                }
            } else if (w == PBT_APMRESUMEAUTOMATIC || w == PBT_APMRESUMESUSPEND) {
                a->suspended = false;
                a->redraw();
                if (a->monitor)
                    a->monitor->update(a->state);
            }
            return TRUE;
        case WM_SETTINGCHANGE:
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
        for (auto &b : app.state.boxes)
            app.createView(b.id);
        app.monitor = std::make_unique<Monitor>(app.controller);
        app.monitor->update(app.state);
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
    app.views.clear();
    GdiplusShutdown(app.gdip);
    CoUninitialize();
    CloseHandle(app.singleton);
    ExitProcess(exitCode);
    return 0;
}
