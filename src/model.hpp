#pragma once
#include <algorithm>
#include <cstdint>
#include <cstring>
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
constexpr size_t MaxBoxes = 50, MaxItems = 2000, MaxTotal = 10000, MaxConfig = 8 * 1024 * 1024;
struct Item {
    std::string path;
};
struct Box {
    uint32_t id = 0;
    std::string name = "新收纳筐";
    int32_t x = 80, y = 80, w = 380, h = 340, scroll = 0;
    uint32_t bg = 0x26324a, fg = 0xf0f3ff, glowColor = 0x8888ee;
    uint32_t opacity = 190, font = 13, icon = 32, brightness = 24, radius = 140, stars = 12;
    bool collapsed = false, locked = false, through = false, glow = true;
    std::vector<Item> items;
};
struct State {
    uint32_t nextId = 1;
    bool effects = true, animations = true;
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
inline bool samePath(std::string a, std::string b) {
    std::replace(a.begin(), a.end(), '/', '\\');
    std::replace(b.begin(), b.end(), '/', '\\');
#ifdef _WIN32
    auto wide = [](const std::string &s) {
        int n =
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
        std::wstring w(n, 0);
        if (n)
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), w.data(),
                                n);
        return w;
    };
    auto x = wide(a), y = wide(b);
    return !x.empty() && !y.empty() &&
           CompareStringOrdinal(x.data(), (int)x.size(), y.data(), (int)y.size(), TRUE) ==
               CSTR_EQUAL;
#else
    auto lower = [](unsigned char c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; };
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
                                              [&](char x, char y) { return lower(x) == lower(y); });
#endif
}
inline bool add(Box &b, const std::string &p) {
    if (!validPath(p) || b.items.size() >= MaxItems)
        return false;
    for (auto &i : b.items)
        if (samePath(i.path, p))
            return false;
    b.items.push_back({p});
    return true;
}
inline bool transfer(Box &a, Box &b, size_t index) {
    if (a.id == b.id || index >= a.items.size() || !add(b, a.items[index].path))
        return false;
    a.items.erase(a.items.begin() + index);
    return true;
}
inline std::string nameOf(const std::string &p) {
    auto i = p.find_last_of("/\\");
    return p.substr(i == std::string::npos ? 0 : i + 1);
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
    size_t total = 0;
    std::vector<uint32_t> ids;
    for (auto &b : s.boxes) {
        if (!b.id || b.id >= s.nextId || std::find(ids.begin(), ids.end(), b.id) != ids.end())
            throw std::runtime_error("id");
        ids.push_back(b.id);
        if (!validUtf8(b.name) ||
            std::any_of(b.name.begin(), b.name.end(), [](unsigned char c) { return c < 32; }) ||
            b.name.empty() || b.name.size() > 320 || b.w < 240 || b.h < 120 || b.w > 8192 ||
            b.h > 8192 || b.x < -100000 || b.x > 100000 || b.y < -100000 || b.y > 100000 ||
            b.scroll < 0 || b.scroll > 10000 || b.opacity < 16 || b.opacity > 245 || b.font < 10 ||
            b.font > 24 || b.icon < 16 || b.icon > 64 || b.brightness > 60 || b.radius < 40 ||
            b.radius > 300 || b.stars > 24 || b.bg > 0xffffff || b.fg > 0xffffff ||
            b.glowColor > 0xffffff || b.items.size() > MaxItems)
            throw std::runtime_error("bounds");
        total += b.items.size();
        for (size_t i = 0; i < b.items.size(); ++i) {
            if (!validPath(b.items[i].path))
                throw std::runtime_error("path");
            for (size_t j = 0; j < i; ++j)
                if (samePath(b.items[i].path, b.items[j].path))
                    throw std::runtime_error("duplicate");
        }
    }
    if (total > MaxTotal)
        throw std::runtime_error("total");
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
        for (auto &i : b.items)
            w.s(i.path);
    }
    Writer out;
    out.u(0x31425844);
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
    if (r.u() != 0x31425844)
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
        if (count > MaxItems)
            throw std::runtime_error("items");
        for (uint32_t j = 0; j < count; ++j)
            b.items.push_back({r.s(32760)});
        s.boxes.push_back(std::move(b));
    }
    if (r.p != a.size())
        throw std::runtime_error("trailing");
    validate(s);
    return s;
}
} // namespace box
