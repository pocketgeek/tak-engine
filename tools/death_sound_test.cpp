#include "client/sound.h"
#include "client/monarchalert.h"
#include "client/retaildeathsfx.h"
#include "cob/vm.h"
#include "sim/matchsetup.h"
#include <iostream>
#include <set>

// Paused dummy device: exercise production admission and PCM mixing without
// racing the audio callback or depending on physical audio hardware.
struct SoundBankTestAccess {
    static bool prepare(SoundBank& bank) {
        SDL_AudioSpec want{};want.freq=11025;want.format=AUDIO_S16SYS;
        want.channels=1;want.samples=256;
        bank.dev_=SDL_OpenAudioDevice(nullptr,0,&want,&bank.spec_,0);
        bank.chan_=1;
        bank.index_["quiet"]="quiet";bank.index_["cry"]="cry";
        bank.cache_["quiet"]=std::vector<int16_t>(64,20);
        bank.cache_["cry"]=std::vector<int16_t>(64,400);
        return bank.dev_!=0;
    }
    static int sample(SoundBank& bank) {int16_t value=0;bank.mix(&value,1);return value;}
    static bool decodes(SoundBank& bank,const std::vector<uint8_t>& data) {
        auto pcm=bank.decodeWav(data.data(),data.size());return pcm && !pcm->empty();
    }
};

