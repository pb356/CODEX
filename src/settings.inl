// Native controls and preview transaction; no HTML or numeric developer parameter form.
namespace settings_ui {
enum Id {
    Name = 2100,
    Theme0,
    Theme1,
    Theme2,
    Opacity,
    Icons,
    Advanced,
    BackgroundColor,
    TextColor,
    FontSize,
    Count,
    Effect0,
    Effect1,
    Effect2,
    Strength,
    EffectAdvanced,
    Range,
    Stars,
    GlowColor,
    Locked,
    Reset = 2190,
    ErrorText,
    GlobalEffects = 2200,
    GlobalAnimations,
    GlobalStartup,
    GlobalInbox,
    GlobalPause,
    RefreshInbox,
    RealDesktop,
    CleanMode,
    RestoreIcons,
    BlankSide,
    BlankWidth,
    Density,
    Tone,
    WallpaperMode,
    OpenPreview
};
struct Panel {
    App *app = nullptr;
    View *view = nullptr;
    box::Box before, draft;
    bool creating = false, accepted = false, advanced = false, effectAdvanced = false,
         global = false;
    HWND host = nullptr, tab = nullptr, body = nullptr;
    HFONT font = nullptr;
    HBRUSH background = nullptr;
    int page = 0, dpi = 96, scroll = 0, extent = 0;
    bool building = false;
    bool effects = false, animations = false, startup = false, inboxEnabled = false, paused = false;
    box::WallpaperSettings wallpaper;
    int px(int n) const {
        return MulDiv(n, dpi, 96);
    }
    HWND get(int id) {
        return GetDlgItem(body, id);
    }
    void text(int id, const std::wstring &s) {
        if (auto h = get(id))
            SetWindowTextW(h, s.c_str());
    }
    HWND add(const wchar_t *cls, const std::wstring &title, int id, int x, int y, int w, int h,
             DWORD style = 0) {
        HWND c =
            CreateWindowExW(cls == std::wstring(L"EDIT") ? WS_EX_CLIENTEDGE : 0, cls, title.c_str(),
                            WS_CHILD | WS_VISIBLE | style, px(x), px(y - scroll), px(w), px(h),
                            body, (HMENU)(INT_PTR)id, app->instance, nullptr);
        SendMessageW(c, WM_SETFONT, (WPARAM)font, TRUE);
        SetWindowTheme(c, L"Explorer", nullptr);
        return c;
    }
    void label(const std::wstring &s, int y) {
        add(L"STATIC", s, 0, 20, y, 480, 22);
    }
    void check(int id, const std::wstring &s, int y, bool value) {
        auto h = add(L"BUTTON", s, id, 20, y, 490, 30, WS_TABSTOP | BS_AUTOCHECKBOX);
        SendMessageW(h, BM_SETCHECK, value ? BST_CHECKED : BST_UNCHECKED, 0);
    }
    void slider(int id, int y, int lo, int hi, int value) {
        auto h = add(TRACKBAR_CLASSW, L"", id, 16, y, 468, 34, WS_TABSTOP | TBS_NOTICKS);
        SendMessageW(h, TBM_SETRANGE, TRUE, MAKELPARAM(lo, hi));
        SendMessageW(h, TBM_SETPOS, TRUE, value);
    }
    void combo(int id, int y, const std::vector<std::wstring> &options, int selected) {
        auto h =
            add(WC_COMBOBOXW, L"", id, 20, y, 480, 180, WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL);
        for (auto &s : options)
            SendMessageW(h, CB_ADDSTRING, 0, (LPARAM)s.c_str());
        SendMessageW(h, CB_SETCURSEL, selected, 0);
    }
    void segmented(int first, int y, const std::vector<std::wstring> &values, int selected) {
        for (size_t k = 0; k < values.size(); ++k) {
            auto h = add(L"BUTTON", values[k], first + (int)k, 20 + (int)k * 164, y, 154, 44,
                         WS_TABSTOP | BS_OWNERDRAW);
            SendMessageW(h, BM_SETCHECK, (int)k == selected ? BST_CHECKED : BST_UNCHECKED, 0);
        }
    }
    void build() {
        building = true;
        scroll = 0;
        while (HWND c = GetWindow(body, GW_CHILD))
            DestroyWindow(c);
        if (global) {
            buildGlobal();
            building = false;
            layout();
            return;
        }
        if (page == 0) {
            label(L"名称", 14);
            auto h =
                add(L"EDIT", wide(draft.name), Name, 20, 40, 480, 34, WS_TABSTOP | ES_AUTOHSCROLL);
            SendMessageW(h, EM_SETLIMITTEXT, 80, 0);
            label(L"主题", 92);
            segmented(Theme0, 120, {L"雾白", L"松影", L"烟蓝"}, (int)draft.theme);
            label(L"背景浓度", 184);
            add(L"STATIC", std::to_wstring((draft.opacity - 16) * 100 / 229) + L"%", Opacity + 100,
                430, 184, 70, 22);
            slider(Opacity, 210, 0, 100, (draft.opacity - 16) * 100 / 229);
            label(L"图标大小", 260);
            combo(Icons, 286, {L"小", L"适中", L"大"},
                  draft.icon <= 32   ? 0
                  : draft.icon <= 44 ? 1
                                     : 2);
            check(Advanced, L"更多外观选项", 338, advanced);
            extent = 390;
            if (advanced) {
                add(L"BUTTON", L"背景颜色…", BackgroundColor, 20, 384, 230, 34, WS_TABSTOP);
                add(L"BUTTON", L"文字颜色…", TextColor, 270, 384, 230, 34, WS_TABSTOP);
                label(L"文字大小", 438);
                combo(FontSize, 464, {L"小", L"适中", L"大"},
                      draft.font <= 12   ? 0
                      : draft.font <= 14 ? 1
                                         : 2);
                check(Count, L"在标题栏显示项目数量", 514, draft.showCount);
                extent = 568;
            }
        } else if (page == 1) {
            label(L"悬停光感", 16);
            segmented(Effect0, 48, {L"关闭", L"柔光", L"星空"}, draft.glow ? (int)draft.effect : 0);
            label(L"光感强度", 120);
            slider(Strength, 148, 0, 100, (int)draft.brightness * 100 / 30);
            add(L"STATIC", L"轻柔地烘托背景，文字和图标保持清晰。", 0, 20, 202, 480, 44);
            check(EffectAdvanced, L"更多特效选项", 262, effectAdvanced);
            extent = 320;
            if (effectAdvanced) {
                label(L"影响范围", 314);
                slider(Range, 342, 60, 200, (int)draft.radius);
                label(L"星点疏密", 396);
                combo(Stars, 422, {L"稀疏", L"适中", L"略多"},
                      draft.stars <= 3   ? 0
                      : draft.stars <= 4 ? 1
                                         : 2);
                add(L"BUTTON", L"光感颜色…", GlowColor, 20, 472, 480, 34, WS_TABSTOP);
                extent = 534;
            }
        } else {
            check(Locked, L"锁定位置与尺寸", 20, draft.locked);
            add(L"STATIC", L"锁定后仍可打开文件、滚动和分类。", 0, 20, 62, 480, 38);
            add(L"STATIC", L"文件增加时保持筐的大小，超出部分在框内滚动。", 0, 20, 124, 480, 48);
            add(L"STATIC", L"整框点击穿透请从标题栏菜单启用，\n从托盘“恢复鼠标交互”关闭。", 0, 20,
                202, 480, 60);
            extent = 310;
        }
        building = false;
        layout();
        InvalidateRect(host, nullptr, TRUE);
    }
    void buildGlobal() {
        if (page == 0) {
            check(GlobalEffects, L"启用收纳筐悬停光感", 20, effects);
            check(GlobalAnimations, L"启用界面过渡动画", 68, animations);
            check(GlobalStartup, L"登录 Windows 后启动", 116, startup);
            add(L"STATIC", L"关闭动画时使用静态光感。\n系统减少动画设置会优先得到尊重。", 0, 20,
                184, 480, 60);
            extent = 280;
        } else if (page == 1) {
            check(GlobalInbox, L"启用桌面收件箱", 16, inboxEnabled);
            add(L"STATIC",
                L"自动显示真实桌面的项目，只维护引用。\n文件继续保存在原路径，不移动、不隐藏。", 0,
                20, 60, 480, 58);
            check(GlobalPause, L"暂停自动收件", 136, paused);
            add(L"BUTTON", L"立即刷新", RefreshInbox, 20, 186, 230, 36, WS_TABSTOP);
            add(L"BUTTON", L"打开真实桌面", RealDesktop, 270, 186, 230, 36, WS_TABSTOP);
            auto c = add(L"BUTTON", L"桌面整洁模式（待兼容验证）", CleanMode, 20, 252, 480, 32,
                         BS_AUTOCHECKBOX);
            EnableWindow(c, FALSE);
            add(L"STATIC",
                L"此版本保持原生桌面图标显示。\n完成 Windows 恢复测试前，不开放图标隐藏。", 0, 20,
                292, 480, 56);
            add(L"BUTTON", L"恢复原生桌面图标…", RestoreIcons, 20, 372, 480, 36, WS_TABSTOP);
            extent = 444;
        } else {
            add(L"STATIC", L"热植海报 · 分层预览", 0, 20, 18, 480, 28);
            add(L"STATIC",
                L"沿用已确认的海报构图与灰绿配色。\n当前预览保留右侧约 40% 的安静办公区。", 0, 20,
                56, 480, 58);
            add(L"BUTTON", L"打开分层运动预览", OpenPreview, 20, 138, 480, 40, WS_TABSTOP);
            add(L"STATIC",
                L"在预览窗口切换静态、节能与标准模式。\n预览只用于审阅画面，不更改系统壁纸。", 0,
                20, 210, 480, 60);
            add(L"STATIC",
                L"动态桌面、疏密与留白调节尚未开放。\n完成素材与 Windows 10 兼容验证后提供。", 0,
                20, 310, 480, 60);
            extent = 410;
        }
    }
    void layout() {
        RECT client;
        GetClientRect(host, &client);
        int w = client.right, h = client.bottom;
        MoveWindow(tab, px(18), px(54), w - px(36), px(36), TRUE);
        MoveWindow(body, px(18), px(98), w - px(36), std::max(px(100), h - px(176)), TRUE);
        MoveWindow(GetDlgItem(host, Reset), px(22), h - px(58), px(112), px(34), TRUE);
        MoveWindow(GetDlgItem(host, IDCANCEL), w - px(224), h - px(58), px(92), px(34), TRUE);
        MoveWindow(GetDlgItem(host, IDOK), w - px(122), h - px(58), px(100), px(34), TRUE);
        MoveWindow(GetDlgItem(host, ErrorText), px(22), h - px(92), w - px(44), px(26), TRUE);
        RECT r;
        GetClientRect(body, &r);
        SCROLLINFO info{sizeof(info),
                        SIF_RANGE | SIF_PAGE | SIF_POS,
                        0,
                        px(extent),
                        UINT(r.bottom),
                        px(scroll),
                        0};
        SetScrollInfo(body, SB_VERT, &info, TRUE);
    }
    void scrollTo(int pos) {
        RECT r;
        GetClientRect(body, &r);
        int max = std::max(0, extent - MulDiv(r.bottom, 96, dpi));
        int next = std::clamp(pos, 0, max);
        int delta = px(scroll - next);
        scroll = next;
        ScrollWindowEx(body, 0, delta, nullptr, nullptr, nullptr, nullptr,
                       SW_SCROLLCHILDREN | SW_INVALIDATE | SW_ERASE);
        SetScrollPos(body, SB_VERT, px(scroll), TRUE);
    }
    bool validName() {
        wchar_t value[81]{};
        if (auto h = get(Name)) {
            GetWindowTextW(h, value, 81);
            if (!value[0]) {
                SetWindowTextW(GetDlgItem(host, ErrorText), L"请填写收纳筐名称");
                EnableWindow(GetDlgItem(host, IDOK), FALSE);
                return false;
            }
            try {
                auto candidate = utf8(value);
                if (std::all_of(candidate.begin(), candidate.end(),
                                [](unsigned char c) { return c == ' '; }) ||
                    std::any_of(candidate.begin(), candidate.end(),
                                [](unsigned char c) { return c < 32; }))
                    throw std::runtime_error("name");
                draft.name = std::move(candidate);
            } catch (...) {
                SetWindowTextW(GetDlgItem(host, ErrorText), L"请填写有效的收纳筐名称");
                EnableWindow(GetDlgItem(host, IDOK), FALSE);
                return false;
            }
        }
        SetWindowTextW(GetDlgItem(host, ErrorText), L"");
        EnableWindow(GetDlgItem(host, IDOK), TRUE);
        return true;
    }
    void read() {
        if (global) {
            if (auto h = get(GlobalEffects))
                effects = SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (auto h = get(GlobalAnimations))
                animations = SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (auto h = get(GlobalStartup))
                startup = SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (auto h = get(GlobalInbox))
                inboxEnabled = SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (auto h = get(GlobalPause))
                paused = SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
            if (auto h = get(BlankSide))
                wallpaper.blankSide = (uint32_t)SendMessageW(h, CB_GETCURSEL, 0, 0);
            if (auto h = get(BlankWidth))
                wallpaper.blankPercent = (uint32_t)SendMessageW(h, TBM_GETPOS, 0, 0);
            if (auto h = get(Density))
                wallpaper.density = (uint32_t)SendMessageW(h, CB_GETCURSEL, 0, 0);
            if (auto h = get(Tone))
                wallpaper.tone = (uint32_t)SendMessageW(h, CB_GETCURSEL, 0, 0);
            return;
        }
        validName();
        if (auto h = get(Opacity)) {
            auto pos = (uint32_t)SendMessageW(h, TBM_GETPOS, 0, 0);
            if (pos != (draft.opacity - 16) * 100 / 229)
                draft.opacity = 16 + pos * 229 / 100;
            text(Opacity + 100, std::to_wstring(SendMessageW(h, TBM_GETPOS, 0, 0)) + L"%");
        }
        if (auto h = get(Icons)) {
            int n = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
            int old = draft.icon <= 32 ? 0 : draft.icon <= 44 ? 1 : 2;
            if (n != old)
                draft.icon = n == 0 ? 32 : n == 1 ? 40 : 52;
        }
        if (auto h = get(FontSize)) {
            int n = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
            int old = draft.font <= 12 ? 0 : draft.font <= 14 ? 1 : 2;
            if (n != old)
                draft.font = n == 0 ? 12 : n == 1 ? 13 : 16;
        }
        if (auto h = get(Count))
            draft.showCount = SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
        if (auto h = get(Strength)) {
            auto pos = (uint32_t)SendMessageW(h, TBM_GETPOS, 0, 0);
            if (pos != std::min(100u, draft.brightness * 100 / 30))
                draft.brightness = pos * 30 / 100;
        }
        if (auto h = get(Range)) {
            auto pos = (uint32_t)SendMessageW(h, TBM_GETPOS, 0, 0);
            if (pos != std::clamp(draft.radius, 60u, 200u))
                draft.radius = pos;
        }
        if (auto h = get(Stars)) {
            int n = (int)SendMessageW(h, CB_GETCURSEL, 0, 0);
            int old = draft.stars <= 3 ? 0 : draft.stars <= 4 ? 1 : 2;
            if (n != old)
                draft.stars = n == 0 ? 3 : n == 1 ? 4 : 6;
        }
        if (auto h = get(Locked))
            draft.locked = SendMessageW(h, BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
    void preview() {
        if (building || global)
            return;
        read();
        if (view) {
            box::appearance(view->data(), draft);
            view->updateStyle();
            SetWindowTextW(view->hwnd, wide(draft.name).c_str());
            view->draw();
        }
        InvalidateRect(body, nullptr, FALSE);
    }
    void choose(uint32_t &rgb) {
        static COLORREF custom[16]{};
        CHOOSECOLORW c{};
        c.lStructSize = sizeof(c);
        c.hwndOwner = host;
        c.rgbResult = RGB(rgb >> 16, (rgb >> 8) & 255, rgb & 255);
        c.lpCustColors = custom;
        c.Flags = CC_FULLOPEN | CC_RGBINIT;
        if (ChooseColorW(&c)) {
            rgb = (GetRValue(c.rgbResult) << 16) | (GetGValue(c.rgbResult) << 8) |
                  GetBValue(c.rgbResult);
            draft.theme = 3;
            preview();
        }
    }
    void command(int id, int event) {
        if (building)
            return;
        if (global) {
            read();
            if (id == RefreshInbox)
                app->updateInbox();
            if (id == RealDesktop)
                openKnownFolder(host, FOLDERID_Desktop, true);
            if (id == RestoreIcons)
                message(host, L"恢复方法：桌面右键 → 查看 → "
                              L"显示桌面图标。\n\n当前版本没有自动隐藏原生图标。");
            if (id == OpenPreview) {
                if (!app->wallpaperPreview)
                    app->wallpaperPreview = std::make_unique<PreviewLauncher>();
                if (!app->wallpaperPreview->open())
                    message(host, L"无法打开分层预览。请检查安装内容是否完整；收纳功能不受影响。",
                            MB_OK | MB_ICONWARNING);
            }
            return;
        }
        if (id >= Theme0 && id <= Theme2) {
            read();
            design::theme(draft, id - Theme0);
            build();
            preview();
            return;
        }
        if (id >= Effect0 && id <= Effect2) {
            read();
            draft.effect = id - Effect0;
            draft.glow = draft.effect != 0;
            if (draft.effect == 2)
                draft.stars = 4;
            build();
            preview();
            return;
        }
        if (id == Advanced) {
            read();
            advanced = SendMessageW(get(Advanced), BM_GETCHECK, 0, 0) == BST_CHECKED;
            build();
            return;
        }
        if (id == EffectAdvanced) {
            read();
            effectAdvanced = SendMessageW(get(EffectAdvanced), BM_GETCHECK, 0, 0) == BST_CHECKED;
            build();
            return;
        }
        if (id == BackgroundColor)
            choose(draft.bg);
        else if (id == TextColor)
            choose(draft.fg);
        else if (id == GlowColor)
            choose(draft.glowColor);
        else if (event == EN_CHANGE || event == CBN_SELCHANGE || event == BN_CLICKED)
            preview();
    }
};
LRESULT CALLBACK bodyProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto p = (Panel *)GetWindowLongPtrW(h, GWLP_USERDATA);
    if (m == WM_NCCREATE) {
        p = (Panel *)((CREATESTRUCTW *)l)->lpCreateParams;
        SetWindowLongPtrW(h, GWLP_USERDATA, (LONG_PTR)p);
    }
    if (!p)
        return DefWindowProcW(h, m, w, l);
    if (m == WM_COMMAND) {
        p->command(LOWORD(w), HIWORD(w));
        return 0;
    }
    if (m == WM_HSCROLL) {
        p->preview();
        return 0;
    }
    if (m == WM_VSCROLL) {
        SCROLLINFO s{sizeof(s), SIF_ALL};
        GetScrollInfo(h, SB_VERT, &s);
        int target = p->scroll;
        switch (LOWORD(w)) {
        case SB_LINEUP:
            target -= 32;
            break;
        case SB_LINEDOWN:
            target += 32;
            break;
        case SB_PAGEUP:
            target -= 180;
            break;
        case SB_PAGEDOWN:
            target += 180;
            break;
        case SB_THUMBTRACK:
            target = MulDiv(s.nTrackPos, 96, p->dpi);
            break;
        }
        p->scrollTo(target);
        return 0;
    }
    if (m == WM_MOUSEWHEEL) {
        p->scrollTo(p->scroll - GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 60);
        return 0;
    }
    if (m == WM_CTLCOLORSTATIC || m == WM_CTLCOLOREDIT) {
        SetTextColor((HDC)w,
                     design::highContrast() ? GetSysColor(COLOR_WINDOWTEXT) : RGB(42, 57, 47));
        SetBkMode((HDC)w, TRANSPARENT);
        return (LRESULT)p->background;
    }
    if (m == WM_ERASEBKGND) {
        RECT r;
        GetClientRect(h, &r);
        FillRect((HDC)w, &r, p->background);
        return 1;
    }
    if (m == WM_DRAWITEM) {
        auto d = (DRAWITEMSTRUCT *)l;
        int id = (int)d->CtlID;
        bool selected = false;
        uint32_t bg = 0xf7f8f5, fg = 0x33483b;
        if (id >= Theme0 && id <= Theme2) {
            auto pal = design::palette(id - Theme0);
            bg = pal.surface;
            fg = pal.text;
            selected = p->draft.theme == uint32_t(id - Theme0);
        }
        if (id >= Effect0 && id <= Effect2)
            selected = p->draft.effect == uint32_t(id - Effect0);
        if (design::highContrast()) {
            bg = design::systemRgb(COLOR_WINDOW);
            fg = design::systemRgb(COLOR_WINDOWTEXT);
        }
        HBRUSH brush = CreateSolidBrush(RGB(bg >> 16, (bg >> 8) & 255, bg & 255));
        FillRect(d->hDC, &d->rcItem, brush);
        DeleteObject(brush);
        SetBkMode(d->hDC, TRANSPARENT);
        SetTextColor(d->hDC, RGB(fg >> 16, (fg >> 8) & 255, fg & 255));
        SelectObject(d->hDC, p->font);
        wchar_t text[64];
        GetWindowTextW(d->hwndItem, text, 64);
        DrawTextW(d->hDC, text, -1, &d->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        if (selected) {
            RECT r = d->rcItem;
            InflateRect(&r, -2, -2);
            HPEN pen = CreatePen(PS_SOLID, p->px(2),
                                 design::highContrast() ? GetSysColor(COLOR_HIGHLIGHT)
                                                        : RGB(137, 164, 145));
            auto old = SelectObject(d->hDC, pen);
            auto stock = SelectObject(d->hDC, GetStockObject(HOLLOW_BRUSH));
            RoundRect(d->hDC, r.left, r.top, r.right, r.bottom, p->px(8), p->px(8));
            SelectObject(d->hDC, stock);
            SelectObject(d->hDC, old);
            DeleteObject(pen);
        }
        if (d->itemState & ODS_FOCUS)
            DrawFocusRect(d->hDC, &d->rcItem);
        return TRUE;
    }
    return DefWindowProcW(h, m, w, l);
}
INT_PTR CALLBACK dialogProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto p = (Panel *)GetWindowLongPtrW(h, DWLP_USER);
    if (m == WM_INITDIALOG) {
        p = (Panel *)l;
        p->host = h;
        SetWindowLongPtrW(h, DWLP_USER, l);
        p->dpi = 96;
        using DpiFn = UINT(WINAPI *)(HWND);
        auto fn = (DpiFn)(void *)GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow");
        if (fn)
            p->dpi = (int)fn(h);
        p->font = CreateFontW(-p->px(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH, L"Microsoft YaHei UI");
        p->background = CreateSolidBrush(design::highContrast() ? GetSysColor(COLOR_WINDOW)
                                                                : RGB(246, 248, 245));
        SetWindowTextW(h, p->global ? L"软件设置" : p->creating ? L"创建收纳筐" : L"收纳筐设置");
        auto label = CreateWindowW(L"STATIC",
                                   p->global     ? L"桌面归纳盒"
                                   : p->creating ? L"给新的收纳筐一个名字"
                                                 : L"调整时可直接查看桌面上的收纳筐",
                                   WS_CHILD | WS_VISIBLE, p->px(22), p->px(18), p->px(510),
                                   p->px(26), h, nullptr, p->app->instance, nullptr);
        SendMessageW(label, WM_SETFONT, (WPARAM)p->font, TRUE);
        p->tab = CreateWindowExW(0, WC_TABCONTROLW, L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP, 0, 0,
                                 0, 0, h, (HMENU)2000, p->app->instance, nullptr);
        SendMessageW(p->tab, WM_SETFONT, (WPARAM)p->font, TRUE);
        std::vector<std::wstring> names =
            p->global ? std::vector<std::wstring>{L"常规", L"桌面收件", L"壁纸"}
                      : std::vector<std::wstring>{L"外观", L"特效", L"布局"};
        for (size_t i = 0; i < names.size(); ++i) {
            TCITEMW item{};
            item.mask = TCIF_TEXT;
            item.pszText = names[i].data();
            SendMessageW(p->tab, TCM_INSERTITEMW, i, (LPARAM)&item);
        }
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.hInstance = p->app->instance;
        wc.lpfnWndProc = bodyProc;
        wc.lpszClassName = L"DesktopBoxes.SettingsBody";
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        RegisterClassExW(&wc);
        p->body = CreateWindowExW(WS_EX_CONTROLPARENT, L"DesktopBoxes.SettingsBody", L"",
                                  WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0, 0, 0, 0, h, nullptr,
                                  p->app->instance, p);
        for (auto pair : std::vector<std::pair<int, std::wstring>>{
                 {Reset, L"恢复默认"},
                 {IDCANCEL, L"取消"},
                 {IDOK, p->creating ? (p->app->state.boxes.empty() ? L"创建第一个收纳筐" : L"创建")
                                    : L"完成"}}) {
            auto c = CreateWindowW(
                L"BUTTON", pair.second.c_str(),
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | (pair.first == IDOK ? BS_DEFPUSHBUTTON : 0), 0,
                0, 0, 0, h, (HMENU)(INT_PTR)pair.first, p->app->instance, nullptr);
            SendMessageW(c, WM_SETFONT, (WPARAM)p->font, TRUE);
        }
        auto error = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, h,
                                   (HMENU)ErrorText, p->app->instance, nullptr);
        SendMessageW(error, WM_SETFONT, (WPARAM)p->font, TRUE);
        POINT cursor;
        GetCursorPos(&cursor);
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromPoint(cursor, MONITOR_DEFAULTTONEAREST), &mi);
        int width = std::min(p->px(570), (int)(mi.rcWork.right - mi.rcWork.left)),
            height = std::min(p->px(620), (int)(mi.rcWork.bottom - mi.rcWork.top));
        SetWindowPos(h, nullptr, mi.rcWork.left + (mi.rcWork.right - mi.rcWork.left - width) / 2,
                     mi.rcWork.top + (mi.rcWork.bottom - mi.rcWork.top - height) / 2, width, height,
                     SWP_NOZORDER);
        p->build();
        return TRUE;
    }
    if (!p)
        return FALSE;
    if (m == WM_NOTIFY && ((NMHDR *)l)->idFrom == 2000 && ((NMHDR *)l)->code == TCN_SELCHANGE) {
        p->read();
        p->page = (int)SendMessageW(p->tab, TCM_GETCURSEL, 0, 0);
        p->build();
        return TRUE;
    }
    if (m == WM_COMMAND) {
        int id = LOWORD(w);
        if (id == IDOK) {
            p->read();
            if (!p->global && !p->validName())
                return TRUE;
            p->accepted = true;
            EndDialog(h, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL) {
            EndDialog(h, IDCANCEL);
            return TRUE;
        }
        if (id == Reset) {
            if (p->global) {
                p->effects = true;
                p->animations = true;
                p->wallpaper = {};
            } else {
                box::Box defaults;
                defaults.name = p->draft.name;
                box::appearance(p->draft, defaults);
            }
            p->build();
            p->preview();
            return TRUE;
        }
    }
    if (m == WM_DPICHANGED) {
        p->dpi = HIWORD(w);
        HFONT old = p->font;
        p->font = CreateFontW(-p->px(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              0, 0, CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        EnumChildWindows(
            h,
            [](HWND child, LPARAM value) -> BOOL {
                SendMessageW(child, WM_SETFONT, (WPARAM)value, TRUE);
                return TRUE;
            },
            (LPARAM)p->font);
        if (old)
            DeleteObject(old);
        auto suggested = (RECT *)l;
        MONITORINFO mi{};
        mi.cbSize = sizeof(mi);
        GetMonitorInfoW(MonitorFromRect(suggested, MONITOR_DEFAULTTONEAREST), &mi);
        int width = std::min(suggested->right - suggested->left, mi.rcWork.right - mi.rcWork.left),
            height = std::min(suggested->bottom - suggested->top, mi.rcWork.bottom - mi.rcWork.top);
        SetWindowPos(h, nullptr,
                     std::clamp(suggested->left, mi.rcWork.left, mi.rcWork.right - width),
                     std::clamp(suggested->top, mi.rcWork.top, mi.rcWork.bottom - height), width,
                     height, SWP_NOZORDER);
        p->build();
        return TRUE;
    }
    if (m == WM_SIZE) {
        p->layout();
        return TRUE;
    }
    if (m == WM_CLOSE) {
        EndDialog(h, IDCANCEL);
        return TRUE;
    }
    if (m == WM_MOUSEWHEEL) {
        p->scrollTo(p->scroll - GET_WHEEL_DELTA_WPARAM(w) / WHEEL_DELTA * 60);
        return TRUE;
    }
    if (m == WM_CTLCOLORSTATIC) {
        SetTextColor((HDC)w,
                     design::highContrast() ? GetSysColor(COLOR_WINDOWTEXT) : RGB(42, 57, 47));
        SetBkMode((HDC)w, TRANSPARENT);
        return (INT_PTR)p->background;
    }
    if (m == WM_ERASEBKGND) {
        RECT r;
        GetClientRect(h, &r);
        FillRect((HDC)w, &r, p->background);
        return TRUE;
    }
    if (m == WM_DESTROY) {
        if (p->font)
            DeleteObject(p->font);
        if (p->background)
            DeleteObject(p->background);
        return TRUE;
    }
    if (m == WM_SETTINGCHANGE || m == WM_THEMECHANGED || m == WM_SYSCOLORCHANGE) {
        if (p->background)
            DeleteObject(p->background);
        p->background = CreateSolidBrush(design::highContrast() ? GetSysColor(COLOR_WINDOW)
                                                                : RGB(246, 248, 245));
        RedrawWindow(h, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE);
        return TRUE;
    }
    return FALSE;
}
inline bool show(Panel &panel) {
    struct {
        DLGTEMPLATE dialog;
        WORD menu, cls, title;
    } t{};
    t.dialog.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME | WS_CLIPCHILDREN;
    t.dialog.dwExtendedStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;
    t.dialog.cx = 360;
    t.dialog.cy = 400;
    panel.app->modal = true;
    std::vector<HWND> disabled;
    for (auto &v : panel.app->views)
        if (IsWindowEnabled(v->hwnd)) {
            disabled.push_back(v->hwnd);
            EnableWindow(v->hwnd, FALSE);
        }
    DialogBoxIndirectParamW(panel.app->instance, &t.dialog,
                            panel.view ? panel.view->hwnd : panel.app->controller, dialogProc,
                            (LPARAM)&panel);
    for (auto h : disabled)
        if (IsWindow(h))
            EnableWindow(h, TRUE);
    panel.app->modal = false;
    return panel.accepted;
}
} // namespace settings_ui
bool editSettings(App *app, View *view, box::Box &initial, bool creating) {
    settings_ui::Panel p;
    p.app = app;
    p.view = view;
    p.creating = creating;
    p.before = initial;
    p.draft = initial;
    bool ok = settings_ui::show(p);
    if (view) {
        box::appearance(view->data(), ok ? p.draft : p.before);
        if (!ok)
            view->data().scroll = p.before.scroll;
        view->updateStyle();
        view->draw();
    }
    if (ok)
        box::appearance(initial, p.draft);
    if (ok || app->pendingSave) {
        app->pendingSave = false;
        app->dirty();
    }
    return ok;
}
void settings(View *v) {
    box::Box copy = v->data();
    editSettings(v->app, v, copy);
}
void globalSettings(App *app) {
    settings_ui::Panel p;
    p.app = app;
    p.global = true;
    p.effects = app->state.effects;
    p.animations = app->state.animations;
    p.startup = startupEnabled();
    p.inboxEnabled = app->inboxData() != nullptr;
    p.paused = app->state.inboxPaused;
    p.wallpaper = app->state.wallpaper;
    if (settings_ui::show(p)) {
        app->state.effects = p.effects;
        app->state.animations = p.animations;
        app->state.wallpaper = p.wallpaper;
        app->state.inboxPaused = p.paused;
        if (p.startup != startupEnabled())
            toggleStartup(app->controller);
        if (p.inboxEnabled && !app->inboxData())
            app->createInbox();
        else if (!p.inboxEnabled && app->inboxData()) {
            auto id = app->inboxData()->id;
            PostMessageW(app->controller, WM_APP + 5, id, 0);
        }
        app->updateInbox();
        app->redraw();
        app->dirty();
    } else if (app->pendingSave) {
        app->pendingSave = false;
        app->dirty();
    }
}
namespace import_details_ui {
struct Dialog {
    View *view;
    HFONT font = nullptr;
};
INT_PTR CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    auto d = (Dialog *)GetWindowLongPtrW(h, DWLP_USER);
    if (m == WM_INITDIALOG) {
        d = (Dialog *)l;
        SetWindowLongPtrW(h, DWLP_USER, l);
        SetWindowTextW(h, L"添加结果明细");
        d->font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, 0, 0,
                              CLEARTYPE_QUALITY, 0, L"Microsoft YaHei UI");
        auto edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", d->view->importDetails.c_str(),
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE |
                                        ES_READONLY | ES_AUTOVSCROLL,
                                    16, 16, 600, 330, h, (HMENU)3001, nullptr, nullptr);
        SendMessageW(edit, WM_SETFONT, (WPARAM)d->font, TRUE);
        CreateWindowW(L"BUTTON", L"关闭", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                      520, 360, 96, 32, h, (HMENU)IDOK, nullptr, nullptr);
        RECT r{0, 0, 632, 410};
        AdjustWindowRect(&r, WS_POPUP | WS_CAPTION | WS_SYSMENU, FALSE);
        SetWindowPos(h, nullptr, 0, 0, r.right - r.left, r.bottom - r.top,
                     SWP_NOMOVE | SWP_NOZORDER);
        return TRUE;
    }
    if ((m == WM_COMMAND && (LOWORD(w) == IDOK || LOWORD(w) == IDCANCEL)) || m == WM_CLOSE) {
        EndDialog(h, IDOK);
        return TRUE;
    }
    if (m == WM_DESTROY && d && d->font)
        DeleteObject(d->font);
    return FALSE;
}
} // namespace import_details_ui
void showImportDetails(View *v) {
    if (v->importDetails.empty())
        return;
    struct {
        DLGTEMPLATE d;
        WORD m, c, t;
    } temp{};
    temp.d.style = WS_POPUP | WS_CAPTION | WS_SYSMENU | DS_MODALFRAME;
    temp.d.cx = 420;
    temp.d.cy = 280;
    import_details_ui::Dialog state{v};
    v->app->modal = true;
    DialogBoxIndirectParamW(v->app->instance, &temp.d, v->hwnd, import_details_ui::proc,
                            (LPARAM)&state);
    v->app->modal = false;
    if (v->app->pendingSave) {
        v->app->pendingSave = false;
        v->app->dirty();
    }
}
