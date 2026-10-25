#include "../src/model.hpp"
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <random>
static int passed = 0;
void require(bool ok, const char *why) {
    if (!ok)
        throw std::runtime_error(why);
}
void test(const char *name, const std::function<void()> &fn) {
    fn();
    ++passed;
    std::cout << "PASS " << name << '\n';
}
void rejects(const std::function<void()> &fn) {
    bool failed = false;
    try {
        fn();
    } catch (const std::exception &) {
        failed = true;
    }
    require(failed, "expected rejection");
}
box::State sample() {
    box::State s;
    s.nextId = 3;
    box::Box a, b;
    a.id = 1;
    b.id = 2;
    a.name = "植物素材";
    b.name = "报价合同";
    a.items = {{"C:\\项目 空格\\方案 & (1).txt"}, {"C:\\Temp\\safe.lnk"}};
    b.items = {{"C:\\项目 空格\\方案 & (1).txt"}};
    s.boxes = {a, b};
    return s;
}
int main() {
    try {
        test("file paths accept Unicode and UNC; reject protocols/device/ADS/control", [] {
            require(box::validPath("C:\\中文 & space\\合同.txt"), "unicode");
            require(box::validPath("\\\\server\\share\\x"), "UNC");
            for (auto p : {"https://example.org", "shell:AppsFolder", "relative.txt", "C:relative",
                           "\\\\?\\C:\\file", "\\\\.\\PhysicalDrive0", "C:\\a:b", "C:\\*.txt",
                           "C:\\bad\nname", "C:\\bad\"name"})
                require(!box::validPath(p), p);
        });
        test("malformed Unicode in configuration rejected", [] {
            require(box::validUtf8("中文\xf0\x9f\x8c\x9f"), "valid UTF8");
            for (auto bad : {std::string("\xc0\xaf"), std::string("\xed\xa0\x80"),
                             std::string("\xf4\x90\x80\x80"), std::string("\xe4\xb8")}) {
                require(!box::validUtf8(bad), "invalid Unicode");
                require(!box::validPath("C:\\Temp\\" + bad), "invalid path encoding");
                auto s = sample();
                s.boxes[0].name = bad;
                rejects([&] { box::encode(s); });
            }
            auto s = sample();
            s.boxes[0].name = std::string("nul\0name", 8);
            rejects([&] { box::encode(s); });
        });
        test("per-box dedup, cross-box references permitted", [] {
            auto s = sample();
            require(!box::add(s.boxes[0], "c:/temp/SAFE.LNK"), "dedup");
            require(box::add(s.boxes[1], "c:/temp/SAFE.LNK"), "cross-box allowed");
            require(!box::add(s.boxes[0], "relative"), "invalid add");
        });
        test("failed transfer preserves source and target", [] {
            auto s = sample();
            auto before = box::encode(s);
            require(!box::transfer(s.boxes[0], s.boxes[1], 0), "duplicate move");
            require(box::encode(s) == before, "transaction");
            require(!box::transfer(s.boxes[0], s.boxes[0], 0), "same box");
            require(!box::transfer(s.boxes[0], s.boxes[1], 100), "invalid index");
        });
        test("successful transfer changes ownership only", [] {
            auto s = sample();
            auto path = s.boxes[0].items[1].path;
            require(box::transfer(s.boxes[0], s.boxes[1], 1), "transfer");
            require(s.boxes[0].items.size() == 1 && s.boxes[1].items.back().path == path,
                    "path invariant");
        });
        test("collapsed states, geometry, scroll, order and settings roundtrip independently", [] {
            auto s = sample();
            auto other = box::encode(s);
            s.boxes[0].collapsed = true;
            s.boxes[0].scroll = 7;
            s.boxes[0].w = 560;
            s.boxes[0].h = 480;
            s.boxes[0].x = -800;
            s.boxes[0].through = true;
            s.boxes[0].locked = true;
            s.effects = false;
            s.animations = false;
            auto bytes = box::encode(s);
            auto t = box::decode(bytes);
            require(box::encode(t) == bytes, "roundtrip");
            require(!t.boxes[1].collapsed && t.boxes[1].x == 80, "independent");
            require(t.boxes[0].items[0].path == sample().boxes[0].items[0].path, "path unchanged");
            require(bytes != other, "actual difference");
        });
        test("all-collapse/all-expand preserve layout and references", [] {
            auto s = sample();
            auto before = s;
            for (auto &b : s.boxes)
                b.collapsed = true;
            for (auto &b : s.boxes)
                b.collapsed = false;
            require(box::encode(before) == box::encode(s), "layout unchanged");
        });
        test("configuration catches every truncation and single-byte corruption", [] {
            auto bytes = box::encode(sample());
            for (size_t i = 0; i < bytes.size(); ++i) {
                std::vector<uint8_t> cut(bytes.begin(), bytes.begin() + i);
                rejects([&] { box::decode(cut); });
                auto corrupt = bytes;
                corrupt[i] ^= 0x80;
                rejects([&] { box::decode(corrupt); });
            }
        });
        test("oversized, trailing data, invalid flags and counts rejected", [] {
            auto bytes = box::encode(sample());
            bytes.push_back(0);
            rejects([&] { box::decode(bytes); });
            std::vector<uint8_t> huge(box::MaxConfig + 1);
            rejects([&] { box::decode(huge); });
            auto rebuild = [](std::vector<uint8_t> &b) {
                std::vector<uint8_t> p(b.begin() + 8, b.end());
                auto crc = box::crc(p);
                for (int i = 0; i < 4; ++i)
                    b[4 + i] = uint8_t(crc >> (8 * i));
            };
            bytes = box::encode(sample());
            bytes[12] = 2;
            rebuild(bytes);
            rejects([&] { box::decode(bytes); });
            bytes = box::encode(sample());
            bytes[20] = 255;
            rebuild(bytes);
            rejects([&] { box::decode(bytes); });
        });
        test("geometry, effects, duplicate IDs and duplicate paths bounded", [] {
            auto s = sample();
            s.boxes[0].stars = 25;
            rejects([&] { box::encode(s); });
            s = sample();
            s.boxes[0].w = 0;
            rejects([&] { box::encode(s); });
            s = sample();
            s.boxes[0].x = INT32_MIN;
            rejects([&] { box::encode(s); });
            s = sample();
            s.boxes[1].id = 1;
            rejects([&] { box::encode(s); });
            s = sample();
            s.boxes[0].items.push_back(s.boxes[0].items[0]);
            rejects([&] { box::encode(s); });
        });
        test("item limits and global limits", [] {
            auto s = sample();
            s.boxes[0].items.clear();
            for (size_t i = 0; i < box::MaxItems; ++i)
                require(box::add(s.boxes[0], "C:\\Temp\\" + std::to_string(i)), "within limit");
            require(!box::add(s.boxes[0], "C:\\Temp\\overflow"), "limit");
            s.boxes.clear();
            for (uint32_t k = 1; k <= 6; ++k) {
                auto b = sample().boxes[0];
                b.id = k;
                b.items.clear();
                for (int i = 0; i < 1700; ++i)
                    b.items.push_back({"C:\\Temp\\" + std::to_string(i)});
                s.boxes.push_back(b);
            }
            s.nextId = 7;
            rejects([&] { box::encode(s); });
        });
        test("seeded decoder fuzz (10000 bounded malformed records)", [] {
            std::mt19937 rng(356);
            for (int i = 0; i < 10000; ++i) {
                std::vector<uint8_t> bytes(rng() % 1024);
                for (auto &b : bytes)
                    b = uint8_t(rng());
                try {
                    auto s = box::decode(bytes);
                    box::validate(s);
                } catch (const std::exception &) {
                }
            }
        });
        test("5 boxes / 200 references serialize without changing path or order", [] {
            box::State s;
            s.nextId = 6;
            for (uint32_t i = 1; i <= 5; ++i) {
                box::Box b;
                b.id = i;
                for (int j = 0; j < 40; ++j)
                    b.items.push_back(
                        {"C:\\Temp\\" + std::to_string(i) + "-" + std::to_string(j) + ".txt"});
                s.boxes.push_back(b);
            }
            require(box::encode(box::decode(box::encode(s))) == box::encode(s), "fixture");
        });
        test("real temporary file remains unchanged during reference operations", [] {
            namespace fs = std::filesystem;
            auto dir = fs::temp_directory_path() /
                       ("desktopboxes-core-" + std::to_string(std::random_device{}()));
            fs::create_directory(dir);
            auto path = dir / "original.txt";
            std::string content = "original bytes\n中文\0tail";
            {
                std::ofstream f(path, std::ios::binary);
                f.write(content.data(), content.size());
            }
            auto modified = fs::last_write_time(path);
            auto s = sample();
            box::add(s.boxes[0], "C:\\temporary-test\\original.txt");
            box::transfer(s.boxes[0], s.boxes[1], 2);
            s = box::decode(box::encode(s));
            s.boxes.clear();
            std::ifstream f(path, std::ios::binary);
            std::string actual{std::istreambuf_iterator<char>(f), {}};
            require(actual == content && fs::last_write_time(path) == modified, "file unchanged");
            f.close();
            fs::remove_all(dir);
        });
        std::cout << passed << " test groups passed; zero skipped\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
