#include "client/settings.h"

#include "client/dev.h"
#include "client/renderdriver.h"
#include "util/prefpath.h"

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

void trim(std::string& t) {
    size_t a = t.find_first_not_of(" \t\r\n");
    size_t b = t.find_last_not_of(" \t\r\n");
    t = (a == std::string::npos) ? std::string() : t.substr(a, b - a + 1);
}

// Split one line into key and value; false for a blank or comment line. '#' starts a
// comment, except inside a dataDir value, where it is part of a folder name
// ("D:\Games\TAK #2"). serializeSettings never writes an inline comment, so only a
// hand-edited file could put one there.
bool splitLine(const std::string& line, std::string& key, std::string& val) {
    const size_t eq = line.find('='), hash = line.find('#');
    if (eq == std::string::npos || (hash != std::string::npos && hash < eq)) return false;
    key = line.substr(0, eq);
    trim(key);
    val = line.substr(eq + 1);
    if (hash != std::string::npos && key != "dataDir") val.erase(hash - eq - 1);
    trim(val);
    return !key.empty();
}

std::string& pathOverride() { static std::string p; return p; }

// The data-folder keys as this process last read or wrote them (see saveSettings).
struct DiskDataFolder { bool known = false; std::string dir, manifest; };
DiskDataFolder& diskDataFolder() { static DiskDataFolder d; return d; }

}  // namespace

void setSettingsPath(std::string path) {
    pathOverride() = std::move(path);
    diskDataFolder() = {};   // a different file: nothing is known about it yet
}

std::string settingsPath() {
    if (!pathOverride().empty()) return pathOverride();
    if (const char* o = devEnv("TAK_SETTINGS_FILE")) if (*o) return o;   // debug builds: test profile
    const std::string base = prefPath("TAKingdoms");
    return base.empty() ? std::string() : base + "settings.ini";
}

std::string replayDir() {
    std::string dir = settingsPath();
    const size_t cut = dir.find_last_of("/\\");
    if (cut == std::string::npos) return {};
    dir.erase(cut + 1);
    return dir;
}

