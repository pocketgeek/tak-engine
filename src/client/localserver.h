#pragma once

// The private takserver a single-player / campaign / benchmark session runs on
// (AIs run only on a server). One owner per session: the destructor stops the
// child, so every way out of a session -- a failed launch looping back to the
// menu, an error return, the normal teardown -- takes the server with it. The
// connect poll asks exited() each attempt, so a server that dies on startup (no
// binary, unreadable data, a crash) fails the launch at once instead of after the
// whole connect timeout. Process control is the one genuinely platform-specific
// bit: fork/exec + waitpid on POSIX, CreateProcess + GetExitCodeProcess on Windows.
// Header-only and SDL-free (tools/local_server_test.cpp drives it directly).

#include <string>
#include <vector>

#ifdef _WIN32
  #include <filesystem>
  #include "util/winargv.h"   // <windows.h> (NOMINMAX) + quoteWindowsArgument
#else
  #include <csignal>
  #include <sys/wait.h>
  #include <unistd.h>
#endif

namespace tak {

class LocalServer {
public:
    LocalServer() = default;
    ~LocalServer() { stop(); }
    LocalServer(const LocalServer&) = delete;
    LocalServer& operator=(const LocalServer&) = delete;

    // Launch `program` (a path; neither platform searches PATH here) with `args`.
    // False when no process could be created. A missing binary is reported here on
    // Windows; on POSIX the fork succeeds and the child exits 127 (see exited()).
    bool start(const std::string& program, const std::vector<std::string>& args) {
        stop();
#ifdef _WIN32
        const auto executable = std::filesystem::u8path(program).wstring();
        std::wstring cmd = quoteWindowsArgument(executable);
        for (const auto& a : args) cmd += L" " + quoteWindowsArgument(std::filesystem::u8path(a).wstring());
        STARTUPINFOW si{}; si.cb = sizeof(si);
        if (!CreateProcessW(executable.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &si, &proc_)) return false;
        CloseHandle(proc_.hThread); proc_.hThread = nullptr;
        up_ = true;
#else
        // Build argv BEFORE forking: the client is multi-threaded, and only
        // async-signal-safe calls (no allocation) are allowed in the child.
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(program.c_str()));
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        const pid_t pid = fork();
        if (pid < 0) return false;
        if (pid == 0) {
            execv(program.c_str(), argv.data());
            _exit(127);   // exec failed
        }
        pid_ = pid;
        up_ = true;
#endif
        exited_ = false;
        exitCode_ = 0;
        return true;
    }

    // Non-blocking: has the child exited (or died)? Reaps it on POSIX. *code gets its
    // exit status (128 + signal for a POSIX child killed by a signal). False while it
    // runs, and when nothing was started.
    bool exited(int* code = nullptr) {
        if (!up_) return false;
        if (!exited_) {
#ifdef _WIN32
            if (WaitForSingleObject(proc_.hProcess, 0) != WAIT_OBJECT_0) return false;
            DWORD c = 0;
            GetExitCodeProcess(proc_.hProcess, &c);
            exitCode_ = int(c);
#else
            int st = 0;
            if (waitpid(pid_, &st, WNOHANG) != pid_) return false;
            exitCode_ = WIFEXITED(st) ? WEXITSTATUS(st) : WIFSIGNALED(st) ? 128 + WTERMSIG(st) : 1;
#endif
            exited_ = true;
        }
        if (code) *code = exitCode_;
        return true;
    }

    // True between a successful start() and stop() (even if the child has since died;
    // ask exited() for that).
    bool started() const { return up_; }

    // Stop the child (if still running) and release it. Safe to call repeatedly.
    void stop() {
        if (!up_) return;
#ifdef _WIN32
        if (!exited()) {
            TerminateProcess(proc_.hProcess, 0);
            WaitForSingleObject(proc_.hProcess, 2000);
        }
        CloseHandle(proc_.hProcess);
        proc_ = {};
#else
        if (!exited()) { kill(pid_, SIGTERM); waitpid(pid_, nullptr, 0); }
        pid_ = 0;
#endif
        up_ = false;
    }

    // The child's process id (for the benchmark's server-side CPU/memory sampling).
    long pid() const {
        if (!up_) return 0;
#ifdef _WIN32
        return long(proc_.dwProcessId);
#else
        return long(pid_);
#endif
    }

private:
    bool up_ = false, exited_ = false;
    int exitCode_ = 0;
#ifdef _WIN32
    PROCESS_INFORMATION proc_{};
#else
    pid_t pid_ = 0;
#endif
};

}  // namespace tak
