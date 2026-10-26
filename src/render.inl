// Bounded, reused per-pixel surface. No child windows or full-screen overlay are used for effects.
void View::render() {
    auto &b = data();
    RECT window;
    GetWindowRect(hwnd, &window);
    int w = window.right - window.left, h = window.bottom - window.top;
    if (w < 1 || h < 1)
        return;
    struct Screen {
        HDC dc = GetDC(nullptr);
        ~Screen() {
            if (dc)
                ReleaseDC(nullptr, dc);
        }
    } screen;
    if (!screen.dc)
        return;
    if (!surfaceDC || surfaceW != w || surfaceH != h) {
        clearSurface();
        surfaceW = w;
        surfaceH = h;
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        surfaceDC = CreateCompatibleDC(screen.dc);
        surfaceBitmap =
            CreateDIBSection(screen.dc, &info, DIB_RGB_COLORS, &surfaceBits, nullptr, 0);
        if (!surfaceDC || !surfaceBitmap) {
            clearSurface();
            return;
        }
        SelectObject(surfaceDC, surfaceBitmap);
    }
    {
        Bitmap bitmap(w, h, w * 4, PixelFormat32bppPARGB, (BYTE *)surfaceBits);
        Graphics g(&bitmap);
        g.SetCompositingMode(CompositingModeSourceCopy);
        g.Clear(Color(0, 0, 0, 0));
        g.SetCompositingMode(CompositingModeSourceOver);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.ScaleTransform(dpi / 96.f, dpi / 96.f);
        float width = (float)dip(w), height = (float)dip(h);
        GraphicsPath outline;
        design::rounded(outline, 0, 0, width, height, design::Radius);
        g.SetClip(&outline);
        auto palette = design::effectivePalette(b);
        bool highContrast = design::highContrast();
        SolidBrush background(design::color(palette.surface, highContrast ? 255 : (BYTE)b.opacity));
        g.FillPath(&background, &outline);
        if (!highContrast && fade > 0 && app->state.effects && b.glow && b.effect &&
            !effectFailed && !b.through) {
            float radius = float(b.collapsed ? std::min(70u, b.radius) : b.radius);
            // Explicit premultiplied, bounded alpha avoids implementation-specific path-gradient
            // interpolation and a bright hard circle. The small bitmap is reused across all frames.
            auto key = (uint64_t(b.glowColor) << 32) | b.brightness;
            if (!glowBitmap || glowKey != key) {
                auto image = std::make_unique<Bitmap>(128, 128, PixelFormat32bppPARGB);
                BitmapData pixels{};
                Rect region(0, 0, 128, 128);
                if (image->LockBits(&region, ImageLockModeWrite, PixelFormat32bppPARGB, &pixels) !=
                    Ok)
                    throw std::runtime_error("glow surface");
                for (int y = 0; y < 128; ++y) {
                    auto row = (uint32_t *)((BYTE *)pixels.Scan0 + y * pixels.Stride);
                    for (int x = 0; x < 128; ++x) {
                        float dx = (x - 63.5f) / 63.5f, dy = (y - 63.5f) / 63.5f;
                        float falloff = std::max(0.f, 1.f - std::sqrt(dx * dx + dy * dy));
                        uint32_t a = (uint32_t)(std::min(b.brightness, 30u) * falloff * falloff);
                        row[x] = (a << 24) | ((((b.glowColor >> 16) & 255) * a / 255) << 16) |
                                 ((((b.glowColor >> 8) & 255) * a / 255) << 8) |
                                 ((b.glowColor & 255) * a / 255);
                    }
                }
                image->UnlockBits(&pixels);
                glowBitmap = std::move(image);
                glowKey = key;
                if (!glowAttributes)
                    glowAttributes = std::make_unique<ImageAttributes>();
            }
            ColorMatrix alpha{};
            for (int k = 0; k < 5; ++k)
                alpha.m[k][k] = 1;
            alpha.m[3][3] = fade;
            glowAttributes->SetColorMatrix(&alpha);
            g.DrawImage(glowBitmap.get(),
                        RectF(softX - radius, softY - radius, radius * 2, radius * 2), 0, 0, 128,
                        128, UnitPixel, glowAttributes.get());
            if (b.effect == 2) {
                if (!starBrush)
                    starBrush = std::make_unique<SolidBrush>(Color(0, 0, 0, 0));
                float t = motionAllowed(app) ? float(GetTickCount64() % 120000) / 1000.f : 0;
                for (uint32_t k = 0; k < std::min(b.stars, 6u); ++k) {
                    float x = 18 + float((k * 97 + 37) % std::max(1, dip(w) - 36)),
                          y = 12 + float((k * 59 + 23) %
                                         std::max(1, (b.collapsed ? Title : dip(h)) - 24));
                    float a = .55f + .16f * std::sin(t * .48f + k * 1.7f);
                    starBrush->SetColor(design::color(palette.accent, (BYTE)(30 * fade * a)));
                    float size = k % 2 ? 1.3f : 1.8f;
                    g.FillEllipse(starBrush.get(), x, y, size, size);
                }
            }
        }
        Pen border(design::color(palette.accent, highContrast ? 255 : 24), 1);
        g.DrawPath(&border, &outline);
        FontFamily family(L"Microsoft YaHei UI");
        auto chosen = family.GetLastStatus() == Ok ? &family : FontFamily::GenericSansSerif();
        Font titleFont(chosen, 14, FontStyleBold, UnitPixel),
            bodyFont(chosen, (REAL)b.font, FontStyleRegular, UnitPixel),
            small(chosen, 12, FontStyleRegular, UnitPixel);
        SolidBrush text(design::color(palette.text)), secondary(design::color(palette.secondary)),
            accent(design::color(palette.accent));
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        StringFormat line;
        line.SetTrimming(StringTrimmingEllipsisCharacter);
        line.SetFormatFlags(StringFormatFlagsNoWrap);
        auto name = wide(b.name);
        g.DrawString(name.c_str(), -1, &titleFont, RectF(16, 10, std::max(30.f, width - 154), 24),
                     &line, &text);
        const auto &list = visible();
        if (b.showCount) {
            auto count =
                std::to_wstring(b.kind == box::Kind::Inbox ? list.size() : b.items.size()) + L" 项";
            g.DrawString(count.c_str(), -1, &small, RectF(std::max(82.f, width - 144), 12, 62, 20),
                         &line, &secondary);
        }
        Pen iconPen(design::color(palette.text, 225), 1.5f);
        float cx = width - 64, cy = 21;
        if (b.collapsed) {
            g.DrawLine(&iconPen, cx - 2, cy - 4, cx + 2, cy);
            g.DrawLine(&iconPen, cx + 2, cy, cx - 2, cy + 4);
        } else {
            g.DrawLine(&iconPen, cx - 4, cy - 2, cx, cy + 2);
            g.DrawLine(&iconPen, cx, cy + 2, cx + 4, cy - 2);
        }
        for (int k = -1; k <= 1; ++k)
            g.FillEllipse(&text, width - 29 + k * 5, 20.f, 2.f, 2.f);
        if (focused && (focusPart == 1 || focusPart == 2)) {
            Pen focus(design::color(palette.accent, 170), 1);
            focus.SetDashStyle(DashStyleDash);
            g.DrawRectangle(&focus, focusPart == 1 ? width - 80 : width - 44, 6.f, 30.f, 30.f);
        }
        if (!b.collapsed) {
            if (b.kind == box::Kind::Inbox) {
                for (int k = 0; k < 2; ++k) {
                    bool active = b.inboxAll == (k == 1);
                    if (active) {
                        GraphicsPath pill;
                        design::rounded(pill, 14.f + k * 82, 44, 76, 28, 7);
                        SolidBrush fill(design::color(palette.accent, 22));
                        g.FillPath(&fill, &pill);
                    }
                    g.DrawString(k ? L"全部" : L"未分类", -1, &small, PointF(27.f + k * 82, 50),
                                 active ? &text : &secondary);
                }
                bool hasNew =
                    std::any_of(b.items.begin(), b.items.end(), [](auto &i) { return i.fresh; });
                if (hasNew)
                    g.DrawString(L"查看新收件", -1, &small, RectF(width - 110, 50, 100, 22), &line,
                                 &accent);
            }
            int top = contentY();
            g.SetClip(RectF(0, (float)top, width, std::max(0.f, height - top)),
                      CombineModeIntersect);
            int cw = design::cellWidth(b), ch = design::cellHeight(b),
                cols = std::max(1, (dip(w) - 2 * Gap) / cw),
                rows = std::max(1, (dip(h) - top - 12) / ch);
            int maxScroll = std::max(0, ((int)list.size() + cols - 1) / cols - rows);
            if (!loading)
                b.scroll = std::min(b.scroll, maxScroll);
            int first = b.scroll * cols,
                last = std::min((int)list.size(), first + (rows + 1) * cols);
            for (int pos = first; pos < last; ++pos) {
                int index = list[pos];
                if (index < 0 || index >= (int)b.items.size() || index >= (int)labels.size())
                    continue;
                auto &item = b.items[index];
                float x = float(Gap + (pos % cols) * cw),
                      y = float(top + 12 + (pos / cols - b.scroll) * ch), tw = float(cw - 8);
                if (index == selected || index == hovered) {
                    GraphicsPath tile;
                    design::rounded(tile, x, y - 3, tw, (float)(ch - 5), 8);
                    SolidBrush fill(design::color(palette.accent, index == selected ? 35 : 16));
                    g.FillPath(&fill, &tile);
                    if (index == selected) {
                        Pen selectedPen(design::color(palette.accent, highContrast ? 255 : 100), 1);
                        g.DrawPath(&selectedPen, &tile);
                    }
                }
                if (focused && focusPart == 3 && index == selected) {
                    Pen focus(design::color(palette.accent, 180), 1);
                    focus.SetDashStyle(DashStyleDash);
                    g.DrawRectangle(&focus, x + 2, y - 1, tw - 4, (float)(ch - 9));
                }
                auto icon = actualIcons.find(item.path);
                if (icon != actualIcons.end() && icon->second && icon->second->icon) {
                    iconAges[item.path] = GetTickCount64();
                    auto bmp = iconBitmaps.find(item.path);
                    if (bmp == iconBitmaps.end())
                        bmp = iconBitmaps
                                  .emplace(item.path, std::make_unique<Bitmap>(icon->second->icon))
                                  .first;
                    g.DrawImage(bmp->second.get(),
                                RectF(x + (tw - b.icon) / 2, y, (float)b.icon, (float)b.icon));
                } else {
                    if (icon == actualIcons.end() && app->iconLoader &&
                        !requestedIcons.count(item.path)) {
                        if (app->iconLoader->submit(item.path, IconJob{item.path}))
                            requestedIcons.insert(item.path);
                        else
                            pendingIconRequests = true;
                    }
                    Bitmap fallback(LoadIconW(nullptr, IDI_APPLICATION));
                    g.DrawImage(&fallback,
                                RectF(x + (tw - b.icon) / 2, y, (float)b.icon, (float)b.icon));
                }
                StringFormat label;
                label.SetTrimming(StringTrimmingEllipsisCharacter);
                label.SetAlignment(StringAlignmentCenter);
                g.DrawString(labels[index].c_str(), -1, &bodyFont,
                             RectF(x, y + b.icon + 5, tw, (float)std::max(32, int(b.font) * 2 + 7)),
                             &label, &text);
                auto status = health.find(item.path);
                if (status != health.end() && status->second == 2)
                    g.DrawString(L"失效", -1, &small, RectF(x + tw - 29, y, 30, 20), &line,
                                 &secondary);
                else if (b.kind == box::Kind::Inbox && item.fresh) {
                    SolidBrush dot(design::color(palette.accent, 210));
                    g.FillEllipse(&dot, x + tw - 10, y + 4, 5.f, 5.f);
                }
            }
            if (list.empty()) {
                const wchar_t *state =
                    loading          ? L"正在整理列表…"
                    : !query.empty() ? L"没有匹配的项目"
                    : b.kind == box::Kind::Inbox
                        ? (app->state.inboxPaused ? L"自动收件已暂停" : L"桌面的新项目会显示在这里")
                        : L"拖入文件，开始整理";
                Pen empty(design::color(palette.secondary, 100), 1.3f);
                float mid = width / 2, yy = top + 42;
                g.DrawLine(&empty, mid - 13, yy, mid - 13, yy + 18);
                g.DrawLine(&empty, mid - 13, yy + 18, mid + 13, yy + 18);
                g.DrawLine(&empty, mid + 13, yy + 18, mid + 13, yy);
                g.DrawLine(&empty, mid - 7, yy + 3, mid + 7, yy + 3);
                StringFormat center;
                center.SetAlignment(StringAlignmentCenter);
                g.DrawString(state, -1, &small, RectF(16, yy + 38, width - 32, 55), &center,
                             &secondary);
            }
            RECT thumb = scrollThumb();
            if (thumb.right > thumb.left) {
                SolidBrush fill(design::color(palette.secondary, 135));
                GraphicsPath bar;
                design::rounded(bar, (float)dip(thumb.left), (float)dip(thumb.top),
                                (float)dip(thumb.right - thumb.left),
                                (float)dip(thumb.bottom - thumb.top), 3);
                g.FillPath(&fill, &bar);
            }
            if (dropFeedback) {
                GraphicsPath target;
                design::rounded(target, 5, (float)top + 4, width - 10, height - top - 9, 10);
                Pen pen(design::color(dropFeedback > 0 ? palette.accent : 0xc58e85, 170), 1.4f);
                pen.SetDashStyle(DashStyleDash);
                g.DrawPath(&pen, &target);
            }
            if (!notice.empty() && GetTickCount64() < noticeUntil) {
                SolidBrush fill(design::color(b.bg, 240));
                g.FillRectangle(&fill, 10.f, height - 32, width - 20, 28.f);
                g.DrawString(notice.c_str(), -1, &small, RectF(16, height - 28, width - 32, 24),
                             &line, &text);
            }
        }
    }
    POINT destination{window.left, window.top}, source{0, 0};
    SIZE size{w, h};
    BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (!UpdateLayeredWindow(hwnd, screen.dc, &destination, &size, surfaceDC, &source, 0, &blend,
                             ULW_ALPHA)) {
        effectFailed = true;
        stop();
    }
}
