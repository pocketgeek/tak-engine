#include "client/hotkeys.h"

#include <SDL.h>

#include <algorithm>
#include <sstream>

namespace tak {

const std::vector<HotkeyDef>& hotkeyDefs() {
    // key chords use SDL keycodes; sections group the config screen.
    static const std::vector<HotkeyDef> defs = {
        {Act::Move,           "order.move",         "MOVE",            "ORDERS",    {SDLK_m, 0}},
        {Act::Attack,         "order.attack",       "ATTACK",          "ORDERS",    {SDLK_a, 0}},
        {Act::FightMove,      "order.fightmove",    "FIGHT-MOVE",      "ORDERS",    {SDLK_f, 0}},
        {Act::Patrol,         "order.patrol",       "PATROL",          "ORDERS",    {SDLK_p, 0}},
        {Act::Guard,          "order.guard",        "GUARD",           "ORDERS",    {SDLK_g, 0}},
        {Act::Stop,           "order.stop",         "STOP",            "ORDERS",    {SDLK_s, 0}},
        {Act::CycleWeapon,    "order.cycleweapon",  "CYCLE WEAPON",    "ORDERS",    {SDLK_w, 0}},
        {Act::NextUnit,       "order.nextunit",     "NEXT UNIT",       "ORDERS",    {SDLK_n, 0}},
        {Act::TrackSelection, "view.track",         "TRACK SELECTION", "ORDERS",    {SDLK_t, 0}},

        {Act::SelectAll,      "select.all",         "SELECT ALL",      "SELECTION", {SDLK_a, HK_CTRL}},
        {Act::SelectSameType, "select.sametype",    "SELECT SAME TYPE","SELECTION", {SDLK_z, HK_CTRL}},
        {Act::SelectOnScreen, "select.onscreen",    "SELECT ON SCREEN","SELECTION", {SDLK_u, HK_CTRL}},
        {Act::SelectMonarch,  "select.monarch",     "SELECT MONARCH",  "SELECTION", {SDLK_m, HK_CTRL}},
        {Act::SelfDestruct,   "unit.selfdestruct",  "SELF-DESTRUCT",   "SELECTION", {SDLK_d, HK_CTRL | HK_SHIFT}},

        {Act::ToggleCounts,   "view.counts",        "UNIT COUNTS",     "VIEW",      {SDLK_F4, 0}},
        {Act::Disco,          "emote.disco",        "DISCO",           "EMOTES",    {SDLK_d, HK_SHIFT}},
        {Act::Headbang,       "emote.headbang",     "HEADBANG",        "EMOTES",    {SDLK_h, HK_SHIFT}},
    };
    return defs;
}

uint16_t normMod(uint16_t sdlMod) {
    uint16_t m = 0;
    if (sdlMod & KMOD_CTRL) m |= HK_CTRL;
    if (sdlMod & KMOD_SHIFT) m |= HK_SHIFT;
    if (sdlMod & KMOD_ALT) m |= HK_ALT;
    return m;
}

std::string chordName(const KeyChord& c) {
    if (!c.bound()) return "UNBOUND";
    std::string s;
    if (c.mod & HK_CTRL) s += "Ctrl+";
    if (c.mod & HK_SHIFT) s += "Shift+";
    if (c.mod & HK_ALT) s += "Alt+";
    const char* kn = SDL_GetKeyName(SDL_Keycode(c.key));
    s += (kn && *kn) ? kn : "?";
    return s;
}

bool parseChord(const std::string& s, KeyChord& out) {
    out = {};
    if (s.empty() || s == "NONE") return true;   // valid: unbound
    std::stringstream ss(s);
    std::string tok, keyName;
    while (std::getline(ss, tok, '+')) {
        std::string low = tok;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        if (low == "ctrl") out.mod |= HK_CTRL;
        else if (low == "shift") out.mod |= HK_SHIFT;
        else if (low == "alt") out.mod |= HK_ALT;
        else keyName = tok;   // the last non-modifier token is the key
    }
    if (keyName.empty()) return false;
    SDL_Keycode k = SDL_GetKeyFromName(keyName.c_str());
    if (k == SDLK_UNKNOWN) return false;
    out.key = int32_t(k);
    return true;
}

void Hotkeys::load(const std::map<std::string, std::string>& overrides) {
    for (const auto& d : hotkeyDefs())
        chords_[size_t(d.act)] = effectiveChord(overrides, d.act);
}

Act Hotkeys::match(int32_t key, uint16_t sdlMod) const {
    uint16_t m = normMod(sdlMod);
    for (const auto& d : hotkeyDefs()) {
        const KeyChord& c = chords_[size_t(d.act)];
        if (c.bound() && c.key == key && c.mod == m) return d.act;
    }
    return Act::Count;
}

// --- override map helpers ---------------------------------------------------

static const HotkeyDef& defFor(Act a) {
    for (const auto& d : hotkeyDefs()) if (d.act == a) return d;
    return hotkeyDefs()[0];   // unreachable
}

KeyChord effectiveChord(const std::map<std::string, std::string>& overrides, Act a) {
    const HotkeyDef& d = defFor(a);
    auto it = overrides.find(d.id);
    if (it == overrides.end()) return d.def;   // no override: the default
    KeyChord c;
    if (!parseChord(it->second, c)) return d.def;   // corrupt override: fall back
    return c;
}

void setChordOverride(std::map<std::string, std::string>& overrides, Act a, KeyChord c) {
    const HotkeyDef& d = defFor(a);
    if (c == d.def) { overrides.erase(d.id); return; }   // back to default: drop the override
    overrides[d.id] = c.bound() ? chordName(c) : "NONE";
}

}  // namespace tak
