// SPDX-License-Identifier: GPL-3.0-or-later
// Reuse the actual registered Session/player/AK47 controlled-engine fixture.
#define main kick_sdk_entry_unused
#include "../csol-human-kick-server/sdk.cpp"
#undef main
#include "../../native/csol/server/custom/action_stamina.h"
#include "../../native/csol/server/custom/challenge_environment.h"
#include "../../native/csol/server/custom/gameplay_settings.h"
#include "../../native/csol/scenario/custom/phase_art.hpp"
#include "goldcraft/csol_gameplay_settings_api.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <thread>
#include <cstdlib>

namespace
{
namespace custom=csol::scenario::custom;
custom::SnapshotPtr settings;
void Resources(std::map<std::string, std::string>& files)
{
    kick_test::Resources(files);
    files[custom::NightAnimationAtlas] = "explicit blood-moon existence fixture; no image decoding claimed";
}
void PrepareLightProvider()
{
    using sdk_test::Require;
    const auto* path=std::getenv("GOLDCRAFT_TEST_LIGHT_PROVIDER");
    Require(path&&*path,"explicit actual engine lightstyle provider supplied");
    const auto module=LoadLibraryA(path);
    Require(module!=nullptr,"actual x86 engine lightstyle provider loads");
    using Map=void (*)(const char*);
    const auto map=reinterpret_cast<Map>(GetProcAddress(module,"EnvironmentTestMap"));
    Require(map!=nullptr,"actual provider exposes its controlled map boundary");
    map("m");
    // Its production service remains loaded through runtime/fixture teardown.
}
}
int main()
{
    try
    {
        using sdk_test::Require;using sdk_test::Check;
        custom::Draft draft;custom::Error configError;
        Require(draft.Set(custom::Feature::TimeCycle,true,configError)&&draft.Finish(settings,configError),"actual typed settings with time requested, without a phase owner");
        // Actual settings adapter opens its controlled engine game-dir during
        // GameMode startup. This executable runs in a fresh build/data root.
        const auto path=std::filesystem::current_path()/"cstrike/addons/goldcraft/config/gameplay.json";
        Require(!std::filesystem::exists(path),"fresh actual settings path required");
        std::filesystem::create_directories(path.parent_path());
        {std::ofstream file(path,std::ios::binary);file<<custom::Serialize(*settings);Require(bool(file),"cold authored settings written to isolated test directory");}
        Require(GetModuleHandleA("swds.dll")==nullptr,"missing-service case runs before loading the engine provider");
        CCSOLChallengeEnvironment unavailable;
        Check(!unavailable.Precache(*settings),"missing engine lightstyle service cannot manufacture TimeCycle capability");
        PrepareLightProvider();
        {
            kick_test::Case c(Resources);csol::scenario::ConfigError error;
            // Case already runs the real kick cold preparation and first update at
            // map time 100. A late reset would incorrectly erase that transport.
            Check(!CSOL_ActionStaminaCanKick({},c.player),"foreign runtime declines kick without manufacturing stamina");
            Require(CSOL_TickActionStamina(c.f.runtime,c.player,100,error),"actual registered live owner retains prepared stamina");
            Check(CSOL_ActionStaminaView(c.f.runtime,c.player)->amount==100,"same real Session/player starts original100");
            Check(!CSOL_DebitGuestCofPushbackStamina({},c.player,85)&&
                !CSOL_DebitGuestCofPushbackStamina(c.f.runtime,c.player,101)&&
                !CSOL_DebitGuestCofPushbackStamina(c.f.runtime,c.player,0)&&
                !CSOL_DebitGuestCofPushbackStamina(c.f.runtime,c.player,NAN),
                "guest cannot grant stamina, set arbitrary balances or use a foreign runtime");
            Require(CSOL_TryDebitKickStamina(c.f.runtime,c.player,1)==CSOLKickStaminaDebit::Applied,
                "real native kick debits the shared ledger first");
            Check(CSOL_DebitGuestCofPushbackStamina(c.f.runtime,c.player,75)&&
                CSOL_ActionStaminaView(c.f.runtime,c.player)->amount==75,
                "CoF push debits15 from the same native90 balance");
            Check(!CSOL_DebitGuestCofPushbackStamina(c.f.runtime,c.player,75),
                "stale requested balance cannot debit the same push twice");
            for(float requested:{60.F,45.F,30.F,15.F,0.F})
                Require(CSOL_DebitGuestCofPushbackStamina(c.f.runtime,c.player,requested),
                    "successive real CoF actions consume only the shared native ledger");
            Check(!CSOL_DebitGuestCofPushbackStamina(c.f.runtime,c.player,-15)&&
                CSOL_ActionStaminaView(c.f.runtime,c.player)->amount==0,
                "zero shared balance rejects CoF debit without an independent refill");
            c.f.players->PlayerSpawnStarted(EHANDLE(&c.player));++c.player.m_iNumSpawns;
            Require(CSOL_TickActionStamina(c.f.runtime,c.player,100,error),"new actual native life restores its own balance");
            // Leave a low positive balance: the original CoF positive gate
            // accepts it, while the existing CSOL ledger saturates at zero.
            for(float requested:{85.F,70.F,55.F,40.F,25.F,10.F})
                Require(CSOL_DebitGuestCofPushbackStamina(c.f.runtime,c.player,requested),"real finite guest debit");
            Check(CSOL_DebitGuestCofPushbackStamina(c.f.runtime,c.player,-5)&&
                CSOL_ActionStaminaView(c.f.runtime,c.player)->amount==0,
                "low positive CoF debit is accepted without making native stamina negative");
            c.f.players->PlayerSpawnStarted(EHANDLE(&c.player));++c.player.m_iNumSpawns;
            Require(CSOL_TickActionStamina(c.f.runtime,c.player,100,error),"separate life for existing native regression");
            for(std::uint64_t token=1;token<=10;++token)
            {
                Check(CSOL_TryDebitKickStamina(c.f.runtime,c.player,token)==CSOLKickStaminaDebit::Applied,"accepted action debit");
                Check(CSOL_TryDebitKickStamina(c.f.runtime,c.player,token)==CSOLKickStaminaDebit::Duplicate,"same action token cannot double debit");
            }
            Check(!CSOL_ActionStaminaCanKick(c.f.runtime,c.player)&&CSOL_TryDebitKickStamina(c.f.runtime,c.player,11)==CSOLKickStaminaDebit::Declined,"insufficient stamina has explicit decline");
            Require(CSOL_TickActionStamina(c.f.runtime,c.player,100,error),"same frame update never refills");
            Check(CSOL_ActionStaminaView(c.f.runtime,c.player)->amount==0,"weapon unchanged or switching never resets owner");
            CSOL_RetireActionStamina(c.player);
            Check(CSOL_TryDebitKickStamina(c.f.runtime,c.player,11)==CSOLKickStaminaDebit::Retired&&!CSOL_TickActionStamina(c.f.runtime,c.player,101,error),"retired native life remains a tombstone");
            c.f.players->PlayerSpawnStarted(EHANDLE(&c.player));++c.player.m_iNumSpawns;
            Require(CSOL_TickActionStamina(c.f.runtime,c.player,102,error),"actual native spawn serial begins separate stamina life");
            Check(CSOL_ActionStaminaView(c.f.runtime,c.player)->amount==100,"new genuine spawn receives max");
            Require(CSOL_TickActionStamina(c.f.runtime,c.player,102,error),"snapshot update alone remains finite");
            Check(!CSOL_ActionStaminaView(c.f.runtime,c.player)->infinite,"raw JSON cannot manufacture noon capability");
            Require(CSOL_SetChallengeStaminaPhase(c.f.runtime,custom::Phase::Noon,true),"explicit real phase-owner boundary");
            Require(CSOL_TickActionStamina(c.f.runtime,c.player,102,error),"consume explicit phase");
            Check(CSOL_ActionStaminaCanKick(c.f.runtime,c.player)&&CSOL_ActionStaminaView(c.f.runtime,c.player)->infinite,"noon remains kick-capable");
            Check(CSOL_TryDebitKickStamina(c.f.runtime,c.player,1)==CSOLKickStaminaDebit::Applied&&CSOL_ActionStaminaView(c.f.runtime,c.player)->amount==100,"accepted noon kick is infinite");
            Check(CSOL_DebitGuestCofPushbackStamina(c.f.runtime,c.player,85)&&
                CSOL_ActionStaminaView(c.f.runtime,c.player)->amount==100,
                "CoF shares the authoritative native noon infinity without an extra refill");
            Require(CSOL_SetChallengeStaminaPhase(c.f.runtime,custom::Phase::Dusk,true)&&CSOL_TickActionStamina(c.f.runtime,c.player,102,error),"explicit dusk phase");
            Check(CSOL_ActionStaminaView(c.f.runtime,c.player)->maximum==150&&CSOL_ActionStaminaView(c.f.runtime,c.player)->amount==100,"dusk expands without reset refill");
            auto* service=static_cast<csol::IGameplaySettingsService*>(CreateInterface(csol::GameplaySettingsServiceVersion,nullptr));
            Require(service!=nullptr,"actual production settings service exists");
            csol::GameplaySettingsStatus status;Require(service->Status(status),"real settings status");
            Require(service->Edit(1,service->UserId(1),status.revision,int(custom::Feature::TimeCycle),0)==csol::GameplayEditResult::Queued,"disable time via actual async service and store");
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
            while(std::chrono::steady_clock::now()<deadline)
            {
                CSOL_PollGameplaySettings();Require(service->Status(status),"real settings poll status");
                if(!status.busy)break;
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            Require(!status.busy&&status.result==csol::GameplayEditResult::Saved,"actual file persistence completed before next map");
            Require(CSOL_TickActionStamina(c.f.runtime,c.player,102,error),"saved next-map setting preserves current stamina owner");
            Check(!CSOL_CustomSettings()->Enabled(custom::Feature::TimeCycle)&&
                !CSOL_ActionStaminaView(c.f.runtime,c.player)->infinite&&
                CSOL_ActionStaminaView(c.f.runtime,c.player)->maximum==150,
                "saved time-off remains pending while this map retains its explicit dusk profile");
            c.player.has_disconnected=true;
            Check(!CSOL_DebitGuestCofPushbackStamina(c.f.runtime,c.player,85),"disconnected CoF action cannot consume a reused balance");
            Check(!CSOL_ActionStaminaCanKick(c.f.runtime,c.player)&&CSOL_TryDebitKickStamina(c.f.runtime,c.player,2)==CSOLKickStaminaDebit::Retired,"disconnected native owner cannot debit");
            c.player.has_disconnected=false;
        }
        {
            kick_test::Case next(Resources);csol::scenario::ConfigError error;
            Require(CSOL_TickActionStamina(next.f.runtime,next.player,100,error),"next registered world owns a new stamina life");
            const auto view=CSOL_ActionStaminaView(next.f.runtime,next.player);
            Check(view&&view->maximum==100&&view->amount==100&&!view->infinite,
                "persisted time-off takes effect on the next real world without granting unlimited noon");
        }
    }
    catch(const std::exception& error){++sdk_test::failures;std::cerr<<"ERROR "<<error.what()<<'\n';}
    std::cout<<"behaviorChecks=1 checks="<<sdk_test::checks<<" failures="<<sdk_test::failures<<'\n';return sdk_test::failures?1:0;
}
