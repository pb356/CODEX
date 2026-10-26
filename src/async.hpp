#pragma once
#include "platform.hpp"
#include <deque>
#include <functional>
namespace desktop {
// One bounded worker, latest job per key. Results are coalesced into one window message.
// Shared context survives a slow OS call without keeping a pointer into a destroyed App/View.
template <class Job, class Result> class AsyncQueue {
    struct Context {
        HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, nullptr),
               wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        HWND owner;
        UINT message;
        std::mutex mutex;
        std::deque<std::pair<std::string, Job>> jobs;
        std::vector<std::pair<std::string, Result>> results;
        bool posted = false;
        std::function<Result(const Job &)> process;
        Context(HWND h, UINT m, std::function<Result(const Job &)> f)
            : owner(h), message(m), process(std::move(f)) {}
        ~Context() {
            if (stop)
                CloseHandle(stop);
            if (wake)
                CloseHandle(wake);
        }
    };
    std::shared_ptr<Context> context_;
    HANDLE thread_ = nullptr;
    static unsigned __stdcall run(void *raw) {
        std::unique_ptr<std::shared_ptr<Context>> holder((std::shared_ptr<Context> *)raw);
        auto c = *holder;
        holder.reset();
        HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        HANDLE events[]{c->stop, c->wake};
        for (;;) {
            if (WaitForMultipleObjects(2, events, FALSE, INFINITE) != WAIT_OBJECT_0 + 1)
                break;
            for (;;) {
                std::unique_ptr<Job> job;
                std::string key;
                {
                    std::lock_guard<std::mutex> lock(c->mutex);
                    if (c->jobs.empty() || c->results.size() >= 128)
                        break;
                    key = std::move(c->jobs.front().first);
                    job = std::make_unique<Job>(std::move(c->jobs.front().second));
                    c->jobs.pop_front();
                }
                if (WaitForSingleObject(c->stop, 0) == WAIT_OBJECT_0)
                    break;
                try {
                    auto result = c->process(*job);
                    if (WaitForSingleObject(c->stop, 0) == WAIT_OBJECT_0)
                        break;
                    std::lock_guard<std::mutex> lock(c->mutex);
                    auto previous = std::find_if(c->results.begin(), c->results.end(),
                                                 [&](auto &entry) { return entry.first == key; });
                    if (previous != c->results.end())
                        previous->second = std::move(result);
                    else
                        c->results.emplace_back(std::move(key), std::move(result));
                    if (!c->posted) {
                        c->posted = PostMessageW(c->owner, c->message, 0, 0) != FALSE;
                        if (!c->posted)
                            c->results.clear();
                    }
                } catch (...) { /* OS icon failure is a fallback, never a reason to abort file
                                   operations. */
                }
            }
            if (WaitForSingleObject(c->stop, 0) == WAIT_OBJECT_0)
                break;
        }
        if (SUCCEEDED(com))
            CoUninitialize();
        return 0;
    }

