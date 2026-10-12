#pragma once
// Shared state of a running Cartographer (see editor.h for the entry point).
// The editor used to be one function whose locals were captured by lambdas;
// those locals are now members, so each concern lives in its own file:
//   main.cpp      startup (Session ctor), Editor setup, the frame loop
//   session.cpp   save / open / undo restore / recovery / Test Map
//   headless.cpp  command line, --stamp/--save/--bundle, --shot capture
//   palette.cpp   left browser: search, categories, thumbnails grid
//   canvas.cpp    map canvas: stamping, units/features/starts/regions editing
//   chrome.cpp    menus, toolbar, keyboard shortcuts, status bar, minimap, hints
//   dialogs.cpp   modal dialogs, regions/use-only/placed-unit lists, async jobs
//   scripting.cpp the trigger-rule overlay
//   symmetrytools.cpp Symmetry menu, Symmetrize, live mirroring, terrain clipboard
// Declaration order is the old local order, so destruction order is unchanged.

#include "cartographer/editor.h"
#include "cartographer/editorfont.h"
#include "cartographer/featureselection.h"
#include "cartographer/features.h"
#include "cartographer/history.h"
#include "cartographer/newmap.h"
#include "cartographer/overlay.h"
#include "cartographer/playtest.h"
#include "cartographer/preferences.h"
#include "cartographer/recovery.h"
#include "cartographer/regions.h"
#include "cartographer/ruleedit.h"
#include "cartographer/sections.h"
#include "cartographer/selection.h"
#include "cartographer/symmetry.h"
#include "cartographer/textedit.h"
#include "cartographer/thumbnails.h"
#include "cartographer/units.h"
#include "cartographer/validation.h"
#include "client/mapview.h"
#include "client/modelview.h"
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "tnt/mapgen.h"
#include "tnt/ota.h"

#include <SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace cart {

// UI magnification. The whole editor is drawn in a fixed logical coordinate
// space and scaled up by this factor via SDL_RenderSetScale, so every panel,
// glyph, and dialog doubles uniformly on hi-DPI displays. Input coordinates are
// divided back down to logical space at the event source.
extern float kUIScale;

inline std::string pathText(const std::filesystem::path& path) {const auto text=path.u8string();return {text.begin(),text.end()};}
inline std::string lowerText(std::string value) {std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return char(std::tolower(c));});return value;}
inline void fillRect(SDL_Renderer* r, int x, int y, int w, int h, Uint8 cr, Uint8 cg, Uint8 cb) {
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_Rect rc{x, y, w, h};
    SDL_RenderFillRect(r, &rc);
}

// A fresh map for the New dialog / --new launch: either a flat ground stamp or
// procedural terrain from the engine's map generator (the "~gen1~" generator the
// lobby uses -- coastlines, relief, trees/rocks/mana, and start positions).
struct FreshMap {
    tak::tnt::Map map;
    std::string recipe;
    std::vector<tak::tnt::StartPos> starts;   // only for random terrain
};
FreshMap buildFreshMap(const tak::hpi::Vfs& vfs, cart::SectionLibrary& sections,
                       tak::terrain::Compositor& comp, const std::string& world,
                       int wUnits, int hUnits, bool random, const tak::mapgen::Params* requested=nullptr);

// The open document and the app services it needs, built by the constructor
// (main.cpp). Usage errors, startup failures and headless one-shots end the
// run by throwing Exit before any interactive state exists.
struct Session {
    using FrameHook=std::function<void(SDL_Window*,SDL_Renderer*,int)>;
    struct Exit {int code;};
    Session(int argc,char** argv,const FrameHook& frameHook,const EditorHooks& hooks);

    // Command line (headless.cpp).
    std::string dataRoot, mapName, outDir = ".", exportPath, bundlePath, stampName, newWorld = "aramon", shotPath;
    int stampBX = 0, stampBY = 0, newW = 0, newH = 0;
    bool explicitOutput=false;
    bool randomTerrain = false;   // --random: generate procedural terrain for --new
    void parseArguments(int argc,char** argv);
    std::optional<int> runHeadless();   // --stamp / --save / --bundle exit code

