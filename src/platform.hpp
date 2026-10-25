#pragma once
#ifndef UNICODE
#define UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "model.hpp"
#include <atomic>
#include <cmath>
#include <commdlg.h>
#include <gdiplus.h>
#include <map>
#include <memory>
#include <mutex>
#include <process.h>
#include <set>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <windows.h>
#include <wtsapi32.h>
namespace desktop {
inline std::wstring wide(const std::string &s) {
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (!n && !s.empty())
        throw std::runtime_error("UTF8");
    std::wstring w(n, 0);
    if (n)
        MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), w.data(), n);
    return w;
}
inline std::string utf8(const std::wstring &s) {
    int n = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0,
                                nullptr, nullptr);
    if (!n && !s.empty())
        throw std::runtime_error("UTF16");
    std::string a(n, 0);
    if (n)
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, s.data(), (int)s.size(), a.data(), n,
                            nullptr, nullptr);
    return a;
}
inline std::string canonical(const std::wstring &path) {
    if (!box::validPath(utf8(path)))
        throw std::runtime_error("path");
    std::vector<wchar_t> b(32768);
    auto n = GetFullPathNameW(path.c_str(), (DWORD)b.size(), b.data(), nullptr);
    if (!n || n >= b.size())
        throw std::runtime_error("path");
    std::wstring w(b.data(), n);
    while (w.size() > 3 && w.back() == L'\\')
        w.pop_back();
    return utf8(w);
}
inline thread_local unsigned modalDepth = 0;
inline int ask(HWND h, const wchar_t *text, const wchar_t *title, UINT flags) {
    struct Guard {
        Guard() {
            ++modalDepth;
        }
        ~Guard() {
            --modalDepth;
        }
    } guard;
    return MessageBoxW(h, text, title, flags);
}
inline void message(HWND h, const std::wstring &text, UINT flags = MB_OK | MB_ICONINFORMATION) {
    ask(h, text.c_str(), L"桌面归纳盒", flags);
}
inline std::wstring configDir() {
    PWSTR p = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &p)))
        throw std::runtime_error("AppData");
    std::wstring d = p;
    CoTaskMemFree(p);
    d += L"\\DesktopBoxes";
    if (!CreateDirectoryW(d.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
        throw std::runtime_error("mkdir");
    return d;
}
inline bool readBytes(const std::wstring &path, std::vector<uint8_t> &a) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    LARGE_INTEGER n;
    bool ok = GetFileSizeEx(f, &n) && n.QuadPart >= 8 && n.QuadPart <= (LONGLONG)box::MaxConfig;
    if (ok) {
        a.resize((size_t)n.QuadPart);
        DWORD got = 0;
        ok = ReadFile(f, a.data(), (DWORD)a.size(), &got, nullptr) && got == a.size();
    }
    CloseHandle(f);
    return ok;
}
inline bool writeConfig(const box::State &s, const std::wstring &dir) {
    auto a = box::encode(s);
    auto target = dir + L"\\state.db", temp = dir + L"\\state.tmp", backup = dir + L"\\state.bak";
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE)
        return false;
    DWORD wrote = 0;
    bool ok = WriteFile(f, a.data(), (DWORD)a.size(), &wrote, nullptr) && wrote == a.size() &&
              FlushFileBuffers(f);
    CloseHandle(f);
    if (ok) {
        if (GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES)
            ok = ReplaceFileW(target.c_str(), temp.c_str(), backup.c_str(), 0, nullptr, nullptr) !=
                 FALSE;
        else
            ok = MoveFileExW(temp.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
    }
    if (!ok)
        DeleteFileW(temp.c_str());
    return ok;
}
struct Health {
    uint32_t id;
    std::string path;
    int status;
}; // 0 unknown, 1 present, 2 missing, 3 offline, 4 directory
// At most 24 fixed-drive parent directories, event driven. Never follows renames speculatively.
class Monitor {
    HANDLE stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr),
           update_ = CreateEventW(nullptr, FALSE, FALSE, nullptr), thread_ = nullptr;
    std::mutex mutex_;
    std::vector<Health> requested_, results_;
    bool posted_ = false;
    HWND owner_;
    struct Watch {
        HANDLE file = INVALID_HANDLE_VALUE, event = nullptr;
        OVERLAPPED ov{};
        std::wstring dir;
        BYTE data[4096]{};
        bool pending = false;
        ~Watch() {
            if (file != INVALID_HANDLE_VALUE) {
                if (pending) {
                    CancelIoEx(file, &ov);
                    DWORD n;
                    GetOverlappedResult(file, &ov, &n, TRUE);
                }
                CloseHandle(file);
            }
            if (event)
                CloseHandle(event);
        }
    };
    static unsigned __stdcall entry(void *p) {
        try {
            ((Monitor *)p)->run();
        } catch (...) {
        }
        return 0;
    }
    static bool local(const std::wstring &p) {
        if (p.size() < 3 || p[1] != L':')
            return false;
        wchar_t root[] = {p[0], L':', L'\\', 0};
        return GetDriveTypeW(root) == DRIVE_FIXED;
    }
    static int inspect(const std::wstring &p) {
        if (!local(p))
            return 0;
        WIN32_FILE_ATTRIBUTE_DATA a{};
        if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &a)) {
            auto e = GetLastError();
            return e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? 2 : 0;
        }
        return a.dwFileAttributes & (FILE_ATTRIBUTE_OFFLINE | 0x00400000 | 0x00040000)
                   ? 3
                   : (a.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ? 4 : 1);
    }
    static bool arm(Watch &w) {
        ResetEvent(w.event);
        ZeroMemory(&w.ov, sizeof(w.ov));
        w.ov.hEvent = w.event;
        w.pending =
            ReadDirectoryChangesW(w.file, w.data, sizeof(w.data), FALSE,
                                  FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                                      FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_ATTRIBUTES,
                                  nullptr, &w.ov, nullptr) != FALSE;
        return w.pending;
    }
    void run() {
        std::vector<Health> current;
        std::vector<std::unique_ptr<Watch>> watches;
        auto clear = [&]() { watches.clear(); };
        auto publish = [&]() {
            std::lock_guard<std::mutex> lock(mutex_);
            results_ = current;
            if (!posted_) {
                posted_ = true;
                PostMessageW(owner_, WM_APP + 4, 0, 0);
            }
        };
        for (;;) {
            std::vector<HANDLE> handles{stop_, update_};
            for (auto &w : watches)
                handles.push_back(w->event);
            DWORD r =
                WaitForMultipleObjects((DWORD)handles.size(), handles.data(), FALSE, INFINITE);
            if (r == WAIT_OBJECT_0 || r == WAIT_FAILED)
                break;
            if (r == WAIT_OBJECT_0 + 1) {
                clear();
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    current = requested_;
                }
                std::set<std::wstring> dirs;
                for (auto &i : current) {
                    auto p = wide(i.path);
                    i.status = inspect(p);
                    if (local(p) && dirs.size() < 24)
                        dirs.insert(p.substr(0, p.find_last_of(L'\\') +
                                                    (p.find_last_of(L'\\') == 2 ? 1 : 0)));
                }
                for (auto &d : dirs) {
                    auto w = std::make_unique<Watch>();
                    w->dir = d;
                    w->file = CreateFileW(d.c_str(), FILE_LIST_DIRECTORY,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                          nullptr, OPEN_EXISTING,
                                          FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED |
                                              FILE_FLAG_OPEN_REPARSE_POINT,
                                          nullptr);
                    if (w->file == INVALID_HANDLE_VALUE)
                        continue;
                    w->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                    if (!w->event || !arm(*w))
                        continue;
                    watches.push_back(std::move(w));
                }
                publish();
            } else {
                size_t k = r - WAIT_OBJECT_0 - 2;
                if (k >= watches.size())
                    continue;
                auto &w = *watches[k];
                DWORD bytes = 0;
                bool ok = GetOverlappedResult(w.file, &w.ov, &bytes, FALSE) != FALSE;
                w.pending = false;
                auto dir = w.dir;
                if (ok) {
                    if (!arm(w)) {
                        watches.erase(watches.begin() + k);
                        continue;
                    }
                    for (auto &i : current) {
                        auto p = wide(i.path);
                        if (p.substr(0, p.find_last_of(L'\\') +
                                            (p.find_last_of(L'\\') == 2 ? 1 : 0)) == dir)
                            i.status = inspect(p);
                    }
                    publish();
                } else {
                    watches.erase(watches.begin() + k);
                }
                if (WaitForSingleObject(stop_, 200) == WAIT_OBJECT_0)
                    break;
            }
        }
        clear();
    }

  public:
    explicit Monitor(HWND owner) : owner_(owner) {
        thread_ = (HANDLE)_beginthreadex(nullptr, 0, entry, this, 0, nullptr);
    }
    void update(const box::State &s) {
        std::lock_guard<std::mutex> lock(mutex_);
        requested_.clear();
        for (auto &b : s.boxes)
            for (auto &i : b.items)
                requested_.push_back({b.id, i.path, 0});
        SetEvent(update_);
    }
    std::vector<Health> take() {
        std::lock_guard<std::mutex> lock(mutex_);
        posted_ = false;
        return std::move(results_);
    }
    bool stop() {
        SetEvent(stop_);
        if (thread_)
            CancelSynchronousIo(thread_);
        return !thread_ || WaitForSingleObject(thread_, 1500) == WAIT_OBJECT_0;
    }
    ~Monitor() {
        if (thread_)
            CloseHandle(thread_);
        CloseHandle(stop_);
        CloseHandle(update_);
    }
};
} // namespace desktop
