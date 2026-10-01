// Synthetic modern FIFO policy; no retail queue assumptions or assets.
#include "server/crusades/matchmaking.h"
#include <iostream>
#include <limits>

namespace c=tak::srv::crusades;
namespace {
int checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
template<class F>void rejects(F run,const char* message){++checks;try{run();}catch(const std::runtime_error&){return;}throw std::runtime_error(message);}
c::MatchEntry entry(const std::string& account,c::Alliance alliance=c::Alliance::Honor){
    c::MatchEntry result;result.accountId=account;result.campaignId="synthetic";result.territory=1;
    result.alliance=alliance;result.expiresUnix=700;return result;
}
void validation(){
    rejects([]{c::MatchQueue q(0);},"zero capacity accepted");
    rejects([]{c::MatchQueue q(c::MatchQueue::kMaximumCapacity+1);},"unbounded capacity accepted");
    for(int kind=0;kind<13;++kind){c::MatchQueue q;auto bad=entry("alice");
        switch(kind){case 0:bad.accountId="Alice";break;case 1:bad.accountId="ab";break;
        case 2:bad.campaignId.clear();break;case 3:bad.campaignId=std::string(129,'a');break;
        case 4:bad.territory=0;break;case 5:bad.alliance=static_cast<c::Alliance>(9);break;
        case 6:bad.allegianceRevision=-1;break;case 7:bad.campaignRevision=-1;break;
        case 8:bad.expiresUnix=100;break;case 9:bad.expiresUnix=701;break;
        case 10:bad.campaignId="bad\n";break;case 11:bad.campaignId=std::string("bad\0",4);break;
        case 12:bad.campaignId=std::string(1,char(0xff));break;}
        rejects([&]{q.put(bad,100);},"invalid entry accepted");check(q.size()==0,"invalid entry changed queue");
    }
    c::MatchQueue q;rejects([&]{q.put(entry("alice"),-1);},"negative insertion time accepted");
    rejects([&]{q.expire(-1);},"negative expiry time accepted");
    rejects([&]{q.firstOpponent(entry("alice"),-1);},"negative matching time accepted");
    rejects([&]{q.firstPair("synthetic",1,0,-1);},"negative pair time accepted");
    rejects([&]{q.firstPair("",1,0,100);},"empty pair campaign accepted");
    rejects([&]{q.firstPair("synthetic",0,0,100);},"zero pair territory accepted");
    rejects([&]{q.firstPair("synthetic",1,-1,100);},"negative pair revision accepted");
    rejects([&]{q.count("synthetic",1,-1);},"negative count time accepted");
    auto unicode=entry("alice");unicode.campaignId="Darien \xc3\xa9";
    check(q.put(unicode,100).campaignId==unicode.campaignId,"valid UTF-8 campaign ID rejected");
    c::MatchQueue high;auto last=entry("alice");last.expiresUnix=std::numeric_limits<int64_t>::max();
    check(high.put(last,last.expiresUnix-600).expiresUnix==last.expiresUnix,"upper signed time overflow");
}
void fifoAndReplacement(){
    c::MatchQueue q(3);auto alice=q.put(entry("alice"),100);
    auto zed=q.put(entry("zed",c::Alliance::Terror),100);
    auto bob=q.put(entry("bob",c::Alliance::Terror),100);
    check(alice.sequence<zed.sequence&&zed.sequence<bob.sequence,"insertion sequence not FIFO");
    check(q.firstOpponent(alice,100)->accountId=="zed","lexical order replaced FIFO");
    auto again=entry("zed",c::Alliance::Terror);again.expiresUnix=750;again.sequence=99999;
    auto unchanged=q.put(again,150);
    check(unchanged.sequence==zed.sequence&&unchanged.expiresUnix==zed.expiresUnix,"idempotent request renewed or changed FIFO");
    check(q.entries().at(1).accountId=="zed","entry enumeration not FIFO");
    again.campaignRevision=1;auto changed=q.put(again,150);
    check(changed.sequence>bob.sequence&&q.size()==3,"replacement did not move to FIFO back");
    check(q.firstOpponent(alice,150)->accountId=="bob","revision mismatch candidate matched");
    rejects([&]{q.put(entry("carol"),150);},"capacity overflow accepted");
    check(q.size()==3&&!q.find("carol"),"full rejection changed queue");
    check(q.erase("bob")&&!q.erase("bob")&&!q.find("bob"),"account removal incorrect");
    check(!q.firstOpponent(alice,150),"removed account still matched");
    again.campaignRevision=0;again.territory=2;q.put(again,150);
    check(!q.firstOpponent(alice,150),"different territory matched");
    again.territory=1;again.campaignId="other";q.put(again,150);
    check(q.count("synthetic",1,150)==1&&!q.firstOpponent(alice,150),"cross-campaign matching or count");
    again.campaignId="synthetic";again.alliance=c::Alliance::Honor;q.put(again,150);
    check(!q.firstOpponent(alice,150),"same alliance matched");
}
void expiry(){
    c::MatchQueue q(2);auto alice=q.put(entry("alice"),100);auto bob=q.put(entry("bob",c::Alliance::Terror),100);
    check(q.firstOpponent(alice,699)->accountId=="bob"&&q.count("synthetic",1,699)==2,"predeadline entries vanished");
    check(!q.firstOpponent(alice,700)&&q.count("synthetic",1,700)==0,"expired account matched or counted");
    auto removed=q.expire(700);check(removed.size()==2&&removed[0].sequence==alice.sequence&&removed[1].sequence==bob.sequence&&q.size()==0,"expiry boundary or FIFO removed order");
    auto replacement=entry("carol");replacement.expiresUnix=1300;
    check(q.put(replacement,700).sequence>bob.sequence&&q.size()==1,"expired capacity not reusable");
    replacement=entry("david");replacement.expiresUnix=1900;q.put(replacement,1300);
    check(q.size()==1&&!q.find("carol"),"insertion failed to prune expired entry");
    check(!q.firstOpponent(entry("david",c::Alliance::Terror),1300),"account matched itself");
}
void pairAfterFailedIssuance(){
    c::MatchQueue q;
    auto zed=q.put(entry("zed"),100);
    check(!q.firstPair("synthetic",1,0,100),"one-sided queue produced pair");
    auto tom=q.put(entry("tom",c::Alliance::Terror),100);
    auto first=q.firstPair("synthetic",1,0,100);
    check(first&&first->first.accountId=="zed"&&first->second.accountId=="tom","oldest pair or host order incorrect");
    // Simulate a transient room/DB issuance failure: neither reservation is
    // removed. A new request from either side must not jump the waiting pair.
    auto alice=entry("alice");alice.expiresUnix=800;
    auto bob=entry("bob",c::Alliance::Terror);bob.expiresUnix=800;
    q.put(alice,200);q.put(bob,200);
    auto retried=q.firstPair("synthetic",1,0,200);
    check(retried&&retried->first.sequence==zed.sequence&&retried->second.sequence==tom.sequence,
          "new arrivals bypassed pair after failed issuance");
    check(q.size()==4&&q.find("zed")->expiresUnix==zed.expiresUnix&&q.find("tom")->expiresUnix==tom.expiresUnix,
          "pair selection altered positions or deadlines");
    check(!q.firstPair("other",1,0,200)&&!q.firstPair("synthetic",2,0,200)&&!q.firstPair("synthetic",1,1,200),
          "pair crossed campaign, territory or revision");
    auto deadline=q.firstPair("synthetic",1,0,700);
    check(deadline&&deadline->first.accountId=="alice"&&deadline->second.accountId=="bob",
          "expired older pair prevented eligible successor");
    q.erase("zed");q.erase("tom");
    auto next=q.firstPair("synthetic",1,0,200);
    check(next&&next->first.accountId=="alice"&&next->second.accountId=="bob","successful removal lost next FIFO pair");
    c::MatchQueue terrorFirst;
    terrorFirst.put(entry("tom",c::Alliance::Terror),100);terrorFirst.put(entry("zed"),100);
    auto oppositeHost=terrorFirst.firstPair("synthetic",1,0,100);
    check(oppositeHost&&oppositeHost->first.accountId=="tom"&&oppositeHost->second.accountId=="zed",
          "host ordering favored alliance instead of arrival");
}
}
int main(){try{validation();fifoAndReplacement();expiry();pairAfterFailedIssuance();std::cout<<"PASS: "<<checks<<" modern matchmaking checks\n";return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<'\n';return 1;}}
