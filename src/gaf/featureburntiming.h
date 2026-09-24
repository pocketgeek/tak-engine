#pragma once
#include "gaf/animationtiming.h"
#include "tdf/tdf.h"
#include <algorithm>
#include <map>

namespace tak::gaf {
// Gameplay and data-hash callers share asset resolution. Pixel-only reskins
// remain cosmetic; the duration that controls feature replacement does not.
class FeatureBurnTiming {
    const hpi::Vfs& vfs_;
    std::map<std::string,uint32_t> effects_;
    std::map<std::string,std::map<std::string,uint32_t>> bodies_;
    uint32_t effect(const std::string& name) {
        if(name.empty())return 0;
        auto [it,added]=effects_.try_emplace(name,0);
        if(added)for(auto delay:animationTiming(vfs_,name))it->second+=std::max(1u,unsigned(delay));
        return it->second;
    }
public:
    explicit FeatureBurnTiming(const hpi::Vfs& vfs):vfs_(vfs) {}
    uint32_t duration(const tdf::Node& definition) {
        const auto file=hpi::MountSet::key(definition.valueOr("filename",""));
        const auto sequence=hpi::MountSet::key(definition.valueOr("seqnameburn",""));
        if(file.empty() || sequence.empty())return 0;
        auto [bank,added]=bodies_.try_emplace(file);
        if(added)try {
            const auto path="anims/"+file+".gaf";
            for(const auto& animation:load(vfs_.read(path),Palette{},-1,path)) {
                uint32_t ticks=0;
                for(const auto& frame:animation.frames)ticks+=std::max(1u,unsigned(frame.retailDelayTicks));
                bank->second[hpi::MountSet::key(animation.name)]=ticks;
            }
        } catch(const std::exception&) {}
        const auto body=bank->second.find(sequence);
        if(body==bank->second.end() || !body->second)return 0;
        const auto front=effect(hpi::MountSet::key(definition.valueOr("seqnamefrontflame","")));
        const auto back=effect(hpi::MountSet::key(definition.valueOr("seqnamebackflame","")));
        return front || back ? std::max(front,back) : body->second;
    }
};
} // namespace tak::gaf
