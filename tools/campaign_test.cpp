#include "campaign/campaign.h"
#include "hpi/hpi.h"
#include "sim/matchsetup.h"
#include "sim/sim.h"
#include <cstdio>

int main(int argc,char** argv) {
    int failures=0;
    auto check=[&](bool valid,const char* why){if(!valid){std::fprintf(stderr,"FAIL %s\n",why);++failures;}};
    tak::Campaign campaign;
    campaign.missions={{"mission39a","","First title"},{"mission39","","Second title"},{"final","",""}};
    campaign.altFinal="otherfinal";campaign.altTitle="Alternate title";
    auto first=tak::campaignChapter(campaign,"mission39a");
    check(first && first->index==0 && first->title=="First title" && first->nextStem=="mission39",
          "campaign order follows authored list, not filename suffix");
    auto last=tak::campaignChapter(campaign,"final");
    check(last && last->title=="CHAPTER 3" && last->nextStem.empty() && !last->alternate,
          "ordinary finale is terminal with a useful missing-title fallback");
    auto alternate=tak::campaignChapter(campaign,"otherfinal");
    check(alternate && alternate->alternate && alternate->index==2 && alternate->title=="Alternate title" && alternate->nextStem.empty(),
          "alternate finale has authored title and no next chapter");
    check(!tak::campaignChapter(campaign,"") && !tak::campaignChapter(campaign,"unknown"),"unknown chapter cannot advance progress");
    if(argc>1) {
        const auto vfs=tak::hpi::mountRetailRoot(argv[1],tak::hpi::OverridePolicy::None);
        const auto campaigns=tak::loadCampaigns(vfs);
        int chapters=0;
        for(const auto& c:campaigns) {
            for(size_t i=0;i<c.missions.size();++i) {
                const auto chapter=tak::campaignChapter(c,c.missions[i].stem);
                check(chapter && chapter->index==int(i) && !chapter->title.empty(),"mounted chapter identity and title");
                ++chapters;
            }
            if(!c.altFinal.empty()) {
                const auto chapter=tak::campaignChapter(c,c.altFinal);
                check(chapter && chapter->alternate && !c.altTitle.empty(),"mounted alternate title retained");
                ++chapters;
            }
        }
        check(chapters==74,"installed base, expansion and alternate-ending chapter count");
        std::printf("mounted chapters: %d\n",chapters);
        if(argc>2 && std::string(argv[2])=="--startup") {
            // A condition rewrite must not silently end real missions as soon
            // as their initial script runs. Exercise every chapter's actual
            // placements/conditions, not only synthetic predicate fixtures.
            for(bool crusades:{false,true}) {
                tak::sim::TypeRegistry registry;
                tak::sim::setupRegistry(registry,vfs,crusades);
                for(const auto& c:campaigns) {
                    std::vector<std::string> stems;
                    for(const auto& m:c.missions)stems.push_back(m.stem);
                    if(!c.altFinal.empty())stems.push_back(c.altFinal);
                    for(const auto& stem:stems) {
                        tak::sim::World world;world.setVisPlayer(-1);world.setSerialThreads(true);
                        int human=0;
                        const bool loaded=tak::sim::setupMission(world,registry,vfs,stem,human);
                        check(loaded,"mounted mission startup loads");
                        if(!loaded)continue;
                        for(int tick=0;tick<30 && !world.missionOutcome();++tick)world.tick(1.f/30.f);
                        std::printf("startup %s crusades=%d tick=%u outcome=%d units=%zu\n",stem.c_str(),crusades,
                                    world.tickCount(),world.missionOutcome(),world.units().size());
                        check(world.missionOutcome()==0,"mission must remain playable through its opening second");
                        std::fflush(stdout);
                    }
                }
            }
        }
    }
    std::printf("%s: campaign chapter identity and progression\n",failures?"FAIL":"PASS");
    return failures?1:0;
}
