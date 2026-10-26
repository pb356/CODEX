#pragma once
#include "platform.hpp"
#include <commctrl.h>
#include <uxtheme.h>
namespace design {
constexpr int Radius = 14, Padding = 16, TitleHeight = 42;
struct Palette {
    uint32_t surface, text, secondary, accent, glow;
    bool light;
};
inline bool highContrast() {
    HIGHCONTRASTW value{sizeof(value), 0, nullptr};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(value), &value, 0) &&
           (value.dwFlags & HCF_HIGHCONTRASTON);
}
inline uint32_t systemRgb(int index) {
    auto c = GetSysColor(index);
    return (uint32_t(GetRValue(c)) << 16) | (uint32_t(GetGValue(c)) << 8) | GetBValue(c);
}
inline Palette palette(uint32_t theme) {
    if (theme == 0)
        return {0xf1f0e9, 0x26332d, 0x60736a, 0x486f59, 0xb9cbbd, true};
    if (theme == 2)
        return {0x303e4c, 0xebeff4, 0xaab8c5, 0xb7cddd, 0xa5bed4, false};
    return {0x28372f, 0xedf2ee, 0xa5b5ad, 0xb5cdbb, 0xa7c2b1, false};
}
inline void theme(box::Box &b, uint32_t id) {
    auto p = palette(id);
    b.theme = id;
    b.bg = p.surface;
    b.fg = p.text;
    b.glowColor = p.glow;
}
inline Palette effectivePalette(const box::Box &b) {
    if (highContrast())
        return {systemRgb(COLOR_WINDOW),     systemRgb(COLOR_WINDOWTEXT),
                systemRgb(COLOR_WINDOWTEXT), systemRgb(COLOR_HIGHLIGHT),
                systemRgb(COLOR_HIGHLIGHT),  true};
    auto p = palette(b.theme);
    p.surface = b.bg;
    p.text = b.fg;
    p.glow = b.glowColor;
    return p;
}
inline Gdiplus::Color color(uint32_t rgb, BYTE alpha = 255) {
    return Gdiplus::Color(alpha, (BYTE)(rgb >> 16), (BYTE)(rgb >> 8), (BYTE)rgb);
}
inline void rounded(Gdiplus::GraphicsPath &path, float x, float y, float w, float h, float radius) {
    float d = std::min({radius * 2, w, h});
    if (d <= 0)
        return;
    path.AddArc(x, y, d, d, 180, 90);
    path.AddArc(x + w - d, y, d, d, 270, 90);
    path.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    path.AddArc(x, y + h - d, d, d, 90, 90);
    path.CloseFigure();
}
inline int cellWidth(const box::Box &b) {
    return std::max(92, int(b.icon) + 54);
}
inline int cellHeight(const box::Box &b) {
    return int(b.icon) + std::max(42, int(b.font) * 2 + 12);
}
inline int contentTop(const box::Box &b) {
    return TitleHeight + (b.kind == box::Kind::Inbox ? 34 : 0);
}
inline bool roundedContains(int x, int y, int width, int height) {
    if (x < 0 || y < 0 || x >= width || y >= height)
        return false;
    int cx = x < Radius ? Radius : (x > width - Radius ? width - Radius : x);
    int cy = y < Radius ? Radius : (y > height - Radius ? height - Radius : y);
    int dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= Radius * Radius;
}
inline bool settle(float &current, float target, float seconds) {
    float d = target - current;
    if (std::abs(d) < 0.25f) {
        current = target;
        return true;
    }
    current += d * (1 - std::exp(-seconds / 0.045f));
    return false;
}
} // namespace design