int main(int argc,char** argv) {
    if(argc==2 && std::string(argv[1])=="--victims") {
        int incoming;
        while(std::cin>>incoming) {
            std::array<tak::SoundVoice,32> voices;
            for(auto& voice:voices)std::cin>>voice.active>>voice.looping>>voice.priority>>voice.started;
            std::cout<<tak::retailSoundVoice(voices,incoming)<<'\n';
        }
        return 0;
    }
    if(argc==2 && std::string(argv[1])=="--routes") {
        int flags;bool visible,selected,free;
        while(std::cin>>flags>>visible>>selected>>free) {
            const int priority=tak::retailSoundPriority(flags);
            const bool admit=tak::retailUnitSoundAudible(flags,visible,selected) && (priority>1 || free);
            std::cout<<(admit?(priority==7?2:1):0)<<'\n';
        }
        return 0;
    }
    int failures=0;
    const auto check=[&](bool ok,const char* message) {if(!ok){++failures;std::cerr<<"FAIL "<<message<<'\n';}};
    {
        tak::MonarchAlert alert;
        check(!alert.admit(2,0,false,100), "other player's monarch does not alert");
        check(!alert.admit(1,0,true,100), "spectators do not hear a local monarch alarm");
        check(alert.admit(1,0,false,100), "first local monarch hit alerts immediately");
        check(!alert.admit(1,0,false,15099), "monarch alarm cooldown is 15 real seconds");
        check(alert.admit(1,0,false,15100), "monarch alarm repeats at cooldown boundary");
        check(!alert.admit(0,0,false,40000), "cooldown expiry alone does not play an alarm");
    }
    SDL_SetMainReady();
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    if(SDL_Init(SDL_INIT_AUDIO)!=0)return 2;
    for(int priority:{3,4,5}) {
        SoundBank bank;
        if(!SoundBankTestAccess::prepare(bank))return 2;
        for(int i=0;i<32;++i)bank.playWorld("quiet",0,0,priority);
        bank.playWorld("cry",0,0,4);
        check(SoundBankTestAccess::sample(bank)==(priority<4?510:320),
              "death cry replaces lower-priority audio but preserves equal/higher priority voices");
    }
    {
        SoundBank bank;if(!SoundBankTestAccess::prepare(bank))return 2;
        for(int i=0;i<32;++i)bank.play("quiet",1,1,1,true);
        bank.playWorld("cry",0,0,4);
        for(int i=0;i<130;++i)check(SoundBankTestAccess::sample(bank)==320,
                                   "looping voices wrap and remain protected from eviction");
    }
    check(!tak::retailUnitSoundAudible(4,false,true),"hidden positional death cry is suppressed");
    check(tak::retailUnitSoundAudible(7,false,false),"global authored sound bypasses visibility");
    check(!tak::retailUnitSoundAudible(1,true,false),"unselected chatter is suppressed");
    check(tak::retailUnitSoundAudible(1,true,true),"selected chatter is eligible");
    for(bool retail:{false,true}) {
        tak::cob::File file;file.names={"cry"};file.scripts={{"Dying",0}};
        file.numStatics=1;
        file.code={0x10021001,4,0x10072000,0,0x10023004,0,0x10021001,0,0x10065000};
        tak::cob::Vm vm(std::move(file));if(retail)vm.enableRetailAnimation();
        int calls=0;
        vm.onPlaySound=[&](int32_t index,int32_t flags) {
            ++calls;check(index==0 && flags==4,"VM preserves death sound name and authored priority");
        };
        vm.start("Dying");vm.tick(1.f/30);
        check(calls==1 && vm.getStatic(0)==0,"unit sound host returns native zero result");
    }
    if(argc==2) {
        auto vfs=tak::hpi::mountRetailRoot(argv[1]);
        std::set<std::string> checkedSounds;
        SoundBank decoder;
        check(vfs.has("sounds/alarmmon.wav"), "retail monarch alarm exists");
        check(SoundBankTestAccess::decodes(decoder,vfs.read("sounds/alarmmon.wav")),
              "retail monarch alarm decodes to PCM");
        int decoded=0,missingShipped=0;
        const auto auditSound=[&](std::string sound) {
            std::transform(sound.begin(),sound.end(),sound.begin(),::tolower);
            check(!sound.empty(),"death callback resolves a sound name");
            if(!checkedSounds.insert(sound).second)return;
            const auto path="sounds/"+sound+".wav";
            if(!vfs.has(path)) {
                // Shipped Gatling Crossbow/Giant Barracuda scripts reference absent WAVs.
                // Retail's failed lookup is silent; do not substitute another unit.
                if(sound=="cregatldie1" || sound=="zonbardie1") {++missingShipped;return;}
                ++failures;std::cerr<<"Missing death sound "<<path<<'\n';return;
            }
            check(SoundBankTestAccess::decodes(decoder,vfs.read(path)),"authored death sound decodes to PCM");
            ++decoded;
        };
        for(bool crusades:{false,true}) {
            tak::sim::TypeRegistry registry;tak::sim::setupRegistry(registry,vfs,crusades);
            int scripts=0,events=0;
            for(const auto& [name,type]:registry.types()) {
                const auto* file=type.script();if(!file)continue;
                ++scripts;
                // Check every authored death-cry variant, including branches not
                // selected by this deterministic callback timeline's random draws.
                for(auto sound:file->names) {
                    std::transform(sound.begin(),sound.end(),sound.begin(),::tolower);
                    if(sound.find("die")!=std::string::npos)auditSound(sound);
                }
                for(int deathType:{0,1,2,3,7,8,14,15}) {
                    tak::cob::Vm vm(*file);vm.enableRetailAnimation();
                    bool stop=false,capture=false;
                    vm.onGet=[](int query,const std::vector<int32_t>&){return query==4?100:0;};
                    vm.onSetUnitValue=[&](int32_t id,int32_t){if(tak::retailOwnerVmStopsOnSetUnitValue(id))stop=true;};
                    vm.onPlaySound=[&](int32_t index,int32_t flags) {
                        if(!capture)return;
                        ++events;
                        check(flags==4 || flags==7,"death cry uses an authored combat/global class");
                        auditSound(file->name(uint32_t(index)));
                    };
                    vm.start("Create");for(int t=0;t<30;++t)vm.tick(1.f/30);
                    vm.reset();vm.setStatic(0,0);stop=false;capture=true;
                    if(deathType<14) {
                        vm.start("Killed",{100,0,deathType});
                        if(!vm.start("Dying",{deathType}))vm.start("death");
                    }
                    for(int t=0;t<600 && !stop && deathType<14;++t)vm.tick(1.f/30);
                }
            }
            std::cout<<(crusades?"Crusades":"Standard")<<": "<<scripts<<" scripts, "<<events<<" death sound events\n";
        }
        std::cout<<decoded<<" distinct death WAVs decoded; "<<missingShipped<<" known absent shipped WAVs remain silent\n";
    }
    SDL_Quit();
    std::cout<<(failures?"FAIL":"PASS")<<": death sound delivery and mixer admission ("<<failures<<" failures)\n";
    return failures?1:0;
}
