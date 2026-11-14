// SPDX-License-Identifier: GPL-3.0-or-later
// Cold admission fixture: actual resource owner + actual decal adapter/GL.
// Audio/particle providers are controlled; their playback is not tested here.
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/weapons/guest_cof_resources.hpp"
#include "../../native/csol/client/weapons/guest_cof_effects.hpp"
#include "../../native/guest/cof/resource_catalog.hpp"
#include "../../native/guest/cof/admission.hpp"
#include "../../native/include/goldcraft/connection_api.h"
#include "../../native/include/goldcraft/precache_ready_api.h"
#include "../../native/include/goldcraft/sound_precache_api.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace
{
using namespace csol::client;using namespace guest::cof::resources;
std::uint64_t generation=100;
GoldCraftPrecacheReadyCallback ready{},retired{};void* listener{};
unsigned subscriptions{},reads{},decalReads{},audioCalls{},particleCalls{};
bool glock{},revolver{},missingDecal{},missingRevolverShell{};
void Check(bool yes,const char* text){if(!yes){std::fprintf(stderr,"FAIL resource owner %s\n",text);std::exit(1);}}
std::uint64_t Generation(){return generation;}
std::uint64_t Subscribe(GoldCraftPrecacheReadyCallback r,GoldCraftPrecacheReadyCallback t,void* c){ready=r;retired=t;listener=c;return ++subscriptions;}
std::uint32_t Unsubscribe(std::uint64_t){ready=nullptr;retired=nullptr;return 1;}
std::uint32_t Inspect(std::uint64_t g,const char* text,std::uint32_t length) noexcept {if(g!=generation)return 0;const std::string_view name{text,length};if(glock)for(const auto& resource:GlockAudioResources)if(resource.remoteAsset==name)return GoldCraftSoundSignonRegistered|GoldCraftSoundPcmResident;if(revolver)for(const auto& resource:RevolverAudioResources)if(resource.remoteAsset==name)return GoldCraftSoundSignonRegistered;return 0;}
int Sprite(const char* path){Check(std::strcmp(path,P345Models[unsigned(P345Model::Fog)].host)==0,"sprite path");return 1;}
}
namespace csol::client
{
std::vector<std::byte> ReadResource(const char* path)
{
    ++reads;
    for(const auto& resource:ShotDecals)if(std::strcmp(path,resource.host)==0){++decalReads;if(missingDecal)return {};std::vector<std::byte> data(18+64*64*3,std::byte{64});data[0]=std::byte{0};data[2]=std::byte{2};data[12]=std::byte{64};data[13]=std::byte{0};data[14]=std::byte{64};data[15]=std::byte{0};data[16]=std::byte{24};data[17]=std::byte{32};return data;}
    return std::vector<std::byte>(16,std::byte{1});
}
guest::cof::client::AudioResult PrepareGuestCofAudio(const GuestCofAudioPlan& plan) noexcept {++audioCalls;for(const auto& asset:plan.assets)Check(plan.remotePrecached(plan.context,asset.remoteAsset),"actual sound signon admission query");return guest::cof::client::AudioResult::Applied;}
void InvalidateGuestCofAudioResources() noexcept {}
guest::cof::client::AuroraResult PrepareGuestCofParticles(const GuestCofParticlePlan&) noexcept {++particleCalls;return guest::cof::client::AuroraResult::Applied;}
void InvalidateGuestCofParticleResources() noexcept {}
bool PrepareGuestCofEffects() noexcept {return false;}
void InvalidateGuestCofEffects() noexcept {}
bool PrepareGuestCofRevolverEvents(const GoldCraftSoundPrecacheAPI*) noexcept {return revolver&&!missingRevolverShell;}
bool GuestCofRevolverEventsReady() noexcept {return revolver&&!missingRevolverShell;}
void InvalidateGuestCofRevolverEvents() noexcept {}
}
void RunDecalResourceOwnerTests()
{
    using namespace csol::client;
    const GoldCraftConnectionAPI connection{sizeof(GoldCraftConnectionAPI),1,Generation};
    const GoldCraftSoundPrecacheAPI sounds{sizeof(GoldCraftSoundPrecacheAPI),1,Inspect};
    const GoldCraftPrecacheReadyAPI lifecycle{sizeof(GoldCraftPrecacheReadyAPI),1,Subscribe,Unsubscribe};
    engine.pfnSPR_Load=Sprite;
    InitializeGuestCofDecals(&connection);InitializeGuestCofResources({4,8*1024*1024},&connection,&sounds,&lifecycle);
    Check(subscriptions==1&&ready&&retired,"single actual signon listener");ready(listener,generation);
    Check(!reads&&GuestCofResourcesState().status==GuestCofResourceStatus::NoAssets,"ordinary server no CoF I/O");
    ++generation;glock=true;InitializeGuestCofResources({4,8*1024*1024},&connection,&sounds,&lifecycle);ready(listener,generation);
    Check(decalReads==5&&audioCalls==1&&particleCalls==1,"five cold reads only after native Glock sound registration");
    Check(GuestCofResourcesState().status==GuestCofResourceStatus::Prepared&&GuestCofResourcesState().decalsPrepared&&GuestCofDecalsState().resourcesPrepared,"single owner publishes actual textures prepared");
    const auto before=reads;ready(listener,generation);Check(reads==before,"duplicate ready does not reread");
    Check(GuestCofDecalsVideoInit(),"before native video releases real GL");GuestCofResourcesVideoInit();
    Check(reads==before&&decalReads==5&&GuestCofResourcesState().decalsPrepared&&GuestCofDecalsState().resourcesPrepared,"video reconstructs retained cold bytes without disk I/O");
    const auto old=generation;++generation;retired(listener,old);Check(!GuestCofDecalsState().resourcesPrepared&&GuestCofResourcesState().status==GuestCofResourceStatus::Cancelled,"actual retired listener invalidates GL");
    missingDecal=true;InitializeGuestCofResources({4,8*1024*1024},&connection,&sounds,&lifecycle);ready(listener,generation);
    Check(GuestCofResourcesState().status==GuestCofResourceStatus::ResourceFailed&&!GuestCofDecalsState().resourcesPrepared,"missing shot file never prepared");
    ++generation;glock=false;revolver=true;missingDecal=false;
    InitializeGuestCofResources({4,8*1024*1024},&connection,&sounds,&lifecycle);ready(listener,generation);
    const auto revolverState=GuestCofResourcesState();
    Check(revolverState.status==GuestCofResourceStatus::Prepared&&revolverState.samples==RevolverAudioResources.size()&&
        revolverState.weapons==guest::cof::PistolCapability(guest::cof::Pistol::Revolver),"only actually prepared Revolver group is identified");
    const auto beforeMissingShell=reads;
    ++generation;missingRevolverShell=true;
    InitializeGuestCofResources({4,8*1024*1024},&connection,&sounds,&lifecycle);ready(listener,generation);
    Check(GuestCofResourcesState().status==GuestCofResourceStatus::NativeSoundUnavailable&&reads==beforeMissingShell,
        "local waves cannot admit an absent native Revolver shell consumer and perform no cold file reads");
    ShutdownGuestCofResources();ShutdownGuestCofDecals();
    std::puts("resource owner: single signon subscription, ordinary-server no I/O, five cold TGA reads, cached video reconstruction, retirement and missing-resource rejection passed");
}
