#include "util/winprocess.h"
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc > 1) {
        if (std::strcmp(argv[1], "--stall") == 0) { Sleep(10000); return 0; }
        if (GetConsoleWindow() != nullptr) return 9;
        std::fputs("background query: 35, 123, 456, GPU\n", stdout);
        std::fputs("diagnostic must not enter captured stdout\n", stderr);
        return 0;
    }
    wchar_t path[32768];
    const DWORD n = GetModuleFileNameW(nullptr, path, 32768);
    if (!n || n >= 32768) return 1;
    const auto command = L"\"" + std::wstring(path, n) + L"\"";
    for (int i = 0; i < 5; ++i) {
        auto output = tak::captureHiddenProcess(command + L" --child");
        if (output != "background query: 35, 123, 456, GPU\r\n") {
            std::fprintf(stderr, "FAIL: hidden child output/console state: %s\n", output.c_str());
            return 2;
        }
    }
    if (!tak::captureHiddenProcess(L"tak-nonexistent-query-785493.exe").empty()) return 3;
    auto start = GetTickCount64();
    if (!tak::captureHiddenProcess(command + L" --stall", 100).empty() ||
        GetTickCount64() - start > 2000) return 4;
    std::puts("PASS: repeated console-free capture, missing query, bounded timeout");
}
