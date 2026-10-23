// The AI Profiles window (SCENARIO > AI Profiles, A): the map's DEFAULT AI profile
// (ai/<aiprofile>.txt) and our six levels' files (ai/d.txt .. ai/ad.txt), shown as
// a unit table or as text. Built from the editor's own widgets (the Scripting-sized
// panel, the Placed Units search list, the Use Only checklist). The model and
// every rule about the two grammars live in aiprofiles.h; this file only draws,
// routes input and writes the bytes back (one undo step per committed edit).

#include "cartographer/editorstate.h"
#include "cartographer/dialog.h"
#include "ai/builtinprofile.h"
#include "sim/sim.h"

#include <charconv>
#include <cstdio>
#include <tuple>

namespace {
using tak::ai::Difficulty;
constexpr const char* kSideCodes[]={"","ARA","TAR","VER","ZON","CRE"};
constexpr const char* kSideNames[]={"ALL SIDES","ARAMON","TAROS","VERUNA","ZHON","CREON"};
constexpr const char* kGroups[]={"ALL GROUPS","AIR","BUILDERS","BUILDINGS","GROUND","MANA","NAVAL","OTHER"};
constexpr int kGroupCount=8,kRowH=26,kLeftW=168;
constexpr const char* kPresetNames[]={"NO FLYERS","NO SHIPS","CLEAR ALL"};

std::string upperText(std::string s) {for(char& c:s)c=char(std::toupper(static_cast<unsigned char>(c)));return s;}
std::string trimmed(std::string s) {
    while(!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))s.pop_back();
    size_t i=0;while(i<s.size() && std::isspace(static_cast<unsigned char>(s[i])))++i;
    return s.substr(i);
}
bool whole(std::string_view s,int& v) {
    if(s.empty())return false;
    const auto r=std::from_chars(s.data(),s.data()+s.size(),v);
    return r.ec==std::errc() && r.ptr==s.data()+s.size();
}
std::string bytesText(const std::vector<uint8_t>& b) {return {b.begin(),b.end()};}
std::string lineText(std::string_view line) {
    std::string out(line);
    while(!out.empty() && (out.back()=='\r' || out.back()==' '))out.pop_back();
    return out;
}
// Wrapped text, at most `maxLines` rows; returns the rows drawn.
int drawWrapped(SDL_Renderer* ren,const std::string& text,int x,int y,int width,int maxLines,Uint8 r,Uint8 g,Uint8 b) {
    const auto rows=cart::TextEdit::lines(text,std::max(1,width/6));
    int n=0;
    for(const auto& row:rows) {
        if(n==maxLines)break;
        cart::drawText(ren,text.substr(row.begin,row.end-row.begin),x,y+n*12,1,r,g,b);++n;
    }
    return n;
}
// A bare text box (drawField without its label).
SDL_Rect inlineField(SDL_Renderer* ren,int x,int y,int w,const std::string& text,bool focused,const cart::TextEdit* edit) {
    return cart::drawField(ren,x,y-10,w,"",text,focused,edit);
}
bool doesNothing(cart::AiLine::What what) {
    using W=cart::AiLine::What;
    return what==W::Unknown || what==W::Other || what==W::Skipped;
}
} // namespace

std::optional<tak::ai::Difficulty> cart::Editor::aiTabLevel() const {
    if(aiTab<=0)return std::nullopt;
    return Difficulty(aiTab-1);
}

std::string cart::Editor::aiKeyFor(int tab) const {
    if(tab>0)return tak::ai::levelProfileFile(Difficulty(tab-1));
    if(!cart::standardAiProfile(scenario.aiProfile))return cart::aiProfileFileKey(scenario.aiProfile);
    if(aiFiles.count("ai/default.txt"))return "ai/default.txt";   // kept from the map; never written new
    return {};
}

const cart::Editor::AiTabView& cart::Editor::aiTabView() {
    const auto key=aiTabKey();const AiKind kind=aiTab?AiKind::Level:AiKind::Default;
    std::string bytes;bool readOnly=false;
    if(key.empty()) {bytes=tak::ai::classicProfileScript(vfs,"").text;readOnly=true;}   // the installed standard profile
    else if(aiView==AiView::Text && aiTextKey==key)bytes=aiText;
    else if(const auto it=aiFiles.find(key);it!=aiFiles.end())bytes=bytesText(it->second);
    auto& v=aiViewCache;
    if(v.key==key && v.bytes==bytes && v.readOnly==readOnly && v.draft.kind==kind && v.built)return v;
    v=AiTabView{};v.built=true;v.key=key;v.bytes=std::move(bytes);v.readOnly=readOnly;
    const auto& catalogue=*aiCatalogue;
    v.draft=cart::parseAiDraft(v.bytes,kind,catalogue);
    v.lines=cart::attributeAiLines(v.bytes,kind,catalogue);   // views into v.bytes
    if(!key.empty())v.issues=cart::checkAiFile(key,v.bytes,{&catalogue,&unitRegistry});
    if(kind==AiKind::Default) {v.classic.reset(catalogue.size());v.classic.execute(v.bytes,catalogue);}
    else {v.builtin=tak::ai::builtinProfile(*aiTabLevel());v.level=v.builtin;tak::ai::applyProfileText(v.bytes,v.level);}
    return v;
}

void cart::Editor::aiSetFile(const std::string& key,std::string_view bytes) {
    const auto it=aiFiles.find(key);
    const bool same=bytes.empty()?it==aiFiles.end():it!=aiFiles.end() && bytesText(it->second)==bytes;
    if(same)return;
    cart::setAiFile(aiFiles,key,bytes);dirty=historyPending=true;
}

// One committed edit of the shown file: the draft is changed, written in the
// canonical form, verified by the engine's own parser and stored. False (with
// aiNote saying why) when the file cannot be edited as rows or the write fails.
bool cart::Editor::aiEdit(const std::function<bool(AiDraft&)>& change,const std::string& header) {
    const auto key=aiTabKey();
    if(key.empty()) {aiNote="Choose \"This map's own profile\" first: the standard profile is the installed file, which this map does not carry.";return false;}
    const auto& view=aiTabView();
    if(view.draft.asWritten) {aiNote="This file was written by hand. Press MAKE EDITABLE first, or edit it in the Text view.";return false;}
    AiDraft draft=view.draft;
    if(draft.header.empty())draft.header=!header.empty()?header:
        std::string(aiTab?cart::aiLevelName(*aiTabLevel()):"DEFAULT AI")+" profile for map \""+mapName+"\" ("+key+"). Edit in Cartographer: SCENARIO > AI Profiles.";
    if(!change(draft))return false;
    std::erase_if(draft.units,[](const auto& unit){return unit.second.empty();});
    std::string bytes,error;
    if(!cart::saveAiDraft(draft,*aiCatalogue,bytes,error)) {aiNote=error;std::fprintf(stderr,"cartographer: %s\n",error.c_str());return false;}
    aiSetFile(key,bytes);aiNote.clear();return true;
}

