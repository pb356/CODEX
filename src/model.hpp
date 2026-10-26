#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif
namespace box {
// Storage and integer bounds protect malformed input; no fixed per-box item quota.
constexpr size_t MaxBoxes = 50, MaxConfig = 256 * 1024 * 1024;
enum class Kind : uint32_t { Normal = 0, Inbox = 1 };
struct WallpaperSettings {
    uint32_t blankSide = 2, blankPercent = 40, density = 1, tone = 1;
    uint32_t brightness = 100, saturation = 90, shade = 15, mode = 0, motion = 20;
    bool enabled = false;
};
struct Item {
    std::string path;
    bool fresh = false;
};
struct Box {
    uint32_t id = 0;
    std::string name = "新收纳筐";
    int32_t x = 80, y = 80, w = 380, h = 340, scroll = 0;
    uint32_t bg = 0x28372f, fg = 0xedf2ee, glowColor = 0xb5cdbb;
    uint32_t opacity = 200, font = 13, icon = 40, brightness = 20, radius = 120, stars = 4;
    bool collapsed = false, locked = false, through = false, glow = true;
    Kind kind = Kind::Normal;
    uint32_t theme = 1, effect = 1, sort = 0;
    bool showCount = true, inboxAll = false;
    std::vector<Item> items;
};
struct State {
    uint32_t nextId = 1;
    bool effects = true, animations = true;
    bool inboxPaused = false;
    WallpaperSettings wallpaper;
    std::vector<Box> boxes;
};
inline bool validUtf8(const std::string &text) {
    for (size_t i = 0; i < text.size();) {
        uint32_t c = (unsigned char)text[i++];
        if (c < 0x80)
            continue;
        unsigned count;
        uint32_t minimum;
        if (c >= 0xc2 && c <= 0xdf) {
            count = 1;
            minimum = 0x80;
            c &= 0x1f;
        } else if (c >= 0xe0 && c <= 0xef) {
            count = 2;
            minimum = 0x800;
            c &= 0x0f;
        } else if (c >= 0xf0 && c <= 0xf4) {
            count = 3;
            minimum = 0x10000;
            c &= 7;
        } else
            return false;
        if (count > text.size() - i)
            return false;
        while (count--) {
            auto next = (unsigned char)text[i++];
            if ((next & 0xc0) != 0x80)
                return false;
            c = (c << 6) | (next & 0x3f);
        }
        if (c < minimum || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
            return false;
    }
    return true;
}
inline bool validPath(const std::string &p) {
    if (!validUtf8(p) || p.size() < 3 || p.size() > 32760 ||
        p.find_first_of("\r\n\t") != std::string::npos || p.find('\0') != std::string::npos)
        return false;
    if (p.rfind("\\\\?", 0) == 0 || p.rfind("\\\\.", 0) == 0)
        return false;
    bool drive = ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':' &&
                 (p[2] == '\\' || p[2] == '/');
    bool unc = p.rfind("\\\\", 0) == 0 && p.find('\\', 2) != std::string::npos;
    if (!drive && !unc)
        return false;
    for (size_t i = drive ? 2 : 0; i < p.size(); ++i)
        if ((unsigned char)p[i] < 32 || p[i] == '*' || p[i] == '?' || p[i] == '"' || p[i] == '<' ||
            p[i] == '>' || p[i] == '|' || p[i] == ':')
            return false;
    return true;
}
#ifdef _WIN32
using PathKey = std::wstring;
inline PathKey pathKey(std::string p) {
    std::replace(p.begin(), p.end(), '/', '\\');
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, p.data(), (int)p.size(), nullptr, 0);
    if (!n && !p.empty())
        throw std::runtime_error("UTF8 path");
    PathKey result(n, 0);
    if (n)
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, p.data(), (int)p.size(), result.data(),
                            n);
    return result;
}
struct PathLess {
    bool operator()(const PathKey &a, const PathKey &b) const {
        return CompareStringOrdinal(a.data(), (int)a.size(), b.data(), (int)b.size(), TRUE) ==
               CSTR_LESS_THAN;
    }
};
#else
using PathKey = std::string;
inline PathKey pathKey(std::string p) {
    for (char &c : p) {
        if (c == '/')
            c = '\\';
        if (c >= 'A' && c <= 'Z')
            c += 32;
    }
    return p;
}
struct PathLess {
    bool operator()(const PathKey &a, const PathKey &b) const {
        return a < b;
    }
};
#endif
using PathSet = std::set<PathKey, PathLess>;
inline bool samePath(const std::string &a, const std::string &b) {
    auto x = pathKey(a), y = pathKey(b);
    PathLess less;
    return !less(x, y) && !less(y, x);
}
inline PathSet itemKeys(const Box &b) {
    PathSet keys;
    for (auto &i : b.items)
        keys.insert(pathKey(i.path));
    return keys;
}
inline bool classified(const State &s, const std::string &path) {
    for (auto &b : s.boxes)
        if (b.kind == Kind::Normal)
            for (auto &i : b.items)
                if (samePath(i.path, path))
                    return true;
    return false;
}
inline bool ignoredDesktopName(std::string name) {
    for (char &c : name)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    auto ends = [&](const char *suffix) {
        std::string x = suffix;
        return name.size() >= x.size() && name.compare(name.size() - x.size(), x.size(), x) == 0;
    };
    return name == "desktop.ini" || name == "thumbs.db" || name.rfind("~$", 0) == 0 ||
           name.rfind(".~lock.", 0) == 0 || name.rfind(".temporaryitems", 0) == 0 ||
           ends(".crdownload") || ends(".partial") || ends(".download") || ends(".tmp") ||
           ends(".temp") || ends(".part");
}
inline void appearance(Box &target, const Box &source) {
    target.name = source.name;
    target.bg = source.bg;
    target.fg = source.fg;
    target.glowColor = source.glowColor;
    target.opacity = source.opacity;
    target.font = source.font;
    target.icon = source.icon;
    target.brightness = source.brightness;
    target.radius = source.radius;
    target.stars = source.stars;
    target.glow = source.glow;
    target.theme = source.theme;
    target.effect = source.effect;
    target.showCount = source.showCount;
    target.locked = source.locked;
}
inline bool add(Box &b, const std::string &p) {
    if (!validPath(p))
        return false;
    for (auto &i : b.items)
        if (samePath(i.path, p))
            return false;
    b.items.push_back({p});
    return true;
}
inline bool transfer(Box &a, Box &b, size_t index) {
    if (a.id == b.id || index >= a.items.size())
        return false;
    if (b.kind == Kind::Inbox) {
        bool inDesktop = false;
        for (auto &i : b.items)
            if (samePath(i.path, a.items[index].path)) {
                inDesktop = true;
                break;
            }
        if (!inDesktop || a.kind == Kind::Inbox)
            return false;
        a.items.erase(a.items.begin() + index);
        return true;
    }
    if (!add(b, a.items[index].path))
        return false;
    if (a.kind != Kind::Inbox)
        a.items.erase(a.items.begin() + index);
    return true;
}
inline std::string nameOf(const std::string &p) {
    auto i = p.find_last_of("/\\");
    return i == p.size() - 1 ? p : p.substr(i == std::string::npos ? 0 : i + 1);
}
inline uint32_t crc(const std::vector<uint8_t> &a) {
    uint32_t c = ~0u;
    for (auto b : a) {
        c ^= b;
        for (int i = 0; i < 8; ++i)
            c = (c >> 1) ^ (0xedb88320u & -(c & 1));
    }
    return ~c;
}
struct Writer {
    std::vector<uint8_t> a;
    void u(uint32_t n) {
        for (int i = 0; i < 4; ++i)
            a.push_back(uint8_t(n >> (i * 8)));
    }
    void s(const std::string &s) {
        u((uint32_t)s.size());
        a.insert(a.end(), s.begin(), s.end());
    }
};
struct Reader {
    const std::vector<uint8_t> &a;
    size_t p = 0;
    uint32_t u() {
        if (a.size() - p < 4)
            throw std::runtime_error("truncated");
        uint32_t n = 0;
        for (int i = 0; i < 4; ++i)
            n |= uint32_t(a[p++]) << (8 * i);
        return n;
    }
    std::string s(size_t max) {
        size_t n = u();
        if (n > max || n > a.size() - p)
            throw std::runtime_error("length");
        std::string s((const char *)a.data() + p, n);
        p += n;
        return s;
    }
    bool flag() {
        auto v = u();
        if (v > 1)
            throw std::runtime_error("flag");
        return v != 0;
    }
};
inline void validate(const State &s) {
    if (s.boxes.size() > MaxBoxes || !s.nextId)
        throw std::runtime_error("boxes");
    unsigned inboxes = 0;
    std::vector<uint32_t> ids;
    for (auto &b : s.boxes) {
        if (!b.id || b.id >= s.nextId || std::find(ids.begin(), ids.end(), b.id) != ids.end())
            throw std::runtime_error("id");
        ids.push_back(b.id);
        if (!validUtf8(b.name) ||
            std::any_of(b.name.begin(), b.name.end(), [](unsigned char c) { return c < 32; }) ||
            b.name.empty() || b.name.size() > 320 || b.w < 240 || b.h < 120 || b.w > 8192 ||
            b.h > 8192 || b.x < -100000 || b.x > 100000 || b.y < -100000 || b.y > 100000 ||
            b.scroll < 0 || b.scroll > INT32_MAX - 1 || b.opacity < 16 || b.opacity > 245 ||
            b.font < 10 || b.font > 24 || b.icon < 16 || b.icon > 64 || b.brightness > 60 ||
            b.radius < 40 || b.radius > 300 || b.stars > 24 || b.bg > 0xffffff || b.fg > 0xffffff ||
            b.glowColor > 0xffffff || b.theme > 3 || b.effect > 2 || b.sort > 2 ||
            uint32_t(b.kind) > 1)
            throw std::runtime_error("bounds");
        if (b.kind == Kind::Inbox && ++inboxes > 1)
            throw std::runtime_error("multiple inboxes");
        PathSet keys;
        for (auto &item : b.items) {
            if (!validPath(item.path))
                throw std::runtime_error("path");
            if (!keys.insert(pathKey(item.path)).second)
                throw std::runtime_error("duplicate");
        }
    }
    auto &w = s.wallpaper;
    if (w.blankSide > 2 || w.blankPercent < 25 || w.blankPercent > 55 || w.density > 2 ||
        w.tone > 2 || w.brightness > 150 || w.saturation > 150 || w.shade > 70 || w.mode > 2 ||
        w.motion > 100)
        throw std::runtime_error("wallpaper settings");
}