    const FrameHook& frameHook;
    const EditorHooks& hooks;
    Uint64 recoveryClock() const;
    struct SdlLifetime { bool active=false; ~SdlLifetime() { if(active) SDL_Quit(); } } sdlLifetime;
    std::filesystem::path preferencesFolder;
    EditorPreferences preferences;
    SDL_Window* win=nullptr;
    std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> windowOwner{nullptr,SDL_DestroyWindow};
    SDL_Renderer* ren=nullptr;
    // Declared before MapView: its worker and textures must die before SDL's
    // renderer, including on export/error returns.
    std::unique_ptr<SDL_Renderer,decltype(&SDL_DestroyRenderer)> rendererOwner{nullptr,SDL_DestroyRenderer};
    std::optional<EditorFont> editorFont;
    void persistPreferences();

    // Mount the retail install exactly like the engine (loose + *.hpi, retail
    // precedence). The Vfs must outlive the MapView (it borrows it by ref).
    tak::hpi::Vfs vfs;
    bool interactive=false;
    std::filesystem::path recoveryFolder, recoveredFrom;
    std::string recoveredMapPath;
    std::unique_ptr<RecoveryFile> recoverySession,recoveredSession;
    std::string recoveryStatus;

    bool showWelcome=false;
    std::string mapPath;
    tak::tnt::Scenario scenario;
    std::unique_ptr<MapView> mapViewPtr;
    // The map's scenario .crt, kept in full so a save preserves the trigger
    // rules, regions, and custom types the unit tool doesn't edit. `units` is
    // the editor's working view (map pixels); `scen` is everything else.
    tak::crt::Scenario scen;
    std::vector<PlacedUnit> units;
    bool unitsEdited = false;
    // Use-only restriction: the allowed unit types (UPPERCASE). Loaded from the
    // sibling <map>.tdf when the OTA references one; empty = unrestricted.
    std::set<std::string> useOnly;
    // Set when the terrain is painted, so Save regenerates the minimaps (an
    // unedited save stays byte-identical to the source; an edited one gets a
    // fresh overview reflecting the paint).
    bool edited = false;
    bool minimapRefreshNeeded=true;
    std::shared_ptr<std::atomic_bool> minimapCancel=std::make_shared<std::atomic_bool>(false);
    uint64_t minimapRevision=0;
    bool minimapPreviewPending=false;
    Uint64 minimapDue=0;
    void invalidateMinimap();
    // Unsaved-changes flag for the exit prompt. Set by every edit (terrain,
    // features, units, starts, scenario props, use-only, triggers), cleared on a
    // successful save. Broader than `edited` (which only gates minimap regen).
    bool dirty = false;
    bool historyPending=false, resetHistory=false;
    bool overlayInvalidated=false;
    History history;
    HistoryState historySnapshot();
    void commitHistory();
    bool wantNew = false;   // deferred "open New Map" after a discard confirmation
    std::string saveError;
    bool saveDocument(const std::string& destination,bool bundle);
    bool saveMap(const std::string& path);
    bool saveBundle(const std::string& path);

    // Section-prefab palette for this map's world (falls back to aramon).
    SectionLibrary sections;
    std::string world;
    FeatureLibrary features;
    std::vector<std::string> unitTypes;
    tak::sim::TypeRegistry unitRegistry;
    // Stamp a named section into the map at block (bx,by) (canvas.cpp).
    bool stampByName(const std::string& name, int bx, int by);
};

// The interactive editor: everything the frame loop reads and writes.
struct Editor : Session {
    Editor(int argc,char** argv,const FrameHook& frameHook,const EditorHooks& hooks);
    int run();   // main.cpp: the frame loop, returns the exit code

    static constexpr int kMenuH = 44;    // top menu-bar strip
    static constexpr int kStatusH = 22;  // bottom status strip
    static constexpr int kPaletteW = 200;   // left section-palette panel
    static constexpr int kPaletteTop=kMenuH+76;
    static constexpr int kCellH=106;
    static constexpr int kThumb = 88;       // section thumbnail cell (px)
    static constexpr int cols = std::max(1, (kPaletteW - 8) / (kThumb + 4));
    MapView& mapView=*mapViewPtr;

    // Frame layout, recomputed at the top of every frame (logical pixels).
    int w=0,h=0,canvasW=0,canvasH=0;
    SDL_Rect miniRect{};
    bool running = true;