void cart::Editor::openAiProfiles() {
    if(aiOpen)return;
    aiCatalogue=tak::ai::ClassicCatalogue::from(unitRegistry);
    const auto reach=cart::aiReachableUnits(unitRegistry);
    auto row=[&](std::string key,const tak::sim::UnitType* type) {
        AiRosterRow r;r.key=std::move(key);r.type=type;
        r.label=type?(type->name.empty()?type->id:type->name):r.key;
        r.group=cart::unitGroupOf(type);r.side=type?upperText(type->side):"";
        r.reachable=type && reach.count(type->id);
        return r;
    };
    aiDefaultRoster.clear();aiLevelRoster.clear();
    for(size_t i=1;i<aiCatalogue->size();++i) {
        const auto* type=aiCatalogue->types[i];
        if(type && type->commander)continue;   // the AI never builds a Monarch
        auto r=row(aiCatalogue->names[i],type);
        r.variant=type && lowerText(type->classicFbi.unitName.empty()?type->id:type->classicFbi.unitName)!=r.key;
        r.lockedForDefault=!cart::classicCanSingleOut(*aiCatalogue,r.key);
        aiDefaultRoster.push_back(std::move(r));
    }
    for(const auto& [id,type]:unitRegistry.types())if(!type.commander)aiLevelRoster.push_back(row(id,&type));
    for(auto* roster:{&aiDefaultRoster,&aiLevelRoster})
        std::sort(roster->begin(),roster->end(),[](const AiRosterRow& a,const AiRosterRow& b) {
            return std::tie(a.group,a.label,a.key)<std::tie(b.group,b.label,b.key);
        });
    try {aiPacks=tak::hpi::modPacks(std::filesystem::u8path(dataRoot),tak::hpi::ModKind::Game);} catch(const std::exception&) {aiPacks.clear();}
    if(aiPack>int(aiPacks.size())) {aiPack=0;aiPackFiles.reset();}
    aiOpened=aiFiles;aiOpen=true;aiStartOpen=false;aiView=AiView::Table;aiFocus=AI_NONE;
    aiNote.clear();aiName=scenario.aiProfile;aiPendingName.clear();aiScroll=0;aiIssues.clear();
    aiViewCache=AiTabView{};menuOpen=-1;
    SDL_StartTextInput();
}

void cart::Editor::closeAiProfiles() {
    if(!aiOpen)return;
    aiCommitText();
    aiOpen=aiStartOpen=false;aiFocus=AI_NONE;aiView=AiView::Table;aiPendingName.clear();
    SDL_StopTextInput();
}

void cart::Editor::aiCommitText() {
    if(aiView!=AiView::Text || aiTextKey.empty())return;
    if(aiText.size()>cart::kAiFileMaxBytes) {aiNote="The text is over 256 KiB, so it was not stored.";return;}
    aiSetFile(aiTextKey,aiText);
}

void cart::Editor::aiSelectTab(int tab) {
    aiCommitText();
    if(aiFocus==AI_CELL || aiFocus==AI_TEXT)aiFocus=AI_NONE;
    aiTab=std::clamp(tab,0,6);aiScroll=0;aiNote.clear();
    if(aiView==AiView::Text) {
        aiTextKey=aiTabKey();
        if(aiTextKey.empty())aiText=tak::ai::classicProfileScript(vfs,"").text;
        else if(const auto it=aiFiles.find(aiTextKey);it!=aiFiles.end())aiText=bytesText(it->second);
        else aiText.clear();
        aiTextScroll=0;aiTextEdit.focus(aiText,false);aiTextEdit.caret=aiTextEdit.anchor=0;
        aiFocus=aiTextKey.empty()?AI_NONE:AI_TEXT;
    }
}

std::vector<const cart::Editor::AiRosterRow*> cart::Editor::aiVisibleRows() {
    const auto& view=aiTabView();
    std::set<std::string> set;
    for(const auto& line:view.lines)
        if(line.what==AiLine::What::Exact || line.what==AiLine::What::Overridden)
            set.insert(aiTab?line.word:aiCatalogue->names[line.unit]);
    const auto query=lowerText(aiSearch);
    std::vector<const AiRosterRow*> out;
    for(const auto& row:aiRoster()) {
        const bool named=set.count(row.key)>0;
        if(aiSetOnly && !named)continue;
        if(!aiShowUnbuildable && !row.reachable && !named)continue;   // a row the file sets is never hidden
        if(aiSide && row.side!=kSideCodes[aiSide])continue;
        if(aiGroup && row.group!=kGroups[aiGroup])continue;
        if(!query.empty() && lowerText(row.label+" "+row.key).find(query)==std::string::npos)continue;
        out.push_back(&row);
    }
    return out;
}

void cart::Editor::aiCommitCell() {
    const auto key=aiCellKey;const int column=aiCellColumn;
    std::string t=lowerText(trimmed(aiCellText));
    aiFocus=AI_NONE;
    if(t.empty()) {aiClearCell(key,column);return;}
    int v=0;
    if(column==0) {
        if(!aiTab) {
            if(t.ends_with('%'))t=trimmed(t.substr(0,t.size()-1));
            if(!whole(t,v) || v<0 || v>100) {aiNote="Type a whole percentage from 0 to 100 (100 % is normal).";aiFocus=AI_CELL;return;}
        } else {
            if(t.find_first_of(".,")!=std::string::npos) {aiNote="Levels use whole numbers (0.5 would read as 0).";aiFocus=AI_CELL;return;}
            if(!whole(t,v) || v<0 || v>999) {aiNote="Type a whole weight from 0 to 999 (0 never builds it).";aiFocus=AI_CELL;return;}
        }
    } else {
        if(t=="no limit" || t=="none" || t=="unlimited" || t=="-1") {
            if(aiTab) {aiNote="Leave the limit blank for the built-in value; 0 means never built.";aiFocus=AI_CELL;return;}
            v=-1;
        } else if(t.find_first_of(".,")!=std::string::npos || !whole(t,v) || v<0 || v>9999) {
            aiNote=aiTab?"Type a whole limit from 0 to 9999 (0 = never built), or leave it blank.":"Type a whole limit from 0 to 9999, \"no limit\", or leave it blank.";aiFocus=AI_CELL;return;
        }
    }
    aiEdit([&](AiDraft& d) {
        auto& rule=d.units[key];
        if(column==1)rule.limit=v;else if(aiTab)rule.weight=v;else rule.chance=v;
        return true;
    });
}

void cart::Editor::aiClearCell(const std::string& key,int column) {
    const auto& view=aiTabView();
    const auto it=view.draft.units.find(key);
    if(!view.draft.asWritten && !view.key.empty() && it==view.draft.units.end())return;   // nothing set
    aiEdit([&](AiDraft& d) {
        auto& rule=d.units[key];
        if(column!=1) {rule.chance.reset();rule.weight.reset();}
        if(column!=0)rule.limit.reset();
        return true;
    });
}

// BUILD off = limit 0 (both grammars). BUILD on undoes this file's zeros, then
// asks the engine whether the unit is still off and, for the DEFAULT AI, names it
// at full chance with no limit (an exact row overrides any group rule).
void cart::Editor::aiToggleBuild(const std::string& key) {
    const auto& catalogue=*aiCatalogue;
    auto builds=[&](const std::string& bytes) {
        if(!aiTab) {
            tak::ai::ClassicProfile p(catalogue.size());p.execute(bytes,catalogue);
            const auto i=catalogue.byName.at(key);return p.pref[i]>0 && p.limit[i]!=0;
        }
        auto p=tak::ai::builtinProfile(*aiTabLevel());tak::ai::applyProfileText(bytes,p);
        const auto w=p.weight.find(key);const auto l=p.limit.find(key);
        return w!=p.weight.end() && w->second>0 && !(l!=p.limit.end() && l->second==0);
    };
    if(!aiTab && !catalogue.byName.count(key))return;
    const bool on=builds(aiTabView().bytes);
    bool stillOff=false;
    aiEdit([&](AiDraft& d) {
        auto& rule=d.units[key];
        if(on) {rule.limit=0;return true;}
        if(rule.limit==0)rule.limit.reset();
        if(rule.chance==0)rule.chance.reset();
        if(rule.weight && *rule.weight<=0)rule.weight.reset();
        if(!builds(cart::writeAiDraft(d))) {
            if(aiTab)stillOff=true;
            else {
                tak::ai::ClassicProfile p(catalogue.size());p.execute(cart::writeAiDraft(d),catalogue);
                const auto i=catalogue.byName.at(key);
                if(p.pref[i]==0)rule.chance=100;
                if(p.limit[i]==0)rule.limit=-1;
            }
        }
        return true;
    });
    if(stillOff)aiNote="This level still never builds it: type a weight above 0 (units the built-in table does not name need one).";
}

