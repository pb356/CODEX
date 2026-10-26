// Offline native asset/motion review. This window is explicitly NOT a dynamic desktop backend.
#include "leaf_motion.hpp"
#include "platform.hpp"
#include "playback_policy.hpp"
#include <commctrl.h>
#include <d2d1.h>
#include <wincodec.h>
using namespace desktop;
namespace {
constexpr UINT EventMessage = WM_APP + 42;
// Public Windows power notification GUID, documented by Microsoft.
constexpr GUID BatterySaverStatus{
    0xe00958c0, 0xc213, 0x4ace, {0xac, 0x77, 0xfe, 0xcc, 0xed, 0x2e, 0xee, 0xa5}};
HWND observed = nullptr;
std::atomic<bool> eventPosted{false};
template <class T> void release(T *&p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}
void CALLBACK changed(HWINEVENTHOOK, DWORD, HWND, LONG object, LONG, DWORD, DWORD) {
    if (object != OBJID_WINDOW && object != 0)
        return;
    if (observed && !eventPosted.exchange(true))
        PostMessageW(observed, EventMessage, 0, 0);
}
struct Preview {
    HWND hwnd = nullptr, mode = nullptr, status = nullptr;
    HFONT font = nullptr;
    HPOWERNOTIFY powerNotification = nullptr;
    ID2D1Factory *factory = nullptr;
    IWICImagingFactory *wic = nullptr;
    ID2D1HwndRenderTarget *target = nullptr;
    ID2D1Bitmap *images[4]{};
    std::unique_ptr<Gdiplus::Bitmap> fallback;
    std::vector<HWINEVENTHOOK> hooks;
    playback::Gate gate;
    playback::Mode playbackMode = playback::Mode::Energy;
    foliage::Clock clock;
    unsigned interval = 0;
    uint64_t renderedFrames = 0;
    ULONGLONG lastPresented = 0;
    bool reduced = false, failed = false;
    std::wstring assetDirectory;
    ~Preview() {
        freeTarget();
        release(wic);
        release(factory);
        if (font)
            DeleteObject(font);
    }
    void freeTarget() {
        for (auto &image : images)
            release(image);
        release(target);
    }
    void pause(playback::Reason reason, bool value) {
        gate.set(reason, value);
        sync();
    }
    void sync() {
        BOOL animations = TRUE;
        SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION, 0, &animations, 0);
        reduced = !animations;
        unsigned next = playback::frameInterval(playbackMode, reduced, gate);
        if (failed)
            next = 0;
        if (next != interval) {
            KillTimer(hwnd, 1);
            interval = next;
            if (next)
                SetTimer(hwnd, 1, next, nullptr);
        }
        clock.setRunning(next != 0, GetTickCount64());
        SetWindowTextW(status, failed          ? L"绘制已降级为静态，收纳功能不受影响"
                               : gate.paused() ? L"已暂停 · 不刷新画面"
                               : reduced       ? L"系统减少动画 · 静态"
                               : next          ? L"独立叶片轻摆 · 预览窗口，不是桌面背景"
                                               : L"静态 · 不刷新画面");
    }
    ID2D1Bitmap *load(const wchar_t *name) {
        IWICBitmapDecoder *decoder = nullptr;
        IWICBitmapFrameDecode *frame = nullptr;
        IWICFormatConverter *converter = nullptr;
        ID2D1Bitmap *image = nullptr;
        auto path = assetDirectory + L"\\" + name;
        HRESULT hr = wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                    WICDecodeMetadataCacheOnLoad, &decoder);
        if (SUCCEEDED(hr))
            hr = decoder->GetFrame(0, &frame);
        if (SUCCEEDED(hr))
            hr = wic->CreateFormatConverter(&converter);
        if (SUCCEEDED(hr))
            hr =
                converter->Initialize(frame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                      nullptr, 0, WICBitmapPaletteTypeCustom);
        if (SUCCEEDED(hr))
            hr = target->CreateBitmapFromWicBitmap(converter, nullptr, &image);
        release(converter);
        release(frame);
        release(decoder);
        if (FAILED(hr))
            throw std::runtime_error("asset decode");
        return image;
    }
    bool ensure() {
        if (target)
            return true;
        RECT r;
        GetClientRect(hwnd, &r);
        unsigned width = std::max(1L, r.right), height = std::max(1L, r.bottom);
        auto props = D2D1::RenderTargetProperties();
        auto window = D2D1::HwndRenderTargetProperties(hwnd, D2D1::SizeU(width, height));
        HRESULT hr = factory->CreateHwndRenderTarget(props, window, &target);
        if (FAILED(hr))
            return false;
        try {
            images[0] = load(L"background.png");
            images[1] = load(L"banana.png");
            images[2] = load(L"monstera.png");
            images[3] = load(L"anthurium.png");
        } catch (...) {
            freeTarget();
            return false;
        }
        return true;
    }
    void paintFallback(HDC dc, RECT r) {
        Gdiplus::Graphics graphics(dc);
        graphics.Clear(Gdiplus::Color(242, 237, 224));
        if (fallback && fallback->GetLastStatus() == Gdiplus::Ok)
            graphics.DrawImage(fallback.get(), 0, 0, r.right, std::max(1L, r.bottom - 82));
    }
    void render() {
        if (gate.paused() && !failed)
            return;
        if (interval && lastPresented && GetTickCount64() - lastPresented < interval)
            return;
        if (failed || !ensure()) {
            failed = true;
            sync();
            InvalidateRect(hwnd, nullptr, FALSE);
            return;
        }
        if (target->CheckWindowState() & D2D1_WINDOW_STATE_OCCLUDED) {
            pause(playback::Reason::Covered, true);
            return;
        }
        auto size = target->GetSize();
        float artHeight = std::max(1.f, size.height - 82.f);
        float sx = size.width / 1664.f, sy = artHeight / 941.f;
        // Letterbox in a matching paper tone; never distort the botanical source aspect ratio.
        float scale = std::min(sx, sy), ox = (size.width - 1664 * scale) / 2,
              oy = (artHeight - 941 * scale) / 2;
        auto base = D2D1::Matrix3x2F::Scale(scale, scale) * D2D1::Matrix3x2F::Translation(ox, oy);
        target->BeginDraw();
        target->Clear(D2D1::ColorF(0xf1e9da));
        target->PushAxisAlignedClip(D2D1::RectF(0, 0, size.width, artHeight),
                                    D2D1_ANTIALIAS_MODE_ALIASED);
        target->SetTransform(base);
        target->DrawBitmap(images[0], D2D1::RectF(0, 0, 1664, 941));
        struct Layer {
            float x, y, w, h, px, py, rest;
        };
        // Pivots correspond to actual stem ends. Full motion remains well left of x=998 (60%).
        const Layer layers[]{{10, -120, 740, 1110, .29f, .98f, -12},
                             {220, 160, 590, 568, .51f, .98f, -8},
                             {-35, 300, 560, 840, .54f, .98f, 5}};
        double seconds = clock.seconds(GetTickCount64());
        for (unsigned k = 0; k < 3; ++k) {
            auto &l = layers[k];
            float motion =
                (playbackMode == playback::Mode::Energy && k == 0) ? 0 : foliage::angle(k, seconds);
            auto pivot = D2D1::Point2F(l.x + l.w * l.px, l.y + l.h * l.py);
            target->SetTransform(D2D1::Matrix3x2F::Rotation(l.rest + motion, pivot) * base);
            target->DrawBitmap(images[k + 1], D2D1::RectF(l.x, l.y, l.x + l.w, l.y + l.h));
        }
        target->SetTransform(D2D1::Matrix3x2F::Identity());
        target->PopAxisAlignedClip();
        HRESULT hr = target->EndDraw();
        if (SUCCEEDED(hr)) {
            ++renderedFrames;
            lastPresented = GetTickCount64();
        }
        if (FAILED(hr)) {
            freeTarget();
            failed = true;
            sync();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
    }
    void visibility() {
        gate.set(playback::Reason::Hidden, !IsWindowVisible(hwnd) || IsIconic(hwnd));
        // A foreground/minimize/window event is a reason to retry the OS occlusion query.
        gate.set(playback::Reason::Covered, false);
        if (target && (target->CheckWindowState() & D2D1_WINDOW_STATE_OCCLUDED))
            gate.set(playback::Reason::Covered, true);
        gate.set(playback::Reason::Remote, GetSystemMetrics(SM_REMOTESESSION) != 0);
        SYSTEM_POWER_STATUS power{};
        if (GetSystemPowerStatus(&power))
            gate.set(playback::Reason::SavingPower, power.SystemStatusFlag == 1);
        sync();
        if (!gate.paused())
            InvalidateRect(hwnd, nullptr, FALSE);
    }
};
LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto p = (Preview *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        p = (Preview *)((CREATESTRUCTW *)l)->lpCreateParams;
        p->hwnd = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)p);
    }
    if (!p)
        return DefWindowProcW(h, m, w, l);
    switch (m) {
    case WM_CREATE: {
        p->font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                              CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        p->mode = CreateWindowW(WC_COMBOBOXW, L"播放模式",
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST, 20, 700, 220,
                                160, h, (HMENU)1, nullptr, nullptr);
        for (auto text : {L"静态", L"节能 · 上限 15 fps", L"标准 · 上限 30 fps"})
            SendMessageW(p->mode, CB_ADDSTRING, 0, (LPARAM)text);
        SendMessageW(p->mode, CB_SETCURSEL, 1, 0);
        SendMessageW(p->mode, WM_SETFONT, (WPARAM)p->font, TRUE);
        p->status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 260, 700, 700, 50, h,
                                  nullptr, nullptr, nullptr);
        SendMessageW(p->status, WM_SETFONT, (WPARAM)p->font, TRUE);
        WTSRegisterSessionNotification(h, NOTIFY_FOR_THIS_SESSION);
        p->powerNotification =
            RegisterPowerSettingNotification(h, &BatterySaverStatus, DEVICE_NOTIFY_WINDOW_HANDLE);
        observed = h;
        for (auto event : {EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_MINIMIZEEND, EVENT_OBJECT_SHOW,
                           EVENT_OBJECT_HIDE, EVENT_OBJECT_LOCATIONCHANGE}) {
            auto hook = SetWinEventHook(event, event, nullptr, changed, 0, 0,
                                        WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
            if (hook)
                p->hooks.push_back(hook);
        }
        p->sync();
        return 0;
    }
    case WM_SIZE: {
        int width = LOWORD(l), height = HIWORD(l);
        MoveWindow(p->mode, 20, std::max(0, height - 62), 220, 160, TRUE);
        MoveWindow(p->status, 260, std::max(0, height - 62), std::max(1, width - 280), 50, TRUE);
        if (p->target)
            p->target->Resize(D2D1::SizeU(std::max(1, width), std::max(1, height)));
        p->visibility();
        return 0;
    }
    case WM_SHOWWINDOW:
        PostMessageW(h, EventMessage, 0, 0);
        break;
    case WM_COMMAND:
        if (LOWORD(w) == 1 && HIWORD(w) == CBN_SELCHANGE) {
            p->playbackMode = (playback::Mode)SendMessageW(p->mode, CB_GETCURSEL, 0, 0);
            p->sync();
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_TIMER:
        if (w == 1)
            p->render();
        return 0;
    case EventMessage:
        eventPosted = false;
        p->visibility();
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT paint;
        HDC dc = BeginPaint(h, &paint);
        RECT r;
        GetClientRect(h, &r);
        if (p->failed)
            p->paintFallback(dc, r);
        EndPaint(h, &paint);
        if (!p->failed) {
            p->gate.set(playback::Reason::Covered, false);
            p->sync();
            p->render();
        }
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_WTSSESSION_CHANGE:
        if (w == WTS_SESSION_LOCK || w == WTS_SESSION_UNLOCK)
            p->pause(playback::Reason::Locked, w == WTS_SESSION_LOCK);
        if (w == WTS_CONSOLE_DISCONNECT || w == WTS_REMOTE_DISCONNECT)
            p->pause(playback::Reason::Disconnected, true);
        if (w == WTS_CONSOLE_CONNECT || w == WTS_REMOTE_CONNECT)
            p->pause(playback::Reason::Disconnected, false);
        p->visibility();
        return 0;
    case WM_POWERBROADCAST:
        if (w == PBT_POWERSETTINGCHANGE && l) {
            auto setting = (POWERBROADCAST_SETTING *)l;
            if (IsEqualGUID(setting->PowerSetting, BatterySaverStatus) &&
                setting->DataLength == sizeof(DWORD)) {
                DWORD active = 0;
                memcpy(&active, setting->Data, sizeof(active));
                p->pause(playback::Reason::SavingPower, active != 0);
                if (!p->gate.paused())
                    InvalidateRect(h, nullptr, FALSE);
                return TRUE;
            }
        }
        if (w == PBT_APMSUSPEND)
            p->pause(playback::Reason::Sleeping, true);
        else if (w == PBT_APMRESUMEAUTOMATIC || w == PBT_APMRESUMESUSPEND)
            p->pause(playback::Reason::Sleeping, false);
        p->visibility();
        return TRUE;
    case WM_SETTINGCHANGE:
        p->visibility();
        return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE)
            DestroyWindow(h);
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        observed = nullptr;
        for (auto hook : p->hooks)
            UnhookWinEvent(hook);
        p->hooks.clear();
        KillTimer(h, 1);
        p->interval = 0;
        WTSUnRegisterSessionNotification(h);
        if (p->powerNotification) {
            UnregisterPowerSettingNotification(p->powerNotification);
            p->powerNotification = nullptr;
        }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
} // namespace
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ULONG_PTR token = 0;
    Gdiplus::GdiplusStartupInput gi;
    Gdiplus::GdiplusStartup(&token, &gi, nullptr);
    int result = 1;
    {
        Preview p;
        wchar_t path[32768];
        DWORD n = GetModuleFileNameW(nullptr, path, 32768);
        if (n && n < 32768) {
            std::wstring module(path, n);
            p.assetDirectory =
                module.substr(0, module.find_last_of(L"\\/")) + L"\\assets\\wallpaper";
        }
        p.fallback = std::make_unique<Gdiplus::Bitmap>(
            (p.assetDirectory + L"\\approved-static.png").c_str());
        HRESULT hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &p.factory);
        if (SUCCEEDED(hr))
            hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&p.wic));
        p.failed = FAILED(hr);
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = instance;
        wc.lpfnWndProc = proc;
        wc.lpszClassName = L"DesktopBoxes.WallpaperPreview";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        HWND hwnd =
            CreateWindowExW(0, wc.lpszClassName, L"热植分层运动预览 · 尚未接入桌面 · Esc 关闭",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                            1280, 830, nullptr, nullptr, instance, &p);
        if (hwnd) {
            ShowWindow(hwnd, SW_SHOWNORMAL);
            MSG msg;
            while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
                if (!IsDialogMessageW(hwnd, &msg)) {
                    TranslateMessage(&msg);
                    DispatchMessageW(&msg);
                }
            }
            result = 0;
        }
    }
    Gdiplus::GdiplusShutdown(token);
    CoUninitialize();
    return result;
}
