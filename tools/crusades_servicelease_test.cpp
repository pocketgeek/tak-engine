#include "server/crusades/servicelease.h"
#include "util/winargv.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#ifndef _WIN32
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
using tak::srv::crusades::CampaignServiceLease;
namespace fs = std::filesystem;
fs::path utf8Path(std::string_view text) {
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()),text.size()));
}
int checks = 0;
void check(bool value, const char* message) { ++checks; if(!value) throw std::runtime_error(message); }
int child(const fs::path& executable, const fs::path& database, bool crash = false) {
#ifdef _WIN32
    std::wstring command = tak::quoteWindowsArgument(executable.native()) + (crash ? L" --crash " : L" --child ") +
        tak::quoteWindowsArgument(database.native());
    STARTUPINFOW start{}; start.cb = sizeof start;
    PROCESS_INFORMATION process{};
    if(!CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,nullptr,&start,&process))
        throw std::runtime_error("cannot launch lease child");
    const auto waited = WaitForSingleObject(process.hProcess,10000);
    if(waited != WAIT_OBJECT_0) TerminateProcess(process.hProcess,2);
    DWORD code = 2; GetExitCodeProcess(process.hProcess,&code);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    check(waited == WAIT_OBJECT_0,"lease child hung");
    return int(code);
#else
    const auto pid = fork();
    if(pid < 0) throw std::runtime_error("cannot fork lease child");
    if(pid == 0) { execl(executable.c_str(),executable.c_str(),crash ? "--crash" : "--child",database.c_str(),static_cast<char*>(nullptr)); _exit(2); }
    int status = 0;
    if(waitpid(pid,&status,0) != pid) throw std::runtime_error("cannot wait for lease child");
    return WIFEXITED(status) ? WEXITSTATUS(status) : 2;
#endif
}
int run(int argc, char** argv) {
    if(argc == 3 && (std::string(argv[1]) == "--child" || std::string(argv[1]) == "--crash")) {
        try { CampaignServiceLease lease(utf8Path(argv[2])); if(std::string(argv[1]) == "--crash") std::_Exit(7); return 0; }
        catch(const std::exception&) { return 1; }
    }
    const auto root = fs::temp_directory_path() / utf8Path("tak-lease-\xc3\xa9-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Remove { fs::path root; ~Remove() { std::error_code ec; fs::remove_all(root,ec); } } remove{root};
    try {
        fs::create_directories(root / "sub");
        const auto db = root / "campaign.sqlite", executable = fs::absolute(utf8Path(argv[0]));
        {
            CampaignServiceLease lease(db);
            check(lease.databasePath() == fs::weakly_canonical(db),"canonical database path differs");
            check(!fs::exists(db),"lease unexpectedly created a database");
            check(child(executable,db) == 1,"second process acquired a live lease");
            check(child(executable,root / "sub" / ".." / "campaign.sqlite") == 1,"relative alias bypassed lease");
        }
        check(fs::exists(fs::path(db).concat(".service-lock")),"companion lease file unexpectedly removed");
        check(child(executable,db) == 0,"released lease was not reusable");
        check(child(executable,db,true) == 7,"crash fixture did not acquire lease");
        check(child(executable,db) == 0,"kernel did not release lease after unclean exit");
        std::ofstream(db) << "fixture";
        {
            CampaignServiceLease lease(db);
            std::error_code ec;
            fs::create_symlink(db,root / "alias.sqlite",ec);
            if(!ec) check(child(executable,root / "alias.sqlite") == 1,"database symlink bypassed lease");
        }
        std::error_code ec;
        fs::create_hard_link(db,root / "hard.sqlite",ec);
        if(!ec) {
            check(child(executable,db) == 1,"hard-linked database accepted");
            check(child(executable,root / "hard.sqlite") == 1,"hard-link alias accepted");
            fs::remove(root / "hard.sqlite");
        }
        auto lock = db; lock += ".service-lock";
        fs::remove(lock);
        fs::create_directory(lock);
        check(child(executable,db) == 1,"directory accepted as lease file");
        fs::remove(lock);
        fs::create_symlink(db,lock,ec);
        if(!ec) check(child(executable,db) == 1,"symlink accepted as lease file");
        check(std::ifstream(db).get() == 'f',"database changed while refusing unsafe lease");
        bool refused = false;
        try { CampaignServiceLease lease(":memory:"); } catch(const std::exception&) { refused = true; }
        check(refused,"nonpersistent service lease accepted");
        std::cout << "PASS: " << checks << " process lease, alias, unsafe-file and crash-safe release checks\n";
        return 0;
    } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
} // namespace
int main(int argc, char** argv) {
#ifdef _WIN32
    (void)argc; (void)argv;
    return tak::utf8Main(run);
#else
    return run(argc,argv);
#endif
}
