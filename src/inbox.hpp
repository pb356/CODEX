#pragma once
#include "async.hpp"
namespace desktop {
struct InboxRequest {
    bool enabled = false, paused = false, markNewOnScan = true;
    uint64_t revision = 0;
    std::vector<box::Item> prior;
    // Test-only injected directories; production uses Known Folder APIs, never hard-coded Desktop.
    std::vector<std::wstring> testRoots;
};
struct InboxSnapshot {
    uint64_t revision = 0;
    std::vector<box::Item> items;
    std::vector<std::wstring> roots;
    bool healthy = true;
    std::wstring problem;
};
class InboxWatcher {
    struct Context {
        HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr),
               wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        HWND owner;
        std::mutex mutex;
        InboxRequest request;
        InboxSnapshot result;
        bool posted = false;
        explicit Context(HWND h) : owner(h) {}
        ~Context() {
            if (stop)
                CloseHandle(stop);
            if (wake)
                CloseHandle(wake);
        }
    };
    struct Watch {
        std::wstring root;
        HANDLE file = INVALID_HANDLE_VALUE, event = nullptr;
        OVERLAPPED ov{};
        BYTE bytes[32768]{};
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
        bool arm() {
            ResetEvent(event);
            ZeroMemory(&ov, sizeof(ov));
            ov.hEvent = event;
            pending = ReadDirectoryChangesW(
                          file, bytes, sizeof(bytes), FALSE,
                          FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
                              FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_ATTRIBUTES,
                          nullptr, &ov, nullptr) != FALSE;
            return pending;
        }
    };
    std::shared_ptr<Context> context_;
    HANDLE thread_ = nullptr;
    using Entries = std::map<box::PathKey, box::Item, box::PathLess>;
    static std::vector<std::wstring> roots() {
        std::vector<std::wstring> out;
        box::PathSet unique;
        for (auto id : {FOLDERID_Desktop, FOLDERID_PublicDesktop}) {
            PWSTR p = nullptr;
            if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) {
                std::wstring path = p;
                CoTaskMemFree(p);
                auto key = box::pathKey(utf8(path));
                if (unique.insert(key).second)
                    out.push_back(std::move(path));
            }
        }
        return out;
    }
    static bool accepted(const std::wstring &name, DWORD attrs) {
        if (name == L"." || name == L".." ||
            (!(attrs & FILE_ATTRIBUTE_DIRECTORY) && box::ignoredDesktopName(utf8(name))))
            return false;
        SHELLSTATE shell{};
        SHGetSetSettings(&shell, SSF_SHOWALLOBJECTS | SSF_SHOWSUPERHIDDEN, FALSE);
        if ((attrs & FILE_ATTRIBUTE_HIDDEN) && !shell.fShowAllObjects)
            return false;
        if ((attrs & FILE_ATTRIBUTE_SYSTEM) && !shell.fShowSuperHidden)
            return false;
        return true;
    }
    static void assign(Entries &entries, const std::wstring &path, DWORD attrs,
                       const Entries &previous, bool markNew) {
        auto name = path.substr(path.find_last_of(L'\\') + 1);
        auto p = canonical(path);
        auto key = box::pathKey(p);
        if (!accepted(name, attrs)) {
            entries.erase(key);
            return;
        }
        auto old = previous.find(key);
        entries[key] = {std::move(p), old != previous.end() ? old->second.fresh : markNew};
    }
    static bool scan(const std::vector<std::wstring> &dirs, Entries &entries, bool markNew,
                     HANDLE stop) {
        Entries next;
        size_t bytes = 0;
        bool ok = !dirs.empty();
        for (auto &root : dirs) {
            WIN32_FIND_DATAW data{};
            HANDLE find = FindFirstFileExW((root + L"\\*").c_str(), FindExInfoBasic, &data,
                                           FindExSearchNameMatch, nullptr, 0);
            if (find == INVALID_HANDLE_VALUE) {
                DWORD e = GetLastError();
                if (e != ERROR_FILE_NOT_FOUND && e != ERROR_NO_MORE_FILES) {
                    ok = false;
                    auto prefix = box::pathKey(utf8(root + L"\\"));
                    for (auto &old : entries)
                        if (old.first.compare(0, prefix.size(), prefix) == 0)
                            next.insert(old);
                }
                continue;
            }
            do {
                if (WaitForSingleObject(stop, 0) == WAIT_OBJECT_0) {
                    FindClose(find);
                    return false;
                }
                if (!accepted(data.cFileName, data.dwFileAttributes))
                    continue;
                auto path = root + L"\\" + data.cFileName;
                bytes += path.size() * sizeof(wchar_t) + 32;
                if (bytes > box::MaxConfig) {
                    ok = false;
                    break;
                }
                try {
                    assign(next, path, data.dwFileAttributes, entries, markNew);
                } catch (...) {
                    ok = false;
                }
            } while (FindNextFileW(find, &data));
            auto error = GetLastError();
            FindClose(find);
            if (error != ERROR_NO_MORE_FILES && error != ERROR_SUCCESS)
                ok = false;
        }
        if (ok)
            entries = std::move(next); // Incomplete enumeration never removes old references.
        else
            for (auto &entry : next)
                entries[entry.first] = std::move(entry.second);
        return ok;
    }
    static unsigned __stdcall entry(void *raw) {
        std::unique_ptr<std::shared_ptr<Context>> holder((std::shared_ptr<Context> *)raw);
        auto c = *holder;
        holder.reset();
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        try {
            run(c);
        } catch (...) {
            InboxSnapshot r;
            r.healthy = false;
            r.problem = L"收件更新暂不可用，请稍后刷新。";
            std::lock_guard<std::mutex> lock(c->mutex);
            r.revision = c->request.revision;
            c->result = std::move(r);
            PostMessageW(c->owner, WM_APP + 8, 0, 0);
        }
        if (SUCCEEDED(com))
            CoUninitialize();
        return 0;
    }
    static void run(const std::shared_ptr<Context> &c) {
        InboxRequest request;
        Entries entries;
        std::vector<std::wstring> dirs;
        std::vector<std::unique_ptr<Watch>> watches;
        bool healthy = true, pending = false, rescan = false;
        ULONGLONG firstEvent = 0, due = 0;
        std::set<std::wstring> changes;
        std::vector<box::PathKey> order;
        auto publish = [&]() {
            InboxSnapshot result;
            result.revision = request.revision;
            result.roots = dirs;
            result.healthy = healthy;
            box::PathSet shown;
            std::vector<box::PathKey> nextOrder;
            for (auto &key : order) {
                auto it = entries.find(key);
                if (it != entries.end() && shown.insert(key).second) {
                    result.items.push_back(it->second);
                    nextOrder.push_back(key);
                }
            }
            for (auto &e : entries)
                if (shown.insert(e.first).second) {
                    result.items.push_back(e.second);
                    nextOrder.push_back(e.first);
                }
            order = std::move(nextOrder);
            if (!healthy)
                result.problem = L"部分桌面项目暂未更新，可稍后重试。";
            std::lock_guard<std::mutex> lock(c->mutex);
            c->result = std::move(result);
            if (!c->posted)
                c->posted = PostMessageW(c->owner, WM_APP + 8, 0, 0) != FALSE;
        };
        for (;;) {
            std::vector<HANDLE> events{c->stop, c->wake};
            for (auto &w : watches)
                events.push_back(w->event);
            ULONGLONG now = GetTickCount64();
            DWORD timeout = pending ? (due <= now ? 0 : (DWORD)(due - now)) : INFINITE;
            DWORD event =
                WaitForMultipleObjects((DWORD)events.size(), events.data(), FALSE, timeout);
            if (event == WAIT_OBJECT_0 || event == WAIT_FAILED)
                break;
            if (event == WAIT_OBJECT_0 + 1) {
                watches.clear();
                changes.clear();
                pending = false;
                rescan = false;
                {
                    std::lock_guard<std::mutex> lock(c->mutex);
                    request = c->request;
                }
                if (!request.enabled || request.paused)
                    continue;
                bool hasPrior = request.markNewOnScan;
                entries.clear();
                order.clear();
                for (auto &i : request.prior) {
                    auto key = box::pathKey(i.path);
                    entries[key] = i;
                    order.push_back(key);
                }
                dirs = request.testRoots.empty() ? roots() : request.testRoots;
                healthy = !dirs.empty();
                for (auto &root : dirs) {
                    auto w = std::make_unique<Watch>();
                    w->root = root;
                    w->file = CreateFileW(
                        root.c_str(), FILE_LIST_DIRECTORY,
                        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                        OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
                    if (w->file == INVALID_HANDLE_VALUE) {
                        healthy = false;
                        continue;
                    }
                    w->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                    if (!w->event || !w->arm()) {
                        healthy = false;
                        continue;
                    }
                    watches.push_back(std::move(w));
                }
                // Arm notifications before enumerating, so files created during the initial scan
                // cannot fall into the gap between enumeration and watch registration.
                healthy = scan(dirs, entries, hasPrior, c->stop) && healthy;
                publish();
                continue;
            }
            if (event == WAIT_TIMEOUT) {
                if (rescan)
                    healthy = scan(dirs, entries, true, c->stop);
                else
                    for (auto &path : changes) {
                        if (WaitForSingleObject(c->stop, 0) == WAIT_OBJECT_0)
                            break;
                        auto p = canonical(path);
                        auto key = box::pathKey(p);
                        WIN32_FILE_ATTRIBUTE_DATA data{};
                        if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) {
                            Entries old;
                            auto it = entries.find(key);
                            if (it != entries.end())
                                old.insert(*it);
                            assign(entries, path, data.dwFileAttributes, old, true);
                        } else {
                            DWORD error = GetLastError();
                            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
                                entries.erase(key);
                            else
                                healthy = false;
                        }
                    }
                changes.clear();
                pending = false;
                rescan = false;
                publish();
                continue;
            }
            size_t index = event - WAIT_OBJECT_0 - 2;
            if (index >= watches.size())
                continue;
            auto &w = *watches[index];
            DWORD length = 0;
            bool ok = GetOverlappedResult(w.file, &w.ov, &length, FALSE) != FALSE;
            w.pending = false;
            if (!ok || length == 0)
                rescan = true;
            else {
                size_t offset = 0;
                while (offset + sizeof(FILE_NOTIFY_INFORMATION) <= length) {
                    auto info = (FILE_NOTIFY_INFORMATION *)(w.bytes + offset);
                    if (info->FileNameLength >
                            length - offset - FIELD_OFFSET(FILE_NOTIFY_INFORMATION, FileName) ||
                        info->FileNameLength % sizeof(wchar_t)) {
                        rescan = true;
                        break;
                    }
                    std::wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));
                    if (name.find_first_of(L"\\/") == std::wstring::npos)
                        changes.insert(w.root + L"\\" + name);
                    if (changes.size() > 4096) {
                        changes.clear();
                        rescan = true;
                    }
                    if (!info->NextEntryOffset)
                        break;
                    if (info->NextEntryOffset < sizeof(FILE_NOTIFY_INFORMATION) ||
                        info->NextEntryOffset > length - offset) {
                        rescan = true;
                        break;
                    }
                    offset += info->NextEntryOffset;
                }
            }
            if (!w.arm()) {
                watches.erase(watches.begin() + index);
                healthy = false;
                rescan = true;
            }
            now = GetTickCount64();
            if (!pending) {
                pending = true;
                firstEvent = now;
            }
            due = std::min(firstEvent + 1200, now + 500);
        }
    }

  public:
    explicit InboxWatcher(HWND owner) : context_(std::make_shared<Context>(owner)) {
        if (!context_->stop || !context_->wake)
            throw std::runtime_error("inbox events");
        auto arg = new std::shared_ptr<Context>(context_);
        thread_ = (HANDLE)_beginthreadex(nullptr, 0, entry, arg, 0, nullptr);
        if (!thread_) {
            delete arg;
            throw std::runtime_error("inbox worker");
        }
    }
    ~InboxWatcher() {
        stop();
        if (thread_)
            CloseHandle(thread_);
    }
    void update(InboxRequest request) {
        std::lock_guard<std::mutex> lock(context_->mutex);
        context_->request = std::move(request);
        SetEvent(context_->wake);
    }
    InboxSnapshot take() {
        std::lock_guard<std::mutex> lock(context_->mutex);
        context_->posted = false;
        return std::move(context_->result);
    }
    bool stop() {
        SetEvent(context_->stop);
        if (thread_)
            CancelSynchronousIo(thread_);
        return !thread_ || WaitForSingleObject(thread_, 1500) == WAIT_OBJECT_0;
    }
};
} // namespace desktop
