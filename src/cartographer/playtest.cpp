#include "cartographer/playtest.h"
#include "util/winargv.h"
#include "cartographer/document.h"
#include "util/virtualpath.h"
#include <chrono>
#include <cerrno>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <spawn.h>
#include <sys/wait.h>
extern char** environ;
#endif

namespace cart {
namespace fs = std::filesystem;
fs::path Playtest::clientPath(const fs::path& directory) {
#ifdef _WIN32
    const auto sibling=directory/"takclient.exe";
#else
    const auto sibling=directory/"takclient";
#endif
    if(fs::is_regular_file(sibling))return sibling;
#ifdef __APPLE__
    const auto bundled=directory.parent_path().parent_path().parent_path()/
        "Total Annihilation - Kingdoms.app"/"Contents"/"MacOS"/"takclient";
    if(fs::is_regular_file(bundled))return bundled;
#endif
    throw std::runtime_error("Cannot find takclient beside Cartographer. Install both applications together.");
}
void Playtest::cleanup() {
    if(!directory_.empty()) {std::error_code error;fs::remove_all(directory_,error);}
    directory_.clear();snapshot_.clear();
}
Playtest::~Playtest() {
    poll();
#ifdef _WIN32
    if(process_)CloseHandle(reinterpret_cast<HANDLE>(process_));
#endif
    if(!process_)cleanup();
}
bool Playtest::start(const fs::path& client,const fs::path& data,
                     const std::vector<tak::hpi::PackFile>& files,std::string& error) {
    error.clear();poll();
    if(running()) {error="A map test is already running. Close its game window before starting another.";return false;}
    try {
        if(!fs::is_regular_file(client))throw std::runtime_error("Game client executable is missing");
        const auto stamp=std::chrono::steady_clock::now().time_since_epoch().count();
        for(unsigned n=0;;++n) {
            directory_=fs::temp_directory_path()/("tak-playtest-"+std::to_string(stamp)+"-"+std::to_string(n));
            if(fs::create_directory(directory_))break;
        }
        snapshot_=directory_/"Cartographer Test.kmp";
        if(!writeDocumentBundle(snapshot_,files,error)) {cleanup();return false;}
#ifdef _WIN32
        std::wstring command=tak::quoteWindowsArgument(fs::absolute(client).wstring())+L" --data "+tak::quoteWindowsArgument(fs::absolute(data).wstring())+
            L" --play-map "+tak::quoteWindowsArgument(snapshot_.wstring());
        STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
        if(!CreateProcessW(fs::absolute(client).c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,
                           nullptr,nullptr,&startup,&process))
            throw std::system_error(int(GetLastError()),std::system_category(),"Start map test");
        CloseHandle(process.hThread);process_=reinterpret_cast<intptr_t>(process.hProcess);
#else
        std::string executable=fs::absolute(client).string(),root=fs::absolute(data).string(),map=snapshot_.string();
        char* args[]={executable.data(),const_cast<char*>("--data"),root.data(),const_cast<char*>("--play-map"),map.data(),nullptr};
        pid_t pid=0;const int result=posix_spawn(&pid,executable.c_str(),nullptr,nullptr,args,environ);
        if(result)throw std::system_error(result,std::generic_category(),"Start map test");
        process_=pid;
#endif
        return true;
    } catch(const std::exception& e) {error=e.what();cleanup();return false;}
}
std::optional<int> Playtest::poll() {
    if(!process_)return {};
    int code=0;
#ifdef _WIN32
    DWORD status=0;
    if(WaitForSingleObject(reinterpret_cast<HANDLE>(process_),0)==WAIT_TIMEOUT)return {};
    if(GetExitCodeProcess(reinterpret_cast<HANDLE>(process_),&status))code=int(status);else code=1;
    CloseHandle(reinterpret_cast<HANDLE>(process_));
#else
    int status=0;const auto result=waitpid(pid_t(process_),&status,WNOHANG);
    if(result==0 || (result<0 && errno==EINTR))return {};
    code=result<0?1:WIFEXITED(status)?WEXITSTATUS(status):128+(WIFSIGNALED(status)?WTERMSIG(status):0);
#endif
    process_=0;cleanup();return code;
}
}
