#pragma once

// Campaign spine: the ordered list of missions in a `camps/<name>.tdf` and a small
// controller for progression (current index, win -> next / lose -> retry). SDL-free
// and data-only, so it lives in tak-formats and is testable headless. The actual
// mission run happens elsewhere (setupMission + the server); this just picks which
// mission and tracks how far the player has got. See docs/campaign-design.md.

#include <string>
#include <vector>

namespace tak {
namespace hpi { class Vfs; }

struct CampaignMission {
    std::string stem;       // "takmission01_mt" -- the bundle stem (missionname)
    std::string otaFile;    // "takmission01_mt.ota" (missionfile, informational)
};

struct Campaign {
    std::string file;       // "camps/book of darien.tdf" (VFS path)
    std::string id;         // "book of darien" (lowercased stem, the persistence key)
    std::string title;      // "Book of Darien" (display)
    std::string side;       // [HEADER] campaignside (vestigial in retail)
    std::vector<CampaignMission> missions;
    bool empty() const { return missions.empty(); }
    int count() const { return int(missions.size()); }
};

// Load one campaign from its `camps/*.tdf` VFS path. Returns false if absent/empty.
bool loadCampaign(const hpi::Vfs& vfs, const std::string& file, Campaign& out);

// Every `camps/*.tdf` in the VFS, ordered for display: Book of Darien, then The Iron
// Plague, then the alt-ending branch, then any others alphabetically.
std::vector<Campaign> loadCampaigns(const hpi::Vfs& vfs);

}  // namespace tak
