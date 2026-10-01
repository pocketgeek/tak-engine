// Local synthetic artifact access: no retail assets or client-controlled paths.
#include "server/crusades/replayfiles.h"
#include "net/crypto.h"
#include "net/replayhdr.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <limits>
#ifndef _WIN32
#include <sys/stat.h>
#endif

namespace c=tak::srv::crusades;
namespace n=tak::net;
namespace w=n::crusades;
namespace fs=std::filesystem;
namespace {
int checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
std::string hash(const w::Bytes& bytes){return tak::crypto::toHex(tak::crypto::sha256(bytes.data(),bytes.size()));}
struct Sample {
    c::IssuedBattle battle{};c::VerifiedMatchResult result{};n::ReplayHeader header{};
    Sample(){battle.id="issued:synthetic";battle.campaignId="test";battle.territory=1;
        battle.context.mapIdentifier="one.ota";battle.context.mapDigest=std::string(64,'a');
        result.gameplayFingerprint=UINT64_C(0x8000000000000001);
        header.mapId=battle.context.mapIdentifier;header.mapDigest=battle.context.mapDigest;
        header.crusades=1;header.overridePolicy=0;header.engineVersion="synthetic-build";
        header.dataHash=result.gameplayFingerprint;header.unitCap=2000;
        header.slotType[0]=header.slotType[1]=1;header.slotFaction[1]=1;header.slotTeam[1]=1;
    }
};
w::Bytes recording(const n::ReplayHeader& header,uint32_t protocol=211){
    n::Writer writer;n::writeReplayHeader(writer,header);
    for(unsigned i=0;i<4;++i)writer.b[8+i]=uint8_t(protocol>>(8*i));
    // The artifact layer checks only a bounded header. Complete replay parsing
    // and final SHA verification belong to the downloading client.
    writer.b.resize(writer.b.size()+w::kReplayChunkBytes+117,0x5a);return writer.b;
}
fs::path save(const fs::path& root,Sample& sample,const w::Bytes& bytes){
    sample.result.replayDigest=hash(bytes);
    sample.result.replayId="battle-"+tak::crypto::toHex(tak::crypto::sha256(sample.battle.id))+"-"+sample.result.replayDigest+".takrep";
    const auto path=root/sample.result.replayId; // Generated filename is ASCII.
    std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));
    if(!file)throw std::runtime_error("write synthetic artifact");
    return path;
}
void validAndChunks(const fs::path& root){
    c::ReplayFiles files(root);
    for(uint32_t protocol:{210u,211u}){
        Sample sample;const auto bytes=recording(sample.header,protocol);const auto path=save(root,sample,bytes);
        const auto metadata=files.inspect(sample.battle,sample.result);
        check(metadata&&metadata->digest==sample.result.replayDigest&&metadata->totalBytes==bytes.size()&&metadata->format==9&&metadata->protocolVersion==protocol,"compatible replay metadata rejected");
        check(metadata->mapDigest==sample.battle.context.mapDigest&&metadata->gameplayFingerprint==sample.result.gameplayFingerprint,"metadata lost gameplay identity");
        w::Bytes downloaded;uint64_t offset=0;
        while(offset<bytes.size()){
            const auto chunk=files.read(sample.battle,sample.result,71,offset,w::kReplayChunkBytes);
            check(chunk&&chunk->requestId==71&&chunk->battleId==sample.battle.id&&chunk->offset==offset&&chunk->totalBytes==bytes.size()&&chunk->digest==sample.result.replayDigest,"chunk identity/range changed");
            check(!chunk->bytes.empty()&&chunk->bytes.size()<=w::kReplayChunkBytes&&chunk->final==(offset+chunk->bytes.size()==bytes.size()),"chunk bounds/final incorrect");
            downloaded.insert(downloaded.end(),chunk->bytes.begin(),chunk->bytes.end());offset+=chunk->bytes.size();
        }
        check(downloaded==bytes&&hash(downloaded)==metadata->digest,"download chunks changed bytes/digest");
        const auto last=files.read(sample.battle,sample.result,72,bytes.size()-1,32);
        check(last&&last->bytes.size()==1&&last->final,"tail chunk did not clamp to file boundary");
        check(!files.read(sample.battle,sample.result,1,bytes.size(),1)&&!files.read(sample.battle,sample.result,1,UINT64_MAX,1),"past-end offset accepted");
        check(!files.read(sample.battle,sample.result,1,0,0)&&!files.read(sample.battle,sample.result,1,0,w::kReplayChunkBytes+1),"invalid chunk limits accepted");
        fs::remove(path);check(!files.inspect(sample.battle,sample.result)&&!files.read(sample.battle,sample.result,1,0,16),"deleted artifact remained readable");
        check(sample.result.replayDigest==metadata->digest,"artifact deletion changed trusted result metadata");
    }
}
void identityAndHeader(const fs::path& root){
    c::ReplayFiles files(root);Sample sample;auto bytes=recording(sample.header);const auto path=save(root,sample,bytes);
    auto bad=sample.result;bad.replayId="../"+bad.replayId;check(!files.inspect(sample.battle,bad),"client-like traversal replay ID accepted");
    bad=sample.result;bad.replayId="/tmp/arbitrary.takrep";check(!files.inspect(sample.battle,bad),"absolute replay path accepted");
    bad=sample.result;bad.replayDigest=std::string(64,'Z');check(!files.inspect(sample.battle,bad),"invalid digest accepted");
    bad=sample.result;bad.replayDigest=std::string(63,'a');check(!files.inspect(sample.battle,bad),"short digest accepted");
    auto wrong=sample.battle;wrong.id="issued:another";check(!files.inspect(wrong,sample.result),"another battle could fetch artifact");
    wrong.id.clear();check(!files.inspect(wrong,sample.result),"empty battle identity accepted");
    check(!c::ReplayFiles({}).inspect(sample.battle,sample.result),"empty artifact root accepted");
    fs::remove(path);
    for(int kind=0;kind<10;++kind){
        Sample altered;auto header=altered.header;
        if(kind==0)header.mapId="other.ota";
        if(kind==1)header.mapDigest=std::string(64,'b');
        if(kind==2)header.dataHash^=1;
        if(kind==3)header.crusades=0;
        if(kind==4)header.overridePolicy=1;
        if(kind==5)header.mission="camp01";
        if(kind==9)header.mapId=std::string(9000,'m');
        auto data=recording(header,kind==6?212u:211u);
        if(kind==7)data[4]=8;
        if(kind==8)data[0]='X';
        const auto badPath=save(root,altered,data);
        check(!files.inspect(altered.battle,altered.result)&&!files.read(altered.battle,altered.result,1,0,16),"bad or incompatible artifact header accepted");fs::remove(badPath);
    }
    Sample shortSample;n::Writer writer;n::writeReplayHeader(writer,shortSample.header);
    for(size_t count=0;count<writer.b.size();++count){const w::Bytes partial(writer.b.begin(),writer.b.begin()+std::ptrdiff_t(count));const auto shortPath=save(root,shortSample,partial);
        check(!files.inspect(shortSample.battle,shortSample.result),"truncated header accepted");fs::remove(shortPath);}
}
void specialFilesAndCorruption(const fs::path& root){
    c::ReplayFiles files(root);Sample sample;auto bytes=recording(sample.header);const auto path=save(root,sample,bytes);
    // Sparse resizing verifies the cap without allocating or reading 512 MiB.
    fs::resize_file(path,w::kMaxReplayBytes);const auto boundary=files.inspect(sample.battle,sample.result);
    check(boundary&&boundary->totalBytes==w::kMaxReplayBytes,"exact maximum artifact size rejected");
    const auto tail=files.read(sample.battle,sample.result,1,w::kMaxReplayBytes-1,1);
    check(tail&&tail->bytes.size()==1&&tail->final,"maximum-size tail range overflowed");
    fs::resize_file(path,w::kMaxReplayBytes+1);check(!files.inspect(sample.battle,sample.result),"oversize artifact accepted");
    fs::remove(path);fs::create_directory(path);check(!files.inspect(sample.battle,sample.result),"directory artifact accepted");fs::remove(path);
    const auto target=root/"symlink-target.takrep";{std::ofstream file(target,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));}
    std::error_code error;fs::create_symlink(target,path,error);
    if(!error){check(!files.inspect(sample.battle,sample.result)&&!files.read(sample.battle,sample.result,1,0,16),"symlink artifact followed");fs::remove(path);}
    else std::cout<<"SKIP: symlink creation unavailable: "<<error.message()<<'\n';
