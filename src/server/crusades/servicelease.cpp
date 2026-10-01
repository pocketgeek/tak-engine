#include "server/crusades/servicelease.h"

#include <stdexcept>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace tak::srv::crusades {
struct CampaignServiceLease::Impl {
    std::filesystem::path database;
#ifdef _WIN32
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~Impl() { if(handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
#else
    int fd = -1;
    ~Impl() { if(fd >= 0) close(fd); }
#endif
};

CampaignServiceLease::CampaignServiceLease(const std::filesystem::path& database)
    : impl_(std::make_unique<Impl>()) {
    namespace fs = std::filesystem;
    if(database.empty() || database == fs::path(":memory:"))
        throw std::runtime_error("campaign service lease requires a persistent database path");
    impl_->database = fs::weakly_canonical(fs::absolute(database));
    if(!fs::is_directory(impl_->database.parent_path()))
        throw std::runtime_error("campaign database parent directory does not exist");
    const auto status = fs::status(impl_->database);
    if(fs::exists(status)) {
        if(!fs::is_regular_file(status) || fs::hard_link_count(impl_->database) != 1)
            throw std::runtime_error("campaign database must be a regular file without hard-link aliases");
    }
    auto lock = impl_->database;
    lock += ".service-lock";
#ifdef _WIN32
    impl_->handle = CreateFileW(lock.c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if(impl_->handle == INVALID_HANDLE_VALUE)
        throw std::runtime_error("cannot open campaign service lease (Windows error " + std::to_string(GetLastError()) + ")");
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(impl_->handle, &info) || info.nNumberOfLinks != 1 ||
       (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)))
        throw std::runtime_error("campaign service lease must be a regular file without aliases");
    OVERLAPPED offset{};
    if(!LockFileEx(impl_->handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                  0, 1, 0, &offset))
        throw std::runtime_error("campaign database is in use; stop takserver and other administration commands first");
#else
    impl_->fd = open(lock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if(impl_->fd < 0)
        throw std::runtime_error("cannot open campaign service lease: " + std::string(std::strerror(errno)));
    struct stat info{};
    if(fstat(impl_->fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_nlink != 1)
        throw std::runtime_error("campaign service lease must be a regular file without aliases");
    if(flock(impl_->fd, LOCK_EX | LOCK_NB) != 0)
        throw std::runtime_error("campaign database is in use; stop takserver and other administration commands first");
#endif
}

CampaignServiceLease::~CampaignServiceLease() = default;
const std::filesystem::path& CampaignServiceLease::databasePath() const { return impl_->database; }
} // namespace tak::srv::crusades
