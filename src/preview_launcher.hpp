#pragma once
#include "platform.hpp"
namespace desktop {
// The preview has no desktop embedding privileges and cannot outlive its launching app.
class PreviewLauncher {
    HANDLE job_ = nullptr, process_ = nullptr;

  public:
    PreviewLauncher() = default;
    PreviewLauncher(const PreviewLauncher &) = delete;
    PreviewLauncher &operator=(const PreviewLauncher &) = delete;
    DWORD pid() const {
        return process_ ? GetProcessId(process_) : 0;
    }
    ~PreviewLauncher() {
        if (job_)
            CloseHandle(job_);
        if (process_)
            CloseHandle(process_);
    }
    bool open() {
        if (process_ && WaitForSingleObject(process_, 0) == WAIT_TIMEOUT) {
            auto pid = GetProcessId(process_);
            EnumWindows(
                [](HWND h, LPARAM value) -> BOOL {
                    DWORD p = 0;
                    GetWindowThreadProcessId(h, &p);
                    if (p == DWORD(value) && IsWindowVisible(h)) {
                        ShowWindow(h, SW_RESTORE);
                        SetForegroundWindow(h);
                        return FALSE;
                    }
                    return TRUE;
                },
                pid);
            return true;
        }
        if (process_) {
            CloseHandle(process_);
            process_ = nullptr;
        }
        if (job_) {
            CloseHandle(job_);
            job_ = nullptr;
        }
        wchar_t module[32768];
        DWORD n = GetModuleFileNameW(nullptr, module, 32768);
        if (!n || n >= 32768)
            return false;
        std::wstring path(module, n);
        auto slash = path.find_last_of(L"\\/");
        if (slash == std::wstring::npos)
            return false;
        auto directory = path.substr(0, slash), exe = directory + L"\\WallpaperPreview.exe";
        auto command = L"\"" + exe + L"\"";
        job_ = CreateJobObjectW(nullptr, nullptr);
        if (!job_)
            return false;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job_, JobObjectExtendedLimitInformation, &limits,
                                     sizeof(limits)))
            return false;
        STARTUPINFOW si{};
        si.cb = sizeof(si);
        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(exe.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_SUSPENDED,
                            nullptr, directory.c_str(), &si, &pi)) {
            return false;
        }
        bool ok = AssignProcessToJobObject(job_, pi.hProcess) != FALSE;
        if (ok)
            ok = ResumeThread(pi.hThread) != DWORD(-1);
        if (!ok)
            TerminateProcess(pi.hProcess, 1); // Only this not-yet-running, app-owned child.
        CloseHandle(pi.hThread);
        if (ok)
            process_ = pi.hProcess;
        else
            CloseHandle(pi.hProcess);
        return ok;
    }
};
} // namespace desktop
