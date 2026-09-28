#pragma once
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <string>
#include <vector>

namespace tak {
namespace winprocess_detail {
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle() = default;
    explicit Handle(HANDLE h) : value(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
}

// Capture a bounded, short-lived background query. No cmd.exe and no console,
// even when the caller is a GUI application. Returns empty on failure/timeout.
inline std::string captureHiddenProcess(std::wstring command, DWORD timeoutMs = 3000) {
    using winprocess_detail::Handle;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    Handle read, write;
    if (!CreatePipe(&read.value, &write.value, &sa, 0) ||
        !SetHandleInformation(read.value, HANDLE_FLAG_INHERIT, 0)) return {};
    Handle null(CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr));
    if (null.value == INVALID_HANDLE_VALUE) return {};
    // Restrict inheritance: other threads may also be creating subprocesses.
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> storage(bytes);
    auto* attributes = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(attributes, 1, 0, &bytes)) return {};
    HANDLE inherit[] = {write.value, null.value};
    if (!UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                    inherit, sizeof(inherit), nullptr, nullptr)) {
        DeleteProcThreadAttributeList(attributes);
        return {};
    }
    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.StartupInfo.wShowWindow = SW_HIDE;
    si.StartupInfo.hStdOutput = write.value;
    si.StartupInfo.hStdError = si.StartupInfo.hStdInput = null.value;
    si.lpAttributeList = attributes;
    PROCESS_INFORMATION pi{};
    const BOOL started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, nullptr,
        &si.StartupInfo, &pi);
    DeleteProcThreadAttributeList(attributes);
    if (!started) return {};
    Handle process(pi.hProcess), thread(pi.hThread);
    CloseHandle(write.value); write.value = nullptr;
    const ULONGLONG start = GetTickCount64();
    std::string out;
    for (;;) {
        DWORD available = 0;
        if (PeekNamedPipe(read.value, nullptr, 0, nullptr, &available, nullptr) && available) {
            char buffer[4096]; DWORD count = 0;
            if (!ReadFile(read.value, buffer, std::min<DWORD>(available, sizeof(buffer)),
                          &count, nullptr)) break;
            out.append(buffer, count);
        } else if (WaitForSingleObject(process.value, 0) == WAIT_OBJECT_0) {
            // The process may have written between the first peek and exit.
            if (PeekNamedPipe(read.value, nullptr, 0, nullptr, &available, nullptr) && available)
                continue;
            DWORD code = 1;
            GetExitCodeProcess(process.value, &code);
            return code == 0 ? out : std::string{};
        } else {
            WaitForSingleObject(process.value, 10);
        }
        if (GetTickCount64() - start >= timeoutMs || out.size() > 65536) break;
    }
    TerminateProcess(process.value, 1);
    WaitForSingleObject(process.value, 1000);
    return {};
}
}
#endif
