#include "client/settings.h"

#include <SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

namespace tak {

namespace {

// A flat "key = value" file, one per line, '#' comments. Unknown keys are skipped
// on read (forward-compat) and current values re-emitted on write.
template <typename T>
T clampv(T v, T lo, T hi) { return std::max(lo, std::min(hi, v)); }

}  // namespace

std::string settingsPath() {
    char* base = SDL_GetPrefPath("TAKengine", "TAKingdoms");
    if (!base) return {};
    std::string p = std::string(base) + "settings.ini";
    SDL_free(base);
    return p;
}

Settings loadSettings() {
    Settings s;   // defaults
    std::string path = settingsPath();
    if (path.empty()) return s;
    std::ifstream in(path);
    if (!in) return s;

    // Older filtering keys, used only when bilinearFilter is absent (in any line
    // order): 0.7.24's bilinear toggle, and the unreleased Zoom Smoothing choice whose
    // Smooth mode was this filter. Its Sharp and Off, and the AA keys
    // (antiAlias/terrainAA/modelAA/unitEdgeAA/zoomedOutTerrain), have no successor;
    // like any unknown key they are skipped and vanish on the next save.
    int legacyBilinear=-1,legacyZoom=-1;
    bool bilinearSeen=false;
    std::string line;
    while (std::getline(in, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq), val = line.substr(eq + 1);
        auto trim = [](std::string& t) {
            size_t a = t.find_first_not_of(" \t\r\n");
            size_t b = t.find_last_not_of(" \t\r\n");
            t = (a == std::string::npos) ? std::string() : t.substr(a, b - a + 1);
        };
        trim(key); trim(val);
        if (key.empty()) continue;

        auto asInt   = [&](int lo, int hi) { return clampv(std::atoi(val.c_str()), lo, hi); };
        auto asFloat = [&](float lo, float hi) { return clampv(float(std::atof(val.c_str())), lo, hi); };
        auto asBool  = [&] { return val == "1" || val == "true" || val == "on"; };

        if(key=="overrides.hostPack" || key=="overrides.cosmeticPack") {
            std::string name;bool valid=val.size()%2==0 && val.size()<=256;
            auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
            for(size_t i=0;valid && i<val.size();i+=2){int a=digit(val[i]),b=digit(val[i+1]);if(a<0 || b<0)valid=false;else name+=char(a*16+b);}
            auto& names=key=="overrides.hostPack"?s.hostOverridePacks:s.cosmeticOverridePacks;
            if(valid && !name.empty() && name.find_first_of("/\\\r\n") == name.npos && name.find('\0')==name.npos && names.size()<64 && std::find(names.begin(),names.end(),name)==names.end())names.push_back(name);
            continue;
        }
        if      (key == "fullscreen")      s.fullscreen = asBool();
        else if (key == "vsync")           s.vsync = asBool();
        else if (key == "maxFps")          s.maxFps = asInt(30, 480);
        else if (key == "scorecardScale")  s.scorecardScale = asFloat(0.75f, 2.0f);
        else if (key == "uiScale")         s.uiScale = asFloat(0.75f, 2.0f);
        else if (key == "bilinearFilter")  { s.bilinear = asBool(); bilinearSeen = true; }
        else if (key == "bilinear")        legacyBilinear = asBool();
        else if (key == "zoomSmoothing")   legacyZoom = val == "smooth" || val == "1";
        else if (key == "buildBarAlign")   s.buildBarAlign = asInt(0, 2);
        // 4.0, matching the slider and GameView. This still said 2.0 when the slider
        // went to 400%, so 300-400% survived until the next restart and then silently
        // snapped back to 200%.
        else if (key == "buildBarScale")   s.buildBarScale = asFloat(0.75f, 4.0f);
        else if (key == "treeSway")        s.treeSway = asBool();
        else if (key == "tacticalDots")    s.tacticalDots = asBool();
        else if (key == "tacticalDotsZoom") s.tacticalDotsZoom = asInt(0, 100);
        else if (key == "unitShadows")     s.unitShadows = asBool();
        else if (key == "smoothArt")       s.smoothArt = asBool();
        else if (key == "videoDeblock")    s.videoDeblock = asBool();
        else if (key == "healthBars")      s.healthBars = asInt(0, 2);
        else if (key == "statsPanel")      s.statsPanel = asBool();
        else if (key == "masterVol")       s.masterVol = asInt(0, 256);
        else if (key == "bgmVol")          s.bgmVol = asInt(0, 256);
        else if (key == "sfxVol")          s.sfxVol = asInt(0, 256);
        else if (key == "mouseZoomSpeed")  s.mouseZoomSpeed = asFloat(0.25f, 4.0f);
        else if (key == "edgeScrollSpeed") s.edgeScrollSpeed = asFloat(0.25f, 4.0f);
        else if (key == "edgeScroll")      s.edgeScroll = asBool();
        else if (key == "cursorScale")     s.cursorScale = asInt(1, 8);
        else if (key == "hardwareCursor")  s.hardwareCursor = asBool();
        else if (key == "smoothMotion")    s.smoothMotion = asBool();
        else if (key == "playerName")      s.playerName = val;
        else if (key == "accountName")     s.accountName = val;
        else if (key == "audioDevice")     s.audioDevice = val;
        else if (key == "gameCreate.crusades") s.gameCreate.crusades = asBool();
        else if (key == "gameCreate.pathfindingMode")
            // Exactly one stable mode byte; anything else falls back to Retail.
            s.gameCreate.pathfindingMode = val.size() == 1 && val[0] >= '0' &&
                sim::validPathfindingMode(uint8_t(val[0] - '0'))
                ? sim::PathfindingMode(val[0] - '0') : sim::PathfindingMode::Retail;
        else if (key == "gameCreate.doubleSight") s.gameCreate.doubleSight = asBool();
        else if (key == "gameCreate.speedUnlock") s.gameCreate.speedUnlock = asBool();
        else if (key == "gameCreate.monarchExpendable") s.gameCreate.monarchExpendable = asBool();
        else if (key == "gameCreate.randomStarts") s.gameCreate.randomStarts = asBool();
        else if (key == "gameCreate.generated") s.gameCreate.generated = asBool();
        else if (key == "gameCreate.unitCap") s.gameCreate.unitCap = asInt(250, 2000);
        else if (key == "gameCreate.fog") s.gameCreate.fog = asInt(0, 2);
        else if (key == "gameCreate.overrides") s.gameCreate.overrides = asInt(0, 2);
        else if (key == "gameCreate.mapSort") s.gameCreate.mapSort = asInt(0, 2);
        else if (key == "gameCreate.mapSortDir") s.gameCreate.mapSortDir = (asInt(-1, 1) < 0 ? -1 : 1);
        else if (key == "gameCreate.name") s.gameCreate.name = val;
        else if (key == "gameCreate.generator") s.gameCreate.generator = val;
        else if (key == "lastMap")         s.lastMap = val;
        else if (key == "knownServers") {  // comma-joined, most recent first
            std::stringstream ks(val);
            std::string tok;
            while (std::getline(ks, tok, ',') && s.knownServers.size() < 8)
                if (!tok.empty()) s.knownServers.push_back(tok);
        }
        else if (key == "dataDir")         s.dataDir = val;
        else if (key == "dataManifest")    s.dataManifest = val;
        else if (key.rfind("chanGain", 0) == 0 && key.size() == 9) {  // chanGain0..7
            int i = key[8] - '0';
            if (i >= 0 && i < 8) s.chanGain[i] = asFloat(0.0f, 1.0f);
        }
        else if (key.rfind("hotkey.", 0) == 0 && key.size() > 7) {   // hotkey.<action-id> = <chord>
            s.hotkeys[key.substr(7)] = val;
        }
        else if (key.rfind("campaigndone.", 0) == 0 && key.size() > 13) {  // campaigndone.<id> = i,j,k
            std::string id = key.substr(13);
            std::stringstream cs(val);
            std::string tok;
            while (std::getline(cs, tok, ','))
                if (!tok.empty()) s.campaignCompleted[id].insert(std::atoi(tok.c_str()));
        }
        else if (key.rfind("campaign.", 0) == 0 && key.size() > 9) {   // legacy: campaign.<id> = <count>
            std::string id = key.substr(9);
            int n = std::atoi(val.c_str());   // migrate the old completed-count watermark
            for (int i = 0; i < n; ++i) s.campaignCompleted[id].insert(i);
        }
    }
    if (!bilinearSeen && (legacyZoom >= 0 || legacyBilinear >= 0))
        s.bilinear = legacyZoom >= 0 ? legacyZoom == 1 : legacyBilinear == 1;
    return s;
}

bool saveSettings(const Settings& s) {
    std::string path = settingsPath();
    if (path.empty()) return false;

    std::ostringstream o;
    o << "# TA:Kingdoms engine settings\n";
    o << "fullscreen = " << (s.fullscreen ? 1 : 0) << "\n";
    o << "vsync = " << (s.vsync ? 1 : 0) << "\n";
    o << "maxFps = " << s.maxFps << "\n";
    o << "scorecardScale = " << s.scorecardScale << "\n";
    o << "uiScale = " << s.uiScale << "\n";
    o << "bilinearFilter = " << (s.bilinear ? 1 : 0) << "\n";
    o << "buildBarAlign = " << s.buildBarAlign << "\n";
    o << "buildBarScale = " << s.buildBarScale << "\n";
    o << "treeSway = " << (s.treeSway ? 1 : 0) << "\n";
    o << "tacticalDots = " << (s.tacticalDots ? 1 : 0) << "\n";
    o << "tacticalDotsZoom = " << s.tacticalDotsZoom << "\n";
    o << "unitShadows = " << (s.unitShadows ? 1 : 0) << "\n";
    o << "smoothArt = " << (s.smoothArt ? 1 : 0) << "\n";
    o << "videoDeblock = " << (s.videoDeblock ? 1 : 0) << "\n";
    o << "healthBars = " << s.healthBars << "\n";
    o << "statsPanel = " << (s.statsPanel ? 1 : 0) << "\n";
    o << "masterVol = " << s.masterVol << "\n";
    o << "bgmVol = " << s.bgmVol << "\n";
    o << "sfxVol = " << s.sfxVol << "\n";
    for (int i = 0; i < 8; ++i) o << "chanGain" << i << " = " << s.chanGain[i] << "\n";
    o << "mouseZoomSpeed = " << s.mouseZoomSpeed << "\n";
    o << "edgeScrollSpeed = " << s.edgeScrollSpeed << "\n";
    o << "edgeScroll = " << (s.edgeScroll ? 1 : 0) << "\n";
    o << "cursorScale = " << s.cursorScale << "\n";
    o << "hardwareCursor = " << (s.hardwareCursor ? 1 : 0) << "\n";
    o << "smoothMotion = " << (s.smoothMotion ? 1 : 0) << "\n";
    o << "playerName = " << s.playerName << "\n";
    o << "accountName = " << s.accountName << "\n";
    o << "audioDevice = " << s.audioDevice << "\n";
    o << "gameCreate.crusades = " << s.gameCreate.crusades << "\n";
    o << "gameCreate.pathfindingMode = " << int(s.gameCreate.pathfindingMode) << "\n";
    o << "gameCreate.doubleSight = " << s.gameCreate.doubleSight << "\n";
    o << "gameCreate.speedUnlock = " << s.gameCreate.speedUnlock << "\n";
    o << "gameCreate.monarchExpendable = " << s.gameCreate.monarchExpendable << "\n";
    o << "gameCreate.randomStarts = " << s.gameCreate.randomStarts << "\n";
    o << "gameCreate.generated = " << s.gameCreate.generated << "\n";
    o << "gameCreate.unitCap = " << s.gameCreate.unitCap << "\n";
    o << "gameCreate.fog = " << s.gameCreate.fog << "\n";
    o << "gameCreate.overrides = " << s.gameCreate.overrides << "\n";
    o << "gameCreate.mapSort = " << s.gameCreate.mapSort << "\n";
    o << "gameCreate.mapSortDir = " << s.gameCreate.mapSortDir << "\n";
    o << "gameCreate.name = " << s.gameCreate.name << "\n";
    o << "gameCreate.generator = " << s.gameCreate.generator << "\n";
    auto writePacks=[&](const char* key,const std::vector<std::string>& names){
        static constexpr char hex[]="0123456789abcdef";
        for(const auto& name:names){o<<key<<" = ";for(unsigned char c:name)o<<hex[c>>4]<<hex[c&15];o<<"\n";}
    };
    writePacks("overrides.hostPack",s.hostOverridePacks);
    writePacks("overrides.cosmeticPack",s.cosmeticOverridePacks);
    o << "lastMap = " << s.lastMap << "\n";
    if (!s.knownServers.empty()) {
        o << "knownServers = ";
        for (size_t i = 0; i < s.knownServers.size(); ++i)
            o << (i ? "," : "") << s.knownServers[i];
        o << "\n";
    }
    o << "dataDir = " << s.dataDir << "\n";
    o << "dataManifest = " << s.dataManifest << "\n";
    for (const auto& [id, chord] : s.hotkeys) o << "hotkey." << id << " = " << chord << "\n";
    for (const auto& [id, done] : s.campaignCompleted) {
        if (done.empty()) continue;
        o << "campaigndone." << id << " = ";
        bool first = true;
        for (int m : done) { o << (first ? "" : ",") << m; first = false; }
        o << "\n";
    }

    // Atomic write: temp file + rename, so a crash mid-write can't corrupt the file.
    std::string tmp = path + ".tmp";
    { std::ofstream out(tmp, std::ios::trunc); if (!out) return false; out << o.str(); if (!out) return false; }
    std::remove(path.c_str());
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

}  // namespace tak
