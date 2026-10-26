#include "../src/leaf_motion.hpp"
#include "../src/model.hpp"
#include "../src/playback_policy.hpp"
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
        test("leaf clocks freeze across long pauses and deterministic motion remains gentle", [] {
            foliage::Clock clock;
            clock.setRunning(true, 1000);
            require(clock.seconds(2500) == 1.5, "active animation time");
            clock.setRunning(false, 2500);
            require(clock.seconds(999999) == 1.5, "pause freezes pose");
            clock.setRunning(true, 999999);
            require(clock.seconds(1000499) == 2.0, "resume excludes paused wall time");
            for (unsigned k = 0; k < 3; ++k)
                for (int ms = 0; ms < 14000; ms += 10)
                    require(std::isfinite(foliage::angle(k, ms / 1000.0)) &&
                                std::abs(foliage::angle(k, ms / 1000.0)) <= 1.61,
                            "angle bounded without frame randomization");
            require(foliage::angle(0, 0) != foliage::angle(1, 0), "independent phases");
        });
        test("independent pause reasons survive interleaved unlock, wake and visibility changes",
             [] {
                 playback::Gate gate;
                 using playback::Reason;
                 using playback::Mode;
                 require(playback::frameInterval(Mode::Standard, false, gate) == 34,
                         "30fps upper bound");
                 require(playback::frameInterval(Mode::Energy, false, gate) == 67,
                         "energy upper bound");
                 require(playback::frameInterval(Mode::Standard, true, gate) == 0,
                         "reduced motion static");
                 gate.set(Reason::Locked, true);
                 gate.set(Reason::Sleeping, true);
                 gate.set(Reason::Sleeping, false);
                 require(gate.paused(), "wake must not undo lock pause");
                 gate.set(Reason::Remote, true);
                 gate.set(Reason::Locked, false);
                 require(playback::frameInterval(Mode::Standard, false, gate) == 0,
                         "remote remains paused");
                 gate.set(Reason::Remote, false);
                 gate.set(Reason::Covered, true);
                 gate.set(Reason::Hidden, true);
                 gate.set(Reason::Covered, false);
                 require(gate.paused(), "uncover must not undo hidden pause");
                 gate.set(Reason::Hidden, false);
                 require(!gate.paused(), "all independent reasons cleared");
                 require(playback::frameInterval(Mode::Static, false, gate) == 0,
                         "static never starts timer");
             });
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
        test("one box accepts more than ten thousand references without a fixed quota", [] {
            box::State state;
            state.nextId = 2;
            box::Box b;
            b.id = 1;
            for (int n = 0; n < 11000; ++n)
                b.items.push_back({"C:\\Temp\\" + std::to_string(n) + ".txt"});
            require(box::add(b, "C:\\Temp\\beyond-11000.txt"), "no per-box quota");
            state.boxes.push_back(std::move(b));
            auto bytes = box::encode(state);
            auto restored = box::decode(bytes);
            require(restored.boxes[0].items.size() == 11001, "complete large restore");
            require(restored.boxes[0].w == 380 && restored.boxes[0].h == 340,
                    "capacity does not resize box");
        });
        test("legacy v1 fixture migrates without changing geometry, custom sizes or references",
             [] {
                 auto path =
                     std::filesystem::path(__FILE__).parent_path() / "fixtures" / "legacy-v1.db";
                 std::ifstream input(path, std::ios::binary);
                 require(bool(input), "legacy fixture exists");
                 std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(input), {}};
                 auto state = box::decode(bytes);
                 require(state.boxes.size() == 2 && state.nextId == 3, "legacy boxes");
                 auto &b = state.boxes[0];
                 require(b.x == -540 && b.y == 120 && b.w == 560 && b.h == 480 && b.scroll == 7 &&
                             b.collapsed && b.locked,
                         "legacy layout exact");
                 require(b.icon == 36 && b.font == 15 && b.opacity == 173,
                         "custom appearance preserved");
                 require(b.items[0].path == "C:\\旧项目\\中文 文件.txt" && state.boxes[1].through,
                         "legacy refs and interaction");
                 auto current = box::encode(state);
                 require(box::decode(current).boxes[0].items[1].path == b.items[1].path,
                         "v2 re-encode");
             });
        test("inbox classification retains inventory; removing final classification restores "
             "unclassified status",
             [] {
                 auto state = sample();
                 state.boxes[0].kind = box::Kind::Inbox;
                 auto path = state.boxes[0].items[1].path;
                 require(!box::classified(state, path), "initially unclassified");
                 auto count = state.boxes[0].items.size();
                 require(box::transfer(state.boxes[0], state.boxes[1], 1),
                         "classify desktop reference");
                 require(state.boxes[0].items.size() == count && box::classified(state, path),
                         "inventory remains");
                 require(
                     box::transfer(state.boxes[1], state.boxes[0], state.boxes[1].items.size() - 1),
                     "return reference to inbox");
                 require(!box::classified(state, path) && state.boxes[0].items.size() == count,
                         "last classification removed only");
                 require(box::add(state.boxes[1], "D:\\Other\\not-on-desktop.txt"),
                         "non-desktop reference");
                 require(!box::transfer(state.boxes[1], state.boxes[0],
                                        state.boxes[1].items.size() - 1),
                         "inbox cannot pretend to move a non-desktop file");
             });
        test("temporary desktop entries filtered without treating a zero byte final file as "
             "temporary",
             [] {
                 for (auto name :
                      {"desktop.ini", "Thumbs.db", "~$document.docx", "file.crdownload",
                       "file.part", "file.partial", "file.tmp", ".~lock.document.docx#"})
                     require(box::ignoredDesktopName(name), "temp filter");
                 for (auto name :
                      {"合法空文件.txt", "final.docx", "项目目录", "应用快捷方式.lnk", "notes.md"})
                     require(!box::ignoredDesktopName(name), "valid final entry");
             });
        test("appearance preview transaction leaves geometry, item paths and inventory flags "
             "untouched",
             [] {
                 auto state = sample();
                 auto original = state.boxes[0];
                 auto changed = original;
                 changed.name = "预览名称";
                 changed.icon = 52;
                 changed.bg = 0xf1f0e9;
                 changed.collapsed = true;
                 changed.items.clear();
                 changed.x = 999;
                 changed.kind = box::Kind::Inbox;
                 box::appearance(state.boxes[0], changed);
                 require(state.boxes[0].items.size() == original.items.size() &&
                             state.boxes[0].items[0].path == original.items[0].path,
                         "appearance never changes references");
                 require(state.boxes[0].x == original.x && state.boxes[0].w == original.w &&
                             !state.boxes[0].collapsed && state.boxes[0].kind == box::Kind::Normal,
                         "geometry and kind unchanged");
                 box::appearance(state.boxes[0], original);
                 require(box::encode(state) == box::encode(sample()), "cancel rollback exact");
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
