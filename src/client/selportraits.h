// The info bar's multi-selection portrait strip: one portrait per unit TYPE in the
// selection, each carrying how many of that type are selected. The first portrait
// sits where the single-unit portrait always does; the rest extend to its LEFT.
// Clicking a portrait narrows the selection to that type, Shift+click drops it.
//
// Pure (no SDL, no GameView) so tools/selportraits_test.cpp can check the grouping,
// the click rule and the fit/overflow layout. Selection is local client state --
// nothing here touches the simulation.
#pragma once

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace tak::selportraits {

struct Group {
    std::string type;        // UnitType::id
    std::vector<int> ids;    // those selected units, in selection order
};

// `units` is the selection in order as (unit id, type id), LIVE units only (the
// caller drops dead/typeless ids). The front unit's type leads -- it is the unit
// whose stats the bar shows -- then the rest by count, most first, ties by type id.
inline std::vector<Group> group(const std::vector<std::pair<int, std::string>>& units) {
    std::vector<Group> out;
    for (const auto& [id, type] : units) {
        auto it = std::find_if(out.begin(), out.end(), [&](const Group& g) { return g.type == type; });
        if (it == out.end()) out.push_back({type, {id}});
        else it->ids.push_back(id);
    }
    if (out.size() > 2)
        std::sort(out.begin() + 1, out.end(), [](const Group& a, const Group& b) {
            return a.ids.size() != b.ids.size() ? a.ids.size() > b.ids.size() : a.type < b.type;
        });
    return out;
}

// A portrait click. Plain: keep only `ids` (the clicked type's selected units).
// remove (Shift): drop them. The surviving units keep their selection order.
inline std::vector<int> click(const std::vector<int>& selection, const std::vector<int>& ids,
                              bool remove) {
    std::vector<int> out;
    for (int id : selection)
        if ((std::find(ids.begin(), ids.end(), id) != ids.end()) != remove) out.push_back(id);
    return out;
}

// Fit `n` portraits into the room LEFT of the primary one. Slot 0 is the primary
// at full size (never moved or shrunk); slots 1.. extend left in a single row at
// `scale` of its size, shrinking to fit but not below minScale. Past that the strip
// folds into TWO rows of half-height portraits (column by column, top first). When
// even that cannot hold them all, the last cell becomes an overflow tile ("+K") and
// `shown` portraits (primary included) are drawn -- the strip never leaves `room`.
struct Layout {
    int shown = 1;           // type portraits drawn, primary included
    int rows = 1;            // 1, or 2 for the folded strip
    float scale = 1.0f;      // size of slots 1.. relative to the primary
    bool overflow = false;   // a "+K" tile follows the last portrait (K = n - shown)
    float width = 0;         // room used left of the primary (every column + its gap)
};

inline Layout layout(int n, float fullW, float fullH, float gap, float room, float minScale) {
    Layout L;
    const int extra = n - 1;
    if (extra <= 0 || fullW <= 0 || fullH <= 0) return L;
    room = std::max(room, 0.0f);
    const float s1 = std::min(1.0f, (room / float(extra) - gap) / fullW);
    int cols = extra;
    if (s1 >= minScale) {
        L.shown = n;
        L.scale = s1;
    } else {
        L.rows = 2;
        L.scale = std::max(0.0f, (fullH - gap) / (2 * fullH));   // two rows + a gap = full height
        cols = int(std::floor(room / (gap + fullW * L.scale)));
        const int cells = 2 * cols;
        if (cells >= extra) {
            L.shown = n;
            cols = (extra + 1) / 2;
        } else if (cells >= 1) {   // the last cell holds the "+K" tile
            L.shown = cells;
            L.overflow = true;
        } else {                   // no room at all: the primary alone
            L.rows = 1;
            cols = 0;
        }
    }
    L.width = float(cols) * (gap + fullW * L.scale);
    return L;
}

// Slot i's rectangle (i >= 1; slot `shown` is the overflow tile), given the
// primary's. A single shrunk row is centred on the primary's height; a folded strip
// fills it, top row first.
struct Rect { float x, y, w, h; };
inline Rect slot(const Layout& L, int i, Rect primary, float gap) {
    if (i <= 0) return primary;
    const float w = primary.w * L.scale, h = primary.h * L.scale;
    const int col = (i - 1) / L.rows, row = (i - 1) % L.rows;
    const float y = L.rows == 1 ? primary.y + (primary.h - h) * 0.5f
                                : primary.y + float(row) * (h + gap);
    return {primary.x - float(col + 1) * (gap + w), y, w, h};
}

}  // namespace tak::selportraits
