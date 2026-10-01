#include "server/crusades/matchmaking.h"
#include <algorithm>
#include <limits>

namespace tak::srv::crusades {
namespace {
bool accountValid(const std::string& value) {
    const auto alnum=[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9');};
    return value.size()>=3&&value.size()<=20&&alnum(value.front())&&
        std::all_of(value.begin(),value.end(),[&](char c){return alnum(c)||c=='_'||c=='-'||c=='.';});
}
bool campaignValid(const std::string& value) {
    if(value.empty()||value.size()>128)return false;
    for(size_t i=0;i<value.size();) {
        const auto lead=static_cast<unsigned char>(value[i++]);
        if(lead<0x80){if(lead<0x20||lead==0x7f)return false;continue;}
        unsigned count;uint32_t point,minimum;
        if(lead>=0xc2&&lead<=0xdf){count=1;point=lead&0x1f;minimum=0x80;}
        else if(lead>=0xe0&&lead<=0xef){count=2;point=lead&0x0f;minimum=0x800;}
        else if(lead>=0xf0&&lead<=0xf4){count=3;point=lead&0x07;minimum=0x10000;}
        else return false;
        while(count--){if(i==value.size())return false;const auto next=static_cast<unsigned char>(value[i++]);
            if((next&0xc0)!=0x80)return false;
            point=(point<<6)|(next&0x3f);}
        if(point<minimum||point>0x10ffff||(point>=0xd800&&point<=0xdfff)||(point>=0x80&&point<=0x9f))return false;
    }
    return true;
}
void timeValid(int64_t now) {
    if(now<0)throw std::runtime_error("invalid matchmaking time");
}
bool sameRequest(const MatchEntry& a,const MatchEntry& b) {
    return a.accountId==b.accountId&&a.campaignId==b.campaignId&&a.territory==b.territory&&
        a.alliance==b.alliance&&a.allegianceRevision==b.allegianceRevision&&a.campaignRevision==b.campaignRevision;
}
}

MatchQueue::MatchQueue(size_t capacity):capacity_(capacity) {
    if(!capacity||capacity>kMaximumCapacity)throw std::runtime_error("invalid matchmaking capacity");
}
MatchEntry MatchQueue::put(MatchEntry entry,int64_t now) {
    timeValid(now);
    if(!accountValid(entry.accountId)||!campaignValid(entry.campaignId)||!entry.territory||
       (entry.alliance!=Alliance::Honor&&entry.alliance!=Alliance::Terror)||
       entry.allegianceRevision<0||entry.campaignRevision<0||entry.expiresUnix<=now||
       entry.expiresUnix-now>kLifetimeSeconds)
        throw std::runtime_error("invalid matchmaking entry");
    expire(now);
    const auto old=entries_.find(entry.accountId);
    if(old!=entries_.end()&&sameRequest(old->second,entry))return old->second;
    if(old==entries_.end()&&entries_.size()>=capacity_)throw std::runtime_error("matchmaking queue is full");
    if(nextSequence_==std::numeric_limits<uint64_t>::max())throw std::runtime_error("matchmaking sequence exhausted");
    entry.sequence=nextSequence_++;
    entries_.insert_or_assign(entry.accountId,entry);
    return entry;
}
bool MatchQueue::erase(const std::string& accountId){return entries_.erase(accountId)!=0;}
const MatchEntry* MatchQueue::find(const std::string& accountId)const {
    const auto found=entries_.find(accountId);return found==entries_.end()?nullptr:&found->second;
}
std::vector<MatchEntry> MatchQueue::expire(int64_t now) {
    timeValid(now);std::vector<MatchEntry> expired;
    for(auto it=entries_.begin();it!=entries_.end();) {
        if(it->second.expiresUnix<=now){expired.push_back(it->second);it=entries_.erase(it);}else ++it;
    }
    std::sort(expired.begin(),expired.end(),[](const auto& a,const auto& b){return a.sequence<b.sequence;});
    return expired;
}
std::optional<MatchEntry> MatchQueue::firstOpponent(const MatchEntry& entry,int64_t now)const {
    timeValid(now);std::optional<MatchEntry> first;
    for(const auto& [account,candidate]:entries_) {
        if(account==entry.accountId||candidate.campaignId!=entry.campaignId||candidate.territory!=entry.territory||
           candidate.campaignRevision!=entry.campaignRevision||candidate.alliance==entry.alliance||candidate.expiresUnix<=now)
            continue;
        if(!first||candidate.sequence<first->sequence)first=candidate;
    }
    return first;
}
std::optional<std::pair<MatchEntry,MatchEntry>> MatchQueue::firstPair(
        const std::string& campaignId,TerritoryId territory,int64_t campaignRevision,int64_t now)const {
    timeValid(now);
    if(!campaignValid(campaignId)||!territory||campaignRevision<0)
        throw std::runtime_error("invalid matchmaking scope");
    const MatchEntry* honor=nullptr;
    const MatchEntry* terror=nullptr;
    for(const auto& [account,candidate]:entries_) {
        (void)account;
        if(candidate.campaignId!=campaignId||candidate.territory!=territory||
           candidate.campaignRevision!=campaignRevision||candidate.expiresUnix<=now)
            continue;
        auto& oldest=candidate.alliance==Alliance::Honor?honor:terror;
        if(!oldest||candidate.sequence<oldest->sequence)oldest=&candidate;
    }
    if(!honor||!terror)return std::nullopt;
    if(honor->sequence<terror->sequence)return std::pair{*honor,*terror};
    return std::pair{*terror,*honor};
}
std::vector<MatchEntry> MatchQueue::entries()const {
    std::vector<MatchEntry> result;result.reserve(entries_.size());
    for(const auto& [account,entry]:entries_){(void)account;result.push_back(entry);}
    std::sort(result.begin(),result.end(),[](const auto& a,const auto& b){return a.sequence<b.sequence;});
    return result;
}
size_t MatchQueue::count(const std::string& campaignId,TerritoryId territory,int64_t now)const {
    timeValid(now);size_t result=0;
    for(const auto& [account,entry]:entries_){(void)account;
        if(entry.campaignId==campaignId&&entry.territory==territory&&entry.expiresUnix>now)++result;}
    return result;
}
} // namespace tak::srv::crusades
