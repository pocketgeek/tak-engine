#include "client/retailquality.h"
#include "client/renderframe.h"
#include <cstdio>
#include <cstring>
#include <cmath>

int main(int argc,char** argv) {
    if (argc==1) {
        tak::sim::Player player;player.mana=500;player.storage=1000;
        for (int tick=0;tick<30;++tick) {
            player.creditMana(10.0/30);player.debitMana(4.0/30);
            player.displayResources.advance();
        }
        PlayerR frame;frame.captureEconomy(player);
        if (std::abs(frame.income-10)>0.001 || std::abs(frame.expenditure-4)>0.001 ||
            std::abs(frame.mana-506)>0.001 || frame.storage!=1000) return 1;
        player.displayResources.samples.back()={1000,2000};
        frame.captureEconomy(player);
        if (std::abs(frame.income-10)>0.001 || std::abs(frame.expenditure-4)>0.001) return 1;
        for (const auto [mana,index]:{std::pair{0.f,0},std::pair{500.f,11},
                                     std::pair{1000.f,23},std::pair{2000.f,23}}) {
            frame.mana=mana;if (frame.manaBulbFrame(24)!=index) return 1;
        }
        player.retailResources=tak::sim::RetailConstructionResources{};
        player.retailResources->capacity=25;player.mana=12.5;
        frame.captureEconomy(player);
        if (frame.storage!=25 || frame.manaBulbFrame(24)!=11) return 1;
        std::puts("PASS: HUD uses recorded resource rates, actual capacity and retail bulb indexing");
    }

    if (argc==2 && std::strcmp(argv[1],"--movement-animation")==0) {
        int blocked,attached,speed,maximum;
        while (std::scanf("%d %d %d %d",&blocked,&attached,&speed,&maximum)==4) {
            tak::sim::UnitType type;type.maxVel=tak::sim::Fixed::raw(maximum);
            tak::sim::Unit unit;unit.type=&type;unit.speed=tak::sim::Fixed::raw(speed);unit.bodyBlockStreak=blocked;
            UnitR frame;frame.type=&type;frame.inTransport=attached;frame.captureMovement(unit);
            std::printf("%d %d\n",frame.animationSpeedPercent(),int(frame.walking()));
        }
        return 0;
    }
    if (argc==1) {
        tak::sim::UnitType type;type.maxVel=tak::sim::Fixed::fromInt(5);
        tak::sim::Unit unit;unit.type=&type;unit.speed=tak::sim::Fixed::fromInt(1);
        UnitR frame;frame.type=&type;
        for (int blocked : {0,1,2,2,0}) {
            unit.bodyBlockStreak=blocked;frame.captureMovement(unit);
            if (frame.animationSpeedPercent()!=(blocked>=2?0:20) || frame.walking()!=(blocked<2) || frame.speed!=30.0f) return 1;
        }
    }
    if (argc==1) {
        tak::sim::UnitType type;type.canFly=true;type.cruiseAlt=150;
        tak::sim::Unit unit;unit.type=&type;
        UnitR frame;frame.type=&type;
        // A low flyer next to high terrain must stay at its simulated height,
        // even with an active conjure order and a much higher clearance datum.
        frame.buildSiteId=42;frame.conjuring=true;
        for (const auto [height,mode] : {std::pair{40,1},std::pair{65,2},
                                        std::pair{190,2},std::pair{55,2},std::pair{40,1}}) {
            unit.flightY=tak::sim::Fixed::fromInt(height);unit.flightGroundMode=mode;
            frame.captureMovement(unit);
            // Later sim updates cannot alter the pinned render snapshot.
            unit.flightY=tak::sim::Fixed::fromInt(511);
            for (float datum : {0.0f,100.0f,215.0f}) {
                const auto [ground,altitude]=frame.flightRenderHeight(datum,40);
                if (ground<0 || altitude<0 || (ground+altitude)*0.5f!=(height-40)*0.5f ||
                    frame.flightGroundMode!=mode) return 1;
            }
        }
        std::puts("PASS: flyer projection follows captured height through ascent, hover and landing");
    }
    if (argc==2 && std::strcmp(argv[1],"--quality")==0) {
        int setting,smooth;double fps;float threshold,bias,probability;unsigned seed;
        while (std::scanf("%d %d %lf %f %f %f %u",&setting,&smooth,&fps,&threshold,&bias,&probability,&seed)==7) {
            tak::RetailVisualQuality state{smooth,probability};unsigned calls=0,origin=0;
            const bool chosen=state.choose(setting,fps,threshold,bias,[&](uint32_t caller) {
                ++calls;origin=caller;seed=seed*214013u+2531011u;return (seed>>16)&32767u;
            });
            std::printf("%d %u %u %u %u %d\n",state.smoothedFps,
                std::bit_cast<uint32_t>(state.probability),seed,calls,origin,int(chosen));
        }
        return 0;
    }
    tak::RetailVisualQuality state;unsigned calls=0;
    for (int i=0;i<24;++i) state.choose(101,0.824,40,5,[&](uint32_t) { ++calls;return 12345u; });
    if (state.smoothedFps!=36 || calls!=5) return 1;
    std::puts("PASS: adaptive visual refresh crosses the random-draw threshold per visible unit");
}