void cart::Editor::aiPreset(int which) {
    const auto key=aiTabKey();
    if(key.empty()) {aiNote="Choose \"This map's own profile\" first.";return;}
    const auto& view=aiTabView();
    if(view.draft.asWritten) {aiNote="This file was written by hand. Press MAKE EDITABLE first.";return;}
    if(which==2) {
        size_t rows=0;for(const auto& [unit,rule]:view.draft.units)rows+=!rule.empty();
        if(!rows) {aiNote="Clear all: this file sets no units.";return;}
        openConfirm("CLEAR ALL","Clear all removes the "+std::to_string(rows)+" unit settings from "+key+". Group rules and kept lines stay. Undo restores them.",[this] {
            aiEdit([](AiDraft& d) {d.units.clear();return true;});
        });
        return;
    }
    // Keep my own settings: units this file already sets are left as they are.
    std::vector<std::string> targets;
    for(const auto& row:aiRoster()) {
        if(!row.type || (!aiTab && row.lockedForDefault))continue;
        const bool match=which==0?row.type->canFly:row.type->domain==tak::sim::UnitType::Domain::Water && !row.type->isStructure();
        if(!match)continue;
        if(const auto it=view.draft.units.find(row.key);it!=view.draft.units.end() && !it->second.empty())continue;
        targets.push_back(row.key);
    }
    const std::string name=which==0?"No flyers":"No ships";
    if(targets.empty()) {aiNote=name+": every matching unit is already set in this file.";return;}
    openConfirm(upperText(name),name+" sets "+std::to_string(targets.size())+" units to never built (limit 0) in "+key+
        ". Units you already set keep their settings. Undo restores the file.",[this,targets] {
        aiEdit([&](AiDraft& d) {for(const auto& unit:targets)d.units[unit].limit=0;return true;});
    });
}

void cart::Editor::aiStartFrom(bool standard) {
    const std::string name=aiPendingName.empty()?scenario.aiProfile:aiPendingName;
    const auto key=cart::aiProfileFileKey(name);
    aiStartOpen=false;
    if(key.empty() || cart::standardAiProfile(name)) {aiPendingName.clear();return;}
    const auto& catalogue=*aiCatalogue;
    const std::string header="DEFAULT AI profile for map \""+mapName+"\" ("+key+"). Edit in Cartographer: SCENARIO > AI Profiles.";
    std::string bytes,error;
    if(standard) {
        // The user's own install, read now; nothing of it is part of the editor.
        const auto script=tak::ai::classicProfileScript(vfs,"");
        if(script.text.empty()) {aiNote="This install has no ai/default.txt, so the profile starts empty.";standard=false;}
        else {
            auto draft=cart::parseAiDraft(script.text,AiKind::Default,catalogue);
            std::string why;
            if(cart::makeEditable(draft,catalogue,&why)) {
                draft.header="Started from the installed "+tak::hpi::MountSet::key(script.path)+". "+header;
                if(!cart::saveAiDraft(draft,catalogue,bytes,error))bytes=script.text;
            } else bytes=script.text;   // kept as written: it still plays exactly as installed
        }
    }
    if(!standard) {
        AiDraft draft;draft.kind=AiKind::Default;draft.header=header;
        draft.kept={"// Started empty."};   // a file, so this map's units are all at full chance
        if(!cart::saveAiDraft(draft,catalogue,bytes,error)) {aiNote=error;aiPendingName.clear();return;}
    }
    if(!aiPendingName.empty() && scenario.aiProfile!=aiPendingName) {scenario.aiProfile=aiPendingName;dirty=historyPending=true;}
    aiName=scenario.aiProfile;aiPendingName.clear();
    aiSetFile(key,bytes);aiTab=0;aiScroll=0;
}

void cart::Editor::aiChooseStandard() {
    if(cart::standardAiProfile(scenario.aiProfile))return;
    const auto key=cart::aiProfileFileKey(scenario.aiProfile);
    auto apply=[this,key] {
        scenario.aiProfile="DEFAULT";aiName=scenario.aiProfile;
        if(aiFiles.erase(key))aiNote.clear();
        dirty=historyPending=true;
    };
    if(!aiFiles.count(key)) {apply();return;}
    openConfirm("STANDARD PROFILE","The DEFAULT AI will use the standard profile (aiprofile=DEFAULT), and this map's "+key+
        " is deleted. Undo restores both.",apply);
}

void cart::Editor::aiChooseOwn() {
    if(!cart::standardAiProfile(scenario.aiProfile))return;
    const auto name=cart::proposeAiProfileName(mapName);
    const auto key=cart::aiProfileFileKey(name);
    if(aiFiles.count(key)) {scenario.aiProfile=name;aiName=name;dirty=historyPending=true;return;}
    if(aiFiles.count("ai/default.txt")) {
        openConfirm("OWN PROFILE","Move this map's ai/default.txt to its own profile "+key+" (aiprofile="+name+
            ")? The editor never writes ai/default.txt into a map. Undo restores it.",[this,name,key] {
            auto bytes=std::move(aiFiles["ai/default.txt"]);aiFiles.erase("ai/default.txt");aiFiles[key]=std::move(bytes);
            scenario.aiProfile=name;aiName=name;dirty=historyPending=true;
        });
        return;
    }
    aiPendingName=name;aiStartOpen=true;   // the name and its first file are one undo step
}

void cart::Editor::aiCommitName() {
    aiFocus=AI_NONE;
    const auto typed=trimmed(aiName);
    if(typed==scenario.aiProfile)return;
    if(const auto error=cart::aiProfileNameError(typed);!error.empty()) {aiNote=error;aiName=scenario.aiProfile;return;}
    if(aiFiles.count(cart::aiProfileFileKey(typed))) {aiNote=cart::aiProfileFileKey(typed)+" already exists in this map; choose another name.";aiName=scenario.aiProfile;return;}
    cart::renameAiProfile(aiFiles,scenario.aiProfile,typed);   // ai/<old>.txt -> ai/<new>.txt, same undo step
    scenario.aiProfile=aiName=typed;dirty=historyPending=true;aiNote.clear();
}

void cart::Editor::aiMakeEditable() {
    const auto& view=aiTabView();
    if(view.key.empty() || !view.draft.asWritten)return;
    AiDraft draft=view.draft;std::string why;
    if(!cart::makeEditable(draft,*aiCatalogue,&why)) {aiNote="Not converted: "+why+". The file stays as written.";return;}
    if(draft.header.empty())draft.header="Converted by Cartographer for map \""+mapName+"\" ("+view.key+"). Edit in Cartographer: SCENARIO > AI Profiles.";
    std::string bytes,error;
    if(!cart::saveAiDraft(draft,*aiCatalogue,bytes,error)) {aiNote=error;return;}
    aiSetFile(view.key,bytes);aiNote="Converted: every unit plays exactly as before.";
}

void cart::Editor::aiCheck() {
    aiCommitText();
    aiIssues=cart::checkAiMap(aiFiles,scenario.aiProfile,vfs,{&*aiCatalogue,&unitRegistry});
    aiView=AiView::Issues;aiIssueScroll=0;if(aiFocus==AI_TEXT || aiFocus==AI_CELL)aiFocus=AI_NONE;
}