    // --- Palette (palette.cpp) ------------------------------------------------
    int selected = sections.list().empty() ? -1 : 0;   // section index (TERRAIN)
    int selectedFeat = features.list().empty() ? -1 : 0; // feature index (FEATURES)
    int paletteScroll = 0;
    bool& showGrid=preferences.showGrid;
    bool& showFeatures=preferences.showFeatures;
    bool& showUnits=preferences.showUnits;
    bool& showStarts=preferences.showStarts;
    StampLayers stampLayers;
    // Feature-sprite textures (GAF frame 0), cached by feature name, used both in
    // the palette and to draw placed features on the canvas.
    std::map<std::string, SDL_Texture*> featTex;
    const FeatSprite* featureSpriteFor(const FeatureRef& ref);
    SDL_Texture* featTextureFor(const FeatureRef& r);
    Thumbnails thumbs{ren,vfs};
    Thumbnails unitPortraits{ren,vfs,64};
    SDL_Texture* thumbFor(const std::string& path);

    // --- Canvas tools (canvas.cpp) --------------------------------------------
    int lastStampX=-1,lastStampY=-1;
    void stampAtMouse(int mx, int my, int w, int h);
    enum Tool { TERRAIN, FEATURES, UNITS, STARTS };
    int selectedType = unitTypes.empty() ? -1 : 0;   // unit-type index (UNITS palette)
    int currentPlayer = 0;                            // player id new units get
    int draggingUnit = -1;                            // index into units while dragging
    int unitAt(int mx, int my);   // placed unit whose marker is near (mx,my), or -1
    Tool tool = TERRAIN;
    enum EditMode { MODE_PLACE,MODE_SELECT,MODE_ERASE,MODE_PAN };
    EditMode editMode=MODE_PLACE;
    bool toolVisible();
    void revealTool();
    UnitSelection selectedUnits;
    FeatureSelection selectedFeatures;
    bool selectionBox=false,selectionAppend=false;
    int selectionX0=0,selectionZ0=0,selectionX1=0,selectionZ1=0;

    // --- Palette browser state (palette.cpp) ----------------------------------
    std::string paletteSearch;
    TextEdit paletteEditor;
    bool paletteFocus=false;
    int categoryIndex=0,factionIndex=0;
    std::vector<int> paletteItems;
    std::vector<std::string> paletteCategories{"ALL"};
    std::string paletteKey;
    const tak::sim::UnitType* unitInfo(int index);
    std::string unitCategory(int index);
    std::string paletteLabel(int index);
    void refreshPalette();
    int paletteRowAt(int x,int y) const;   // browser row under (x,y), unclamped
    std::string paletteHint(int y) const;
    bool handlePaletteFocus(SDL_Event& e);
    bool handlePaletteClick(SDL_Event& e);
    bool handlePaletteWheel(SDL_Event& e);
    void drawPalette();

    // --- Canvas editing (canvas.cpp) ------------------------------------------
    void placeFeature(int mx, int my, bool erase);   // feature plane at the mouse cell
    int draggingStart = -1;           // index into scenario.starts while dragging
    bool clearArm = false, clearDrag = false;   // Clear Area drag-box (screen px)
    int clx0 = 0, cly0 = 0, clx1 = 0, cly1 = 0;
    bool mouseCell(int mx, int my, int& cx, int& cz);   // canvas mouse -> 16px cell
    int startAt(int mx, int my);   // start marker near (mx,my), or -1
    bool handleRegionDrag(SDL_Event& e);
    bool handleCanvasKeys(SDL_Event& e);
    void handleCanvasInput(SDL_Event& e,float pointerDX,float pointerDZ);
    void drawCanvas();
    void drawClearArea();

    // --- Minimap (chrome.cpp) -------------------------------------------------
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> minimapTexture{nullptr,SDL_DestroyTexture};
    const uint8_t* minimapSource=nullptr;
    bool minimapDrag=false;
    void refreshMinimap();
    std::future<MinimapPreview> minimapJob;
    uint64_t minimapJobRevision=0;
    std::string minimapError;
    void updateLiveMinimap();
    bool handleMinimapInput(SDL_Event& e);
    void drawMinimap();