Settings loadSettings() {
    Settings s;   // defaults
    std::string path = settingsPath();
    if (path.empty()) return s;
    std::ifstream in(path);
    if (!in) { diskDataFolder() = {true, {}, {}}; return s; }

    // Filtering keys, resolved after the loop so line order does not matter (see
    // resolveTextureFilter): textureFilter, else 0.7.25-0.7.28's bilinearFilter, else
    // 0.7.24's bilinear toggle or the unreleased Zoom Smoothing choice whose Smooth mode
    // was that filter. Zoom Smoothing's Sharp and Off, and the AA keys (antiAlias/
    // terrainAA/modelAA/unitEdgeAA/zoomedOutTerrain), have no successor; like any
    // unknown key they are skipped and vanish on the next save.
    int textureFilter=-1,bilinearFilter=-1,legacyBilinear=-1,legacyZoom=-1;
    std::string line;
    while (std::getline(in, line)) {
        std::string key, val;
        if (!splitLine(line, key, val)) continue;

        auto asInt   = [&](int lo, int hi) { return clampv(std::atoi(val.c_str()), lo, hi); };
        auto asFloat = [&](float lo, float hi) { return clampv(float(std::atof(val.c_str())), lo, hi); };
        auto asBool  = [&] { return val == "1" || val == "true" || val == "on"; };

        if(key=="mods.gamePack" || key=="mods.cosmeticPack") {
            std::string name;bool valid=val.size()%2==0 && val.size()<=256;
            auto digit=[](char c){return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
            for(size_t i=0;valid && i<val.size();i+=2){int a=digit(val[i]),b=digit(val[i+1]);if(a<0 || b<0)valid=false;else name+=char(a*16+b);}
            auto& names=key=="mods.gamePack"?s.gameModPacks:s.cosmeticModPacks;
            if(valid && !name.empty() && name.find_first_of("/\\\r\n") == name.npos && name.find('\0')==name.npos && names.size()<64 && std::find(names.begin(),names.end(),name)==names.end())names.push_back(name);
            continue;
        }
        if      (key == "fullscreen")      s.fullscreen = asBool();
        else if (key == "vsync")           s.vsync = asBool();
        else if (key == "maxFps")          s.maxFps = asInt(30, 480);
        else if (key == "renderer")        s.renderer = renderdriver::normalize(val);
        else if (key == "ignoreDpiScaling") s.ignoreDpiScaling = asBool();
        else if (key == "dpiMigrated")     s.dpiMigrated = asBool();
        else if (key == "scorecardScale")  s.scorecardScale = asFloat(0.5f, 2.0f);
        else if (key == "uiScale")         s.uiScale = asFloat(0.5f, 2.0f);
        else if (key == "textureFilter")   textureFilter = asInt(0, 2);
        else if (key == "bilinearFilter")  bilinearFilter = asBool();
        else if (key == "bilinear")        legacyBilinear = asBool();
        else if (key == "zoomSmoothing")   legacyZoom = val == "smooth" || val == "1";
        else if (key == "buildBarAlign")   s.buildBarAlign = asInt(0, 2);
        // 4.0, matching the slider and GameView. This still said 2.0 when the slider
        // went to 400%, so 300-400% survived until the next restart and then silently
        // snapped back to 200%.
        else if (key == "buildBarScale")   s.buildBarScale = asFloat(0.75f, 4.0f);
        else if (key == "chatScale")       s.chatScale = asFloat(0.5f, 3.0f);
        else if (key == "chatAlign")       s.chatAlign = asInt(0, 1);
        else if (key == "treeSway")        s.treeSway = asBool();
        else if (key == "tacticalDots")    s.tacticalDots = asBool();
        else if (key == "screenShake")     s.screenShake = asBool();
        else if (key == "tacticalDotsZoom") s.tacticalDotsZoom = asInt(0, 100);
        else if (key == "unitShadows")     s.unitShadows = asBool();
        else if (key == "gpuMemoryLimit")  s.gpuMemoryLimit = parseGpuMemoryLimit(val);
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
        else if (key == "streamRememberKey") s.streamRememberKey = asBool();
        else if (key == "audioDevice")     s.audioDevice = val;
        else if (key == "voiceEnabled")    s.voiceEnabled = asBool();
        else if (key == "voiceActivation") s.voiceActivation = asBool();
        else if (key == "voiceActivationLevel") s.voiceActivationLevel = asInt(0, 100);
        else if (key == "voiceMicDevice")  s.voiceMicDevice = val;
        else if (key == "voiceOutputSameAsGame") s.voiceOutputSameAsGame = asBool();
        else if (key == "voiceOutputDevice") s.voiceOutputDevice = val;
        else if (key == "voiceMicVolume")  s.voiceMicVolume = asInt(0, 100);
        else if (key == "voiceOutputVolume") s.voiceOutputVolume = asInt(0, 100);
        else if (key == "gameCreate.crusades") s.gameCreate.crusades = asBool();
        else if (key == "gameCreate.pathfindingMode")
            // Exactly one stable mode byte; anything else falls back to Retail.
            s.gameCreate.pathfindingMode = val.size() == 1 && val[0] >= '0' &&
                sim::validPathfindingMode(uint8_t(val[0] - '0'))
                ? sim::PathfindingMode(val[0] - '0') : sim::PathfindingMode::Retail;
        else if (key == "gameCreate.doubleSight") s.gameCreate.doubleSight = asBool();
        else if (key == "gameCreate.speedUnlock") s.gameCreate.speedUnlock = asBool();
        else if (key == "gameCreate.monarchExpendable") s.gameCreate.monarchExpendable = asBool();
        else if (key == "gameCreate.randomStarts")   // was a bool: "true"/"on" meant RANDOM
            s.gameCreate.randomStarts = val == "true" || val == "on" ? 1 : asInt(0, 2);
        else if (key == "gameCreate.allowSpectators") s.gameCreate.allowSpectators = asBool();
        else if (key == "gameCreate.powerCodes") s.gameCreate.powerCodes = asBool();
        else if (key == "gameCreate.generated") s.gameCreate.generated = asBool();
        else if (key == "gameCreate.unitCap") s.gameCreate.unitCap = asInt(250, 2000);
        else if (key == "gameCreate.fog") s.gameCreate.fog = asInt(0, 2);
        else if (key == "gameCreate.gameMods") s.gameCreate.gameMods = asBool();
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
    s.textureFilter = resolveTextureFilter(textureFilter, bilinearFilter, legacyBilinear, legacyZoom);
    diskDataFolder() = {true, s.dataDir, s.dataManifest};
    return s;
}

std::string serializeSettings(const Settings& s) {
    std::ostringstream o;
    o << "# TA:Kingdoms engine settings\n";
    o << "fullscreen = " << (s.fullscreen ? 1 : 0) << "\n";
    o << "vsync = " << (s.vsync ? 1 : 0) << "\n";
    o << "maxFps = " << s.maxFps << "\n";
    o << "renderer = " << (s.renderer.empty() ? "auto" : s.renderer) << "\n";
    o << "ignoreDpiScaling = " << (s.ignoreDpiScaling ? 1 : 0) << "\n";
    o << "dpiMigrated = " << (s.dpiMigrated ? 1 : 0) << "\n";
    o << "scorecardScale = " << s.scorecardScale << "\n";
    o << "uiScale = " << s.uiScale << "\n";
    o << "textureFilter = " << std::clamp(s.textureFilter, 0, 2) << "\n";
    o << "buildBarAlign = " << s.buildBarAlign << "\n";
    o << "buildBarScale = " << s.buildBarScale << "\n";
    o << "chatScale = " << std::clamp(s.chatScale, 0.5f, 3.0f) << "\n";
    o << "chatAlign = " << std::clamp(s.chatAlign, 0, 1) << "\n";
    o << "treeSway = " << (s.treeSway ? 1 : 0) << "\n";
    o << "tacticalDots = " << (s.tacticalDots ? 1 : 0) << "\n";
    o << "screenShake = " << (s.screenShake ? 1 : 0) << "\n";
    o << "tacticalDotsZoom = " << s.tacticalDotsZoom << "\n";
    o << "unitShadows = " << (s.unitShadows ? 1 : 0) << "\n";
    o << "gpuMemoryLimit = " << gpuMemoryLimitValue(s.gpuMemoryLimit) << "\n";
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
    o << "streamRememberKey = " << (s.streamRememberKey ? 1 : 0) << "\n";
    o << "audioDevice = " << s.audioDevice << "\n";
    o << "voiceEnabled = " << (s.voiceEnabled ? 1 : 0) << "\n";
    o << "voiceActivation = " << (s.voiceActivation ? 1 : 0) << "\n";
    o << "voiceActivationLevel = " << std::clamp(s.voiceActivationLevel, 0, 100) << "\n";
    o << "voiceMicDevice = " << s.voiceMicDevice << "\n";
    o << "voiceOutputSameAsGame = " << (s.voiceOutputSameAsGame ? 1 : 0) << "\n";
    o << "voiceOutputDevice = " << s.voiceOutputDevice << "\n";
    o << "voiceMicVolume = " << std::clamp(s.voiceMicVolume, 0, 100) << "\n";
    o << "voiceOutputVolume = " << std::clamp(s.voiceOutputVolume, 0, 100) << "\n";
    o << "gameCreate.crusades = " << s.gameCreate.crusades << "\n";
    o << "gameCreate.pathfindingMode = " << int(s.gameCreate.pathfindingMode) << "\n";
    o << "gameCreate.doubleSight = " << s.gameCreate.doubleSight << "\n";
    o << "gameCreate.speedUnlock = " << s.gameCreate.speedUnlock << "\n";
    o << "gameCreate.monarchExpendable = " << s.gameCreate.monarchExpendable << "\n";
    o << "gameCreate.randomStarts = " << s.gameCreate.randomStarts << "\n";
    o << "gameCreate.allowSpectators = " << s.gameCreate.allowSpectators << "\n";
    o << "gameCreate.powerCodes = " << s.gameCreate.powerCodes << "\n";
    o << "gameCreate.generated = " << s.gameCreate.generated << "\n";
    o << "gameCreate.unitCap = " << s.gameCreate.unitCap << "\n";
    o << "gameCreate.fog = " << s.gameCreate.fog << "\n";
    o << "gameCreate.gameMods = " << s.gameCreate.gameMods << "\n";
    o << "gameCreate.mapSort = " << s.gameCreate.mapSort << "\n";
    o << "gameCreate.mapSortDir = " << s.gameCreate.mapSortDir << "\n";
    o << "gameCreate.name = " << s.gameCreate.name << "\n";
    o << "gameCreate.generator = " << s.gameCreate.generator << "\n";
    auto writePacks=[&](const char* key,const std::vector<std::string>& names){
        static constexpr char hex[]="0123456789abcdef";
        for(const auto& name:names){o<<key<<" = ";for(unsigned char c:name)o<<hex[c>>4]<<hex[c&15];o<<"\n";}
    };
    writePacks("mods.gamePack",s.gameModPacks);
    writePacks("mods.cosmeticPack",s.cosmeticModPacks);
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
    return o.str();
}

bool saveSettings(const Settings& s) {
    std::string path = settingsPath();
    if (path.empty()) return false;
    Settings out = s;
    auto& seen = diskDataFolder();
    if (seen.known && s.dataDir == seen.dir && s.dataManifest == seen.manifest) {
        // Unchanged by this process since it last read or wrote the file, so the file's
        // CURRENT value wins: another program (Cartographer, a second game) may have set it.
        std::ifstream in(path);
        std::string line, key, val;
        while (in && std::getline(in, line))
            if (splitLine(line, key, val)) {
                if (key == "dataDir") out.dataDir = val;
                else if (key == "dataManifest") out.dataManifest = val;
            }
    }
    const std::string text = serializeSettings(out);
    // Atomic write: temp file + rename, so a crash mid-write can't corrupt the file.
    std::string tmp = path + ".tmp";
    { std::ofstream o(tmp, std::ios::trunc); if (!o) return false; o << text; if (!o) return false; }
    std::remove(path.c_str());
    if (std::rename(tmp.c_str(), path.c_str()) != 0) return false;
    seen = {true, out.dataDir, out.dataManifest};
    return true;
}

bool saveDataFolderSetting(const std::string& folder, const std::string& manifest) {
    Settings s = loadSettings();   // the file as it is now; nothing else in it changes
    s.dataDir = folder;
    s.dataManifest = manifest;
    return saveSettings(s);
}

}  // namespace tak
