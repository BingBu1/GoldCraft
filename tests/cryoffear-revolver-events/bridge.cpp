// SPDX-License-Identifier: GPL-3.0-or-later
#include "native/csol/client/runtime.hpp"
#include "native/csol/client/weapons/guest_cof_effects.hpp"
#include "native/guest/cof/resource_catalog.hpp"
#include "native/include/goldcraft/connection_api.h"
#include "native/include/goldcraft/sound_precache_api.h"
#include <event_api.h>
#include <event_args.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace csol::client {cl_enginefunc_t engine{};}
namespace
{
using namespace csol::client;
using guest::cof::client::RevolverEventResult;
using namespace guest::cof::resources;
unsigned checks{},registrations{},inspections{},lookups{},randoms{},plays{};
std::uint64_t generation=8;
std::uint32_t soundFlags=GoldCraftSoundSignonRegistered;
bool registrationFailure{},missingEntity{},retireLookup{},changeRandom{},retirePlay{},changeInspect{},retireInspect{};
event_api_t events{};
cl_entity_t actor{};
event_args_t arguments{};
void (*callback)(event_args_s*);
void Check(bool value,int line)
{++checks;if(!value){std::fprintf(stderr,"Revolver bridge failed line %d\n",line);std::exit(2);}}
#define CHECK(x) Check((x),__LINE__)
std::uint64_t Generation(){return generation;}
void Register(const char* path,void (*handler)(event_args_s*))
{
    CHECK(!std::strcmp(path,RevolverEvents[2].host));++registrations;
    if(registrationFailure)throw std::runtime_error("controlled registration failure");
    callback=handler;
}
std::uint32_t Inspect(std::uint64_t expected,const char* name,std::uint32_t length) noexcept
{
    ++inspections;CHECK(expected==generation);
    const std::string_view wanted=RevolverSounds[unsigned(RevolverSound::Shell)].host;
    CHECK(std::string_view(name,length)==wanted);
    if(changeInspect)++generation;
    if(retireInspect)InvalidateGuestCofRevolverEvents();
    return soundFlags;
}
cl_entity_t* Entity(int index)
{
    ++lookups;CHECK(index==arguments.entindex);
    if(retireLookup)InvalidateGuestCofRevolverEvents();
    return missingEntity?nullptr:&actor;
}
int Random(int low,int high)
{++randoms;CHECK(low==0&&high==30);if(changeRandom)++generation;return 13;}
void Play(int index,float* origin,int channel,const char* sample,float volume,float attenuation,int flags,int pitch)
{
    ++plays;CHECK(index==arguments.entindex&&channel==0&&volume==1.0F&&attenuation==.8F&&flags==0&&pitch==113);
    CHECK(!std::strcmp(sample,RevolverSounds[unsigned(RevolverSound::Shell)].host));
    CHECK(origin[0]==2&&origin[1]==3&&origin[2]==4);
    if(retirePlay)InvalidateGuestCofRevolverEvents();
}
}
unsigned RevolverBridgeCases()
{
    using namespace csol::client;
    engine={};engine.pfnHookEvent=Register;engine.GetEntityByIndex=Entity;engine.pfnRandomLong=Random;
    events.EV_PlaySound=Play;engine.pEventAPI=&events;
    actor.origin[0]=2;actor.origin[1]=3;actor.origin[2]=4;actor.attachment[0][0]=500;
    arguments.entindex=7;arguments.origin[0]=900;
    const GoldCraftConnectionAPI connection{sizeof(GoldCraftConnectionAPI),1,Generation};
    const GoldCraftSoundPrecacheAPI sounds{sizeof(GoldCraftSoundPrecacheAPI),1,Inspect};
    CHECK(!InitializeGuestCofRevolverEvents(nullptr));
    auto wrong=connection;wrong.version=2;CHECK(!InitializeGuestCofRevolverEvents(&wrong));
    CHECK(InitializeGuestCofRevolverEvents(&connection)&&registrations==1&&callback);
    CHECK(InitializeGuestCofRevolverEvents(&connection)&&registrations==1);
    CHECK(!GuestCofRevolverEventsReady()&&!PrepareGuestCofRevolverEvents(nullptr));
    callback(&arguments);CHECK(lookups==0&&randoms==0&&plays==0);
    soundFlags=0;CHECK(!PrepareGuestCofRevolverEvents(&sounds)&&!GuestCofRevolverEventsReady());
    callback(&arguments);CHECK(lookups==0&&plays==0);
    // Native shell audio is independent of local PCM and of the held gun.
    soundFlags=GoldCraftSoundSignonRegistered;
    CHECK(PrepareGuestCofRevolverEvents(&sounds)&&GuestCofRevolverEventsReady());
    const auto coldInspections=inspections;
    callback(&arguments);CHECK(lookups==1&&randoms==1&&plays==1&&inspections==coldInspections);
    CHECK(GuestCofLastRevolverEvent()==RevolverEventResult::Issued);
    missingEntity=true;callback(&arguments);missingEntity=false;
    CHECK(lookups==2&&randoms==1&&plays==1&&GuestCofLastRevolverEvent()==RevolverEventResult::Ignored);
    callback(nullptr);CHECK(lookups==2&&plays==1);
    std::thread other([&]{CHECK(!GuestCofRevolverEventsReady());callback(&arguments);});other.join();
    CHECK(lookups==2&&plays==1);
    ++generation;callback(&arguments);CHECK(!GuestCofRevolverEventsReady()&&lookups==2);
    CHECK(PrepareGuestCofRevolverEvents(&sounds));
    retireLookup=true;callback(&arguments);retireLookup=false;
    CHECK(!GuestCofRevolverEventsReady()&&lookups==3&&randoms==1&&plays==1);
    CHECK(PrepareGuestCofRevolverEvents(&sounds));
    changeRandom=true;callback(&arguments);changeRandom=false;
    CHECK(!GuestCofRevolverEventsReady()&&lookups==4&&randoms==2&&plays==1);
    CHECK(PrepareGuestCofRevolverEvents(&sounds));
    retirePlay=true;callback(&arguments);retirePlay=false;
    CHECK(!GuestCofRevolverEventsReady()&&lookups==5&&randoms==3&&plays==2);
    changeInspect=true;CHECK(!PrepareGuestCofRevolverEvents(&sounds));changeInspect=false;
    retireInspect=true;CHECK(!PrepareGuestCofRevolverEvents(&sounds));retireInspect=false;
    auto wrongSounds=sounds;wrongSounds.version=2;CHECK(!PrepareGuestCofRevolverEvents(&wrongSounds));
    CHECK(PrepareGuestCofRevolverEvents(&sounds));
    ShutdownGuestCofRevolverEvents();callback(&arguments);CHECK(!GuestCofRevolverEventsReady()&&plays==2);
    registrationFailure=true;CHECK(!InitializeGuestCofRevolverEvents(&connection));registrationFailure=false;
    callback(&arguments);CHECK(plays==2&&!GuestCofRevolverEventsReady());
    CHECK(InitializeGuestCofRevolverEvents(&connection)&&PrepareGuestCofRevolverEvents(&sounds));
    callback(&arguments);CHECK(plays==3);
    InvalidateGuestCofRevolverEvents();CHECK(!GuestCofRevolverEventsReady());
    ShutdownGuestCofRevolverEvents();return checks;
}
