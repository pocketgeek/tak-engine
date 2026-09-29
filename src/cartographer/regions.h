#pragma once
#include "crt/crt.h"
#include <string>
namespace cart {
// Renames update typed location operands; other strings are never rewritten.
bool setRegion(tak::crt::Scenario& scenario,int index,tak::crt::Region region,
               int width,int height,std::string& error);
bool removeRegion(tak::crt::Scenario& scenario,int index,std::string& error);
}