std::string cart::Editor::aiRowExplanation(const AiRosterRow& row) {
    const auto& view=aiTabView();
    std::string out=row.label+" ("+row.key+")";
    if(!aiTab) {
        if(row.lockedForDefault)return out+": the DEFAULT AI cannot single out this unit (an original-game rule). Only group rules change it.";
        const auto& catalogue=*aiCatalogue;const auto i=catalogue.byName.at(row.key);
        const AiLine *weight=nullptr,*limit=nullptr,*groupWeight=nullptr,*groupLimit=nullptr;
        for(const auto& line:view.lines) {
            if(line.what==AiLine::What::Exact && line.unit==i)(line.weight?weight:limit)=&line;
            if(line.what==AiLine::What::Group) {
                const auto it=catalogue.categories.find(line.word);
                if(it!=catalogue.categories.end() && std::find(it->second.begin(),it->second.end(),i)!=it->second.end())
                    (line.weight?groupWeight:groupLimit)=&line;
            }
        }
        const int pref=view.classic.pref[i],lim=view.classic.limit[i];
        const std::string chance=std::to_string(pref)+" %";
        if(weight) {
            out+=": chance "+chance+" (line "+std::to_string(weight->number)+")";
            if(weight->value!=cart::classicChanceText(pref) || !weight->combines.empty()) {
                out+=", file says "+(weight->value.empty()?std::string("nothing"):weight->value)+" = "+chance;
                if(!weight->combines.empty())out+=" (after '"+lineText(view.lines[size_t(weight->combines.back()-1)].text)+"')";
            }
        } else if(groupWeight)
            out+=": chance "+chance+" from line "+std::to_string(groupWeight->number)+" ('"+lineText(groupWeight->text)+"')";
        else out+=": chance "+chance+" (not named)";
        const std::string limitText=lim<0?"no limit":"limit "+std::to_string(lim);
        if(limit)out+="; "+limitText+" (line "+std::to_string(limit->number)+").";
        else if(groupLimit)out+="; "+limitText+" from line "+std::to_string(groupLimit->number)+" ('"+lineText(groupLimit->text)+"').";
        else out+="; "+limitText+(weight?".":" (not named).");
        if(row.variant)out+=" A variant: only the DEFAULT AI lists it.";
    } else {
        const AiLine *weight=nullptr,*limit=nullptr;
        for(const auto& line:view.lines)if(line.what==AiLine::What::Exact && line.word==row.key)(line.weight?weight:limit)=&line;
        const auto w=view.level.weight.find(row.key);const auto l=view.level.limit.find(row.key);
        const bool builtinWeight=view.builtin.weight.count(row.key)>0;
        if(weight)out+=": weight "+std::to_string(weight->levelValue)+" (line "+std::to_string(weight->number)+")";
        else if(w!=view.level.weight.end())out+=": weight "+std::to_string(w->second)+" (built-in)";
        else out+=": never built by this level (not named)";
        if(limit)out+="; limit "+std::to_string(limit->levelValue)+" (line "+std::to_string(limit->number)+").";
        else if(l!=view.level.limit.end())out+="; limit "+std::to_string(l->second)+" (built-in).";
        else out+="; no cap"+std::string(builtinWeight || weight?".":" ");
    }
    for(const auto& issue:view.issues)if(issue.unit==row.key && issue.severity!=AiIssue::Severity::Info) {out+=" "+issue.message;break;}
    if(!row.reachable)out+=" No builder of its side can make it.";
    return out;
}

