#include "../src/platform.hpp"
#include <iostream>
void check(bool b, const char *why) {
    if (!b)
        throw std::runtime_error(why);
}
int wmain() {
    using namespace desktop;
    std::wstring dir;
    try {
        wchar_t temp[32768];
        check(GetTempPathW(32768, temp) > 0, "temp path");
        dir = std::wstring(temp) + L"DesktopBoxesTests-" + std::to_wstring(GetCurrentProcessId()) +
              L"-" + std::to_wstring(GetTickCount64());
        check(CreateDirectoryW(dir.c_str(), nullptr) != 0, "temp dir");
        auto file = dir + L"\\原文件 & (空格).txt";
        HANDLE h = CreateFileW(file.c_str(), GENERIC_WRITE,
                               FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(h != INVALID_HANDLE_VALUE, "temp file");
        const BYTE content[] = {0, 1, 2, 3, 127, 128, 255};
        DWORD n;
        check(WriteFile(h, content, sizeof(content), &n, nullptr) && n == sizeof(content),
              "write temp");
        CloseHandle(h);
        box::State s;
        s.nextId = 3;
        box::Box a, b;
        a.id = 1;
        b.id = 2;
        s.boxes = {a, b};
        auto p = canonical(file);
        check(box::add(s.boxes[0], p), "add");
        check(!box::add(s.boxes[0], canonical(file)), "dedup");
        check(writeConfig(s, dir), "first atomic write");
        s.boxes[0].collapsed = true;
        s.boxes[0].scroll = 3;
        check(writeConfig(s, dir), "replace and backup");
        std::vector<uint8_t> bytes;
        check(readBytes(dir + L"\\state.db", bytes), "read state");
        auto state = box::decode(bytes);
        check(state.boxes[0].collapsed && state.boxes[0].scroll == 3, "restart model");
        check(readBytes(dir + L"\\state.bak", bytes), "read backup");
        check(!box::decode(bytes).boxes[0].collapsed, "prior state retained");
        check(box::transfer(s.boxes[0], s.boxes[1], 0), "transfer");
        check(writeConfig(s, dir), "save transfer");
        s.boxes.clear();
        check(writeConfig(s, dir), "remove all references");
        h = CreateFileW(file.c_str(), GENERIC_READ,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(h != INVALID_HANDLE_VALUE, "original still exists");
        BYTE actual[sizeof(content)];
        check(ReadFile(h, actual, sizeof(actual), &n, nullptr) && n == sizeof(actual) &&
                  memcmp(actual, content, sizeof(content)) == 0,
              "original content invariant");
        CloseHandle(h);
        check(MoveFileW(file.c_str(), (dir + L"\\renamed.txt").c_str()) != FALSE, "not held open");
        check(DeleteFileW((dir + L"\\renamed.txt").c_str()) != FALSE, "delete temporary original");
        for (auto name : {L"state.db", L"state.bak", L"state.tmp"})
            DeleteFileW((dir + L"\\" + name).c_str());
        RemoveDirectoryW(dir.c_str());
        std::cout << "PASS Windows atomic persistence, backup, reference-only lifecycle, original "
                     "content and released handles\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL " << e.what() << "; inspect test-only directory before cleanup\n";
        return 1;
    }
}
