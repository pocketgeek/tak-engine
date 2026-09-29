#pragma once
#include "tnt/mapgen.h"
#include <array>
namespace cart {
// Seed, players, layout, tree/rock/mana/water/relief densities.
using GeneratorFields=std::array<std::string,8>;
GeneratorFields generatorFields(const tak::mapgen::Params& params);
bool parseGeneratorFields(const GeneratorFields& fields,tak::mapgen::Params& params,std::string& error);
}