// The window swallows input while up (like Scripting); Ctrl+Z waits until it closes.
void cart::Editor::handleAiProfiles(SDL_Event& e) {
    if(aiStartOpen) {
        if(e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_ESCAPE) {aiStartOpen=false;aiPendingName.clear();}
        else if(e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT) {
            const int mx=e.button.x,my=e.button.y;
            if(cart::pointIn(mx,my,aiStartStandard))aiStartFrom(true);
            else if(cart::pointIn(mx,my,aiStartEmpty))aiStartFrom(false);
            else if(cart::pointIn(mx,my,aiStartCancel)) {aiStartOpen=false;aiPendingName.clear();}
        }
        return;
    }
    const int textColumns=std::max(1,(aiTextBox.w-8)/6);
    auto typeInto=[&](const SDL_Event& ev) {
        if(aiFocus==AI_SEARCH) {if(aiSearchEdit.input(ev,aiSearch))aiScroll=0;}
        else if(aiFocus==AI_NAME)aiNameEdit.input(ev,aiName);
        else if(aiFocus==AI_CELL)aiCellEdit.input(ev,aiCellText);
        else if(aiFocus==AI_TEXT) {aiTextEdit.input(ev,aiText,false,true,textColumns);aiTextFollow=true;}
    };
    if(e.type==SDL_TEXTINPUT) {typeInto(e);return;}
    if(e.type==SDL_KEYDOWN) {
        const auto key=e.key.keysym.sym;const bool ctrl=(e.key.keysym.mod&(KMOD_CTRL|KMOD_GUI))!=0;
        const bool enter=key==SDLK_RETURN || key==SDLK_KP_ENTER;
        if(ctrl && (key==SDLK_z || key==SDLK_y)) {aiNote="Undo and Redo work after closing this window (DONE).";return;}
        if(aiFocus==AI_CELL) {if(key==SDLK_ESCAPE)aiFocus=AI_NONE;else if(enter)aiCommitCell();else typeInto(e);return;}
        if(aiFocus==AI_NAME) {if(key==SDLK_ESCAPE) {aiFocus=AI_NONE;aiName=scenario.aiProfile;} else if(enter)aiCommitName();else typeInto(e);return;}
        if(aiFocus==AI_SEARCH) {if(key==SDLK_ESCAPE || enter)aiFocus=AI_NONE;else typeInto(e);return;}
        if(aiFocus==AI_TEXT) {
            if(key==SDLK_ESCAPE) {aiCommitText();aiFocus=AI_NONE;}
            else if(enter) {SDL_Event line{};line.type=SDL_TEXTINPUT;SDL_strlcpy(line.text.text,"\n",sizeof line.text.text);typeInto(line);}
            else typeInto(e);
            return;
        }
        if(key==SDLK_ESCAPE)closeAiProfiles();
        else if((key==SDLK_DELETE || key==SDLK_BACKSPACE) && !aiSelected.empty() && aiView==AiView::Table)aiClearCell(aiSelected,-1);
        return;
    }
    if(e.type==SDL_MOUSEWHEEL) {
        int mx,my;SDL_GetMouseState(&mx,&my);mx=int(mx/kUIScale);my=int(my/kUIScale);
        if(aiView==AiView::Text && cart::pointIn(mx,my,aiTextBox)) {aiTextScroll=std::max(0,aiTextScroll-e.wheel.y*3);aiTextFollow=false;}
        else if(aiView==AiView::Issues)aiIssueScroll=std::max(0,aiIssueScroll-e.wheel.y*36);
        else aiScroll=std::max(0,aiScroll-e.wheel.y*kRowH*2);
        return;
    }
    if(e.type!=SDL_MOUSEBUTTONDOWN)return;
    const int mx=e.button.x,my=e.button.y;
    if(e.button.button==SDL_BUTTON_RIGHT) {   // a cell back to "not set"
        if(aiView==AiView::Table)for(const auto& cell:aiCells)if(cell.column<2 && cart::pointIn(mx,my,cell.rect)) {aiFocus=AI_NONE;aiClearCell(cell.key,cell.column);return;}
        return;
    }
    if(e.button.button!=SDL_BUTTON_LEFT)return;
    // A click elsewhere leaves a field: cell and name edits are dropped (Enter commits them).
    if(aiFocus==AI_NAME && !cart::pointIn(mx,my,aiNameBox)) {aiFocus=AI_NONE;aiName=scenario.aiProfile;}
    if(aiFocus==AI_CELL)aiFocus=AI_NONE;
    if(aiFocus==AI_SEARCH && !cart::pointIn(mx,my,aiSearchBox))aiFocus=AI_NONE;
    for(int t=0;t<7;++t)if(cart::pointIn(mx,my,aiTabRects[t])) {if(t!=aiTab)aiSelectTab(t);return;}
    for(int p=0;p<3;++p)if(cart::pointIn(mx,my,aiPresetRects[p])) {aiPreset(p);return;}
    if(cart::pointIn(mx,my,aiDoneButton)) {closeAiProfiles();return;}
    if(cart::pointIn(mx,my,aiPackButton)) {
        aiPack=(aiPack+1)%(int(aiPacks.size())+1);aiPackFiles.reset();
        if(aiPack) try {
            auto files=tak::hpi::modFiles(std::filesystem::u8path(dataRoot),tak::hpi::ModKind::Game,{aiPacks[size_t(aiPack-1)]});
            std::erase_if(files,[](const auto& file){return !file.first.starts_with("ai/");});   // only ai/ is asked
            aiPackFiles=std::move(files);
        } catch(const std::exception& error) {aiNote=std::string("Game mod: ")+error.what();aiPack=0;}
        return;
    }
    if(cart::pointIn(mx,my,aiViewButton)) {
        if(aiView==AiView::Text) {aiCommitText();aiView=AiView::Table;aiFocus=AI_NONE;}
        else {aiView=AiView::Text;aiTextKey.clear();aiSelectTab(aiTab);}
        return;
    }
    if(cart::pointIn(mx,my,aiCheckButton)) {if(aiView==AiView::Issues)aiView=AiView::Table;else aiCheck();return;}
    if(cart::pointIn(mx,my,aiEditableButton)) {aiCommitText();aiMakeEditable();return;}
    if(cart::pointIn(mx,my,aiStartButton)) {aiCommitText();if(!aiTab && !cart::standardAiProfile(scenario.aiProfile))aiStartOpen=true;return;}
    if(cart::pointIn(mx,my,aiInsertButton)) {
        if(aiSelected.empty()) {aiNote="Select a unit in the table first; INSERT UNIT adds its "+std::string(aiTab?"UnitType id":"unit name")+".";return;}
        if(aiFocus!=AI_TEXT)return;
        SDL_Event word{};word.type=SDL_TEXTINPUT;SDL_strlcpy(word.text.text,aiSelected.c_str(),sizeof word.text.text);
        aiTextEdit.input(word,aiText,false,true,textColumns);aiTextFollow=true;return;
    }
    if(!aiTab && cart::pointIn(mx,my,aiStandardRadio)) {aiChooseStandard();return;}
    if(!aiTab && cart::pointIn(mx,my,aiOwnRadio)) {aiChooseOwn();return;}
    if(!aiTab && cart::pointIn(mx,my,aiNameBox) && !cart::standardAiProfile(scenario.aiProfile)) {
        if(aiFocus!=AI_NAME) {aiFocus=AI_NAME;aiName=scenario.aiProfile;aiNameEdit.focus(aiName);}
        return;
    }
    if(aiView==AiView::Text) {
        if(cart::pointIn(mx,my,aiTextBox) && !aiTextKey.empty()) {
            aiFocus=AI_TEXT;aiTextEdit.click(aiText,textColumns,(my-aiTextBox.y-4)/12+aiTextScroll,(mx-aiTextBox.x-4)/6,(SDL_GetModState()&KMOD_SHIFT)!=0);aiTextFollow=true;
        }
        return;
    }
    if(aiView==AiView::Issues) {
        for(const auto& [rect,index]:aiIssueRows)if(cart::pointIn(mx,my,rect) && index<aiIssues.size()) {
            const auto issue=aiIssues[index];
            int tab=-1;
            if(const auto level=cart::aiLevelOf(issue.file))tab=int(*level)+1;
            else if(issue.file.empty() || issue.file==aiKeyFor(0))tab=0;
            if(tab<0) {aiNote=issue.file+" is not one of the profiles shown here.";return;}
            aiView=AiView::Text;aiTextKey.clear();aiSelectTab(tab);
            size_t at=0;for(int n=1;n<issue.line && at!=std::string::npos;++n) {at=aiText.find('\n',at);if(at!=std::string::npos)++at;}
            aiTextEdit.caret=aiTextEdit.anchor=at==std::string::npos?aiText.size():at;aiTextFollow=true;
            return;
        }
        return;
    }
    if(cart::pointIn(mx,my,aiSearchBox)) {if(aiFocus!=AI_SEARCH) {aiFocus=AI_SEARCH;aiSearchEdit.focus(aiSearch);}return;}
    if(cart::pointIn(mx,my,aiSideBox)) {aiSide=(aiSide+1)%6;aiScroll=0;return;}
    if(cart::pointIn(mx,my,aiGroupBox)) {aiGroup=(aiGroup+1)%kGroupCount;aiScroll=0;return;}
    if(cart::pointIn(mx,my,aiSetOnlyBox)) {aiSetOnly=!aiSetOnly;aiScroll=0;return;}
    if(cart::pointIn(mx,my,aiUnbuildableBox)) {aiShowUnbuildable=!aiShowUnbuildable;aiScroll=0;return;}
    for(const auto& cell:aiCells)if(cart::pointIn(mx,my,cell.rect)) {
        aiSelected=cell.key;
        const AiRosterRow* row=nullptr;for(const auto& r:aiRoster())if(r.key==cell.key) {row=&r;break;}
        if(!aiTab && row && row->lockedForDefault)return;   // disabled on the DEFAULT tab only
        if(cell.column==2) {aiToggleBuild(cell.key);return;}
        if(aiTabKey().empty()) {aiNote="Choose \"This map's own profile\" first: the standard profile is the installed file, which this map does not carry.";return;}
        if(aiTabView().draft.asWritten) {aiNote="This file was written by hand. Press MAKE EDITABLE first, or edit it in the Text view.";return;}
        const auto& rules=aiTabView().draft.units;const auto it=rules.find(cell.key);
        std::optional<int> value;
        if(it!=rules.end())value=cell.column==1?it->second.limit:aiTab?it->second.weight:it->second.chance;
        aiCellText=!value?"":cell.column==1 && *value<0?"no limit":std::to_string(*value);
        aiCellKey=cell.key;aiCellColumn=cell.column;aiFocus=AI_CELL;aiCellEdit.focus(aiCellText);aiNote.clear();
        return;
    }
    for(const auto& [rect,unit]:aiRows)if(cart::pointIn(mx,my,rect)) {aiSelected=unit;return;}
}

