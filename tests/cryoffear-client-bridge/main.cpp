// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/weapons/guest_cof.hpp"
#include "../../native/csol/client/weapons/guest_cof_effects.hpp"
#include "../../native/guest/cof/presentation_protocol.hpp"
#include "../../native/guest/cof/catalog.hpp"
#include "../../native/include/goldcraft/connection_api.h"

#include <cdll_int.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <type_traits>

namespace csol::client
{
metahook_api_t* api{};
cl_enginefunc_t engine{};
cl_exportfuncs_t downstream{};
engine_studio_api_t studio{};
ref_params_t view{};
static unsigned sceneResets{};
void ClearScene() { ++sceneResets; }
void ResetWeapons(bool) {}
void ResetHudStyle() {}
void ResetCamera() {}
void ResetProjectiles() {}
void ResetCharacters() {}
void ResetStatusIcons() {}
void ResetWallStatus() {}
void ResetBuyMenu() {}
void ResetScenarioMapVote() {}
void ResetDamageDisplay() {}
void ResetSpray() {}
void ResetScenarioKill() {}
void ResetScenarioEffects() {}
void ResetCharacterSkillEffects() {}
void ResetScenarioSmoke() {}
void ResetEliteVisuals() {}
void ResetScenarioUpgrades() {}
void ResetScenarioAbilities() {}
void ResetBossHealth() {}
void ResetScenarioRoundHud() {}
void ResetZeCounterHud() {}
void ResetHumanKick() {}
void ResetScenarioRespawnUi() {}
void ResetPlayerClips() {}
void ResetRenderSettings() {}
void ResetGuestCofCrosshair() noexcept{}
void GuestCofCrosshairPresentation(const guest::cof::PresentationState&) noexcept{}
void ResetResourceSession();
// The fixture has no matched host catalogue. Missing resolution must leave the
// real ironsight adapter inactive, before any native instruction hook request.
hook_t* Hook(void*, void*, void**) { std::abort(); }
void RemoveHook(hook_t*) { std::abort(); }
}

namespace
{
using namespace csol::client;
using namespace guest::cof;
unsigned checks{}, failures{}, lookups{}, registrations{}, downstreamCalls{};
unsigned missingConnectionWarnings{};
std::uint64_t nativeConnectionGeneration = 5;
float clockTime{1.0f}, nativeFov{90.0f}, receivedTime{};
cl_entity_t cylinderView{}, cylinderOther{};
cl_entity_t* currentView = &cylinderView;
bool optionalPresent{};
const char* levelName = "maps/cof_bridge_test.bsp";
pfnUserMsgHook installed{};
cvar_t defaultFov{}, sensitivity{}, zoomRatio{}, zoomSpeed{}, multiplier{};

void Check(bool condition, int line)
{
    ++checks;
    if (!condition) { ++failures; std::printf("failed line %d\n", line); }
}
#define CHECK(x) Check((x), __LINE__)

cvar_t* Lookup(const char* name)
{
    ++lookups;
    if (!std::strcmp(name, "default_fov")) return &defaultFov;
    if (!std::strcmp(name, "sensitivity")) return &sensitivity;
    if (!std::strcmp(name, "zoom_sensitivity_ratio")) return &zoomRatio;
    if (optionalPresent && !std::strcmp(name, "cl_zoomspeed")) return &zoomSpeed;
    if (optionalPresent && !std::strcmp(name, "cl_fovmultiplier")) return &multiplier;
    return nullptr;
}

cvar_t* Register(const char* name, const char* value, int flags)
{
    ++registrations;
    if (!std::strcmp(name, "cl_zoomspeed"))
    {
        CHECK(!std::strcmp(value, "100") && flags == 0);
        return &zoomSpeed;
    }
    CHECK(!std::strcmp(name, "cl_fovmultiplier"));
    CHECK(!std::strcmp(value, "1") && flags == FCVAR_ARCHIVE);
    return &multiplier;
}

pfnUserMsgHook Hook(const char* name, pfnUserMsgHook replacement)
{
    CHECK(!std::strcmp(name, StateMessage));
    const auto previous = installed;
    if (previous) installed = replacement;
    return previous;
}

int RegisterMessage(const char* name, pfnUserMsgHook replacement)
{
    CHECK(!std::strcmp(name, StateMessage));
    CHECK(!installed);
    installed = replacement;
    return 1;
}

float Clock() { return clockTime; }
cl_entity_t* ViewModel() { return currentView; }
std::uint64_t ConnectionGeneration() { return nativeConnectionGeneration; }
const char* LevelName() { return levelName; }
void Console(const char*, ...) { ++missingConnectionWarnings; }
int NativeData(client_data_t* data, float time)
{
    ++downstreamCalls;
    receivedTime = time;
    if (data) data->fov = nativeFov;
    return 37;
}
int OtherConsumer(const char*, int, void*) { return 1; }

void Deliver(PresentationState state)
{
    if (state.revolverAngle)
    {
        RevolverStatePacket bytes{};
        CHECK(EncodeState(state,bytes));
        CHECK(installed && installed(StateMessage,int(bytes.size()),bytes.data())==1);
        return;
    }
    StatePacket bytes{};
    CHECK(EncodeState(state, bytes));
    CHECK(installed && installed(StateMessage, int(bytes.size()), bytes.data()) == 1);
}

float Frame(float now, float native = 90.0f)
{
    clockTime = now;
    nativeFov = native;
    client_data_t data{};
    data.fov = -998.0f;
    const auto before = downstreamCalls;
    CHECK(GuestCofUpdateClientData(&data, now + 0.5f) == 37);
    CHECK(downstreamCalls == before + 1 && receivedTime == now + 0.5f);
    return data.fov;
}
}

