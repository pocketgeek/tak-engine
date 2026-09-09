#pragma once

// A parsed .takrep replay: enough to rebuild the world and replay it.
// Extracted from client/main.cpp; kept at global scope so its existing
// unqualified use sites there are unchanged.

#include <cstdint>
#include <string>
#include <vector>

#include "net/client.h"        // tak::net::Bundle (+ transitively net/protocol.h)
#include "sim/matchsetup.h"    // tak::sim::MatchConfig

struct ReplayFile {
    std::string mapId;
    bool crusades = false;
    uint8_t overridePolicy = 1;   // override tier the recorded game ran under
    tak::sim::MatchConfig cfg;
    std::vector<tak::net::Bundle> bundles;
};

// Load a .takrep (header + tick bundles). Returns false on a malformed file.
bool loadReplayFile(const std::string& path, ReplayFile& out);