void cart::Editor::drawAiProfiles() {
    if(!aiOpen)return;
    int pointerX,pointerY;SDL_GetMouseState(&pointerX,&pointerY);pointerX=int(pointerX/kUIScale);pointerY=int(pointerY/kUIScale);
    aiHint.clear();
    const auto ct=cart::drawPanel(ren,w,h,780,520,"AI PROFILES - "+upperText(mapName));
    const auto& view=aiTabView();
    const auto& catalogue=*aiCatalogue;
    const bool standard=cart::standardAiProfile(scenario.aiProfile);

    // ---- left column: the profiles, presets, game-mod preview ----
    for(int t=0;t<7;++t) {
        const auto key=aiKeyFor(t);
        const SDL_Rect r=t==0?SDL_Rect{ct.x,ct.y,kLeftW,26}:SDL_Rect{ct.x,ct.y+28+(t-1)*16,kLeftW,16};
        aiTabRects[t]=r;
        fillRect(ren,r.x,r.y,r.w,r.h,t==aiTab?58:30,t==aiTab?68:33,t==aiTab?95:42);
        const auto file=key.empty()?aiFiles.end():aiFiles.find(key);
        const bool inMap=file!=aiFiles.end();
        const auto opened=aiOpened.find(key);
        const bool edited=!key.empty() && (inMap!=(opened!=aiOpened.end()) || (inMap && opened->second!=file->second));
        bool written=false;
        if(inMap) {
            auto& cached=aiWrittenCache[key];const auto bytes=bytesText(file->second);
            if(cached.first!=bytes)cached={bytes,cart::parseAiDraft(bytes,t?AiKind::Level:AiKind::Default,catalogue).asWritten};
            written=cached.second;
        }
        const std::string label=t?cart::aiLevelName(Difficulty(t-1)):"DEFAULT AI";
        cart::drawText(ren,(t==aiTab?">":" ")+label,r.x+3,r.y+3,1,t==aiTab?255:215,t==aiTab?235:220,t==aiTab?200:230);
        const std::string tag=std::string(written?"as written ":"")+(inMap?"map":"")+(edited?" *":"");
        cart::drawText(ren,tag,r.x+r.w-4-cart::textWidth(tag,1),r.y+3,1,written?235:160,written?190:200,written?120:170);
        if(t==0)cart::drawText(ren,key.empty()?"   standard profile":"   "+key,r.x+3,r.y+15,1,150,165,185);
    }
    int ly=ct.y+28+6*16+10;
    cart::drawText(ren,"PRESETS",ct.x,ly,1,150,200,150);
    for(int p=0;p<3;++p)aiPresetRects[p]=cart::drawButton(ren,ct.x,ly+12+p*22,kLeftW,18,kPresetNames[p],false);
    ly+=12+3*22+10;
    cart::drawText(ren,"PREVIEW WITH GAME MOD",ct.x,ly,1,150,200,150);
    std::string pack=aiPack?aiPacks[size_t(aiPack-1)]:"NONE";
    if(pack.size()>24)pack=pack.substr(0,22)+"..";
    aiPackButton=cart::drawButton(ren,ct.x,ly+12,kLeftW,18,"PACK: "+pack,false);
    drawWrapped(ren,aiPacks.empty()?"No game mods in this install.":"Click to step through mods/game packs.",ct.x,ly+34,kLeftW,2,140,150,165);

    // ---- right side ----
    const int x0=ct.x+kLeftW+10,W=ct.x+ct.w-x0,y0=ct.y;
    cart::drawText(ren,aiTab?"OUR LEVEL RULES: WHOLE NUMBERS; UNITS BY NAME ONLY; LATER LINES WIN.":"ORIGINAL-GAME RULES: THE FIRST LINE NAMING A UNIT WINS; GROUP RULES COMBINE.",x0,y0,1,235,220,160);
    aiStandardRadio=aiOwnRadio=aiNameBox={};
    if(!aiTab) {
        aiStandardRadio={x0,y0+13,300,12};
        cart::drawText(ren,std::string(standard?"(o)":"( )")+" The standard profile (aiprofile=DEFAULT)",x0,y0+15,1,220,225,235);
        aiOwnRadio={x0,y0+28,206,14};
        cart::drawText(ren,std::string(standard?"( )":"(o)")+" This map's own profile, named",x0,y0+31,1,220,225,235);
        if(!standard) {
            aiNameBox=inlineField(ren,x0+210,y0+27,150,aiName,aiFocus==AI_NAME,aiFocus==AI_NAME?&aiNameEdit:nullptr);
            cart::drawText(ren,"-> "+cart::aiProfileFileKey(scenario.aiProfile)+" in the map",x0+366,y0+31,1,160,175,195);
        } else cart::drawText(ren,"(proposed: "+cart::proposeAiProfileName(mapName)+")",x0+210,y0+31,1,140,150,165);
    } else {
        cart::drawText(ren,"File: "+aiTabKey()+". Rows not set keep the BUILT-IN value (grey).",x0,y0+15,1,200,205,215);
        cart::drawText(ren,"Units use their UnitType id; variants (such as tarnecr2) are DEFAULT AI only.",x0,y0+31,1,150,160,175);
    }
    const int listTop=y0+90,listBottom=ct.y+ct.h-112;
    aiCells.clear();aiRows.clear();aiIssueRows.clear();
    aiSearchBox=aiSideBox=aiGroupBox=aiSetOnlyBox=aiUnbuildableBox=aiTextBox={};aiList={};
    if(aiView==AiView::Table) {
        aiSearchBox=cart::drawField(ren,x0,y0+46,150,"FIND (NAME OR KEY)",aiSearch,aiFocus==AI_SEARCH,aiFocus==AI_SEARCH?&aiSearchEdit:nullptr);
        aiSideBox=cart::drawChoice(ren,x0+158,y0+46,100,"SIDE",kSideNames[aiSide],false);
        aiGroupBox=cart::drawChoice(ren,x0+266,y0+46,100,"GROUP",kGroups[aiGroup],false);
        aiSetOnlyBox=cart::drawButton(ren,x0+374,y0+56,74,16,std::string(aiSetOnly?"[X]":"[ ]")+" SET ONLY",aiSetOnly);
        aiUnbuildableBox=cart::drawButton(ren,x0+454,y0+56,W-454,16,std::string(aiShowUnbuildable?"[X]":"[ ]")+" UNBUILDABLE",aiShowUnbuildable);
        cart::drawText(ren,"UNIT",x0+46,y0+80,1,150,200,150);
        cart::drawText(ren,"BUILD",x0+234,y0+80,1,150,200,150);
        cart::drawText(ren,aiTab?"WEIGHT":"CHANCE",x0+280,y0+80,1,150,200,150);
        cart::drawText(ren,aiTab?"BUILT-IN":"SET BY",x0+378,y0+80,1,150,200,150);
        cart::drawText(ren,"LIMIT",x0+462,y0+80,1,150,200,150);
        aiList={x0,listTop,W,listBottom-listTop};
        fillRect(ren,aiList.x,aiList.y,aiList.w,aiList.h,22,24,32);
        const auto rows=aiVisibleRows();
        aiScroll=std::clamp(aiScroll,0,std::max(0,int(rows.size())*kRowH-aiList.h));
        std::map<std::string,const AiLine*> setWeight,setLimit;
        for(const auto& line:view.lines)if(line.what==AiLine::What::Exact) {
            const auto unit=aiTab?line.word:catalogue.names[line.unit];
            (line.weight?setWeight:setLimit)[unit]=&line;
        }
        std::set<std::string> flagged;
        for(const auto& issue:view.issues)if(issue.severity!=AiIssue::Severity::Info && !issue.unit.empty())flagged.insert(issue.unit);
        SDL_RenderSetClipRect(ren,&aiList);
        const bool editable=!view.key.empty() && !view.draft.asWritten;
        for(int i=aiScroll/kRowH;i<int(rows.size()) && i*kRowH<aiScroll+aiList.h;++i) {
            const auto& row=*rows[size_t(i)];const int y=aiList.y+i*kRowH-aiScroll;
            const bool selected=row.key==aiSelected,locked=!aiTab && row.lockedForDefault;
            if(selected)fillRect(ren,aiList.x,y,aiList.w,kRowH-1,50,60,82);
            aiRows.push_back({{aiList.x,y,aiList.w,kRowH-1},row.key});
            if(flagged.count(row.key))cart::drawText(ren,"!",x0+2,y+9,1,255,170,90);
            if(row.type) {
                if(auto* portrait=unitPortraits.get("anims/buildpic/"+row.type->id+".jpg")) {
                    int pw=0,ph=0;SDL_QueryTexture(portrait,nullptr,nullptr,&pw,&ph);
                    if(pw>0 && ph>0) {
                        const float scale=std::min(32.f/pw,24.f/ph);
                        SDL_Rect dst{x0+10+(32-int(pw*scale))/2,y+1+(24-int(ph*scale))/2,int(pw*scale),int(ph*scale)};
                        SDL_RenderCopy(ren,portrait,nullptr,&dst);
                    }
                }
            }
            const SDL_Rect nameArea{x0+46,y,182,kRowH};SDL_Rect nameClip{};SDL_IntersectRect(&nameArea,&aiList,&nameClip);
            SDL_RenderSetClipRect(ren,&nameClip);
            cart::drawText(ren,(locked?"(lock) ":"")+row.label,x0+46,y+3,1,locked?150:235,locked?150:230,locked?150:200);
            cart::drawText(ren,row.key+" - "+row.group+(row.side.empty()?"":" - "+row.side)+(row.variant?" - variant":""),x0+46,y+14,1,150,165,185);
            SDL_RenderSetClipRect(ren,&aiList);
            if(locked) {
                cart::drawText(ren,"can't be set alone (?)",x0+234,y+9,1,150,150,150);
                if(cart::pointIn(pointerX,pointerY,{aiList.x,y,aiList.w,kRowH}) && cart::pointIn(pointerX,pointerY,aiList))
                    aiHint="The DEFAULT AI cannot single out this unit (an original-game rule). Only group rules change it.";
                continue;
            }
            // Effective values, from the engine's reading of the file.
            bool builds=true;std::string value,limit,middle;
            bool valueSet=setWeight.count(row.key)>0,limitSet=setLimit.count(row.key)>0;
            Uint8 vr=235,vg=235,vb=240;
            if(!aiTab) {
                const auto index=catalogue.byName.at(row.key);
                const int pref=view.classic.pref[index],lim=view.classic.limit[index];
                builds=pref>0 && lim!=0;
                value=std::to_string(pref)+" %";limit=lim<0?"no limit":std::to_string(lim);
                if(valueSet || limitSet)middle="line "+std::to_string((valueSet?setWeight:setLimit)[row.key]->number);
                else if(pref!=100 || lim!=-1)middle="group rule";
            } else {
                const auto wv=view.level.weight.find(row.key);const auto lv=view.level.limit.find(row.key);
                const auto bw=view.builtin.weight.find(row.key);const auto bl=view.builtin.limit.find(row.key);
                builds=wv!=view.level.weight.end() && wv->second>0 && !(lv!=view.level.limit.end() && lv->second==0);
                if(wv!=view.level.weight.end())value=std::to_string(wv->second);
                else {value="never";vr=255;vg=120;vb=110;}
                limit=lv!=view.level.limit.end()?std::to_string(lv->second):"no cap";
                middle=bw==view.builtin.weight.end()?"-":std::to_string(bw->second);
                middle+=bl==view.builtin.limit.end()?"":" / "+std::to_string(bl->second);
            }
            const SDL_Rect buildCell{x0+232,y+5,40,16},valueCell{x0+278,y+5,92,16},limitCell{x0+460,y+5,W-462,16};
            aiCells.push_back({buildCell,row.key,2});aiCells.push_back({valueCell,row.key,0});aiCells.push_back({limitCell,row.key,1});
            fillRect(ren,buildCell.x,buildCell.y,buildCell.w,buildCell.h,builds?40:70,builds?70:36,builds?46:36);
            cart::drawText(ren,builds?"ON":"OFF",buildCell.x+(builds?14:11),buildCell.y+5,1,builds?190:255,builds?240:170,builds?190:160);
            auto cell=[&](const SDL_Rect& r,int column,const std::string& text,bool set,Uint8 cr,Uint8 cg,Uint8 cb) {
                if(aiFocus==AI_CELL && aiCellKey==row.key && aiCellColumn==column) {inlineField(ren,r.x,r.y,r.w,aiCellText,true,&aiCellEdit);return;}
                SDL_SetRenderDrawColor(ren,editable?62:44,editable?66:46,editable?82:54,255);SDL_RenderDrawRect(ren,&r);
                if(!set) {cr=std::min<Uint8>(cr,150);cg=std::min<Uint8>(cg,150);cb=std::min<Uint8>(cb,160);}
                cart::drawText(ren,text+(set?" *":""),r.x+4,r.y+5,1,cr,cg,cb);
            };
            cell(valueCell,0,value,valueSet,vr,vg,vb);
            cart::drawText(ren,middle,x0+378,y+10,1,140,150,165);
            cell(limitCell,1,limit,limitSet,235,235,240);
        }
        if(rows.empty())cart::drawText(ren,aiSetOnly?"This file sets no units that match.":"No units match.",x0+6,aiList.y+6,1,205,210,220);
        SDL_RenderSetClipRect(ren,nullptr);
        SDL_SetRenderDrawColor(ren,70,74,90,255);SDL_RenderDrawRect(ren,&aiList);
    } else if(aiView==AiView::Text) {
        const int boxTop=y0+46;
        const std::string label=view.readOnly?"TEXT (THE INSTALLED STANDARD PROFILE: READ ONLY)":"TEXT OF "+aiTextKey+" (! DOES NOTHING, ~ SHADOWED; HOVER A MARK)";
        const std::string& shown=aiTextKey.empty()?view.bytes:aiText;
        aiTextBox=cart::drawTextArea(ren,x0+14,boxTop,W-14,listBottom-boxTop-10,label,shown,aiFocus==AI_TEXT,aiTextEdit,aiTextScroll,aiTextFollow);
        // Gutter marks on each line's first row (the rows drawTextArea wrapped).
        const int columns=std::max(1,(aiTextBox.w-8)/6),visible=std::max(1,(aiTextBox.h-8)/12);
        const auto rows=cart::TextEdit::lines(shown,columns);
        int number=1;
        for(int i=0;i<int(rows.size());++i) {
            const bool first=rows[size_t(i)].begin==0 || shown[rows[size_t(i)].begin-1]=='\n';
            if(i>0 && first)++number;
            if(!first || i<aiTextScroll || i>=aiTextScroll+visible || number>int(view.lines.size()))continue;
            const auto& line=view.lines[size_t(number-1)];
            const char* mark=doesNothing(line.what)?"!":line.what==AiLine::What::Shadowed || line.what==AiLine::What::Overridden?"~":nullptr;
            if(!mark)continue;
            const int my=aiTextBox.y+4+(i-aiTextScroll)*12;
            cart::drawText(ren,mark,x0+4,my,1,255,170,90);
            if(cart::pointIn(pointerX,pointerY,{x0,my-2,14,12})) {
                aiHint="line "+std::to_string(number)+": this line does nothing.";
                for(const auto& issue:view.issues)if(issue.line==number) {aiHint=issue.message;break;}
            }
        }
    } else {
        cart::drawText(ren,std::to_string(aiIssues.size())+" AI PROFILE CHECKS. CLICK ONE TO SHOW ITS LINE. NONE BLOCK SAVING.",x0,y0+46,1,235,220,160);
        const SDL_Rect list{x0,y0+60,W,listBottom-y0-60};
        fillRect(ren,list.x,list.y,list.w,list.h,22,24,32);
        SDL_RenderSetClipRect(ren,&list);
        int y=list.y+4-aiIssueScroll,total=0;
        for(size_t i=0;i<aiIssues.size();++i) {
            const auto& issue=aiIssues[i];
            const char* kind=issue.severity==AiIssue::Severity::Error?"ERROR: ":issue.severity==AiIssue::Severity::Warning?"WARNING: ":"NOTE: ";
            const std::string where=issue.file.empty()?"aiprofile: ":issue.file+(issue.line?"":": ");
            const std::string text=std::string(kind)+where+(issue.line?" "+issue.message:issue.message);
            const auto lines=cart::TextEdit::lines(text,std::max(1,(list.w-8)/6));
            const int height=int(lines.size())*12+4;
            aiIssueRows.push_back({{list.x,y,list.w,height},i});
            const bool error=issue.severity==AiIssue::Severity::Error,info=issue.severity==AiIssue::Severity::Info;
            for(size_t l=0;l<lines.size();++l)
                cart::drawText(ren,text.substr(lines[l].begin,lines[l].end-lines[l].begin),list.x+4,y+int(l)*12,1,error?255:info?170:235,error?145:info?190:215,error?140:info?205:180);
            y+=height;total+=height;
        }
        if(aiIssues.empty())cart::drawText(ren,"No problems found in this map's AI files.",list.x+4,list.y+4,1,200,230,205);
        aiIssueScroll=std::clamp(aiIssueScroll,0,std::max(0,total-list.h+8));
        SDL_RenderSetClipRect(ren,nullptr);
        SDL_SetRenderDrawColor(ren,70,74,90,255);SDL_RenderDrawRect(ren,&list);
    }

    // ---- help strip: the selected row, the hand-written banner, or the last note ----
    const int helpY=listBottom+4;
    std::string help;Uint8 hr=200,hg=210,hb=225;
    if(!aiNote.empty()) {help=aiNote;hr=255;hg=200;hb=130;}
    else if(view.draft.asWritten && aiView==AiView::Table) {
        int nothing=0,skipped=0;std::string first,shadow;
        for(const auto& line:view.lines) {
            if(doesNothing(line.what)) {if(!nothing++)first=lineText(line.text);}
            if(line.what==AiLine::What::Shadowed || line.what==AiLine::What::Overridden) {if(!skipped++)shadow=lineText(line.text)+"\" (line "+std::to_string(line.by)+(aiTab?" wins)":" already set this unit)");}
        }
        help="This profile was written by hand. Showing what it does; it is saved unchanged.";
        if(nothing)help+=" "+std::to_string(nothing)+(nothing==1?" line does":" lines do")+" nothing: \""+first+"\".";
        if(skipped)help+=" "+std::to_string(skipped)+(skipped==1?" line is":" lines are")+" skipped: \""+shadow+".";
        help+=" MAKE EDITABLE converts it if every unit plays the same.";hr=235;hg=215;hb=150;
    } else if(!aiSelected.empty() && aiView==AiView::Table) {
        for(const auto& row:aiRoster())if(row.key==aiSelected) {help=aiRowExplanation(row);break;}
    }
    if(help.empty() && aiView==AiView::Table) {
        if(!aiTab && view.key.empty())help="The standard profile is shown read-only: the installed ai/default.txt (or every unit at 100 % when there is none). Choose this map's own profile to edit.";
        else if(!aiTab && view.key=="ai/default.txt")help="This map carries ai/default.txt: every map without its own profile name also reads ai/default.txt, and an installed one may be used instead. Choose this map's own profile to move it.";
        else help=aiTab?"WEIGHT: higher is picked more often among what this builder can make; 0 never builds it. LIMIT: blank keeps the built-in; 0 means never. Easy halves limits, Hard and Absurd raise them by half (minimum 1); Defensive levels ignore limits except 0.":
            "CHANCE: 100 % is normal; lower numbers make the AI pick this unit less often against others it wants equally; 0 % never builds it. The real odds also depend on what the AI needs during play. LIMIT: a number, or no limit.";
    }
    drawWrapped(ren,help,x0,helpY,W,3,hr,hg,hb);

    // ---- footer: which file plays, the Test Map hint, buttons ----
    const int footY=ct.y+ct.h-64;
    fillRect(ren,ct.x,footY-3,ct.w,1,70,74,90);
    const auto choice=cart::whichFile(aiTab?*aiTabLevel():Difficulty::Classic,vfs,aiFiles,scenario.aiProfile,aiPackFiles?&*aiPackFiles:nullptr);
    std::string plays="Plays: "+choice.message;
    if(aiPack)plays+=" (with pack "+aiPacks[size_t(aiPack-1)]+" ticked)";
    drawWrapped(ren,plays,ct.x,footY+2,ct.w,2,180,220,190);
    cart::drawText(ren,"Test Map: pick the AI level you are testing in the room. Game-mod files are tested by ticking that pack.",ct.x,footY+28,1,150,160,175);
    const int by=ct.y+ct.h-18;
    aiStartButton=aiEditableButton=aiInsertButton={};
    int bx=ct.x;
    if(!aiTab && !standard) {aiStartButton=cart::drawButton(ren,bx,by,96,16,"START FROM...",false);bx+=102;}
    aiViewButton=cart::drawButton(ren,bx,by,96,16,aiView==AiView::Text?"TABLE VIEW":"TEXT VIEW",aiView==AiView::Text);bx+=102;
    aiCheckButton=cart::drawButton(ren,bx,by,64,16,aiView==AiView::Issues?"TABLE":"CHECK",aiView==AiView::Issues);bx+=70;
    if(view.draft.asWritten && !view.key.empty()) {aiEditableButton=cart::drawButton(ren,bx,by,104,16,"MAKE EDITABLE",true);bx+=110;}
    if(aiView==AiView::Text && !aiTextKey.empty()) {aiInsertButton=cart::drawButton(ren,bx,by,96,16,"INSERT UNIT",false);bx+=102;}
    cart::drawText(ren,"Undo after DONE. Right-click a cell: not set.",bx+6,by+5,1,140,150,165);
    aiDoneButton=cart::drawButton(ren,ct.x+ct.w-60,by,60,16,"DONE",true);

    if(aiStartOpen) {
        const auto pc=cart::drawPanel(ren,w,h,460,136,"START FROM");
        const auto key=cart::aiProfileFileKey(aiPendingName.empty()?scenario.aiProfile:aiPendingName);
        cart::drawText(ren,"The game's standard DEFAULT AI: copies your install's ai/default.txt.",pc.x,pc.y,1,220,225,235);
        cart::drawText(ren,"Empty: every unit at full chance with no limit (plays worse).",pc.x,pc.y+14,1,220,225,235);
        cart::drawText(ren,aiFiles.count(key)?"This replaces the map's "+key+" (Undo restores it).":"The profile is written to "+key+" in the map.",pc.x,pc.y+34,1,170,185,200);
        aiStartStandard=cart::drawButton(ren,pc.x,pc.y+pc.h-20,190,18,"STANDARD DEFAULT AI",true);
        aiStartEmpty=cart::drawButton(ren,pc.x+196,pc.y+pc.h-20,80,18,"EMPTY",false);
        aiStartCancel=cart::drawButton(ren,pc.x+pc.w-74,pc.y+pc.h-20,70,18,"CANCEL",false);
    }
    if(!aiHint.empty() && !aiStartOpen && modal==M_NONE)cart::drawTooltip(ren,w,h,pointerX,pointerY,aiHint);
}