int main()
{
    using namespace csol::client;
    static_assert(std::is_same_v<decltype(&GuestCofUpdateClientData), HUD_UPDATECLIENTDATA_FUNC>);
    metahook_api_t host{};
    host.HookUserMsg = Hook;
    api = &host;
    engine.pfnGetCvarPointer = Lookup;
    engine.pfnRegisterVariable = Register;
    engine.pfnHookUserMsg = RegisterMessage;
    engine.GetClientTime = Clock;
    engine.GetViewModel = ViewModel;
    engine.pfnGetLevelName = LevelName;
    engine.Con_Printf = Console;
    downstream.HUD_UpdateClientData = NativeData;
    defaultFov.value = 90;
    sensitivity.value = 2;
    zoomRatio.value = 1.2f;
    zoomSpeed.value = 100;
    multiplier.value = 1;
    const GoldCraftConnectionAPI connection{sizeof(GoldCraftConnectionAPI), 1, ConnectionGeneration};
    // The real effects adapter cannot prepare without an installed matched
    // host patch/event consumer; FOV authority remains independently usable.
    CHECK(!InitializeGuestCofEffects(&connection));
    CHECK(!PrepareGuestCofEffects() && !GuestCofEffectsReady());
    InitializeGuestCof(&connection);
    CHECK(GuestCofConnectionReady());
    CHECK(registrations == 2 && lookups == 5 && installed);
    CHECK(Frame(1, 110) == 110);

    PresentationState state{{1, 1, 1}, 1, 3, true, 2, 0};
    Deliver(state);
    CHECK(Frame(1) == 90);
    CHECK(Frame(1.125f, 30) == 77.5f); // Native scope output cannot overwrite CoF ownership.
    CHECK(lookups == 5 && registrations == 2); // No hot cvar discovery.

    state.key.generation = 2;
    state.originalWeapon = 7;
    Deliver(state);
    CHECK(Frame(1.25f) == 65); // Same-life weapon change preserves FOV interpolation.
    CHECK(Frame(1.375f) == 60);
    GuestCofVideoInit();
    CHECK(Frame(1.5f) == 60); // Video reset retains server FOV mode.

    const auto obsolete = state;
    ++state.revision;
    state.originalWeapon = 0;
    state.aimEnabled = false;
    Deliver(state);
    CHECK(Frame(1.625f, 40) == 40); // Explicit release preserves native CS sniper FOV.
    Deliver(obsolete);
    CHECK(Frame(1.75f, 35) == 35);

    state.key.lifeEpoch = 2;
    state.key.generation = 1;
    state.revision = 1;
    state.originalWeapon = 21;
    state.revolverAngle = 72.0F;
    state.aimEnabled = true;
    Deliver(state);
    CHECK(Frame(2, 100) == 100); // New life starts from the actual downstream value.
    auto malformed = StatePacket{};
    CHECK(installed(StateMessage, int(malformed.size()), malformed.data()) == 1);
    CHECK(installed(StateMessage, -1, malformed.data()) == 1);
    CHECK(installed(StateMessage, 24, nullptr) == 1);
    CHECK(Frame(2.125f) < 100);
    auto previousLife = state;
    previousLife.key.lifeEpoch = 1;
    previousLife.revision = 900;
    previousLife.originalWeapon = 0;
    previousLife.aimEnabled = false;
    previousLife.revolverAngle.reset();
    Deliver(previousLife);
    CHECK(Frame(2.25f, 115) < 100);

    ResetGuestCof();
    Deliver({{1, 1, 1}, 1, 19, true, 3, 0});
    zoomSpeed.value = std::numeric_limits<float>::infinity();
    CHECK(Frame(3, 75) == 75);
    zoomSpeed.value = 100;
    CHECK(Frame(3.125f, 75) == 75); // Invalid settings did not consume the state.
    CHECK(Frame(3.25f, 75) == 37.5f);
    const auto priorSceneResets = sceneResets;
    levelName = "maps/cof_bridge_next.bsp";
    CHECK(Frame(3.375f, 95) == 95); // Retired before the downstream callback.
    CHECK(sceneResets == priorSceneResets + 1);
    Deliver({{1, 1, 1}, 1, 19, true, 3, 0});
    CHECK(Frame(3.5f, 85) == 85);
    ResetResourceSession(); // Same-map reconnect is a real resource-overlay boundary.
    CHECK(Frame(3.625f, 98) == 98);

    // Empty-overlay reconnect has no ResetResourceSession call and may never
    // expose an empty map name between callbacks. The real core epoch is the
    // independent boundary, while VideoInit alone keeps ordinary FOV state.
    Deliver({{50, 9, 4}, 1, 19, true, 3, 0});
    CHECK(Frame(4, 30) == 30);
    GuestCofVideoInit();
    CHECK(Frame(4.125f, 90) == 30);
    ++nativeConnectionGeneration;
    CHECK(Frame(4.25f, 90) == 90);
    Deliver({{1, 1, 1}, 1, 7, true, 2, 0});
    CHECK(Frame(4.375f, 85) == 85); // New connection can restart protocol epochs.
    CHECK(Frame(4.5f) < 85);
    nativeConnectionGeneration = 0;
    CHECK(!GuestCofConnectionReady());
    CHECK(Frame(4.625f, 90) == 90);
    ResetGuestCof();
    CHECK(GuestCofUpdateClientData(nullptr, 4.75f) == 37);

    optionalPresent = true;
    const auto priorRegistrations = registrations;
    nativeConnectionGeneration = 7;
    InitializeGuestCof(&connection); // Same consumer is safe; no duplicate cvars.
    CHECK(registrations == priorRegistrations);
    CHECK(Frame(4, 88) == 88);

    ResetGuestCof();
    const int revolverId=guest::cof::FindPistol(guest::cof::Pistol::Revolver)->hostId;
    const PresentationState cylinder{{10,11,12},1,21,false,0,0,72.0F};
    cylinderView.curstate.controller[3]=19;
    cylinderView.latched.prevcontroller[3]=27;
    CHECK(!GuestCofPrepareStudioModel(&cylinderView,revolverId));
    Deliver(cylinder);
    CHECK(!GuestCofPrepareStudioModel(nullptr,revolverId));
    CHECK(!GuestCofPrepareStudioModel(&cylinderOther,revolverId));
    CHECK(!GuestCofPrepareStudioModel(&cylinderView,896));
    CHECK(GuestCofPrepareStudioModel(&cylinderView,revolverId));
    for (unsigned i=0;i<3;++i)
        CHECK(cylinderView.curstate.controller[i]==204 && cylinderView.latched.prevcontroller[i]==204);
    CHECK(cylinderView.curstate.controller[3]==19 && cylinderView.latched.prevcontroller[3]==27);
    auto late=cylinder;late.revolverAngle=144.0F;
    Deliver(late); // Same revision is stale even if its angle changed.
    CHECK(GuestCofPrepareStudioModel(&cylinderView,revolverId) && cylinderView.curstate.controller[0]==204);
    late.revision++;Deliver(late);
    CHECK(GuestCofPrepareStudioModel(&cylinderView,revolverId) && cylinderView.curstate.controller[0]==153);
    ++nativeConnectionGeneration;
    CHECK(!GuestCofPrepareStudioModel(&cylinderView,revolverId)); // Reject before a HUD update resets cache.
    CHECK(Frame(4.125F,88)==88);
    CHECK(!GuestCofPrepareStudioModel(&cylinderView,revolverId));
    Deliver(cylinder);
    currentView=nullptr;CHECK(!GuestCofPrepareStudioModel(&cylinderView,revolverId));currentView=&cylinderView;
    CHECK(GuestCofPrepareStudioModel(&cylinderView,revolverId));
    Deliver({cylinder.key,2,0,false,0,0});
    CHECK(!GuestCofPrepareStudioModel(&cylinderView,revolverId));
    Deliver(cylinder);CHECK(!GuestCofPrepareStudioModel(&cylinderView,revolverId));
    late=cylinder;late.key.lifeEpoch++;late.revision=1;Deliver(late);
    CHECK(GuestCofPrepareStudioModel(&cylinderView,revolverId));
    GuestCofVideoInit();CHECK(GuestCofPrepareStudioModel(&cylinderView,revolverId));
    ResetGuestCof();CHECK(!GuestCofPrepareStudioModel(&cylinderView,revolverId));
    installed = OtherConsumer;
    bool collision{};
    try { InitializeGuestCof(&connection); } catch (const std::runtime_error&) { collision = true; }
    CHECK(collision && installed == OtherConsumer); // Preserve another message owner's handler.

    installed = nullptr;
    InitializeGuestCof(nullptr);
    CHECK(!GuestCofConnectionReady());
    Deliver({{1, 1, 1}, 1, 7, true, 2, 0});
    Deliver({{1, 1, 1}, 2, 7, true, 2, 0});
    CHECK(Frame(5, 95) == 95 && missingConnectionWarnings == 1);
    ShutdownGuestCof();
    ShutdownGuestCofEffects();
    CHECK(!GuestCofConnectionReady() && Frame(5.125f, 80) == 80);
    std::printf("checks=%u failures=%u\n", checks, failures);
    return failures ? 1 : 0;
}