inline std::vector<uint8_t> encode(const State &s) {
    validate(s);
    Writer w;
    w.u(s.nextId);
    w.u(s.effects);
    w.u(s.animations);
    w.u((uint32_t)s.boxes.size());
    for (auto &b : s.boxes) {
        w.u(b.id);
        w.s(b.name);
        w.u(b.x);
        w.u(b.y);
        w.u(b.w);
        w.u(b.h);
        w.u(b.scroll);
        w.u(b.bg);
        w.u(b.fg);
        w.u(b.glowColor);
        w.u(b.opacity);
        w.u(b.font);
        w.u(b.icon);
        w.u(b.brightness);
        w.u(b.radius);
        w.u(b.stars);
        w.u(b.collapsed);
        w.u(b.locked);
        w.u(b.through);
        w.u(b.glow);
        w.u((uint32_t)b.items.size());
        for (auto &i : b.items) {
            w.s(i.path);
            w.u(i.fresh);
        }
        w.u(uint32_t(b.kind));
        w.u(b.theme);
        w.u(b.effect);
        w.u(b.sort);
        w.u(b.showCount);
        w.u(b.inboxAll);
    }
    w.u(s.inboxPaused);
    auto &wp = s.wallpaper;
    w.u(wp.blankSide);
    w.u(wp.blankPercent);
    w.u(wp.density);
    w.u(wp.tone);
    w.u(wp.brightness);
    w.u(wp.saturation);
    w.u(wp.shade);
    w.u(wp.mode);
    w.u(wp.motion);
    w.u(wp.enabled);
    Writer out;
    out.u(0x32425844);
    out.u(crc(w.a));
    out.a.insert(out.a.end(), w.a.begin(), w.a.end());
    if (out.a.size() > MaxConfig)
        throw std::runtime_error("size");
    return out.a;
}
inline State decode(const std::vector<uint8_t> &a) {
    if (a.size() < 8 || a.size() > MaxConfig)
        throw std::runtime_error("size");
    Reader r{a};
    auto magic = r.u();
    bool v2 = magic == 0x32425844;
    if (!v2 && magic != 0x31425844)
        throw std::runtime_error("version");
    auto checksum = r.u();
    std::vector<uint8_t> payload(a.begin() + 8, a.end());
    if (crc(payload) != checksum)
        throw std::runtime_error("checksum");
    State s;
    s.nextId = r.u();
    s.effects = r.flag();
    s.animations = r.flag();
    auto n = r.u();
    if (n > MaxBoxes)
        throw std::runtime_error("count");
    for (uint32_t k = 0; k < n; ++k) {
        Box b;
        b.id = r.u();
        b.name = r.s(320);
        b.x = (int32_t)r.u();
        b.y = (int32_t)r.u();
        b.w = (int32_t)r.u();
        b.h = (int32_t)r.u();
        b.scroll = (int32_t)r.u();
        b.bg = r.u();
        b.fg = r.u();
        b.glowColor = r.u();
        b.opacity = r.u();
        b.font = r.u();
        b.icon = r.u();
        b.brightness = r.u();
        b.radius = r.u();
        b.stars = r.u();
        b.collapsed = r.flag();
        b.locked = r.flag();
        b.through = r.flag();
        b.glow = r.flag();
        auto count = r.u();
        if (count > (a.size() - r.p) / 4)
            throw std::runtime_error("items");
        b.items.reserve(count);
        for (uint32_t j = 0; j < count; ++j) {
            Item item;
            item.path = r.s(32760);
            if (v2)
                item.fresh = r.flag();
            b.items.push_back(std::move(item));
        }
        if (v2) {
            b.kind = Kind(r.u());
            b.theme = r.u();
            b.effect = r.u();
            b.sort = r.u();
            b.showCount = r.flag();
            b.inboxAll = r.flag();
        } else {
            b.theme = 3;
            b.effect = b.glow ? 1 : 0;
        }
        s.boxes.push_back(std::move(b));
    }
    if (v2) {
        s.inboxPaused = r.flag();
        auto &wp = s.wallpaper;
        wp.blankSide = r.u();
        wp.blankPercent = r.u();
        wp.density = r.u();
        wp.tone = r.u();
        wp.brightness = r.u();
        wp.saturation = r.u();
        wp.shade = r.u();
        wp.mode = r.u();
        wp.motion = r.u();
        wp.enabled = r.flag();
    }
    if (r.p != a.size())
        throw std::runtime_error("trailing");
    validate(s);
    return s;
}
} // namespace box
