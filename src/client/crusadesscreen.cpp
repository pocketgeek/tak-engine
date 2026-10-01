#include "client/crusadesscreen.h"
#include "client/crusadesmap.h"
#include "client/font.h"
#include "client/blockfont.h"
#include "net/client.h"
#include "net/auth.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include "net/replayhdr.h"
#include <iomanip>
#include <set>
#include <sstream>
#include <utility>
namespace tak {
namespace {
namespace cw=net::crusades;
constexpr SDL_Color ink{235,228,208,255}, muted{165,166,159,255}, gold{235,193,103,255};
bool contains(SDL_Rect r,int x,int y){return x>=r.x&&y>=r.y&&x<r.x+r.w&&y<r.y+r.h;}
std::string glyphs(const std::string& utf8,bool bitmap){
 std::string out;
 for(size_t i=0;i<utf8.size();){uint32_t cp=uint8_t(utf8[i++]);int n=0;if(cp>=0xc0&&cp<0xe0){cp&=31;n=1;}else if(cp>=0xe0&&cp<0xf0){cp&=15;n=2;}else if(cp>=0xf0){cp&=7;n=3;}
  while(n--&&i<utf8.size())cp=(cp<<6)|(uint8_t(utf8[i++])&63);
  if(cp<128){out+=char(cp);continue;}if(bitmap&&cp<=255){out+=char(cp);continue;}
  const char* base="?";if(cp==0x2019||cp==0x2018)base="'";else if(cp==0x2013||cp==0x2014)base="-";
  else if((cp>=192&&cp<=197)||(cp>=224&&cp<=229))base="A";else if(cp==199||cp==231)base="C";
  else if((cp>=200&&cp<=203)||(cp>=232&&cp<=235))base="E";else if((cp>=204&&cp<=207)||(cp>=236&&cp<=239))base="I";
  else if(cp==209||cp==241)base="N";else if((cp>=210&&cp<=214)||(cp>=242&&cp<=246)||cp==216||cp==248)base="O";
  else if((cp>=217&&cp<=220)||(cp>=249&&cp<=252))base="U";else if(cp==221||cp==253||cp==255)base="Y";
  else if(cp==198||cp==230)base="AE";else if(cp==223)base="SS";
  out+=base;
 }
 return out;
}
std::string owner(const std::optional<cw::Owner>& value){if(!value)return "Unknown";switch(*value){case cw::Owner::Honor:return "Honor";case cw::Owner::Terror:return "Terror";default:return "Contested";}}
SDL_Color color(const std::optional<cw::Owner>& value){if(!value)return {105,109,109,255};if(*value==cw::Owner::Honor)return {80,136,210,255};if(*value==cw::Owner::Terror)return {201,86,83,255};return {176,153,75,255};}
std::string metric(const std::optional<double>& value){if(!value)return "Unknown";std::ostringstream out;out<<std::setprecision(7)<<*value;return out.str();}
std::string phase(cw::BattlePhase value){switch(value){case cw::BattlePhase::Issued:return "Invitation";case cw::BattlePhase::Started:return "In progress / result pending";case cw::BattlePhase::Completed:return "Completed";case cw::BattlePhase::Cancelled:return "Cancelled";default:return "Expired";}}
std::string recordedDate(uint64_t stamp){
 if(stamp>uint64_t(INT64_MAX))return "Unknown date";
 const auto time=std::time_t(stamp);if(uint64_t(time)!=stamp)return "Unknown date";
 const auto* utc=std::gmtime(&time);if(!utc)return "Unknown date";
 char value[32]{};std::strftime(value,sizeof value,"%Y-%m-%d %H:%M UTC",utc);return value;
}
std::string outcome(cw::Outcome value){switch(value){
 case cw::Outcome::Victory:return "Victory";case cw::Outcome::Resignation:return "Resignation";
 case cw::Outcome::Disconnect:return "Disconnect";case cw::Outcome::Timeout:return "Timeout";
 case cw::Outcome::ServerAbort:return "Server abort";case cw::Outcome::Draw:return "Draw";
 case cw::Outcome::RefereeFailure:return "Referee failure";case cw::Outcome::Desync:return "Desync";
 case cw::Outcome::InvalidClient:return "Invalid client";default:return "Participant substitution";
}}
std::string accounts(const cw::HistoryBattle& battle){std::string out;for(const auto& p:battle.participants){if(!out.empty())out+=" vs ";out+=p.accountId;}return out;}
struct Clip {SDL_Renderer* r;SDL_Rect old;bool on;Clip(SDL_Renderer* renderer,SDL_Rect rect):r(renderer),on(SDL_RenderIsClipEnabled(r)==SDL_TRUE){SDL_RenderGetClipRect(r,&old);SDL_RenderSetClipRect(r,&rect);}~Clip(){SDL_RenderSetClipRect(r,on?&old:nullptr);}};
}
struct CrusadesScreen::Impl {
 SDL_Renderer* ren;net::MpClient& mp;Font font;float fontScale=1;SDL_Texture* fallbackFont=nullptr;
 crusadesmap::LoadResult local;SDL_Texture* mapTexture=nullptr;std::string mapCampaign;uint64_t mapRevision=UINT64_MAX;bool retail=false;
 int width=960,height=540,listOffset=0,detailOffset=0,battleOffset=0;uint32_t selected=0,pendingSelected=0,lastCatalogId=UINT32_MAX,pendingMatchRequest=0;enum class PendingMatch{None,Search,Cancel};PendingMatch pendingMatchAction=PendingMatch::None;
 std::string selectedCampaign,search,opponent,selectedBattle,statusText,boardQueryKey;enum class Focus{None,Search,Opponent}focus=Focus::None;
 bool historyOpen=false;int historyOffset=0,historyPage=0;std::string selectedHistoryBattle,historyQueryKey;uint32_t historyRequest=0;
 std::vector<std::optional<cw::HistoryCursor>> historyCursors{std::nullopt};
 std::set<std::string> queriedBattles;std::vector<uint32_t> filtered;SDL_Rect mapRect{228,112,390,228},imageRect=mapRect;
 enum class Command {Back,Reconnect,Refresh,Campaign,More,Honor,Terror,Issue,Find,CancelSearch,Join,Cancel,PrevBattle,NextBattle,History,PrevHistory,NextHistory,Watch,CancelDownload};
 struct Button {SDL_Rect rect;std::string label;Command command;bool enabled;};std::vector<Button> buttons;
 Impl(SDL_Renderer* r,const hpi::Vfs& v,net::MpClient& client):ren(r),mp(client),local(crusadesmap::loadPresentation(v)){
  for(const char* path:{"fonts/b_times new roman (100).gaf","fonts/bodfontbody.gaf"})try{font=Font(r,v,path);if(font.ok())break;}catch(...){}
  if(font.ok())fontScale=14.f/std::max(1,font.height());
  else if(auto* surface=SDL_CreateRGBSurfaceWithFormat(0,96,64,32,SDL_PIXELFORMAT_RGBA32)){
   SDL_FillRect(surface,nullptr,SDL_MapRGBA(surface->format,0,0,0,0));
   for(int c=0;c<128;++c)if(const auto* bits=blockGlyph(char(std::toupper(c))))for(int x=0;x<5;++x)for(int y=0;y<7;++y)if(bits[x]&(1<<y)){
    auto* row=reinterpret_cast<uint32_t*>(static_cast<uint8_t*>(surface->pixels)+(c/16*8+y)*surface->pitch);row[c%16*6+x]=SDL_MapRGBA(surface->format,255,255,255,255);
   }
   fallbackFont=SDL_CreateTextureFromSurface(ren,surface);SDL_FreeSurface(surface);
   if(fallbackFont){SDL_SetTextureBlendMode(fallbackFont,SDL_BLENDMODE_BLEND);SDL_SetTextureScaleMode(fallbackFont,SDL_ScaleModeNearest);}
  }
 }
 ~Impl(){if(fallbackFont)SDL_DestroyTexture(fallbackFont);if(mapTexture)SDL_DestroyTexture(mapTexture);if(focus!=Focus::None)SDL_StopTextInput();}
 const cw::Snapshot* snapshot()const{return mp.campaignReplica().find(mp.subscribedCampaign());}
 bool connected()const{return mp.state()!=net::MpClient::State::Done&&mp.state()!=net::MpClient::State::Offline;}
 bool authenticated()const{return connected()&&(mp.auth()==net::MpClient::Auth::Ok||mp.auth()==net::MpClient::Auth::Created);}
 const cw::PlayerStatus* player()const {const auto& p=mp.playerCampaignStatus();return p&&p->campaignId==mp.subscribedCampaign()?&*p:nullptr;}
 const cw::MatchmakingStatus* board()const {const auto& b=mp.campaignMatchmaking();return b&&b->campaignId==mp.subscribedCampaign()?&*b:nullptr;}
 const cw::MatchTerritory* matchTerritory()const {const auto* b=board();const auto* s=snapshot();if(!b||!s||b->campaignRevision!=s->revision)return nullptr;auto it=std::find_if(b->territories.begin(),b->territories.end(),[&](const auto& t){return t.id==selected;});return it==b->territories.end()?nullptr:&*it;}
 const cw::Territory* territory()const {const auto* s=snapshot();if(!s)return nullptr;auto i=std::find_if(s->territories.begin(),s->territories.end(),[&](const auto& t){return t.id==selected;});return i==s->territories.end()?nullptr:&*i;}
 const cw::TerritoryHistory* history()const{const auto* h=selected?mp.territoryHistory(mp.subscribedCampaign(),selected):nullptr;return h&&(!historyRequest||h->requestId==historyRequest)?h:nullptr;}
 const cw::HistoryBattle* historyBattle()const{const auto* h=history();if(!h)return nullptr;auto it=std::find_if(h->entries.begin(),h->entries.end(),[&](const auto& b){return b.battleId==selectedHistoryBattle;});return it==h->entries.end()?nullptr:&*it;}
 bool replaySafe()const{const auto* p=player();return authenticated()&&mp.state()==net::MpClient::State::Lobby&&p&&p->allegiance&&(!board()||!board()->searchingTerritory)&&std::none_of(p->battles.begin(),p->battles.end(),[](const auto& b){return b.status==cw::BattlePhase::Issued||b.status==cw::BattlePhase::Started;});}
 void resetHistory(){historyPage=historyOffset=0;historyCursors={std::nullopt};selectedHistoryBattle.clear();historyQueryKey.clear();historyRequest=0;mp.cancelCampaignReplayDownload();}
 void requestHistory(){const auto* p=player();if(!historyOpen||!selected||!authenticated()||!p||!p->allegiance)return;const auto key=mp.subscribedCampaign()+":"+std::to_string(selected)+":"+std::to_string(historyPage);if(key!=historyQueryKey){const auto id=mp.getTerritoryHistory(mp.subscribedCampaign(),selected,historyCursors[size_t(historyPage)],16);if(id){historyRequest=id;historyQueryKey=key;}}}
 void rect(SDL_Rect r,SDL_Color c){SDL_SetRenderDrawColor(ren,c.r,c.g,c.b,c.a);SDL_RenderFillRect(ren,&r);}
 int textWidth(const std::string& s)const{return font.ok()?font.width(s,fontScale):int(s.size()*8.4f);}
 void text(const std::string& value,int x,int y,int maxWidth,SDL_Color c=ink){
  auto s=glyphs(value,font.ok());while(!s.empty()&&textWidth(s)>maxWidth)s.pop_back();
  if(font.ok()){float top,h;font.vbounds(s,fontScale,top,h);font.draw(ren,s,float(x),float(y)-top,fontScale,c);}else if(fallbackFont){SDL_SetTextureColorMod(fallbackFont,c.r,c.g,c.b);float at=float(x);for(unsigned char ch:s){const int code=ch<128?ch:'?';SDL_Rect source{code%16*6,code/16*8,6,8};SDL_FRect destination{at,float(y),8.4f,11.2f};SDL_RenderCopyF(ren,fallbackFont,&source,&destination);at+=8.4f;}}else drawBlockText(ren,s,float(x),float(y),1.4f,c);
 }
 void button(SDL_Rect r,std::string label,Command command,bool enabled=true){buttons.push_back({r,label,command,enabled});rect(r,enabled?SDL_Color{65,65,57,255}:SDL_Color{37,40,39,255});SDL_SetRenderDrawColor(ren,enabled?155:75,enabled?139:77,enabled?96:69,255);SDL_RenderDrawRect(ren,&r);text(label,r.x+7,r.y+7,r.w-14,enabled?ink:muted);}
 void focusOn(Focus f){focus=f;if(f==Focus::None)SDL_StopTextInput();else SDL_StartTextInput();}
 void refresh(){if(!authenticated())return;mp.listCampaigns();if(!mp.subscribedCampaign().empty()){mp.getCampaignSnapshot(mp.subscribedCampaign());mp.getPlayerCampaignStatus(mp.subscribedCampaign());mp.getCampaignMatchmaking(mp.subscribedCampaign());}queriedBattles.clear();statusText.clear();resetHistory();requestHistory();}
 void select(uint32_t id){if(selected!=id)resetHistory();selected=id;pendingSelected=0;detailOffset=0;requestHistory();}
 void update(){
  const auto* s=snapshot();if(selectedCampaign!=mp.subscribedCampaign()){selectedCampaign=mp.subscribedCampaign();selected=0;listOffset=detailOffset=0;selectedBattle.clear();queriedBattles.clear();boardQueryKey.clear();pendingMatchRequest=0;pendingMatchAction=PendingMatch::None;statusText.clear();resetHistory();}
  if(pendingMatchRequest&&board()&&(board()->requestId==pendingMatchRequest||(pendingMatchAction==PendingMatch::Search&&(board()->searchingTerritory||!board()->canSearch))||(pendingMatchAction==PendingMatch::Cancel&&!board()->searchingTerritory))){pendingMatchRequest=0;pendingMatchAction=PendingMatch::None;statusText.clear();}
  filtered.clear();if(s){std::string needle=glyphs(search,false);std::transform(needle.begin(),needle.end(),needle.begin(),[](unsigned char c){return char(std::tolower(c));});for(const auto& t:s->territories){auto name=glyphs(t.displayName,false);std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return char(std::tolower(c));});if(needle.empty()||name.find(needle)!=std::string::npos||std::to_string(t.id).find(needle)!=std::string::npos)filtered.push_back(t.id);}if(pendingSelected){auto it=std::find_if(s->territories.begin(),s->territories.end(),[&](const auto& t){return t.id==pendingSelected;});if(it!=s->territories.end())select(pendingSelected);else pendingSelected=0;}if(selected&&!territory())selected=0;}
  listOffset=std::clamp(listOffset,0,std::max(0,int(filtered.size())-13));
  if(s&&(mapCampaign!=s->campaignId||mapRevision!=s->revision)){
   if(mapTexture){SDL_DestroyTexture(mapTexture);mapTexture=nullptr;}mapCampaign=s->campaignId;mapRevision=s->revision;retail=local.presentation&&local.presentation->compatible(*s);
   if(retail)try{auto im=local.presentation->compose(*s);mapTexture=SDL_CreateTexture(ren,SDL_PIXELFORMAT_RGBA32,SDL_TEXTUREACCESS_STATIC,int(im.width),int(im.height));if(!mapTexture||SDL_UpdateTexture(mapTexture,nullptr,im.rgba.data(),int(im.width*4))!=0){if(mapTexture)SDL_DestroyTexture(mapTexture);mapTexture=nullptr;retail=false;}}catch(...){retail=false;}
  }
  if(authenticated()){if(const auto* p=player()){
   // The server pushes queue changes. Only re-read when authoritative enrollment
   // or strategic revision changes, rather than polling the board every frame.
   if(s){const auto key=selectedCampaign+":"+std::to_string(s->revision)+":"+(p->allegiance?std::to_string(p->allegiance->revision):"none");if(key!=boardQueryKey&&mp.getCampaignMatchmaking(selectedCampaign))boardQueryKey=key;}
   for(const auto& b:p->battles){const auto key=b.id+":"+std::to_string(int(b.status));if(queriedBattles.size()>128)queriedBattles.clear();if(!queriedBattles.count(key)&&mp.getCampaignBattleStatus(b.id))queriedBattles.insert(key);}
  }}
  requestHistory();
  if(const auto* h=history()){historyOffset=std::clamp(historyOffset,0,std::max(0,int(h->entries.size())-3));if(!historyBattle()&&!h->entries.empty())selectedHistoryBattle=h->entries.front().battleId;}
  // A matched guest receives an invitation without entering its room. Select
  // that card by default so joining does not require finding it among history.
  if(selectedBattle.empty())for(const auto* b:battles())if(b->status==cw::BattlePhase::Issued&&b->roomId){selectedBattle=b->battleId;auto all=battles();auto it=std::find(all.begin(),all.end(),b);battleOffset=std::max(0,int(it-all.begin())-2);break;}
 }
 std::vector<const cw::BattleStatus*> battles()const{std::vector<const cw::BattleStatus*> out;for(const auto& [id,b]:mp.campaignBattles()){(void)id;if(b.campaignId==mp.subscribedCampaign())out.push_back(&b);}return out;}
 void drawMap(const cw::Snapshot& s){
  rect(mapRect,{26,35,34,255});text(retail?"Darien":"Modern schematic - not geography",mapRect.x,mapRect.y-20,mapRect.w,gold);
  if(retail&&mapTexture){float scale=std::min(float(mapRect.w)/local.presentation->width(),float(mapRect.h)/local.presentation->height());imageRect={mapRect.x+(mapRect.w-int(local.presentation->width()*scale))/2,mapRect.y+(mapRect.h-int(local.presentation->height()*scale))/2,int(local.presentation->width()*scale),int(local.presentation->height()*scale)};SDL_RenderCopy(ren,mapTexture,nullptr,&imageRect);if(const auto* p=local.presentation->parcel(selected)){SDL_Rect marker{imageRect.x+int(p->fireX*scale)-4,imageRect.y+int(p->fireY*scale)-4,9,9};SDL_SetRenderDrawColor(ren,255,255,255,255);SDL_RenderDrawRect(ren,&marker);}}
  else {imageRect=mapRect;int cols=std::max(1,int(std::ceil(std::sqrt(s.territories.size()*float(mapRect.w)/mapRect.h))));int rows=std::max(1,int((s.territories.size()+cols-1)/cols));for(size_t i=0;i<s.territories.size();++i){const auto& t=s.territories[i];int x=int(i)%cols,y=int(i)/cols;SDL_Rect cell{mapRect.x+x*mapRect.w/cols,mapRect.y+y*mapRect.h/rows,std::max(1,mapRect.w/cols-2),std::max(1,mapRect.h/rows-2)};rect(cell,color(t.owner));if(cell.w>42&&cell.h>20)text(std::to_string(t.id),cell.x+3,cell.y+4,cell.w-6);if(t.id==selected){SDL_SetRenderDrawColor(ren,255,255,255,255);SDL_RenderDrawRect(ren,&cell);}}}
 }
 std::vector<std::string> details(){std::vector<std::string> lines;const auto* t=territory();if(!t){lines.push_back("Select a territory");return lines;}lines={t->displayName,"Territory "+std::to_string(t->id),"Owner: "+owner(t->owner),"Open battles: "+(t->activity?std::to_string(t->activity->offered):"Unknown"),"Active battles: "+(t->activity?std::to_string(t->activity->active):"Unknown"),"Native faction: "+t->nativeFaction.value_or("Unknown"),"Terrain: "+t->terrain.value_or("Unknown"),"Battle map: "+t->assignedMap.value_or(t->mapIdentifier.value_or("Unknown")),"Fatigue: "+metric(t->recon.fatigueVictoryPoints),"Honor required: "+metric(t->recon.honorRequiredVictoryPoints),"Honor support: "+metric(t->recon.honorSupportVictoryPoints),"Honor battle points: "+metric(t->recon.honorBattleVictoryPoints),"Terror required: "+metric(t->recon.terrorRequiredVictoryPoints),"Terror support: "+metric(t->recon.terrorSupportVictoryPoints),"Terror battle points: "+metric(t->recon.terrorBattleVictoryPoints)};
  if(historyOpen)if(const auto* b=historyBattle()){
   lines={"Battle history",recordedDate(b->recordedUnix),"Outcome: "+outcome(b->result.outcome),"Battle: "+b->battleId,"Map: "+b->mapIdentifier,"Campaign revision: "+std::to_string(b->campaignRevision),"Final tick: "+std::to_string(b->result.finalTick)};
   std::string winners="Winners:";for(const auto& who:b->result.winners)winners+=" "+who;lines.push_back(b->result.winners.empty()?"No verified winner":winners);
   for(const auto& p:b->participants){lines.push_back(p.accountId+" ("+p.faction+")");lines.push_back("Kills: "+std::to_string(p.kills)+"  Losses: "+std::to_string(p.losses));lines.push_back("Score: "+std::to_string(p.score));lines.push_back("Built: "+std::to_string(p.built)+"  Alive: "+std::to_string(p.currentUnits));}
   lines.push_back(b->replay?"Replay recorded (server checks retention)":"Replay unavailable; history retained");
   if(b->replay){lines.push_back("Replay bytes: "+std::to_string(b->replay->totalBytes));lines.push_back("Format: "+std::to_string(b->replay->format)+"  Protocol: "+std::to_string(b->replay->protocolVersion));}
  }
  const auto* m=matchTerritory();if(!historyOpen)lines.insert(lines.begin()+3,{"Match available: "+std::string(m?(m->eligible?"Yes":"No"):"Unknown"),"Waiting Honor: "+(m?std::to_string(m->waitingHonor):"Unknown"),"Waiting Terror: "+(m?std::to_string(m->waitingTerror):"Unknown")});
  if(!historyOpen){if(!t->neighbors)lines.push_back("Neighbors: Unknown");else{std::string list="Neighbors:";for(auto id:*t->neighbors)list+=" "+std::to_string(id);if(t->neighbors->empty())list+=" None";lines.push_back(list);}
  }
  if(!historyOpen&&local.presentation){const auto* p=local.presentation->parcel(t->id);if(p&&p->name==t->displayName&&!p->description.empty()){lines.push_back("Local description:");lines.push_back(p->description);}}
  std::vector<std::string> wrapped;for(const auto& line:lines){std::string part;std::istringstream words(line);std::string word;while(words>>word){if(!part.empty()&&textWidth(glyphs(part+" "+word,font.ok()))>300){wrapped.push_back(part);part.clear();}if(part.empty())while(textWidth(glyphs(word,font.ok()))>300){size_t cut=1;while(cut<word.size()&&textWidth(glyphs(word.substr(0,cut+1),font.ok()))<=300)++cut;while(cut>0&&cut<word.size()&&(uint8_t(word[cut])&0xc0)==0x80)--cut;if(!cut)break;wrapped.push_back(word.substr(0,cut));word.erase(0,cut);}if(!part.empty())part+=' ';part+=word;}wrapped.push_back(part);}return wrapped;
 }
 void drawHistory(){
  rect({228,112,390,298},{26,35,34,255});text("Verified terminal history",234,119,378,gold);
  const auto* p=player();const auto* h=history();
  if(!p||!p->allegiance)text("Join an alliance to view history",234,150,378,muted);
  else if(!h)text("Waiting for territory history",234,150,378,muted);
  else if(h->entries.empty())text("No verified battles recorded",234,150,378,muted);
  else for(int row=0;row<3&&historyOffset+row<int(h->entries.size());++row){const auto& b=h->entries[size_t(historyOffset+row)];const int y=146+row*63;if(b.battleId==selectedHistoryBattle)rect({230,y-3,386,61},{65,64,48,255});text(recordedDate(b.recordedUnix),234,y,378);text(outcome(b.result.outcome),234,y+17,378,gold);text(accounts(b),234,y+34,378);}
  button({228,349,99,27},"Previous",Command::PrevHistory,historyPage>0);
  button({336,349,99,27},"Next",Command::NextHistory,h&&h->nextCursor.has_value());
  const auto* b=historyBattle();const auto& download=mp.campaignReplayDownload();
  const bool downloading=download.state==net::MpClient::CampaignReplayState::Downloading;
  button({446,349,172,27},downloading?"Cancel download":"Watch replay",downloading?Command::CancelDownload:Command::Watch,downloading||(b&&b->replay&&net::supportedReplayProtocol(b->replay->format,b->replay->protocolVersion)&&replaySafe()));
  if(downloading)text("Downloading "+std::to_string(download.receivedBytes)+" / "+std::to_string(download.totalBytes)+" bytes",234,385,378,muted);
  else if(b&&!b->replay)text("Replay unavailable; history retained",234,385,378,muted);
  else if(b&&b->replay&&!net::supportedReplayProtocol(b->replay->format,b->replay->protocolVersion))text("Recorded replay version unsupported",234,385,378,muted);
  else text("Page "+std::to_string(historyPage+1)+" - scroll to browse",234,385,378,muted);
 }
 void draw(int w,int h){width=std::max(1,w);height=std::max(1,h);update();buttons.clear();rect({0,0,960,540},{20,27,29,255});
  text("DARIEN CRUSADES",14,12,630,gold);text("Modern territory FIFO duels - historical capture rules incomplete",14,35,745,muted);
  button({770,10,88,28},"Refresh",Command::Refresh,authenticated());button({866,10,80,28},"Back",Command::Back);
  if(!connected()){text("Disconnected. Sign in again to refresh this campaign.",14,70,900);button({14,108,190,32},"Sign in again",Command::Reconnect);return;}
  if(!authenticated()){text("Sign in to an account to view campaigns.",14,80,880);return;}
  const auto& catalog=mp.campaignList();std::string campaignLabel=mp.subscribedCampaign().empty()?"Select campaign":mp.subscribedCampaign();if(const auto* s=snapshot())campaignLabel=s->displayName;
  button({14,60,360,28},campaignLabel,Command::Campaign,catalog&&!catalog->entries.empty());button({384,60,98,28},catalog&&!catalog->nextCursor.empty()?"More":"First page",Command::More,bool(catalog));
  const auto* ownStatus=player();
  const std::string activeLabel=ownStatus&&ownStatus->battlesTruncated?"Recent active battles: ":"Your active battles: ";
  const auto* mm=board();
  text(mm&&mm->searchingTerritory?"Finding opposite alliance at #"+std::to_string(*mm->searchingTerritory):activeLabel+(ownStatus?std::to_string(std::count_if(ownStatus->battles.begin(),ownStatus->battles.end(),[](const auto& b){return b.status==cw::BattlePhase::Issued||b.status==cw::BattlePhase::Started;})):"Unknown"),502,67,440,muted);
  rect({14,96,202,25},focus==Focus::Search?SDL_Color{68,70,58,255}:SDL_Color{40,46,45,255});text(search.empty()?"Search territories":search,20,102,190);
  const auto* s=snapshot();if(s){if(historyOpen)drawHistory();else drawMap(*s);button({502,90,116,22},historyOpen?"Show map":"History",Command::History,territory()!=nullptr);Clip listClip(ren,{14,126,202,270});for(int row=0;row<13&&listOffset+row<int(filtered.size());++row){auto id=filtered[size_t(listOffset+row)];const auto& t=*std::find_if(s->territories.begin(),s->territories.end(),[&](const auto& v){return v.id==id;});if(id==selected)rect({14,126+row*20,202,20},{72,70,54,255});rect({17,132+row*20,7,7},color(t.owner));text(t.displayName,29,130+row*20,182);} }
  else text(mp.subscribedCampaign().empty()?"No campaign selected":"Waiting for campaign snapshot",238,142,370,muted);
  text(std::to_string(filtered.size())+" territories - scroll to browse",14,400,205,muted);
  rect({630,96,316,320},{31,38,39,255});{Clip clip(ren,{634,100,308,312});auto lines=details();detailOffset=std::clamp(detailOffset,0,std::max(0,int(lines.size())-17));int y=103;for(size_t i=size_t(detailOffset);i<lines.size()&&y<414;++i,y+=18)text(lines[i],638,y,300,i==0?gold:ink);}
  auto bs=battles();if(!historyOpen){battleOffset=std::clamp(battleOffset,0,std::max(0,int(bs.size())-3));for(int i=0;i<3&&battleOffset+i<int(bs.size());++i){const auto& b=*bs[size_t(battleOffset+i)];if(b.battleId==selectedBattle)rect({228,349+i*20,390,20},{66,66,50,255});text("#"+std::to_string(b.territory)+" "+phase(b.status)+(b.result?" - "+(b.result->winners.empty()?std::string("No winner"):b.result->winners.front()):""),234,352+i*20,378);}
  if(bs.empty())text("Your battles will appear here",234,357,380,muted);}
  const auto* p=player();const auto side=p&&p->allegiance?std::optional<cw::Alliance>{p->allegiance->alliance}:std::nullopt;
  text("Allegiance: "+(side?(*side==cw::Alliance::Honor?std::string("Honor"):std::string("Terror")):std::string(p?"Not joined":"Unknown")),14,428,260,gold);
  button({14,450,95,29},"Honor",Command::Honor,p&&(!side||*side!=cw::Alliance::Honor));button({116,450,95,29},"Terror",Command::Terror,p&&(!side||*side!=cw::Alliance::Terror));
  text("Opponent account",228,428,370,muted);rect({228,450,208,29},focus==Focus::Opponent?SDL_Color{68,70,58,255}:SDL_Color{40,46,45,255});text(opponent,234,457,196);
  const auto* t=territory();button({446,450,172,29},"Request battle",Command::Issue,mp.state()==net::MpClient::State::Lobby&&side&&t&&(t->assignedMap||t->mapIdentifier)&&auth::validUsername(opponent)&&mp.campaignReplayDownload().state!=net::MpClient::CampaignReplayState::Downloading);
  const auto* mt=matchTerritory();const bool searching=mm&&mm->searchingTerritory.has_value();
  button({228,485,188,24},searching?"Searching...":"Find opponent",Command::Find,mp.state()==net::MpClient::State::Lobby&&mm&&mm->canSearch&&!searching&&mt&&mt->eligible&&mp.campaignReplayDownload().state!=net::MpClient::CampaignReplayState::Downloading);
  button({426,485,192,24},"Cancel search",Command::CancelSearch,mp.state()==net::MpClient::State::Lobby&&searching);
  text("Opposite alliances only",14,488,200,muted);
  const auto found=mp.campaignBattles().find(selectedBattle);const auto* battle=found==mp.campaignBattles().end()?nullptr:&found->second;
  button({630,430,65,26},"Prev",Command::PrevBattle,!bs.empty());button({702,430,65,26},"Next",Command::NextBattle,!bs.empty());
  button({630,465,150,29},"Join invitation",Command::Join,battle&&battle->status==cw::BattlePhase::Issued&&battle->roomId&&mp.state()==net::MpClient::State::Lobby);
  button({790,465,156,29},"Leave battle",Command::Cancel,mp.state()==net::MpClient::State::InRoom);
  std::string message=statusText;if(message.empty()&&mp.campaignError())message=mp.campaignError()->reason;
  if(mp.campaignReplayDownload().state==net::MpClient::CampaignReplayState::Failed)message=mp.campaignReplayDownload().error;
  if(mp.state()==net::MpClient::State::InRoom)message="Battle lobby ready. Back returns to the lobby.";
  text(message,14,513,932,muted);
 }
 CrusadesScreen::Action input(const SDL_Event& e,int x,int y){x=int(int64_t(x)*960/width);y=int(int64_t(y)*540/height);
  if(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_ESCAPE){focusOn(Focus::None);mp.cancelCampaignReplayDownload();return CrusadesScreen::Action::Back;}
  if(e.type==SDL_TEXTINPUT&&focus!=Focus::None){auto& value=focus==Focus::Search?search:opponent;std::string add=e.text.text;if(value.size()+add.size()<=(focus==Focus::Search?128:20)){value+=add;listOffset=0;}return CrusadesScreen::Action::None;}
  if(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_BACKSPACE&&focus!=Focus::None){auto& value=focus==Focus::Search?search:opponent;if(!value.empty()){size_t i=value.size()-1;while(i>0&&(uint8_t(value[i])&0xc0)==0x80)--i;value.erase(i);}return CrusadesScreen::Action::None;}
  if(e.type==SDL_MOUSEWHEEL){int delta=e.wheel.y;if(e.wheel.direction==SDL_MOUSEWHEEL_FLIPPED)delta=-delta;if(x>=630)detailOffset-=delta*3;else if(historyOpen&&x>=228&&x<630&&y>=112&&y<410)historyOffset-=delta;else if(x>=228&&y>=345&&y<420)battleOffset-=delta;else listOffset-=delta*3;update();return CrusadesScreen::Action::None;}
  if(e.type!=SDL_MOUSEBUTTONDOWN||(e.button.button!=SDL_BUTTON_LEFT&&e.button.button!=SDL_BUTTON_RIGHT))return CrusadesScreen::Action::None;
  if(!authenticated()) {for(const auto& b:buttons)if(b.enabled&&contains(b.rect,x,y)){if(b.command==Command::Back){mp.cancelCampaignReplayDownload();return CrusadesScreen::Action::Back;}if(b.command==Command::Reconnect)return CrusadesScreen::Action::Reconnect;}return CrusadesScreen::Action::None;}
  if(contains({14,96,202,25},x,y)){focusOn(Focus::Search);return CrusadesScreen::Action::None;}if(contains({228,450,208,29},x,y)){focusOn(Focus::Opponent);return CrusadesScreen::Action::None;}focusOn(Focus::None);
  for(const auto& b:buttons)if(contains(b.rect,x,y)){if(!b.enabled)return CrusadesScreen::Action::None;const auto* p=player();switch(b.command){case Command::Back:mp.cancelCampaignReplayDownload();return CrusadesScreen::Action::Back;case Command::Reconnect:return CrusadesScreen::Action::Reconnect;case Command::Refresh:refresh();break;
   case Command::Campaign:{const auto& list=mp.campaignList();if(list&&!list->entries.empty()){auto it=std::find_if(list->entries.begin(),list->entries.end(),[&](const auto& c){return c.id==mp.subscribedCampaign();});int index=it==list->entries.end()?(e.button.button==SDL_BUTTON_RIGHT?0:-1):int(it-list->entries.begin());index=(index+(e.button.button==SDL_BUTTON_RIGHT?-1:1)+int(list->entries.size()))%int(list->entries.size());mp.subscribeCampaign(list->entries[size_t(index)].id);}}break;
   case Command::More:mp.listCampaigns(mp.campaignList()?mp.campaignList()->nextCursor:"");break;
   case Command::Honor:case Command::Terror:if(p)mp.setCampaignAllegiance(mp.subscribedCampaign(),p->allegiance?p->allegiance->revision:UINT64_MAX,b.command==Command::Honor?cw::Alliance::Honor:cw::Alliance::Terror);break;
   case Command::Issue:if(territory()&&(territory()->assignedMap||territory()->mapIdentifier)&&p&&p->allegiance&&auth::validUsername(opponent)&&mp.state()==net::MpClient::State::Lobby)mp.issueCampaignBattle(mp.subscribedCampaign(),selected,opponent);break;
   case Command::Find:if(board()&&board()->canSearch&&!board()->searchingTerritory&&matchTerritory()&&matchTerritory()->eligible&&mp.state()==net::MpClient::State::Lobby){if((pendingMatchRequest=mp.searchCampaignBattle(mp.subscribedCampaign(),selected))){pendingMatchAction=PendingMatch::Search;statusText="Search requested; waiting for server confirmation.";}}break;
   case Command::CancelSearch:if(board()&&board()->searchingTerritory&&mp.state()==net::MpClient::State::Lobby){if((pendingMatchRequest=mp.cancelCampaignSearch(mp.subscribedCampaign()))){pendingMatchAction=PendingMatch::Cancel;statusText="Cancellation requested; waiting for server confirmation.";}}break;
   case Command::History:historyOpen=!historyOpen;detailOffset=0;if(historyOpen)requestHistory();else mp.cancelCampaignReplayDownload();break;
   case Command::PrevHistory:if(historyPage>0){--historyPage;historyOffset=0;selectedHistoryBattle.clear();historyQueryKey.clear();mp.cancelCampaignReplayDownload();requestHistory();}break;
   case Command::NextHistory:if(history()&&history()->nextCursor){const auto cursor=history()->nextCursor;historyCursors.resize(size_t(historyPage+1));historyCursors.push_back(cursor);++historyPage;historyOffset=0;selectedHistoryBattle.clear();historyQueryKey.clear();mp.cancelCampaignReplayDownload();requestHistory();}break;
   case Command::Watch:if(const auto* b=historyBattle();b&&b->replay&&replaySafe()){statusText.clear();mp.requestCampaignReplay(b->battleId);}break;
   case Command::CancelDownload:mp.cancelCampaignReplayDownload();statusText="Replay download cancelled.";break;
   case Command::Join:{auto it=mp.campaignBattles().find(selectedBattle);if(it!=mp.campaignBattles().end()&&it->second.status==cw::BattlePhase::Issued&&it->second.roomId&&mp.state()==net::MpClient::State::Lobby)mp.joinGame(it->second.roomId,"");}break;
   case Command::Cancel:if(mp.state()==net::MpClient::State::InRoom)mp.leaveGame();break;
   case Command::PrevBattle:case Command::NextBattle:{auto all=battles();if(!all.empty()){auto it=std::find_if(all.begin(),all.end(),[&](auto* v){return v->battleId==selectedBattle;});int i=it==all.end()?(b.command==Command::PrevBattle?0:-1):int(it-all.begin());i=(i+(b.command==Command::PrevBattle?-1:1)+int(all.size()))%int(all.size());selectedBattle=all[size_t(i)]->battleId;battleOffset=std::max(0,i-2);}}break;
  }return CrusadesScreen::Action::None;}
  if(const auto* s=snapshot()){if(contains({14,126,202,260},x,y)){const int row=(y-126)/20+listOffset;if(row<int(filtered.size()))select(filtered[size_t(row)]);}
   else if(!historyOpen&&contains(imageRect,x,y)){if(retail&&local.presentation){auto id=local.presentation->regionAt((x-imageRect.x)*int(local.presentation->width())/imageRect.w,(y-imageRect.y)*int(local.presentation->height())/imageRect.h);if(id)select(*id);}else{int cols=std::max(1,int(std::ceil(std::sqrt(s->territories.size()*float(mapRect.w)/mapRect.h))));int rows=std::max(1,int((s->territories.size()+cols-1)/cols));int i=(y-mapRect.y)*rows/mapRect.h*cols+(x-mapRect.x)*cols/mapRect.w;if(i>=0&&i<int(s->territories.size()))select(s->territories[size_t(i)].id);}}
  }
  if(historyOpen&&contains({228,143,390,189},x,y)){if(const auto* h=history()){const int row=(y-143)/63+historyOffset;if(row<int(h->entries.size())){selectedHistoryBattle=h->entries[size_t(row)].battleId;detailOffset=0;mp.cancelCampaignReplayDownload();}}}
  if(!historyOpen&&contains({228,349,390,60},x,y)){auto all=battles();int i=(y-349)/20+battleOffset;if(i<int(all.size()))selectedBattle=all[size_t(i)]->battleId;}
  return CrusadesScreen::Action::None;
 }
};
CrusadesScreen::CrusadesScreen(SDL_Renderer* r,const hpi::Vfs& v,net::MpClient& mp):d_(std::make_unique<Impl>(r,v,mp)){}
CrusadesScreen::~CrusadesScreen()=default;
void CrusadesScreen::update(){d_->update();}
void CrusadesScreen::draw(int w,int h){float sx,sy;SDL_RenderGetScale(d_->ren,&sx,&sy);SDL_RenderSetScale(d_->ren,sx*float(std::max(1,w))/960,sy*float(std::max(1,h))/540);d_->draw(w,h);SDL_RenderSetScale(d_->ren,sx,sy);}
CrusadesScreen::Action CrusadesScreen::input(const SDL_Event& e,int x,int y){return d_->input(e,x,y);}
uint32_t CrusadesScreen::selectedTerritory()const{return d_->selected;}
void CrusadesScreen::selectTerritory(uint32_t id){d_->pendingSelected=id;d_->update();}
std::string CrusadesScreen::takeReplayPath(){
 const auto& download=d_->mp.campaignReplayDownload();
 if(download.state!=net::MpClient::CampaignReplayState::Ready)return {};
 std::string path;
 if(d_->historyOpen&&d_->replaySafe()&&d_->historyBattle()&&d_->historyBattle()->battleId==download.battleId){const auto utf8=download.path.u8string();path.assign(utf8.begin(),utf8.end());}
 d_->mp.cancelCampaignReplayDownload();return path;
}
void CrusadesScreen::setReplayError(const std::string& error){d_->statusText=error;}

} // namespace tak