    // --- Modal dialogs (dialogs.cpp) ------------------------------------------
    enum Modal { M_NONE, M_SCENARIO, M_RESIZE, M_UNIT, M_MESSAGE, M_RULE, M_RULENAME, M_CONFIRM, M_NEW, M_QUITSAVE, M_SAVEAS, M_OPENPATH, M_REGION, M_GENERATOR, M_GENERATING, M_GENERATED, M_ANALYZING, M_CHECKING, M_ISSUES, M_MODEL_LOADING, M_MODEL, M_OBJECTS, M_SYMMETRIZE };
    static constexpr int kMaxFields = 11;
    std::function<void()> confirmAction;   // M_CONFIRM: run on OK
    Modal modal = M_NONE;
    std::string mfOriginal[kMaxFields];
    std::string mf[kMaxFields];              // field buffers
    bool mfNumeric[kMaxFields] = {};
    // Choice lists are read-only unless explicitly editable (flag suggestions).
    const std::vector<std::string>* mfChoices[kMaxFields] = {};
    bool mfEditableChoice[kMaxFields] = {};
    int scrPlayer = 0;                     // 0=All Players, 1..8=Player 1..8
    int mDropScroll=0;
    std::vector<std::string> ruleChoices[kMaxFields];
    int mDropOpen = -1;                       // which field's dropdown list is open (-1 none)
    std::vector<SDL_Rect> mDropRects;         // hit rects of the open list's rows
    SDL_Rect mRandom{};                       // New Map: the RANDOM button rect
    SDL_Rect mRegenerate{};
    // Dropdown option lists (New Map): map sizes in units, and the four worlds.
    const std::vector<std::string> kSizeOpts = {"8", "16", "24", "32", "48", "64"};
    const std::vector<std::string> kWorldOpts = {"aramon", "veruna", "taros", "zhon", "creon"};
    tak::mapgen::Params generatorParams;
    bool regeneratingCurrent=false;
    std::array<std::string,4> generatorDraft;
    std::vector<std::string> generatorLayouts;
    const std::vector<std::string> oldGeneratorLayouts={tak::mapgen::layoutName(0),tak::mapgen::layoutName(1),tak::mapgen::layoutName(2)};
    const std::vector<std::string> legacyGeneratorLayouts={tak::mapgen::layoutName(0)};
    const char* mLabel[kMaxFields] = {};
    std::string mTitle,modalError;
    int mN = 0;                              // active field count
    int mfocus = 0;
    TextEdit fieldEditor;
    int descriptionScroll=0;bool descriptionFollowCaret=true;
    std::vector<std::pair<std::string,std::string>> openCatalog,openMatches;
    SDL_Rect openList{};int openScroll=0;std::string openFilterPrevious;bool openFilterDirty=true;
    void filterOpenMaps();
    bool regionsOpen=false;
    bool& showRegions=preferences.showRegions;
    bool regionCanvas=false;
    RegionDrag regionDrag;
    int regionSelected=-1,regionScroll=0,editRegion=-1;
    SDL_Rect regionList{},regionNew{},regionEdit{},regionDelete{},regionDone{},regionOnMap{};
    int editUnit = -1;                       // UNITS: index being edited (M_UNIT)
    bool editRuleAction=false;
    tak::crt::RuleGroup* editRuleGroup=nullptr;
    tak::crt::Rule* editRule = nullptr;      // M_RULE: rule whose params are edited
    std::vector<std::string> mMsg;           // M_MESSAGE: wrapped text lines
    SDL_Rect mBox[kMaxFields]{}, mOK{}, mCancel{}, mQuit{};   // render-computed hit rects
    void openMessage(const std::string& title, const std::string& text);
    void openConfirm(const std::string& title, const std::string& text,std::function<void()> action);
    void openModal(Modal m, int unitIdx = -1);
    std::future<FreshMap> generatorJob;
    std::optional<FreshMap> generatedPreview;
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> generatedTexture{nullptr,SDL_DestroyTexture};
    std::string generatingName,generatingWorld;
    bool cancelGeneration=false,quitAfterGeneration=false;
    void adoptFreshMap(FreshMap fm,const std::string& nm,const std::string& wld);
    void applyNewMap(bool random);
    void openGeneratorRecipe();
    void applyModal();
    void openRuleEditor(tak::crt::Rule* r, bool isAction);
    void openRuleName(tak::crt::RuleGroup* group);

