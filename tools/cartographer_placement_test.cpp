// Cartographer placement and robustness tests. Each run drives the real editor
// (cart::runEditor) with synthetic SDL input pushed from the frame hook: the
// events of one frame are handled by the real loop during the next, so every
// placement goes through the same palette-click and canvas-click path a user's
// does. The attached Editor is only read back to check the document.
//
//   cartographer_placement_test <data> starts [world...]
//       Regression: switching to the STARTS browser (or changing the start list
//       while it shows) crashed -- the browser drew the previous tool's items.
//       Blank, generated, New-dialog and shipped maps of every world.
//   ... rescan            the browser after its library is rescanned (a Maps/
//                         archive's features vanish when a map opens from disk)
//   ... terrain [world...]  every section: browser pick, stamp by click, undo, redo
//   ... features [world...] every feature: pick, place, undo, redo, erase, undo
//   ... units [world...]    every unit type, on terrain the game accepts
//   ... slots [world...]    start slots 1-8 (a ninth is refused) and a region
// Each placement mode then saves the map, reopens it through Open and compares.
// Every frame checks that the browser lists only items of the current list.
// Without data every mode exits 77 (skipped).

#include "cartographer/editor.h"
#include "cartographer/editorstate.h"
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"
#include "tdf/tdf.h"
#include "tnt/ota.h"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace fs=std::filesystem;
using Editor=cart::Editor;
using cart::lowerText;

