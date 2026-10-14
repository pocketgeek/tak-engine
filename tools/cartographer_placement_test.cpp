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
// Without data every mode exits 77 (skipped).

#include "cartographer/editor.h"
#include "cartographer/editorstate.h"
#include "hpi/hpi.h"
#include "tnt/ota.h"

#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace fs=std::filesystem;
using Editor=cart::Editor;

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
SDL_Point cellScreen(const Editor& e,float wx,float wz) {
    return {Editor::kPaletteW+int(std::lround((wx-e.mapView.offX())*e.mapView.zoom())),Editor::kMenuH+int(std::lround((wz-e.mapView.offY())*e.mapView.zoom()))};
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
    for(const auto& variants:e.paletteVariants)for(const int i:variants)if(i<0 || size_t(i)>=e.sections.list().size())
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

} // namespace

int main(int argc,char** argv) {
    if(argc<3)return 2;
    const std::string data=argv[1],mode=argv[2];
    if(data.empty() || !tak::hpi::validInstall(fs::u8path(data),nullptr)) {std::cout<<"SKIP: no game data at '"<<data<<"'\n";return 77;}
    std::vector<std::string> rest(argv+3,argv+argc);
    if(mode=="starts")return startsWorkflow(data,rest);
    return 2;
}