    // --- Use Only restriction list, placed units, model, overlay, Check Map ---
    bool useOnlyOpen = false;
    int useOnlyScroll = 0;
    SDL_Rect uoList{}, uoDone{}, uoClear{};   // render-computed hit rects
    std::string objectSearch;
    TextEdit objectEditor;
    std::vector<int> objectMatches;
    int objectScroll=0,objectRow=0;
    SDL_Rect objectList{},objectSearchBox{},objectClose{};
    void filterObjects();
    void openObjects();
    void chooseObject(int row,bool edit);
    std::future<ModelView::Asset> modelJob;
    std::shared_ptr<std::atomic_bool> modelCancel;
    std::unique_ptr<ModelView> modelPreview;
    std::string modelTitle;
    std::string modelWarning;
    SDL_Rect modelArea{},modelClose{};
    Uint64 modelTime=0;
    bool quitAfterModel=false;
    void previewModel();
    std::future<TerrainOverlay> overlayJob;
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> overlayTexture{nullptr,SDL_DestroyTexture};
    std::string overlayLegend;
    int overlayWidth=0,overlayHeight=0;
    bool discardOverlay=false,quitAfterOverlay=false;
    std::shared_ptr<std::atomic_bool> overlayCancel;
    void buildOverlay(OverlayKind kind);
    std::vector<MapIssue> mapIssues;
    std::future<std::vector<MapIssue>> checkJob;
    bool discardCheck=false,quitAfterCheck=false;
    uint64_t mapIssueRevision=0,checkRevision=0;
    int issueScroll=0,issueContentHeight=0;
    SDL_Rect issueList{},issueRecheck{},issueClose{};
    std::vector<SDL_Rect> issueRows;
    size_t issueIndex=0;
    void showMapIssue();
    void checkMap();
    void pollJobs();                          // collect finished background jobs
    bool swallowWhileBusy(const SDL_Event& e); // input while a job's busy modal is up
    bool handleGeneratedPreview(SDL_Event& e);
    void handleRegionsList(SDL_Event& e);
    void handleUseOnly(SDL_Event& e);
    void handleModal(SDL_Event& e);
    void drawRegionsList();
    void drawModal();
    void drawUseOnly();

    // --- Scenario Scripting (per-player trigger rules) overlay (scripting.cpp) -
    bool scriptOpen = false;
    int scrGroup = -1;                     // selected rule-group in this player
    int scrCondSel = -1, scrActSel = -1;   // selected condition / action row
    int scrRuleScroll = 0, scrCondScroll = 0, scrActScroll = 0;
    int scrDetailScroll=0,scrDetailMax=0;
    std::string scrDetailText;
    SDL_Rect rRuleDetail{},rCopyRuleText{};
    bool pickOpen = false, pickAction = false, pickTemplate=false;
    std::vector<RuleTemplate> templateCatalog;
    std::string templateQuery;
    TextEdit templateEditor;
    int templateSelection=0;
    SDL_Rect rTemplates{},rTemplateSearch{};
    int pickScroll = 0;
    SDL_Rect rRuleList{}, rCondList{}, rActList{}, rPickList{};   // render-computed
    SDL_Rect rPrevP{}, rNextP{}, rAddRule{}, rDelRule{}, rAddCond{}, rDelCond{},
             rAddAct{}, rDelAct{}, rNameRule{}, rScrDone{}, rPickCancel{};
    RuleColumn ruleColumn=RuleColumn::Group;
    RuleClipboard ruleClipboard;
    SDL_Rect ruleCopy{},rulePaste{},ruleDuplicate{},ruleUp{},ruleDown{},playerCopy{},playerPaste{};
    std::vector<tak::crt::RuleGroup>& scrGroups();
    tak::crt::RuleGroup* curGroup();
    void ruleOperation(int operation);
    void openScripting();
    void openTemplates();
    void insertTemplate();
    void handleScripting(SDL_Event& e);
    void drawScripting();

    // --- Document commands (session.cpp) --------------------------------------
    bool openDocument(const std::string& request);
    Playtest playtest;
    void testMap();

