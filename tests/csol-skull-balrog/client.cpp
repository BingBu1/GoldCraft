// SPDX-License-Identifier: GPL-3.0-or-later
// Execute the complete production client TU with recording SDK callbacks.
#ifdef TEST_BALROG11
#include "../../native/csol/client/balrog11.cpp"
namespace gun = csol::balrog11;
#define InitializeGun csol::client::InitializeBalrog11
#define ResetGun csol::client::ResetBalrog11
#define PredictGun csol::client::PredictBalrog11
#define PredictedAmmo csol::client::Balrog11PredictedAmmo
#define PredictedReserve csol::client::Balrog11PredictedReserve
#else
#include "../../native/csol/client/m249ex.cpp"
namespace gun = csol::m249ex;
#define InitializeGun csol::client::InitializeM249EX
#define ResetGun csol::client::ResetM249EX
#define PredictGun csol::client::PredictM249EX
#define PredictedAmmo csol::client::M249EXPredictedAmmo
#define PredictedReserve csol::client::M249EXPredictedReserve
#endif
#include <iostream>

using namespace csol;
using namespace csol::client;
namespace csol::client {
cl_enginefunc_t engine{};
cl_exportfuncs_t downstream{};
WeaponDefinition definition{gun::Id, 0, 1, 11, -1, gun::Reserve, -1, gun::Clip, 0, gun::Classname};
bool allowPresentation = true;
const WeaponDefinition* FindWeaponDefinition(int id) { return id == gun::Id ? &definition : nullptr; }
bool WeaponPresentationAllowed(int id) { return id == gun::Id && allowPresentation; }
bool Exists(const char*) { return false; }
std::vector<std::byte> ReadResource(const char*) { return {}; }
}
namespace {
int checks{}, failures{}, sounds{}, animations{}, lastAnimation{}, lastBody{}, pushes{}, pops{}, traces{}, particles{}, shells{};
float clientWeapons = 1;
float shellOrigin[3]{};
float angleInput[3]{};
int angleCalls{};
std::string soundPath, hookPath, shellPath;
void (*eventHook)(event_args_t*){};
event_api_t events{};
efx_api_t effectsApi{};
cl_entity_t local{}, model{};
TEMPENTITY temporary{};
physent_t physical{};
bool hitWorld{};
void Check(bool ok, const char* message)
{
    ++checks;
    if (!ok) { ++failures; std::cerr << "FAIL " << message << '\n'; }
}
bool Near(float a, float b) { return std::abs(a - b) < .0001f; }
void Setup()
{
    local.index = 1;
    model.curstate.body = 7;
    model.attachment[1][0] = 10;
    model.attachment[1][1] = 20;
    model.attachment[1][2] = 30;
    engine.pfnGetCvarFloat = [](const char* name) { return !std::strcmp(name, "cl_lw") ? clientWeapons : 1.f; };
    engine.pfnAngleVectors = [](const float* angles, float* f, float* r, float* u)
    { ++angleCalls; std::copy_n(angles,3,angleInput); f[0] = 1; f[1] = f[2] = 0; r[1] = -1; r[0] = r[2] = 0; u[2] = 1; u[0] = u[1] = 0; };
    // Deliberately do not update curstate.sequence: GoldSrc commits it later.
    engine.pfnWeaponAnim = [](int sequence, int body) { ++animations; lastAnimation = sequence; lastBody = body; };
    engine.GetViewModel = [] { return &model; };
    engine.GetLocalPlayer = [] { return &local; };
    engine.pfnRandomLong = [](int low, int) { return low; };
    engine.pfnRandomFloat = [](float low, float high) { return (low + high) * .5f; };
    engine.pfnHookEvent = [](const char* name, void (*callback)(event_args_t*)) { hookPath = name; eventHook = callback; };
    engine.pEventAPI = &events;
    engine.pEfxAPI = &effectsApi;
    downstream.HUD_PostRunCmd = [](local_state_t* from, local_state_t* to, usercmd_t*, int, double, unsigned)
    {
        Check(from->client.m_iId == 0, "extended ID hidden while stock predictor runs");
        *to = *from;
        to->client.weaponanim = 99;
        to->client.viewmodel = 99;
        to->client.vuser4[1] = 99;
    };
    events.EV_IsLocal = [](int index) { return int(index == 0); };
    events.EV_FindModelIndex = [](const char* path) { shellPath = path; return 15; };
    events.EV_PlaySound = [](int, float*, int, const char* path, float, float, int, int) { ++sounds; soundPath = path; };
    events.EV_LocalPlayerViewheight = [](float* h) { h[0] = h[1] = 0; h[2] = 28; };
    events.EV_SetUpPlayerPrediction = [](int, int) {};
    events.EV_PushPMStates = [] { ++pushes; };
    events.EV_PopPMStates = [] { ++pops; };
    events.EV_SetSolidPlayers = [](int) {};
    events.EV_SetTraceHull = [](int hull) { Check(hull == 2, "bullet point hull"); };
    events.EV_PlayerTrace = [](float* start, float* end, int, int, pmtrace_t* trace)
    {
        ++traces;
        trace->fraction = hitWorld ? .25f : 1.f;
        trace->ent = 0;
        for (int i = 0; i < 3; ++i) trace->endpos[i] = start[i] + (end[i] - start[i]) * trace->fraction;
    };
    events.EV_GetPhysent = [](int) { return &physical; };
    effectsApi.R_BulletImpactParticles = [](float*) { ++particles; };
    effectsApi.R_TempModel = [](float* origin, float*, float*, float, int, int) -> TEMPENTITY*
    { ++shells; std::copy_n(origin, 3, shellOrigin); return &temporary; };
    InitializeGun();
    Check(hookPath == gun::Event && eventHook, "own original event registered");
}
local_state_t State(int clip = gun::Clip, int reserve = gun::Reserve)
{
    local_state_t state{};
    state.client.m_iId = gun::Id;
    state.client.health = 100;
    state.client.iuser3 = 1;
    state.client.flags = FL_ONGROUND;
    state.client.fov = 90;
    state.client.viewmodel = 12;
    state.client.weaponanim = gun::Draw;
    state.client.vuser4[1] = float(reserve);
    auto& data = state.weapondata[ActiveWeaponSlot];
    data.m_iId = gun::Id;
    data.m_iClip = clip;
    data.fuser1 = .25f;
    data.m_flNextPrimaryAttack = data.m_flNextSecondaryAttack = data.m_flTimeWeaponIdle = -.001f;
    return state;
}
local_state_t Predict(local_state_t state, int buttons, int run = 1, int milliseconds = 10)
{
    local_state_t output{};
    usercmd_t command{};
    command.buttons = short(buttons);
    command.msec = byte(milliseconds);
    command.viewangles[0] = 12; command.viewangles[1] = 37; command.viewangles[2] = -2;
    Check(PredictGun(&state, &output, &command, run, 0, 123), "custom weapon handled");
    Check(state.client.m_iId == gun::Id && output.client.m_iId == gun::Id, "weapon identity restored");
    Check(output.client.viewmodel == 12, "view model preserved");
    return output;
}
}
int main()
{
    Setup();
    auto result = Predict(State(), IN_ATTACK);
    Check(result.weapondata[0].m_iClip == gun::Clip - 1 && PredictedAmmo() == gun::Clip - 1 && PredictedReserve() == gun::Reserve,
        "one primary shot consumes one clip round and preserves reserve");
    Check(lastBody == 7 && (lastAnimation == 1 || lastAnimation == 2), "shot keeps chosen character bodygroup");
    Check(Near(shellOrigin[0], 10) && Near(shellOrigin[1], 20) && Near(shellOrigin[2], 30), "local shell uses attachment1");
    Check(pushes == pops, "trace prediction stack balanced");
    const int animationsBefore = animations, soundsBefore = sounds;
    Predict(result, IN_ATTACK);
    Check(animations == animationsBefore && sounds == soundsBefore, "cooldown with stale render sequence does not replay attack");
    auto replay = Predict(State(), IN_ATTACK, 0);
    Check(replay.weapondata[0].m_iClip == gun::Clip - 1 && animations == animationsBefore && sounds == soundsBefore,
        "prediction replay changes state without replaying effects");
    auto blocked = State(); blocked.client.health = 0;
    Check(Predict(blocked, IN_ATTACK).weapondata[0].m_iClip == gun::Clip, "dead player cannot fire");
    blocked = State(); blocked.client.iuser3 = 0;
    Check(Predict(blocked, IN_ATTACK).weapondata[0].m_iClip == gun::Clip, "round attack lock blocks fire");
    blocked = State(); blocked.client.m_flNextAttack = .5f;
    Check(Predict(blocked, IN_ATTACK).weapondata[0].m_iClip == gun::Clip, "deploy delay blocks fire");
#ifdef TEST_BALROG11
    Check(sounds == soundsBefore && traces == 6 && shellPath == "models/shotgunshell.mdl", "primary event leaves sound to server and emits six pellet traces");
    result = State();
    for (int i = 1; i <= 4; ++i)
    {
        result.weapondata[0].m_flNextPrimaryAttack = result.weapondata[0].m_flNextSecondaryAttack = -.001f;
        result = Predict(result, IN_ATTACK);
        Check(gun::UnpackCharge(result.weapondata[0].m_iWeaponState).charges == (i == 4 ? 1 : 0), "charge appears only on fourth continuous shot");
    }
    Check(gun::UnpackCharge(result.weapondata[0].m_iWeaponState).consecutiveShots == 0 && Balrog11PredictedCharges() == 1, "fourth shot resets counter and exports charge");
    result = Predict(result, 0);
    Check(gun::UnpackCharge(result.weapondata[0].m_iWeaponState).charges == 1, "releasing primary keeps earned charge");
    result.weapondata[0].m_flNextSecondaryAttack = -.001f;
    const int traceBefore = traces;
    result = Predict(result, IN_ATTACK2);
    Check(gun::UnpackCharge(result.weapondata[0].m_iWeaponState).charges == 0 && result.weapondata[0].m_iClip == 3, "flame consumes charge without clip round");
    Check(soundPath == "weapons/balrog11-2.wav" && shellPath == "models/shell_bcs.mdl" && traces == traceBefore,
        "flame uses independent sound and shell with no pellet trace");
    Check(lastAnimation == gun::ShootFlame && Near(result.weapondata[0].m_flNextPrimaryAttack, .34f), "flame animation and attack cooldown");
    result = State(); result.weapondata[0].m_iWeaponState = gun::PackCharge({3, 0});
    result = Predict(result, IN_ATTACK2);
    Check(gun::UnpackCharge(result.weapondata[0].m_iWeaponState).consecutiveShots == 0 && gun::UnpackCharge(result.weapondata[0].m_iWeaponState).charges == 0, "empty flame trigger resets primary streak");
    result = State(); result.weapondata[0].m_iWeaponState = gun::PackCharge({3, 7});
    result = Predict(result, IN_ATTACK);
    Check(gun::UnpackCharge(result.weapondata[0].m_iWeaponState).consecutiveShots == 0 && gun::UnpackCharge(result.weapondata[0].m_iWeaponState).charges == 7, "full charge magazine caps at seven and resets streak");
    result = State(); result.weapondata[0].m_iWeaponState = gun::PackCharge({3, 0});
    Check(Predict(result, 0).weapondata[0].iuser1 == 0, "idle breaks continuous shot streak");
    result = Predict(State(5, 1), IN_RELOAD);
    Check(result.client.weaponanim == gun::StartReload && result.weapondata[0].m_fInSpecialReload == 1 &&
        Near(result.client.m_flNextAttack, .465f) && result.weapondata[0].m_iClip == 5, "reload starts without granting ammo");
    result.client.m_flNextAttack = 0;
    result.weapondata[0].m_flNextPrimaryAttack = result.weapondata[0].m_flTimeWeaponIdle = -.001f;
    result = Predict(result, 0);
    Check(result.client.weaponanim == gun::Insert && result.weapondata[0].m_fInSpecialReload == 2 &&
        result.weapondata[0].m_iClip == 5, "insert waits for animation deadline");
    result.weapondata[0].m_flTimeWeaponIdle = -.001f;
    result = Predict(result, 0);
    Check(result.weapondata[0].m_iClip == 6 && result.client.vuser4[1] == 0, "one insertion consumes one reserve");
    result = Predict(result, 0);
    Check(result.client.weaponanim == gun::AfterReload && !result.weapondata[0].m_fInSpecialReload, "empty reserve ends reload once");
    result = State(); result.client.waterlevel = 3;
    Check(Predict(result, IN_ATTACK).weapondata[0].m_iClip == gun::Clip, "underwater primary blocked");
    for(float punch : {-12.f,-3.25f,0.f,5.f,11.f}) for(int button : {IN_ATTACK,IN_ATTACK2})
    {
        auto aiming=State();aiming.client.punchangle[0]=punch;
        aiming.weapondata[0].m_iWeaponState=gun::PackCharge({0,1});
        const int before=angleCalls;
        Predict(aiming,button);
        Check(angleCalls==before+1,"both predicted fire paths reach the real event AngleVectors boundary");
        Check(Near(angleInput[0],12.07f)&&Near(angleInput[1],37.f)&&Near(angleInput[2],-2.f),
            "weapon event has original pitch encoding and zero weapon yaw independent of player punch pitch");
    }
#else
    Check(Near(result.weapondata[0].m_flNextPrimaryAttack, .115f) && Near(result.weapondata[0].fuser1, .30526316f), "original interval and accuracy");
    result = Predict(State(), IN_ATTACK2);
    Check(result.client.fov == 65 && result.weapondata[0].m_iClip == gun::Clip && soundPath == "weapons/zoom.wav", "zoom changes FOV without consuming ammo");
    result.weapondata[0].m_flNextPrimaryAttack = -.001f;
    result = Predict(result, IN_ATTACK);
    Check(Near(result.weapondata[0].m_flNextPrimaryAttack, .14f), "zoom uses slower original shot interval");
    result = State(100, 5); result.client.fov = 65;
    result = Predict(result, IN_RELOAD);
    Check(result.weapondata[0].m_fInReload && result.client.fov == 90 && Near(result.client.m_flNextAttack, 3.79f), "reload exits zoom and waits full duration");
    result.client.m_flNextAttack = 0;
    result = Predict(result, 0);
    Check(result.weapondata[0].m_iClip == 105 && result.client.vuser4[1] == 0 && !result.weapondata[0].m_fInReload, "partial reserve reload exact count");
    Check(!Predict(State(gun::Clip, 100), IN_RELOAD).weapondata[0].m_fInReload, "full clip cannot reload");
    Check(Predict(State(0, 5), 0).weapondata[0].m_fInReload, "empty clip auto reloads");
#endif
    clientWeapons = 0;
    const int beforeAuthority = sounds;
    Check(Predict(State(), IN_ATTACK).weapondata[0].m_iClip == gun::Clip && sounds == beforeAuthority,
        "disabled prediction waits for authoritative event");
    event_args_t args{}; args.entindex = 1; args.fparam1 = 1; args.fparam2 = .0735f; args.bparam2 = 6;
    allowPresentation = false;
    const int beforeLate = animations;
    eventHook(&args);
    Check(animations == beforeLate && !Near(shellOrigin[0], 10), "late event cannot animate or eject from new weapon viewmodel");
    allowPresentation = true; args.entindex = 2;
    eventHook(&args);
    Check(animations == beforeLate, "remote event cannot animate local gun");
    hitWorld = true; physical.solid = SOLID_SLIDEBOX; physical.studiomodel = reinterpret_cast<model_t*>(1);
    eventHook(&args);
    Check(particles == 0 && pushes == pops, "monster impacts never emit wall sparks");
#ifdef TEST_BALROG11
    args.angles[0]=10;args.angles[1]=20;args.angles[2]=30;args.iparam1=123;args.iparam2=-456;
    eventHook(&args);
    Check(Near(angleInput[0],11.23f)&&Near(angleInput[1],15.44f)&&Near(angleInput[2],30.f),
        "remote event still consumes explicit iparam1 pitch and iparam2 yaw independently");
#endif
    ResetGun();
    Check(PredictedAmmo() == -1 && PredictedReserve() == -1, "reset discards prediction HUD values");
    std::cout << gun::Classname << " checks=" << checks << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
