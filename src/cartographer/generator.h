#pragma once
#include "tnt/mapgen.h"
#include <array>
namespace tak::hpi { class Vfs; }
namespace cart {
// Seed, players, layout, tree/rock/mana/water/relief densities.
using GeneratorFields=std::array<std::string,8>;
GeneratorFields generatorFields(const tak::mapgen::Params& params);
bool parseGeneratorFields(const GeneratorFields& fields,tak::mapgen::Params& params,std::string& error);
// Old editor maps stored the recipe as the description's first line. Explicit
// metadata wins, including malformed metadata (never silently use other inputs).
bool restoreGeneratorRecipe(const std::string& recipe,const std::string& description,
                            tak::mapgen::Params& params,std::string& error);
std::string mapGeneratorRecipe(const tak::hpi::Vfs& vfs,const std::string& mapPath,
                               const std::string& embedded);
}
