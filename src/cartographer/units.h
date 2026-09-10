#pragma once

// Cartographer's placed-unit layer. Units live in the map's binary .crt
// (loaded via tak::crt); this holds an editable list + the unit-type name list
// for the palette / Unit Properties combo. The .crt WRITER (full 568-byte
// record) lands with the record-offset RE; until then units load + display +
// arrange in memory.

#include <string>
#include <vector>

namespace tak::hpi { class Vfs; }

namespace cart {

struct PlacedUnit {
    std::string type;      // unit FBI name (objectname), UPPERCASE as in the .crt
    int player = 0;        // player id / start slot
    float x = 0, z = 0;    // map pixels (cell*16 = 16px cells; .crt stores 2px units)
    // Full properties (defaults until the record-offset RE fills their .crt slots).
    int health = 100, veteran = 0, armor = 100, weapon = 100;
    float angle = 0;
    std::string name;      // optional unique name
};

// Load placed units from a map's .crt (via tak::crt::load). Empty if absent.
std::vector<PlacedUnit> loadUnits(const tak::hpi::Vfs& vfs, const std::string& crtPath);

// Sorted UPPERCASE unit-type names from every units/*.fbi (for the palette/combo).
std::vector<std::string> unitTypeNames(const tak::hpi::Vfs& vfs);

} // namespace cart