    // --- Menus, toolbar, shortcuts, status, hints (chrome.cpp) ----------------
    int menuOpen=-1;
    std::string hoveredHint;Uint64 hintSince=0;
    struct ViewBookmark {float x,z,zoom;};
    std::optional<ViewBookmark> viewBookmark;
    const std::vector<std::string> menuNames={"FILE","EDIT","VIEW","SCENARIO","SYMMETRY","HELP"};
    const std::vector<std::vector<std::string>> menuRows={
        {"New map (Ctrl+N)","Open map (Ctrl+O)","Save (Ctrl+S)","Save As (Ctrl+Shift+S)","Export loose files","Test Map (F5)","Exit"},
        {"Undo (Ctrl+Z)","Redo (Ctrl+Y)","Clear area (K)","Terrain brush: protect objects","Copy terrain selection (Ctrl+C)","Paste terrain clipboard (Ctrl+V)"},
        {"Fit map","100% terrain zoom","Toggle grid (G)","Toggle regions","Frame selection","Store view bookmark","Restore view bookmark","Overlay: movement","Overlay: buildability","Overlay: water depth","Overlay: slopes","Hide terrain overlay","Smaller UI","Larger UI","Show features","Show units","Show starts","Unit model preview (F6)"},
        {"Properties (P)","Resize (R)","Use Only units (U)","Check map (C)","Scripting (T)","Next issue","Regions","Validation results","Regenerate from recipe","Log Test Map triggers","Placed units"},
        {"Live symmetry (M)","Symmetrize map (Shift+M)","Axis: vertical (left | right)","Axis: horizontal (top / bottom)","Axis: diagonal \\ (square maps)",
         "Axis: diagonal / (square maps)","Rotate 2-way (180)","Rotate 4-way (90, square maps)","Mirror 4-way (both axes)","Locate unmirrored terrain"},
        {"Editor controls","About"}};
    void menuAction(int menu,int row);
    bool handleMenuInput(SDL_Event& e);
    bool handleToolbarModes(SDL_Event& e);
    bool handleShortcuts(SDL_Event& e);
    bool handleToolbarTools(SDL_Event& e);
    void drawChrome();
    void drawHints();

    // --- Recovery and undo (session.cpp) --------------------------------------
    bool recoveryFilesPresent=!recoveredFrom.empty();
    std::future<std::string> recoveryJob;
    Uint64 nextRecovery=0;
    void collectRecovery();
    void clearRecovery();
    void autosaveRecovery();
    void restoreHistory(const HistoryState* state);

    // --- Symmetry and the terrain clipboard (symmetrytools.cpp) ---------------
    cart::Mirror symmetryKind=cart::Mirror::Vertical;
    bool liveSymmetry=false;
    std::string symmetryStatus;Uint64 symmetryStatusAt=0;
    std::vector<MapIssue> symmetryIssues;      // unmirrored terrain from the last Symmetrize
    std::vector<std::string> symmetryKindOpts,symmetrySourceOpts;
    const std::vector<std::string> symmetryTerrainOpts={"MIRROR SECTION TWINS","LEAVE TERRAIN"};
    cart::SectionMatcher sectionMatcher;
    const cart::SectionMatcher& symmetrySections();   // built lazily per world
    cart::FootprintLookup featureFootprint();
    bool symmetryFits(cart::Mirror) const;            // square-only kinds on square maps
    bool symmetrySelection() const;                   // a selection can be the source
    void setSymmetryStatus(std::string text);
    std::string symmetryStatusText() const;
    std::string symmetryMenuLabel(size_t row) const;
    void symmetryMenu(int row);
    void toggleLiveSymmetry();
    void setupSymmetrizeFields();                     // openModal(M_SYMMETRIZE)
    void refreshSymmetryDialog();
    void applySymmetrize();
    // Snapshot of the edited layer before a canvas handler; the destructor
    // mirrors whatever the handler changed (live symmetry).
    struct LiveEdit {
        Editor& editor;bool active=false;
        std::vector<PlacedUnit> units;std::vector<tak::tnt::StartPos> starts;
        std::vector<uint16_t> features;std::vector<std::string> featureNames;
        LiveEdit(Editor& owner,const SDL_Event& e,bool keyboard);
        ~LiveEdit();
    };
    void stampMirrors(int section,int x,int z);
    std::optional<cart::Box> terrainSelection;
    bool terrainSelecting=false;int terrainSelX=0,terrainSelZ=0;
    cart::TerrainClip terrainClip;
    bool clipBrush=false;int clipBrushSection=-1;
    std::string clipWorldAccepted;
    std::unique_ptr<SDL_Texture,decltype(&SDL_DestroyTexture)> clipTexture{nullptr,SDL_DestroyTexture};
    bool clipboardBrush() const;
    cart::Box clipBrushBox(float wx,float wz) const;
    bool handleTerrainSelection(SDL_Event& e);
    bool handleTerrainKeys(SDL_Event& e);
    void copyTerrainSelection();
    void useTerrainClipboard();
    void pasteTerrainAtMouse(int mx,int my);
    void pasteTerrainAt(int bx,int bz);
    void drawSymmetryOverlay();

    // --- --shot (headless.cpp) ------------------------------------------------
    void prepareShot();
    void captureShot();
};

} // namespace cart
