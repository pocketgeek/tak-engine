#pragma once
// The pathfinders Settings -> Benchmark offers. The pick is remembered in
// Settings::benchmarkPathfinding and sent as the benchmark room's own pathfindingMode;
// the results and the stats panel name the mode the run's world really used.
#include "sim/pathmode.h"

namespace tak {

// The screen's PATHFINDING choices, left to right.
inline constexpr sim::PathfindingMode kBenchmarkPaths[2] = {sim::PathfindingMode::Retail,
                                                            sim::PathfindingMode::Legion};

constexpr const char* benchmarkPathLabel(sim::PathfindingMode mode) {
    return sim::isLegionPathfinding(mode) ? "LEGION" : "RETAIL";
}

}  // namespace tak