  public:
    AsyncQueue(HWND owner, UINT message, std::function<Result(const Job &)> fn)
        : context_(std::make_shared<Context>(owner, message, std::move(fn))) {
        if (!context_->stop || !context_->wake)
            throw std::runtime_error("worker events");
        auto arg = new std::shared_ptr<Context>(context_);
        thread_ = (HANDLE)_beginthreadex(nullptr, 0, run, arg, 0, nullptr);
        if (!thread_) {
            delete arg;
            throw std::runtime_error("worker thread");
        }
    }
    ~AsyncQueue() {
        stop();
        if (thread_)
            CloseHandle(thread_);
    }
    bool submit(std::string key, Job job) {
        std::lock_guard<std::mutex> lock(context_->mutex);
        for (auto &pending : context_->jobs)
            if (pending.first == key) {
                pending.second = std::move(job);
                SetEvent(context_->wake);
                return true;
            }
        if (context_->jobs.size() >= 128)
            return false;
        context_->jobs.emplace_back(std::move(key), std::move(job));
        SetEvent(context_->wake);
        return true;
    }
    std::vector<Result> take() {
        std::lock_guard<std::mutex> lock(context_->mutex);
        context_->posted = false;
        std::vector<Result> out;
        out.reserve(context_->results.size());
        for (auto &entry : context_->results)
            out.push_back(std::move(entry.second));
        context_->results.clear();
        SetEvent(context_->wake); // Resume a worker blocked by a full result queue.
        return out;
    }
    bool stop() {
        SetEvent(context_->stop);
        if (thread_)
            CancelSynchronousIo(thread_);
        return !thread_ || WaitForSingleObject(thread_, 1500) == WAIT_OBJECT_0;
    }
};
struct ProjectionJob {
    uint32_t id = 0;
    uint64_t revision = 0;
    box::Box snapshot;
    box::PathSet excluded;
    std::string query;
};
struct ProjectionResult {
    uint32_t id = 0;
    uint64_t revision = 0;
    std::vector<int> indices;
    std::vector<std::wstring> labels, types;
};
inline std::wstring folded(std::wstring value) {
    if (value.empty())
        return value;
    int n = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, value.data(), (int)value.size(),
                          nullptr, 0, nullptr, nullptr, 0);
    if (!n)
        return value;
    std::wstring result(n, 0);
    LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_LOWERCASE, value.data(), (int)value.size(),
                  result.data(), n, nullptr, nullptr, 0);
    return result;
}
inline ProjectionResult project(const ProjectionJob &j) {
    ProjectionResult r;
    r.id = j.id;
    r.revision = j.revision;
    r.labels.reserve(j.snapshot.items.size());
    r.types.reserve(j.snapshot.items.size());
    auto q = folded(wide(j.query));
    for (size_t k = 0; k < j.snapshot.items.size(); ++k) {
        auto &i = j.snapshot.items[k];
        auto name = wide(box::nameOf(i.path));
        auto dot = name.find_last_of(L'.');
        auto type = dot == std::wstring::npos ? L"" : folded(name.substr(dot));
        r.labels.push_back(name);
        r.types.push_back(type);
        if (j.snapshot.kind == box::Kind::Inbox && !j.snapshot.inboxAll &&
            j.excluded.count(box::pathKey(i.path)))
            continue;
        if (q.empty() || folded(name).find(q) != std::wstring::npos)
            r.indices.push_back((int)k);
    }
    if (j.snapshot.sort)
        std::stable_sort(r.indices.begin(), r.indices.end(), [&](int a, int b) {
            if (j.snapshot.sort == 2) {
                auto &x = r.types[a];
                auto &y = r.types[b];
                int c = CompareStringOrdinal(x.c_str(), -1, y.c_str(), -1, TRUE);
                if (c != CSTR_EQUAL)
                    return c == CSTR_LESS_THAN;
            }
            return CompareStringOrdinal(r.labels[a].c_str(), -1, r.labels[b].c_str(), -1, TRUE) ==
                   CSTR_LESS_THAN;
        });
    return r;
}
struct IconJob {
    std::string path;
};
struct SharedIcon {
    HICON icon = nullptr;
    ~SharedIcon() {
        if (icon)
            DestroyIcon(icon);
    }
};
struct IconResult {
    std::string path;
    std::shared_ptr<SharedIcon> image;
};
inline IconResult systemIcon(const IconJob &j) {
    IconResult r;
    r.path = j.path;
    r.image = std::make_shared<SharedIcon>();
    auto p = wide(j.path);
    DWORD a = INVALID_FILE_ATTRIBUTES;
    if (p.size() > 2 && p[1] == L':') {
        wchar_t root[]{p[0], L':', L'\\', 0};
        if (GetDriveTypeW(root) == DRIVE_FIXED)
            a = GetFileAttributesW(p.c_str());
    }
    SHFILEINFOW info{};
    UINT flags = SHGFI_ICON | SHGFI_LARGEICON;
    if (a == INVALID_FILE_ATTRIBUTES ||
        (a & (FILE_ATTRIBUTE_OFFLINE | 0x00400000 | 0x00040000 | FILE_ATTRIBUTE_REPARSE_POINT))) {
        flags |= SHGFI_USEFILEATTRIBUTES;
        auto name = wide(box::nameOf(j.path));
        auto dot = name.find_last_of(L'.');
        p = dot == std::wstring::npos ? L"file" : name.substr(dot);
        a = FILE_ATTRIBUTE_NORMAL;
    }
    if (SHGetFileInfoW(p.c_str(), a, &info, sizeof(info), flags))
        r.image->icon = info.hIcon;
    return r;
}
} // namespace desktop