#ifndef _WIN32
    check(::mkfifo(path.c_str(),0600)==0,"create FIFO test artifact");
    check(!files.inspect(sample.battle,sample.result)&&!files.read(sample.battle,sample.result,1,0,16),"nonregular FIFO artifact accepted");fs::remove(path);
#endif
    save(root,sample,bytes);const auto expected=sample.result.replayDigest;
    bytes.back()^=1;{std::ofstream file(path,std::ios::binary|std::ios::trunc);file.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));}
    const auto metadata=files.inspect(sample.battle,sample.result);
    check(metadata&&metadata->digest==expected,"body corruption unexpectedly erased trusted header metadata");
    check(hash(bytes)!=metadata->digest,"final client SHA would not detect body corruption");
    const auto chunk=files.read(sample.battle,sample.result,1,bytes.size()-1,16);
    check(chunk&&chunk->bytes[0]==bytes.back()&&sample.result.replayDigest==expected,"body corruption changed authoritative result digest");
}
}
int main(){const auto root=fs::temp_directory_path()/("tak-replay-files-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup{fs::path path;~Cleanup(){std::error_code error;fs::remove_all(path,error);}}cleanup{root};
    try{fs::create_directories(root);validAndChunks(root);identityAndHeader(root);specialFilesAndCorruption(root);std::cout<<"PASS: "<<checks<<" Crusades replay artifact checks\n";return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
