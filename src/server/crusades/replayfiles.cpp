#include "server/crusades/replayfiles.h"
#include "net/crypto.h"
#include "net/replayhdr.h"
#include <algorithm>
#include <array>
#include <cerrno>
#include <limits>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace tak::srv::crusades {
namespace wire = net::crusades;
namespace {
bool digest(const std::string& s) {
    return s.size()==64 && std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');});
}
class File {
public:
    explicit File(const std::filesystem::path& path) {
#ifdef _WIN32
        handle_=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
        if(handle_==INVALID_HANDLE_VALUE)return;
        BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER length{};
        if(!GetFileInformationByHandle(handle_,&info) ||
            (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)) ||
            GetFileType(handle_)!=FILE_TYPE_DISK || !GetFileSizeEx(handle_,&length) || length.QuadPart<0)return;
        size_=uint64_t(length.QuadPart);valid_=true;
#else
        fd_=::open(path.c_str(),O_RDONLY|O_CLOEXEC|O_NOFOLLOW|O_NONBLOCK);
        struct stat info{};
        if(fd_<0 || ::fstat(fd_,&info)!=0 || !S_ISREG(info.st_mode) || info.st_size<0)return;
        size_=uint64_t(info.st_size);valid_=true;
#endif
    }
    ~File() {
#ifdef _WIN32
        if(handle_!=INVALID_HANDLE_VALUE)CloseHandle(handle_);
#else
        if(fd_>=0)::close(fd_);
#endif
    }
    File(const File&)=delete;File& operator=(const File&)=delete;
    bool valid() const {return valid_&&size_>0&&size_<=wire::kMaxReplayBytes;}
    uint64_t size() const {return size_;}
    bool read(uint64_t offset,uint8_t* out,size_t count) {
        if(!valid() || offset>size_ || count>size_-offset)return false;
#ifdef _WIN32
        LARGE_INTEGER pos{};pos.QuadPart=LONGLONG(offset);
        if(!SetFilePointerEx(handle_,pos,nullptr,FILE_BEGIN))return false;
        while(count){DWORD got=0;if(!ReadFile(handle_,out,DWORD(count),&got,nullptr)||got==0)return false;
            out+=got;count-=got;}
#else
        while(count){const auto got=::pread(fd_,out,count,off_t(offset));
            if(got<0&&errno==EINTR)continue;
            if(got<=0)return false;
            out+=got;count-=size_t(got);offset+=uint64_t(got);}
#endif
        return true;
    }
private:
#ifdef _WIN32
    HANDLE handle_=INVALID_HANDLE_VALUE;
#else
    int fd_=-1;
#endif
    uint64_t size_=0;bool valid_=false;
};
std::optional<std::filesystem::path> artifactPath(const std::filesystem::path& directory,
    const IssuedBattle& battle,const VerifiedMatchResult& result) {
    if(!digest(result.replayDigest)||battle.id.empty() || directory.empty())return {};
    const auto expected="battle-"+crypto::toHex(crypto::sha256(battle.id))+"-"+result.replayDigest+".takrep";
    if(result.replayId!=expected)return {};
    return directory/expected; // Generated ASCII, independent of the native locale.
}
std::optional<wire::ReplayMetadata> metadata(File& file,const IssuedBattle& battle,const VerifiedMatchResult& result) {
    if(!file.valid())return {};
    // All valid campaign headers fit here (map ID <=4096 bytes). A malicious
    // length can only consume this bounded buffer, never allocate the file size.
    std::array<uint8_t,8192> bytes{};const auto count=size_t(std::min<uint64_t>(file.size(),bytes.size()));
    if(!file.read(0,bytes.data(),count)||count<12 ||
        bytes[0]!='T'||bytes[1]!='A'||bytes[2]!='K'||bytes[3]!='R')return {};
    net::Reader reader(bytes.data()+4,count-4);net::ReplayHeader header;uint32_t format=0,protocol=0;
    if(!net::readReplayHeader(reader,header,format,protocol) || format!=9 ||
        !net::supportedReplayProtocol(format,protocol) || header.crusades!=1 || header.overridePolicy!=0 ||
        !header.mission.empty() || header.mapId!=battle.context.mapIdentifier ||
        header.mapDigest!=battle.context.mapDigest || header.dataHash!=result.gameplayFingerprint)return {};
    return wire::ReplayMetadata{result.replayDigest,file.size(),format,protocol,header.mapDigest,header.dataHash};
}
}
std::optional<wire::ReplayMetadata> ReplayFiles::inspect(const IssuedBattle& battle,const VerifiedMatchResult& result) const {
    try {const auto path=artifactPath(directory_,battle,result);if(!path)return {};
        File file(*path);return metadata(file,battle,result);
    }catch(const std::exception&){return {};}
}
std::optional<wire::ReplayChunk> ReplayFiles::read(const IssuedBattle& battle,const VerifiedMatchResult& result,
    uint32_t requestId,uint64_t offset,uint32_t limit) const {
    if(limit==0||limit>wire::kReplayChunkBytes)return {};
    try {const auto path=artifactPath(directory_,battle,result);if(!path)return {};
        File file(*path);const auto meta=metadata(file,battle,result);
        if(!meta||offset>=meta->totalBytes)return {};
        wire::ReplayChunk out;out.requestId=requestId;out.battleId=battle.id;out.digest=meta->digest;
        out.totalBytes=meta->totalBytes;out.offset=offset;
        out.bytes.resize(size_t(std::min<uint64_t>(limit,meta->totalBytes-offset)));
        if(!file.read(offset,out.bytes.data(),out.bytes.size()))return {};
        out.final=offset+out.bytes.size()==out.totalBytes;return out;
    }catch(const std::exception&){return {};}
}
}