namespace {

std::vector<std::string> failures;
bool check(bool ok,const std::string& what) {
    if(!ok) {failures.push_back(what);std::cerr<<"FAIL: "<<what<<'\n';}
    return ok;
}

const std::vector<std::string> kWorlds={"aramon","veruna","taros","zhon","creon"};

// Synthetic input in the editor's logical coordinates (scaled like the real
// window: the loop divides event positions by kUIScale).
struct Input {
    SDL_Window* window=nullptr;SDL_Renderer* renderer=nullptr;
    float scale() const {float sx=1,sy=1;SDL_RenderGetScale(renderer,&sx,&sy);return sx;}
    int physical(int v) const {return int(std::ceil(v*scale()));}
    void button(Uint32 type,int x,int y,Uint8 which,Uint8 clicks) const {
        SDL_Event e{};e.type=type;e.button.button=which;e.button.clicks=clicks;
        e.button.state=type==SDL_MOUSEBUTTONDOWN?SDL_PRESSED:SDL_RELEASED;
        e.button.x=physical(x);e.button.y=physical(y);SDL_PushEvent(&e);
    }
    void click(int x,int y,Uint8 which=SDL_BUTTON_LEFT,Uint8 clicks=1) const {
        button(SDL_MOUSEBUTTONDOWN,x,y,which,clicks);button(SDL_MOUSEBUTTONUP,x,y,which,clicks);
    }
    void motion(int x,int y,Uint32 state) const {
        SDL_Event e{};e.type=SDL_MOUSEMOTION;e.motion.state=state;e.motion.x=physical(x);e.motion.y=physical(y);SDL_PushEvent(&e);
    }
    void drag(int x,int y,int tx,int ty) const {
        button(SDL_MOUSEBUTTONDOWN,x,y,SDL_BUTTON_LEFT,1);motion((x+tx)/2,(y+ty)/2,SDL_BUTTON_LMASK);
        motion(tx,ty,SDL_BUTTON_LMASK);button(SDL_MOUSEBUTTONUP,tx,ty,SDL_BUTTON_LEFT,1);
    }
    void key(SDL_Keycode k,Uint16 mod=0) const {
        SDL_Event e{};e.type=SDL_KEYDOWN;e.key.state=SDL_PRESSED;e.key.keysym.sym=k;e.key.keysym.mod=mod;SDL_PushEvent(&e);
        e.type=SDL_KEYUP;e.key.state=SDL_RELEASED;SDL_PushEvent(&e);
    }
    void text(const std::string& value) const {
        for(size_t i=0;i<value.size();i+=20) {
            SDL_Event e{};e.type=SDL_TEXTINPUT;const auto part=value.substr(i,20);SDL_strlcpy(e.text.text,part.c_str(),sizeof(e.text.text));SDL_PushEvent(&e);
        }
    }
    // Move the real pointer (paste targets and wheel routing read SDL_GetMouseState).
    void pointer(int x,int y) const {SDL_WarpMouseInWindow(window,physical(x),physical(y));}
};

// Toolbar and canvas geometry, from the editor's own layout constants.
void tool(const Input& in,Editor::Tool t) {in.click(96+int(t)*72+30,32);}
void mode(const Input& in,Editor::EditMode m) {in.click(400+int(m)*64+30,32);}
SDL_Point cellScreen(const Editor& e,float wx,float wz) {   // floor: at 1/16 a cell is one pixel
    return {Editor::kPaletteW+int(std::floor((wx-e.mapView.offX())*e.mapView.zoom())),Editor::kMenuH+int(std::floor((wz-e.mapView.offY())*e.mapView.zoom()))};
}
// Centre the 100% view on a world point, at a canvas spot clear of the minimap.
SDL_Point focus(Editor& e,float wx,float wz) {
    e.mapView.setZoom(1);e.mapView.setOffset(wx-160,wz-160);
    return cellScreen(e,wx,wz);
}
// A browser point whose row is `row`, found through the editor's own hit test.
std::optional<SDL_Point> paletteRow(const Editor& e,int row) {
    for(int y=Editor::kPaletteTop+2;y<e.h-Editor::kStatusH-2;y+=2)
        for(int x=6;x<Editor::kPaletteW-6;x+=4)if(e.paletteRowAt(x,y)==row)return SDL_Point{x,y};
    return std::nullopt;
}

// The browser must only ever list items of the current tool's source list.
std::string browserProblem(const Editor& e) {
    const size_t count=e.tool==Editor::UNITS?e.unitTypes.size():e.tool==Editor::FEATURES?e.features.list().size():
        e.tool==Editor::STARTS?e.scenario.starts.size():e.sections.list().size();
    static const char* names[]={"TERRAIN","FEATURES","UNITS","STARTS"};
    for(const int i:e.paletteItems)if(i<0 || size_t(i)>=count)
        return std::string(names[int(e.tool)])+" browser lists item "+std::to_string(i)+" of "+std::to_string(count);
    if(e.tool==Editor::TERRAIN)for(const auto& variants:e.paletteVariants)for(const int i:variants)if(i<0 || size_t(i)>=e.sections.list().size())   // only the terrain browser reads them
        return "terrain browser variant "+std::to_string(i)+" of "+std::to_string(e.sections.list().size());
    if(e.selected>=int(e.sections.list().size()))return "selected section out of range";
    if(e.selectedFeat>=int(e.features.list().size()))return "selected feature out of range";
    if(e.selectedType>=int(e.unitTypes.size()))return "selected unit type out of range";
    return {};
}

std::string tempRoot(const std::string& tag) {
    const char* base=std::getenv("TMPDIR");
    const auto root=fs::path(base && *base?base:fs::temp_directory_path().string())/("tak-carto-"+tag+"-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);return root.string();
}
// Point every editor profile lookup at a private folder (see cartographer_ui_test).
void isolateProfile(const std::string& root) {
    SDL_setenv("XDG_CONFIG_HOME",root.c_str(),1);SDL_setenv("XDG_DATA_HOME",root.c_str(),1);
    const auto profile=fs::path(root)/"TAKengine"/"Cartographer";
    std::error_code ec;fs::create_directories(profile,ec);
    SDL_setenv("TAK_CARTOGRAPHER_PROFILE",profile.string().c_str(),1);
}

// One editor session: `frame` runs after every drawn frame and returns false to
// end the session. Each frame also checks the browser invariant.
bool drive(std::vector<std::string> args,const std::function<bool(Editor&,const Input&)>& frame,int seconds,const std::string& label) {
    Editor* editor=nullptr;cart::EditorHooks hooks;hooks.attached=[&](Editor& e){editor=&e;};
    std::vector<char*> raw;for(auto& arg:args)raw.push_back(arg.data());
    const auto began=std::chrono::steady_clock::now();bool browserOk=true;
    const int result=cart::runEditor(int(raw.size()),raw.data(),[&](SDL_Window* window,SDL_Renderer* renderer,int) {
        if(std::chrono::steady_clock::now()-began>std::chrono::seconds(seconds)) {
            std::cerr<<"FAIL: "<<label<<" timed out\n";std::_Exit(3);
        }
        if(const auto problem=browserProblem(*editor);!problem.empty() && browserOk)browserOk=check(false,label+": "+problem);
        const Input in{window,renderer};
        if(!frame(*editor,in))editor->running=false;
    },hooks);
    return check(result==0,label+": editor exit code "+std::to_string(result)) && browserOk;
}

// One map of each world in the install: the alphabetically first shipped
// (maps/) one, else the first downloaded one.
std::map<std::string,std::string> mapsByWorld(const std::string& data) {
    std::map<std::string,std::pair<bool,std::string>> found;
    const auto vfs=tak::hpi::mountRetailRoot(fs::u8path(data));
    for(const auto& [name,path]:tak::hpi::listMaps(vfs)) {
        try {
            const auto bytes=vfs.read(path.substr(0,path.rfind('.'))+".ota");
            auto world=tak::tnt::Scenario::parse(std::string(bytes.begin(),bytes.end())).kingdom;
            for(char& c:world)c=char(std::tolower((unsigned char)c));
            const std::pair<bool,std::string> rank{!path.starts_with("maps/"),name};
            if(std::find(kWorlds.begin(),kWorlds.end(),world)!=kWorlds.end() && (!found.count(world) || rank<found[world]))found[world]=rank;
        } catch(const std::exception&) {}
    }
    std::map<std::string,std::string> names;for(const auto& [world,rank]:found)names[world]=rank.second;
    return names;
}

// ---- starts: the crash regression -------------------------------------------

// Tool switches into STARTS and start edits while the STARTS browser is up,
// on one document. `dialogWorld` first creates the map with the New dialog.
bool startsCase(const std::string& data,const std::string& label,std::vector<std::string> args,const std::string& dialogWorld) {
    const auto root=tempRoot("starts");isolateProfile(root);
    args.insert(args.begin(),"cartographer");args.insert(args.end(),{"--data",data,"--out",root});
    static constexpr Editor::Tool kWalk[]={Editor::FEATURES,Editor::TERRAIN,Editor::UNITS,Editor::TERRAIN,Editor::STARTS,
        Editor::FEATURES,Editor::STARTS,Editor::UNITS,Editor::STARTS};
    int stage=dialogWorld.empty()?1:0;size_t before=0,walk=0;int placed=-1,cellX=0,cellZ=0;bool ok=true;
    auto expect=[&](bool value,const std::string& what){ok&=check(value,label+": "+what);};
    const bool ran=drive(args,[&](Editor& e,const Input& in) {
        auto& starts=e.scenario.starts;
        switch(stage++) {
        case 0: {   // New Map dialog: WORLD is the fourth field, cycled with the arrow keys
            expect(e.modal==Editor::M_NEW,"New Map dialog opens");
            const auto& worlds=e.kWorldOpts;
            const int from=int(std::find(worlds.begin(),worlds.end(),e.mf[3])-worlds.begin());
            const int to=int(std::find(worlds.begin(),worlds.end(),dialogWorld)-worlds.begin());
            for(int i=0;i<3;++i)in.key(SDLK_TAB);
            for(int i=0;i<(to-from+int(worlds.size()))%int(worlds.size());++i)in.key(SDLK_RIGHT);
            in.key(SDLK_RETURN);return true;
        }
        case 1:   // one tool switch per frame, so each browser is drawn right after its switch
            if(!dialogWorld.empty() && walk==0)expect(e.modal==Editor::M_NONE && e.world==dialogWorld,"New Map dialog creates a "+dialogWorld+" map");
            if(walk>0)expect(e.tool==kWalk[walk-1],"toolbar switches tool");
            before=starts.size();
            if(walk<std::size(kWalk)) {tool(in,kWalk[walk++]);--stage;}
            return true;
        case 2:   // and every switch into STARTS within one frame
            expect(e.tool==Editor::STARTS,"STARTS tool chosen");
            for(const auto from:{Editor::TERRAIN,Editor::FEATURES,Editor::UNITS}) {tool(in,from);tool(in,Editor::STARTS);}
            for(int i=0;i<4;++i)in.key(SDLK_TAB);   // Tab cycles back round to STARTS
            in.key(SDLK_l,KMOD_CTRL);in.key(SDLK_l,KMOD_CTRL);tool(in,Editor::STARTS);
            return true;
        case 3: {   // place a new start on an empty cell away from the others
            expect(e.tool==Editor::STARTS,"STARTS tool after switching round");
            if(!cart::nextStartNumber(starts) && !starts.empty()) {   // all eight used: erase the last first
                const auto p=focus(e,starts.back().xpos*16+8.f,starts.back().zpos*16+8.f);mode(in,Editor::MODE_ERASE);in.click(p.x,p.y);
                before=starts.size()-1;--stage;return true;
            }
            const auto& map=e.mapView.map();
            for(int z=map.height/2,found=0;!found && z<map.height;z+=3)for(int x=map.width/2;x<map.width;x+=3) {
                bool clear=true;for(const auto& s:starts)clear&=std::abs(s.xpos-x)>2 || std::abs(s.zpos-z)>2;
                if(clear) {cellX=x;cellZ=z;found=1;break;}
            }
            const auto p=focus(e,cellX*16+8.f,cellZ*16+8.f);mode(in,Editor::MODE_PLACE);in.click(p.x,p.y);return true;
        }
        case 4: {
            expect(starts.size()==before+1,"start placed by a canvas click");
            for(size_t i=0;i<starts.size();++i)if(starts[i].xpos==cellX && starts[i].zpos==cellZ)placed=int(i);
            expect(placed>=0,"new start at the clicked cell");
            // Click its browser row: the view centres on it.
            const auto row=std::find(e.paletteItems.begin(),e.paletteItems.end(),placed)-e.paletteItems.begin();
            const auto point=paletteRow(e,int(row));
            expect(point.has_value(),"new start listed in the STARTS browser");
            if(point) {e.mapView.setOffset(0,0);in.click(point->x,point->y);}
            return true;
        }
        case 5: {
            expect(std::abs(e.mapView.offX()-(cellX*16-e.canvasW/(2*e.mapView.zoom())))<1,"STARTS browser row centres the view on the start");
            const auto p=focus(e,cellX*16+8.f,cellZ*16+8.f);mode(in,Editor::MODE_ERASE);in.click(p.x,p.y);return true;
        }
        case 6: expect(starts.size()==before,"erase removes the start while the STARTS browser lists it");in.key(SDLK_z,KMOD_CTRL);return true;
        case 7: expect(starts.size()==before+1,"undo restores the erased start");in.key(SDLK_y,KMOD_CTRL);return true;
        case 8:
            expect(starts.size()==before,"redo erases it again");
            if(!starts.empty()) {   // the last browser row is the one a shrinking list loses
                const auto p=focus(e,starts.back().xpos*16+8.f,starts.back().zpos*16+8.f);in.click(p.x,p.y);
            }
            return true;
        case 9:   // back to the placed start (one more undo when an original was erased)
            expect(starts.size()==(before?before-1:0),"erase an existing start");
            in.key(SDLK_z,KMOD_CTRL);if(before)in.key(SDLK_z,KMOD_CTRL);return true;
        case 10: {   // Select mode: drag the placed start two cells right
            expect(starts.size()==before+1,"undo across start edits");
            if(starts.size()!=before+1)return false;
            for(size_t i=0;i<starts.size();++i)if(starts[i].xpos==cellX && starts[i].zpos==cellZ)placed=int(i);
            const auto p=focus(e,cellX*16+8.f,cellZ*16+8.f);mode(in,Editor::MODE_SELECT);in.drag(p.x,p.y,p.x+32,p.y);return true;
        }
        case 11:
            expect(placed>=0 && placed<int(starts.size()) && starts[placed].xpos==cellX+2 && starts[placed].zpos==cellZ,"Select mode drags a start");
            tool(in,Editor::TERRAIN);tool(in,Editor::STARTS);in.key(SDLK_z,KMOD_CTRL);return true;
        default: return false;
        }
    },120,label);
    expect(stage>12,"script finished (stopped at stage "+std::to_string(stage)+")");
    std::error_code ec;fs::remove_all(root,ec);
    if(ran && ok)std::cout<<"ok: "<<label<<'\n';
    return ran && ok;
}

int startsWorkflow(const std::string& data,std::vector<std::string> worlds) {
    if(worlds.empty())worlds=kWorlds;
    const auto shipped=mapsByWorld(data);
    for(const auto& world:worlds) {
        startsCase(data,world+" blank map",{"--new","8x8","--world",world},"");
        startsCase(data,world+" generated map",{"--new","8x8","--world",world,"--random"},"");
        startsCase(data,world+" New Map dialog",{},world);
        if(const auto found=shipped.find(world);found!=shipped.end())startsCase(data,world+" map '"+found->second+"'",{found->second},"");
        else std::cout<<"note: no shipped "<<world<<" map in this install\n";
    }
    if(!failures.empty())return 1;
    std::cout<<"PASS: STARTS browser and start edits on blank, generated, New-dialog and shipped maps of "<<worlds.size()<<" worlds\n";
    return 0;
}

// ---- every browser item, placed through the real click path -----------------

// Choose `index` in the current tool's browser the way a user does: search for
// it (Ctrl+F) in one frame, then click its row (terrain: until the wanted
// variant of the piece is selected), scrolling the list first when the row is
// out of view. click(): 1 = clicks queued, 0 = scrolled (retry next frame),
// -1 = the search does not list the item.
struct BrowserPick {
    int index=-1,scrolls=0;
    void search(const Input& in,int item,const std::string& query) {
        index=item;scrolls=0;in.key(SDLK_f,KMOD_CTRL);in.text(query);in.key(SDLK_ESCAPE);
    }
    int click(const Editor& e,const Input& in) {
        int row=-1,clicks=1;
        if(e.tool==Editor::TERRAIN) {
            for(size_t r=0;r<e.paletteVariants.size() && row<0;++r) {
                const auto& v=e.paletteVariants[r];
                if(std::find(v.begin(),v.end(),index)==v.end())continue;
                // Replay handlePaletteClick: a click selects the shown variant, or steps the selected piece.
                int selected=e.selected,shown=e.paletteItems[r];row=int(r);clicks=0;
                while(selected!=index && clicks<=int(v.size())) {
                    if(shown==selected && v.size()>1) {const auto at=std::find(v.begin(),v.end(),selected);selected=shown=at+1==v.end()?v.front():*(at+1);}
                    else selected=shown;
                    ++clicks;
                }
            }
        } else if(const auto at=std::find(e.paletteItems.begin(),e.paletteItems.end(),index);at!=e.paletteItems.end())row=int(at-e.paletteItems.begin());
        if(row<0)return -1;
        const auto point=paletteRow(e,row);
        if(!point) {   // out of view: wheel over the browser towards it, a few notches a frame
            if(++scrolls>400)return -1;
            in.pointer(Editor::kPaletteW/2,Editor::kPaletteTop+40);
            const bool up=row<e.paletteRowAt(10,Editor::kPaletteTop+4);
            for(int i=0;i<8;++i) {SDL_Event wheel{};wheel.type=SDL_MOUSEWHEEL;wheel.wheel.y=up?1:-1;SDL_PushEvent(&wheel);}
            return 0;
        }
        for(int i=0;i<clicks;++i)in.click(point->x,point->y);
        return 1;
    }
};

// The first difference between two documents' terrain planes; features compare
// by name (a reload keeps 63 bytes of each name, like the game).
std::string planeDifference(const tak::tnt::Map& a,const tak::tnt::Map& b) {
    if(a.width!=b.width || a.height!=b.height || a.blocksX!=b.blocksX || a.blocksY!=b.blocksY)return "map size";
    if(a.tileKeys!=b.tileKeys || a.tileCols!=b.tileCols || a.tileRows!=b.tileRows)return "tile plane";
    if(a.heights!=b.heights)return "heights";
    if(a.features.size()!=b.features.size())return "feature plane size";
    if(a.features==b.features && a.featureNames==b.featureNames)return {};
    auto name=[](const tak::tnt::Map& m,size_t i) {
        const auto v=m.features[i];
        return v>=0xFFFA?"#"+std::to_string(v):v<m.featureNames.size()?m.featureNames[v].substr(0,63):std::string("?");
    };
    for(size_t i=0;i<a.features.size();++i)if(name(a,i)!=name(b,i))
        return "feature at cell "+std::to_string(i%a.width)+","+std::to_string(i/a.width)+": "+name(a,i)+" vs "+name(b,i);
    return {};
}

// An engine world over the document's terrain and features (Check Map's view).
std::unique_ptr<tak::sim::World> engineWorld(const Editor& e) {
    const auto& map=e.mapView.map();auto world=std::make_unique<tak::sim::World>();
    world->setTerrain(map.heights,map.width,map.height,map.seaLevel,&map.features);
    world->buildNavClasses(e.unitRegistry);
    tak::sim::registerMapFeatures(*world,map,e.vfs,&e.unitRegistry);
    return world;
}
// What the game makes of the document: unreadable terrain images and feature
// names it cannot resolve (Check Map's own predicates).
std::vector<std::string> engineProblems(const Editor& e) {
    std::vector<std::string> out;
    const auto& map=e.mapView.map();
    for(const auto& issue:cart::validateTerrainResources(map,e.vfs))out.push_back(issue.message);
    const auto world=engineWorld(e);const auto& types=world->mapPlacementTypes();
    std::set<uint16_t> seen;
    for(const auto v:map.features)if(v<0xFFFA && seen.insert(v).second && (v>=types.size() || types[v].name.empty()))
        out.push_back("the game cannot resolve feature '"+(v<map.featureNames.size()?map.featureNames[v]:std::to_string(v))+"'");
    return out;
}

struct Tally {
    int listed=0,placed=0,undone=0,redone=0,erased=0,restored=0,reloaded=0,refused=0;
    std::map<std::string,int> kinds;   // per-category breakdown for the report
    void print(const std::string& what) const {
        std::cout<<what<<": "<<listed<<" listed, "<<placed<<" placed, "<<undone<<" undone, "<<redone<<" redone";
        if(erased)std::cout<<", "<<erased<<" erased, "<<restored<<" restored";
        std::cout<<", "<<reloaded<<" survived save/reopen";
        if(refused)std::cout<<", "<<refused<<" refused";
        std::cout<<'\n';
        if(!kinds.empty()) {std::cout<<"   ";for(const auto& [kind,count]:kinds)std::cout<<' '<<kind<<'='<<count;std::cout<<'\n';}
    }
};

// Save with Ctrl+S, dismiss the confirmation, reopen the .kmp through Open:
// queue() in one frame; the next frame sees the reopened document.
void saveAndReopen(const Editor& e,const Input& in,const std::string&,const std::string& root) {
    in.key(SDLK_s,KMOD_CTRL);in.key(SDLK_RETURN);
    in.key(SDLK_o,KMOD_CTRL);in.text((fs::path(root)/(e.mapName+".kmp")).string());in.key(SDLK_RETURN);
}
bool reopened(const Editor& e,const std::string& label) {
    return check(e.modal==Editor::M_NONE && !e.dirty && e.mapPath.starts_with("kmap/"),label+": saved map reopens clean (modal "+std::to_string(int(e.modal))+", "+e.mapPath+")");
}

// One session per batch of sections on a fresh 16x16-unit map of `world`: each
// section is stamped by a canvas click in its own slot, undone and redone; the
// filled map is saved, reopened and compared. Returns the next section index.
size_t terrainSession(const std::string& data,const std::string& world,size_t first,Tally& tally) {
    const auto root=tempRoot("terrain");isolateProfile(root);
    enum {START,SEARCH,PICK,PLACED,UNDONE,REDONE,REOPENED} phase=START;
    size_t item=first;BrowserPick pick;tak::tnt::Map expected,prior;
    int slot=0,slotsX=0,slots=0,slotBlocks=1;const tak::tnt::Map* prefab=nullptr;
    const std::string label=world+" terrain";
    drive({"cartographer","--new","16x16","--world",world,"--data",data,"--out",root},[&](Editor& e,const Input& in) {
        const auto& sections=e.sections.list();
        auto fail=[&](const std::string& what) {check(false,label+" '"+sections[item].category+"/"+sections[item].name+"': "+what);};
        for(;;)switch(phase) {
        case START: {
            if(first==0)tally.listed+=int(sections.size());
            // Slots fit the largest section; stampAtMouse snaps each to its own size.
            for(const auto& s:sections)if(const auto* m=e.sections.load(e.vfs,s.path))slotBlocks=std::max({slotBlocks,m->blocksX,m->blocksY});
            slotsX=e.mapView.map().blocksX/slotBlocks;slots=slotsX*(e.mapView.map().blocksY/slotBlocks);
            expected=e.mapView.map();tool(in,Editor::TERRAIN);mode(in,Editor::MODE_PLACE);phase=SEARCH;return true;
        }
        case SEARCH:
            if(item>=sections.size() || slot>=slots) {
                if(item==first) return false;
                for(const auto& problem:engineProblems(e))check(false,label+": "+problem);   // tile images decode, prefab features resolve
                saveAndReopen(e,in,label,root);phase=REOPENED;return true;
            }
            if(const auto d=planeDifference(e.mapView.map(),expected);!d.empty()) {fail("document differs before placing: "+d);return false;}
            prefab=e.sections.load(e.vfs,sections[item].path);
            if(!prefab || prefab->blocksX<=0 || prefab->blocksY<=0) {fail("prefab does not load");++item;continue;}
            ++tally.kinds[std::to_string(prefab->blocksX)+"x"+std::to_string(prefab->blocksY)+"-blocks"];
            if(!e.vfs.has(sections[item].path,true))++tally.kinds["from-downloaded-map"];
            pick.search(in,int(item),sections[item].name);phase=PICK;return true;
        case PICK: {
            const int clicked=pick.click(e,in);
            if(!clicked)return true;
            if(clicked<0) {fail("browser search does not list it");++item;phase=SEARCH;continue;}
            const int sx=(slot%slotsX)*slotBlocks,sz=(slot/slotsX)*slotBlocks;++slot;
            const int ox=sx/prefab->blocksX*prefab->blocksX,oz=sz/prefab->blocksY*prefab->blocksY;   // the click snaps here
            const auto p=focus(e,(ox+prefab->blocksX/2)*32+16.f,(oz+prefab->blocksY/2)*32+16.f);
            in.click(p.x,p.y);
            prior=expected;cart::stampSection(expected,*prefab,ox,oz,e.stampLayers);
            phase=PLACED;return true;
        }
        case PLACED:
            if(!check(e.selected==int(item),label+": browser click selects '"+sections[item].name+"'")) {return false;}
            if(const auto d=planeDifference(e.mapView.map(),expected);!d.empty()) {fail("click did not stamp it at the clicked block: "+d);return false;}
            ++tally.placed;
            if(planeDifference(prior,expected).empty()) {   // the blank map's own fill: no change, no undo step
                ++tally.kinds["same-as-blank-fill"];++tally.undone;++tally.redone;++item;phase=SEARCH;continue;
            }
            in.key(SDLK_z,KMOD_CTRL);phase=UNDONE;return true;
        case UNDONE:
            if(const auto d=planeDifference(e.mapView.map(),prior);!d.empty()) {fail("undo: "+d);return false;}
            ++tally.undone;in.key(SDLK_y,KMOD_CTRL);phase=REDONE;return true;
        case REDONE:
            if(const auto d=planeDifference(e.mapView.map(),expected);!d.empty()) {fail("redo: "+d);return false;}
            ++tally.redone;++item;phase=SEARCH;continue;
        case REOPENED:
            if(reopened(e,label)) {
                if(const auto d=planeDifference(e.mapView.map(),expected);!d.empty())check(false,label+": saved and reopened map differs: "+d);
                else tally.reloaded+=int(item-first);
            }
            return false;
        }
    },900,label);
    std::error_code ec;fs::remove_all(root,ec);
    return item;
}

int terrainWorkflow(const std::string& data,std::vector<std::string> worlds) {
    if(worlds.empty())worlds=kWorlds;
    for(const auto& world:worlds) {
        Tally tally;size_t next=0;
        for(size_t done=SIZE_MAX;next!=done;) {done=next;next=terrainSession(data,world,next,tally);if(!failures.empty())break;}
        tally.print(world+" terrain sections");
        check(tally.placed==tally.listed && tally.reloaded==tally.listed,world+": every terrain section placed and saved");
    }
    if(!failures.empty())return 1;
    std::cout<<"PASS: every terrain section of "<<worlds.size()<<" worlds stamps by click, undoes, redoes and survives save/reopen\n";
    return 0;
}

// The feature TDF fields the report breaks placements down by.
// Where a definition comes from: the install's own archives (world / shared),
// an override pack (override / overridden), or only a downloaded map in Maps/
// (downloaded-map; downloaded-map-shadowed when such a map replaces a retail
// file). Retail mounts every Maps/ archive globally, so the browser offers them.
std::string definitionSource(const tak::hpi::Vfs& vfs,const tak::hpi::Vfs& plain,const std::string& path,const std::string& sharedDir) {
    const auto full=vfs.tryRead(path),installed=vfs.tryRead(path,true),retail=plain.tryRead(path,true);
    return !installed?"downloaded-map":*installed!=full?"downloaded-map-shadowed":!retail?"override":*retail!=*installed?"overridden":
        !sharedDir.empty() && path.starts_with(sharedDir)?"shared":"world";
}
struct FeatureTraits {bool animated=false,reclaimable=false,blocking=false;int height=0;std::string source;};
std::map<std::string,FeatureTraits> featureTraits(const tak::hpi::Vfs& vfs,const tak::hpi::Vfs& plain,const std::string& world) {
    std::map<std::string,FeatureTraits> out;
    for(const std::string dir:{"features/"+world,std::string("features/all worlds")})for(const auto& path:vfs.list(dir)) {
        if(!path.ends_with(".tdf"))continue;
        try {
            const auto bytes=vfs.read(path);const auto root=tak::tdf::parseText(std::string(bytes.begin(),bytes.end()),path);
            const auto source=definitionSource(vfs,plain,path,"features/all worlds");
            for(const auto& name:root.childOrder)if(const auto* n=root.child(name)) {
                auto& traits=out[name];
                traits.animated=n->numberOr("animating",0)!=0;traits.reclaimable=n->numberOr("reclaimable",0)!=0;
                traits.blocking=n->numberOr("blocking",0)!=0;traits.height=int(n->numberOr("height",0));traits.source=source;
            }
        } catch(const std::exception&) {}
    }
    return out;
}

// Every feature of `world` on one 8x8-unit map: browser pick, Place click,
// undo, redo, Erase click, undo; then save, reopen, compare, and check the game
// resolves each name with the browser's footprint.
bool featureSession(const std::string& data,const std::string& world,Tally& tally) {
    const auto root=tempRoot("features");isolateProfile(root);
    const auto traits=featureTraits(tak::hpi::mountRetailRoot(fs::u8path(data)),tak::hpi::mountRetailRoot(fs::u8path(data),tak::hpi::OverridePolicy::None),world);
    enum {START,SEARCH,PICK,PLACED,UNDONE,REDONE,ERASED,RESTORED,REOPENED} phase=START;
    size_t item=0;int cellX=0,cellZ=0;BrowserPick pick;tak::tnt::Map expected,prior;
    std::vector<std::pair<cart::FeatureRef,size_t>> placed;   // browser entry, cell
    const std::string label=world+" features";bool finished=false;
    drive({"cartographer","--new","8x8","--world",world,"--data",data,"--out",root},[&](Editor& e,const Input& in) {
        const auto& list=e.features.list();const auto& map=e.mapView.map();
        auto fail=[&](const std::string& what) {check(false,label+" '"+list[item].name+"': "+what);};
        auto cellPoint=[&] {return focus(e,cellX*16+8.f,cellZ*16+8.f);};
        for(;;)switch(phase) {
        case START:
            tally.listed+=int(list.size());expected=map;tool(in,Editor::FEATURES);phase=SEARCH;return true;
        case SEARCH: {
            if(item>=list.size()) {
                // What the game makes of them (this install's view, as when the map is
                // played from Maps/): every name resolves, with the browser's footprint.
                for(const auto& problem:engineProblems(e))check(false,label+": "+problem);
                const auto game=engineWorld(e);const auto& types=game->mapPlacementTypes();
                for(const auto& [ref,cell]:placed) {
                    const auto v=map.features[cell];
                    if(v<types.size() && (types[v].footX!=ref.footX || types[v].footZ!=ref.footZ))
                        check(false,label+" '"+ref.name+"': game footprint "+std::to_string(types[v].footX)+"x"+std::to_string(types[v].footZ)+
                              ", browser "+std::to_string(ref.footX)+"x"+std::to_string(ref.footZ));
                }
                saveAndReopen(e,in,label,root);phase=REOPENED;return true;
            }
            const int slot=int(item),perRow=map.width/8;
            if(slot>=perRow*(map.height/8)) {fail("no free slot");return false;}
            cellX=slot%perRow*8+3;cellZ=slot/perRow*8+3;
            const auto& ref=list[item];
            const auto t=traits.find(ref.name);
            if(t!=traits.end()) {
                if(t->second.animated)++tally.kinds["animated"];
                if(t->second.reclaimable)++tally.kinds["reclaimable"];
                if(t->second.blocking)++tally.kinds["blocking"];
                if(t->second.height>=64)++tally.kinds["tall"];
                ++tally.kinds["from-"+t->second.source];
            }
            if(ref.footX*ref.footZ>1)++tally.kinds["multi-cell"];
            if(const auto* sprite=e.features.sprite(e.vfs,ref);!sprite || sprite->w<=0)++tally.kinds["no-art"];
            pick.search(in,int(item),ref.name);phase=PICK;return true;
        }
        case PICK: {
            const int clicked=pick.click(e,in);
            if(!clicked)return true;
            if(clicked<0) {fail("browser search does not list it");++item;phase=SEARCH;continue;}
            const auto p=cellPoint();mode(in,Editor::MODE_PLACE);in.click(p.x,p.y);
            prior=expected;
            const auto& name=list[item].name;
            auto at=std::find(expected.featureNames.begin(),expected.featureNames.end(),name);
            if(at==expected.featureNames.end())at=expected.featureNames.insert(expected.featureNames.end(),name);
            expected.features[size_t(cellZ)*expected.width+cellX]=uint16_t(at-expected.featureNames.begin());
            phase=PLACED;return true;
        }
        case PLACED:
            if(!check(e.selectedFeat==int(item),label+": browser click selects '"+list[item].name+"'"))return false;
            if(const auto d=planeDifference(map,expected);!d.empty()) {fail("click did not place it at the clicked cell: "+d);return false;}
            ++tally.placed;in.key(SDLK_z,KMOD_CTRL);phase=UNDONE;return true;
        case UNDONE:
            if(const auto d=planeDifference(map,prior);!d.empty()) {fail("undo: "+d);return false;}
            ++tally.undone;in.key(SDLK_y,KMOD_CTRL);phase=REDONE;return true;
        case REDONE: {
            if(const auto d=planeDifference(map,expected);!d.empty()) {fail("redo: "+d);return false;}
            ++tally.redone;const auto p=cellPoint();mode(in,Editor::MODE_ERASE);in.click(p.x,p.y);phase=ERASED;return true;
        }
        case ERASED:
            if(map.features[size_t(cellZ)*map.width+cellX]!=0xFFFF) {fail("Erase click left it");return false;}
            ++tally.erased;in.key(SDLK_z,KMOD_CTRL);phase=RESTORED;return true;
        case RESTORED:
            if(const auto d=planeDifference(map,expected);!d.empty()) {fail("undo of the erase: "+d);return false;}
            ++tally.restored;placed.push_back({list[item],size_t(cellZ)*map.width+cellX});++item;phase=SEARCH;continue;
        case REOPENED:
            if(reopened(e,label)) {
                if(const auto d=planeDifference(map,expected);!d.empty())check(false,label+": saved and reopened map differs: "+d);
                else tally.reloaded+=int(placed.size());
                finished=true;
            }
            return false;
        }
    },900,label);
    std::error_code ec;fs::remove_all(root,ec);
    return finished;
}

int featuresWorkflow(const std::string& data,std::vector<std::string> worlds) {
    if(worlds.empty())worlds=kWorlds;
    for(const auto& world:worlds) {
        Tally tally;
        if(featureSession(data,world,tally))
            check(tally.placed==tally.listed && tally.reloaded==tally.listed,world+": every feature placed and saved");
        tally.print(world+" features");
    }
    if(!failures.empty())return 1;
    std::cout<<"PASS: every feature of "<<worlds.size()<<" worlds places by click, undoes, redoes, erases and survives save/reopen\n";
    return 0;
}

// Every unit type on one 8x8-unit map whose right half is first stamped with the
// world's deepest water section through the terrain browser. Each type goes
// where the game accepts it -- land, then water, then the shoreline -- and
// owners cycle through players 1-8: pick, Place click, undo, redo, Erase
// click, undo; then save, reopen and compare (the .crt round trip).
bool unitSession(const std::string& data,const std::string& world,Tally& tally) {
    const auto root=tempRoot("units");isolateProfile(root);
    tak::hpi::Vfs plain=tak::hpi::mountRetailRoot(fs::u8path(data),tak::hpi::OverridePolicy::None);
    enum {START,WATER,WATERED,SEARCH,PICK,PLACED,UNDONE,REDONE,ERASED,RESTORED,REOPENED} phase=START;
    size_t item=0;int water=-1;BrowserPick pick;tak::tnt::Map expectedMap;
    std::vector<cart::PlacedUnit> expected;std::vector<bool> legal;
    std::vector<SDL_Point> land,sea,shore;size_t usedLand=0,usedSea=0,usedShore=0;
    std::unique_ptr<tak::sim::World> game;int cellX=0,cellZ=0,player=0;
    const std::string label=world+" units";bool finished=false;
    drive({"cartographer","--new","8x8","--world",world,"--data",data,"--out",root},[&](Editor& e,const Input& in) {
        const auto& types=e.unitTypes;const auto& map=e.mapView.map();
        auto fail=[&](const std::string& what) {check(false,label+" '"+types[item]+"': "+what);};
        auto unitPoint=[&](const cart::PlacedUnit& u) {return focus(e,u.x,u.z);};
        auto same=[](const cart::PlacedUnit& a,const cart::PlacedUnit& b) {
            return a.type==b.type && a.player==b.player && a.x==b.x && a.z==b.z && a.footX==b.footX && a.footZ==b.footZ &&
                   a.health==b.health && a.armor==b.armor && a.weapon==b.weapon && a.veteran==b.veteran && a.angle==b.angle;
        };
        auto unitsMatch=[&] {
            if(e.units.size()!=expected.size())return false;
            for(size_t i=0;i<expected.size();++i)if(!same(e.units[i],expected[i]))return false;
            return true;
        };
        for(;;)switch(phase) {
        case START: {
            tally.listed+=int(types.size());expectedMap=map;
            // The deepest water section: the most metres between sea level and its highest cell.
            int best=0;
            for(size_t i=0;i<e.sections.list().size();++i)if(const auto* m=e.sections.load(e.vfs,e.sections.list()[i].path)) {
                if(m->heights.empty())continue;
                const int depth=map.seaLevel-int(*std::max_element(m->heights.begin(),m->heights.end()));
                if(depth>best) {best=depth;water=int(i);}
            }
            if(water<0) {std::cout<<"note: "<<world<<" has no water section; naval units stay on land\n";phase=WATERED;continue;}
            tool(in,Editor::TERRAIN);mode(in,Editor::MODE_PLACE);pick.search(in,water,e.sections.list()[size_t(water)].name);phase=WATER;return true;
        }
        case WATER: {
            const int clicked=pick.click(e,in);
            if(!clicked)return true;
            if(!check(clicked>0,label+": water section listed by the terrain browser"))return false;
            // The whole map in view: every stamp of the right half in one frame.
            const auto* m=e.sections.load(e.vfs,e.sections.list()[size_t(water)].path);
            e.mapView.setZoom(0.125f);e.mapView.setOffset(0,0);
            for(int bz=0;bz+m->blocksY<=map.blocksY;bz+=m->blocksY)for(int bx=map.blocksX/2;bx+m->blocksX<=map.blocksX;bx+=m->blocksX) {
                const auto p=cellScreen(e,(bx+m->blocksX/2)*32+16.f,(bz+m->blocksY/2)*32+16.f);in.click(p.x,p.y);
                cart::stampSection(expectedMap,*m,bx,bz,e.stampLayers);
            }
            phase=WATERED;return true;
        }
        case WATERED: {
            if(water>=0 && !check(planeDifference(map,expectedMap).empty(),label+": water stamped on the right half"))return false;
            // Slots 12 cells apart: land on the left, water on the right, and the shore between.
            for(int z=6;z+8<map.height;z+=12) {
                for(int x=6;x+8<map.width/2;x+=12)land.push_back({x,z});
                for(int x=map.width/2+10;x+8<map.width;x+=12)sea.push_back({x,z});
                shore.push_back({map.width/2-2,z});
            }
            game=engineWorld(e);tool(in,Editor::UNITS);phase=SEARCH;return true;
        }
        case SEARCH: {
            if(item>=types.size()) {saveAndReopen(e,in,label,root);phase=REOPENED;return true;}
            const auto* type=e.unitRegistry.find(lowerText(types[item]));
            const int fx=type?type->footX:1,fz=type?type->footZ:1;
            // The first free slot of the kinds the game accepts this type on.
            bool accepted=false;SDL_Point slot{};
            for(auto* kind:{&land,&sea,&shore}) {
                size_t& used=kind==&land?usedLand:kind==&sea?usedSea:usedShore;
                if(used>=kind->size())continue;
                const auto s=(*kind)[used];
                if(!type || type->canFly || game->canPlace(type,s.x*16.f+fx*8,s.y*16.f+fz*8)) {slot=s;++used;accepted=true;break;}
            }
            if(!accepted) {   // still placed (the editor allows it); Check Map warns
                ++tally.refused;std::cout<<"note: the game accepts "<<types[item]<<" on none of land, deep water or shore ("<<fx<<"x"<<fz<<")\n";
                if(usedLand>=land.size()) {fail("no free land slot");return false;}
                slot=land[usedLand++];
            }
            legal.push_back(accepted);cellX=slot.x;cellZ=slot.y;
            ++tally.kinds[e.unitCategory(int(item))];
            if(type)++tally.kinds["side-"+type->side];
            else {++tally.kinds["no-definition"];std::cout<<"note: "<<types[item]<<" has an .fbi the unit registry does not define (placed 1x1; Check Map reports it)\n";}
            ++tally.kinds["from-"+definitionSource(e.vfs,plain,"units/"+lowerText(types[item])+".fbi","")];
            player=int(item%8);
            for(int p=e.currentPlayer;p!=player;p=(p+1)%8)in.key(SDLK_RIGHTBRACKET);
            pick.search(in,int(item),types[item]);phase=PICK;return true;
        }
        case PICK: {
            const int clicked=pick.click(e,in);
            if(!clicked)return true;
            if(clicked<0) {fail("browser search does not list it");return false;}
            const auto p=focus(e,cellX*16+8.f,cellZ*16+8.f);mode(in,Editor::MODE_PLACE);in.click(p.x,p.y);
            const auto* type=e.unitRegistry.find(lowerText(types[item]));
            cart::PlacedUnit u;u.type=types[item];u.player=player;
            if(type) {u.footX=type->footX;u.footZ=type->footZ;}
            u.x=cellX*16.f+u.footX*8;u.z=cellZ*16.f+u.footZ*8;expected.push_back(u);
            phase=PLACED;return true;
        }
        case PLACED:
            if(!check(e.selectedType==int(item),label+": browser click selects '"+types[item]+"'"))return false;
            if(!unitsMatch()) {fail("click did not place it centred on the clicked cell for player "+std::to_string(player+1));return false;}
            ++tally.placed;in.key(SDLK_z,KMOD_CTRL);phase=UNDONE;return true;
        case UNDONE:
            if(e.units.size()+1!=expected.size()) {fail("undo");return false;}
            ++tally.undone;in.key(SDLK_y,KMOD_CTRL);phase=REDONE;return true;
        case REDONE: {
            if(!unitsMatch()) {fail("redo");return false;}
            ++tally.redone;const auto p=unitPoint(expected.back());mode(in,Editor::MODE_ERASE);in.click(p.x,p.y);phase=ERASED;return true;
        }
        case ERASED:
            if(e.units.size()+1!=expected.size()) {fail("Erase click left it");return false;}
            ++tally.erased;in.key(SDLK_z,KMOD_CTRL);phase=RESTORED;return true;
        case RESTORED:
            if(!unitsMatch()) {fail("undo of the erase");return false;}
            ++tally.restored;++item;phase=SEARCH;continue;
        case REOPENED:
            if(reopened(e,label)) {
                if(!unitsMatch())check(false,label+": saved and reopened units differ");
                else tally.reloaded+=int(expected.size());
                const auto reloaded=engineWorld(e);
                for(size_t i=0;i<expected.size() && i<e.units.size();++i)if(legal[i]) {
                    const auto* type=e.unitRegistry.find(lowerText(e.units[i].type));
                    if(type && !type->canFly && !reloaded->canPlace(type,e.units[i].x,e.units[i].z))
                        check(false,label+" '"+e.units[i].type+"': the game rejects the reopened placement");
                }
                finished=true;
            }
            return false;
        }
    },1200,label);
    std::error_code ec;fs::remove_all(root,ec);
    return finished;
}

int unitsWorkflow(const std::string& data,std::vector<std::string> worlds) {
    if(worlds.empty())worlds=kWorlds;
    for(const auto& world:worlds) {
        Tally tally;
        if(unitSession(data,world,tally))
            check(tally.placed==tally.listed && tally.reloaded==tally.listed,world+": every unit type placed and saved");
        tally.print(world+" units");
    }
    if(!failures.empty())return 1;
    std::cout<<"PASS: every unit type places by click on terrain the game accepts, undoes, redoes, erases and survives save/reopen\n";
    return 0;
}

// Start slots 1-8 and a region on a fresh map: each placed by a canvas click,
// undone, redone, erased and restored; a ninth start is refused with a message;
// then save, reopen and compare (.ota starts, .crt region).
bool slotSession(const std::string& data,const std::string& world,Tally& tally) {
    const auto root=tempRoot("slots");isolateProfile(root);
    enum {START,PLACE,PLACED,UNDONE,REDONE,ERASED,RESTORED,NINTH,REGION,REGION_UNDONE,REGION_REDONE,REGION_ERASED,REGION_RESTORED,REOPENED} phase=START;
    int number=0;std::vector<tak::tnt::StartPos> expected;tak::crt::Region region;
    const std::string label=world+" start slots";bool finished=false;
    drive({"cartographer","--new","8x8","--world",world,"--data",data,"--out",root},[&](Editor& e,const Input& in) {
        const auto& starts=e.scenario.starts;
        auto matches=[&] {
            if(starts.size()!=expected.size())return false;
            for(size_t i=0;i<starts.size();++i)if(starts[i].number!=expected[i].number || starts[i].xpos!=expected[i].xpos || starts[i].zpos!=expected[i].zpos)return false;
            return true;
        };
        auto fail=[&](const std::string& what) {check(false,label+" "+std::to_string(number)+": "+what);};
        auto startPoint=[&](const tak::tnt::StartPos& s) {return focus(e,s.xpos*16+8.f,s.zpos*16+8.f);};
        auto regionPoint=[&](float x,float z) {return cellScreen(e,x*16+8,z*16+8);};
        for(;;)switch(phase) {
        case START: check(starts.empty(),label+": a blank map has no starts");tool(in,Editor::STARTS);mode(in,Editor::MODE_PLACE);phase=PLACE;return true;
        case PLACE: {
            if(++number>8) {   // all slots used: the ninth click is refused
                const auto p=focus(e,200*16+8.f,200*16+8.f);mode(in,Editor::MODE_PLACE);in.click(p.x,p.y);phase=NINTH;return true;
            }
            ++tally.listed;
            expected.push_back({number,20+(number-1)%4*60,30+(number-1)/4*150});
            const auto p=startPoint(expected.back());mode(in,Editor::MODE_PLACE);in.click(p.x,p.y);phase=PLACED;return true;
        }
        case PLACED: if(!matches()) {fail("click did not place StartPos at the clicked cell");return false;}
            ++tally.placed;in.key(SDLK_z,KMOD_CTRL);phase=UNDONE;return true;
        case UNDONE: if(starts.size()+1!=expected.size()) {fail("undo");return false;}
            ++tally.undone;in.key(SDLK_y,KMOD_CTRL);phase=REDONE;return true;
        case REDONE: {
            if(!matches()) {fail("redo");return false;}
            ++tally.redone;const auto p=startPoint(expected.back());mode(in,Editor::MODE_ERASE);in.click(p.x,p.y);phase=ERASED;return true;
        }
        case ERASED: if(starts.size()+1!=expected.size()) {fail("Erase click left it");return false;}
            ++tally.erased;in.key(SDLK_z,KMOD_CTRL);phase=RESTORED;return true;
        case RESTORED: if(!matches()) {fail("undo of the erase");return false;}
            ++tally.restored;phase=PLACE;continue;
        case NINTH: {
            if(!check(matches() && e.modal==Editor::M_MESSAGE,label+": a ninth start is refused with a message (have "+std::to_string(starts.size())+")"))return false;
            ++tally.refused;in.key(SDLK_RETURN);
            // A region: the REGIONS canvas, Place mode, drag a box.
            in.click(700,32);mode(in,Editor::MODE_PLACE);
            e.mapView.setZoom(0.5f);e.mapView.setOffset(0,0);
            region={"Region 1",30,40,49,52};
            const auto a=regionPoint(30,40),b=regionPoint(49,52);in.drag(a.x,a.y,b.x,b.y);phase=REGION;return true;
        }
        case REGION: {
            const auto& rs=e.scen.regions;
            if(!check(rs.size()==1 && rs[0].name==region.name && rs[0].x1==region.x1 && rs[0].z1==region.z1 && rs[0].x2==region.x2 && rs[0].z2==region.z2,
                      label+": region drawn by a Place drag"))return false;
            ++tally.kinds["regions"];in.key(SDLK_z,KMOD_CTRL);phase=REGION_UNDONE;return true;
        }
        case REGION_UNDONE: if(!check(e.scen.regions.empty(),label+": region undo"))return false;
            in.key(SDLK_y,KMOD_CTRL);phase=REGION_REDONE;return true;
        case REGION_REDONE: {
            if(!check(e.scen.regions.size()==1,label+": region redo"))return false;
            e.mapView.setZoom(0.5f);e.mapView.setOffset(0,0);
            const auto p=regionPoint(40,46);mode(in,Editor::MODE_ERASE);in.click(p.x,p.y);phase=REGION_ERASED;return true;
        }
        case REGION_ERASED: if(!check(e.scen.regions.empty(),label+": Erase click removes the region"))return false;
            in.key(SDLK_z,KMOD_CTRL);phase=REGION_RESTORED;return true;
        case REGION_RESTORED:
            if(!check(e.scen.regions.size()==1,label+": region restored"))return false;
            in.key(SDLK_ESCAPE);   // leave the REGIONS canvas
            saveAndReopen(e,in,label,root);phase=REOPENED;return true;
        case REOPENED:
            if(reopened(e,label)) {
                const auto& rs=e.scen.regions;
                if(!matches())check(false,label+": saved and reopened starts differ");else tally.reloaded+=int(expected.size());
                check(rs.size()==1 && rs[0].name==region.name && rs[0].x1==region.x1 && rs[0].z2==region.z2,label+": saved and reopened region differs");
                finished=true;
            }
            return false;
        }
    },300,label);
    std::error_code ec;fs::remove_all(root,ec);
    return finished;
}

int slotsWorkflow(const std::string& data,std::vector<std::string> worlds) {
    if(worlds.empty())worlds=kWorlds;
    for(const auto& world:worlds) {
        Tally tally;
        if(slotSession(data,world,tally))check(tally.placed==8 && tally.reloaded==8 && tally.refused==1,world+": eight start slots and a region");
        tally.print(world+" start slots");
    }
    if(!failures.empty())return 1;
    std::cout<<"PASS: start slots 1-8 and regions place by click, undo, redo, erase and survive save/reopen; a ninth start is refused\n";
    return 0;
}

// ---- rescan: the browser after its library is rescanned ------------------------

// A private install: links to the real install's root archives plus Maps/ with
// one downloaded map that ships its own feature file. Retail mounts every Maps/
// archive globally, so the FEATURES browser offers those features -- until a map
// opened from disk replaces the view and the library is rescanned without them.
std::optional<std::string> packInstall(const std::string& data,const std::string& root) {
    const auto install=fs::path(root)/"install";fs::create_directories(install/"Maps");
    for(const auto& entry:fs::directory_iterator(fs::u8path(data))) {
        if(!entry.is_regular_file())continue;   // root archives are all validInstall needs
        std::error_code ec;fs::create_symlink(fs::absolute(entry.path()),install/entry.path().filename(),ec);
        if(ec)fs::create_hard_link(entry.path(),install/entry.path().filename(),ec);
        if(ec)return std::nullopt;
    }
    cart::FeatureLibrary library;library.scan(tak::hpi::mountRetailRoot(fs::u8path(data)),"aramon");
    if(library.list().empty())return std::nullopt;
    const auto& art=library.list().front();   // borrowed sprite
    std::string tdf;
    for(int i=1;i<=3;++i)tdf+="[CartoTestStone"+std::to_string(i)+"]\r\n{\r\nworld=Aramon;\r\ncategory=rocks;\r\nfootprintx=2;\r\nfootprintz=2;\r\nfilename="+art.filename+";\r\nseqname="+art.seqname+";\r\n}\r\n";
    const auto packed=tak::hpi::pack({{"features/aramon/cartotest.tdf",{tdf.begin(),tdf.end()}}});
    std::ofstream(install/"Maps"/"Carto Test Pack.kmp",std::ios::binary).write(reinterpret_cast<const char*>(packed.data()),std::streamsize(packed.size()));
    return install.string();
}

int rescanWorkflow(const std::string& data) {
    const auto root=tempRoot("rescan");isolateProfile(root);
    const auto install=packInstall(data,root);
    if(!install) {std::cout<<"SKIP: cannot link the install's archives\n";return 77;}
    int stage=0;size_t listed=0,cell=0;const std::string label="rescan";
    auto expect=[&](bool value,const std::string& what){return check(value,label+": "+what);};
    drive({"cartographer","--new","8x8","--world","aramon","--data",*install,"--out",root},[&](Editor& e,const Input& in) {
        const auto& map=e.mapView.map();
        switch(stage++) {
        case 0:   // the downloaded map's features are listed; place one
            listed=e.features.list().size();
            if(!expect(e.features.byName("cartoteststone1")!=nullptr,"a Maps/ archive's feature is offered"))return false;
            tool(in,Editor::FEATURES);in.key(SDLK_f,KMOD_CTRL);in.text("cartoteststone3");in.key(SDLK_ESCAPE);return true;
        case 1: {
            const auto& list=e.features.list();
            const int index=int(std::find_if(list.begin(),list.end(),[](const auto& f){return f.name=="cartoteststone3";})-list.begin());
            const auto row=std::find(e.paletteItems.begin(),e.paletteItems.end(),index);
            const auto point=row==e.paletteItems.end()?std::nullopt:paletteRow(e,int(row-e.paletteItems.begin()));
            if(!expect(point.has_value(),"the search lists that feature"))return false;
            in.click(point->x,point->y);
            const auto p=focus(e,40*16+8.f,40*16+8.f);cell=40*size_t(map.width)+40;mode(in,Editor::MODE_PLACE);in.click(p.x,p.y);
            in.key(SDLK_f,KMOD_CTRL);in.key(SDLK_BACKSPACE);in.key(SDLK_ESCAPE);   // every feature listed again
            return true;
        }
        case 2:
            expect(map.features[cell]<map.featureNames.size() && map.featureNames[map.features[cell]]=="cartoteststone3","placed by click");
            expect(e.paletteItems.size()==listed,"the cleared search lists every feature");
            saveAndReopen(e,in,label,root);return true;
        case 3:   // opened from disk: the library is rescanned without the Maps/ archive
            if(!reopened(e,label))return false;
            expect(e.features.list().size()+3==listed,"the reopened map's browser no longer offers the archive's features");
            expect(map.features[cell]<map.featureNames.size() && map.featureNames[map.features[cell]]=="cartoteststone3","the placed name survives the round trip");
            // An undefined name stays selectable and deletable (drawn as a red marker).
            {const auto p=focus(e,40*16+8.f,40*16+8.f);mode(in,Editor::MODE_SELECT);in.click(p.x,p.y);in.key(SDLK_DELETE);}
            return true;
        case 4:
            expect(map.features[cell]==0xFFFF,"an undefined feature can be selected and deleted");
            return false;
        default: return false;
        }
    },120,label);
    std::error_code ec;fs::remove_all(root,ec);
    if(!failures.empty())return 1;
    std::cout<<"PASS: browser rebuilt after a library rescan (Maps/ archive features), undefined names stay selectable\n";
    return 0;
}

} // namespace

int main(int argc,char** argv) {
    if(argc<3)return 2;
    const std::string data=argv[1],mode=argv[2];
    if(data.empty() || !tak::hpi::validInstall(fs::u8path(data),nullptr)) {std::cout<<"SKIP: no game data at '"<<data<<"'\n";return 77;}
    std::vector<std::string> rest(argv+3,argv+argc);
    if(mode=="starts")return startsWorkflow(data,rest);
    if(mode=="terrain")return terrainWorkflow(data,rest);
    if(mode=="features")return featuresWorkflow(data,rest);
    if(mode=="units")return unitsWorkflow(data,rest);
    if(mode=="slots")return slotsWorkflow(data,rest);
    if(mode=="rescan")return rescanWorkflow(data);
    return 2;
}
