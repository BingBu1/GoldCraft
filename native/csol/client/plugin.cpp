// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "weapons.hpp"
#include "camera.hpp"
#include "projectiles.hpp"
#include "death_notices.hpp"
#include "weapon_inventory.hpp"
#include "characters.hpp"
#include "player_animation.hpp"
#include <ostream>
#include <cstring>
#include <cmath>
#include "stock_hands.hpp"
#include <pm_defs.h>
#include "../player_collision.hpp"

namespace csol::client
{
metahook_api_t* api{};
mh_interface_t* interfaces{};
cl_enginefunc_t engine{};
cl_exportfuncs_t downstream{};
engine_studio_api_t studio{};
r_studio_interface_t* studioInterface{};
ref_params_t view{};
bool glewReady{};
static std::vector<hook_t*> hooks;
static int (*originalDrawModel)(int);
static int (*originalDrawPlayer)(int,entity_state_t*);
static unsigned playerDraws{},playerAliases{};
static int playerSequence=-1,playerGait=-1;
static int renderedPlayerSequence=-1;
static std::string renderedPlayerLabel;
static bool animationProbe{};
static cvar_t* studioBoneCache{};
static std::array<std::array<float,3>,3> playerPose{};
static std::array<float,3> leftHandInRight{};
static studiohdr_t* observedPlayerHeader{};
static Bones observedPlayerBones{};
static int observedPlayerFrame=-1,heldMatches{},heldErrorBone=-1,playerCacheId{},heldCacheId{};
static float heldError{};
static std::string observedPlayerName,observedHeldName;
static std::array<std::array<float,3>,MAXSTUDIOBONES> heldPose{};
static int heldBoneCount{};
static void ObserveHeldPose()
{
    if(!animationProbe || !currentStudioHeader)return;
    auto* entity=studio.GetCurrentEntity();
    if(!entity || !entity->player || entity->index!=view.playernum+1)return;
    int frame{};double now{},old{};studio.GetTimes(&frame,&now,&old);
    auto* h=static_cast<studiohdr_t*>(currentStudioHeader);
    if(h->numbones<1 || h->numbones>MAXSTUDIOBONES)return;
    const auto* matrices=reinterpret_cast<const Bones*>(studio.StudioGetBoneTransform());
    auto* player=studio.SetupPlayerModel(entity->index-1);
    if(player && studio.Mod_Extradata(player)==h) {
        observedPlayerHeader=h;observedPlayerFrame=frame;observedPlayerBones=*matrices;
        const auto* bones=reinterpret_cast<mstudiobone_t*>(reinterpret_cast<char*>(h)+h->boneindex);
        int left=-1,right=-1;
        for(int i=0;i<h->numbones;++i) {
            if(!std::strncmp(bones[i].name,"Bip01 L Hand",32))left=i;
            if(!std::strncmp(bones[i].name,"Bip01 R Hand",32))right=i;
        }
        leftHandInRight={};
        if(left>=0 && right>=0)for(int a=0;a<3;++a)for(int b=0;b<3;++b)
            leftHandInRight[a]+=(*matrices)[right][b][a]*((*matrices)[left][b][3]-(*matrices)[right][b][3]);
        renderedPlayerSequence=entity->curstate.sequence;
        if(renderedPlayerSequence>=0 && renderedPlayerSequence<h->numseq) {
            const auto* sequence=reinterpret_cast<mstudioseqdesc_t*>(reinterpret_cast<char*>(h)+h->seqindex);
            renderedPlayerLabel.assign(sequence[renderedPlayerSequence].label,strnlen(sequence[renderedPlayerSequence].label,32));
        }
        observedPlayerName=player->name;playerCacheId=h->soundtable;return;
    }
    auto* held=studio.GetModelByIndex(entity->curstate.weaponmodel);
    if(!held || studio.Mod_Extradata(held)!=h || observedPlayerFrame!=frame || !observedPlayerHeader)return;
    heldMatches=0;heldError=0;heldErrorBone=-1;observedHeldName=held->name;heldCacheId=h->soundtable;heldBoneCount=h->numbones;
    const auto* pb=reinterpret_cast<mstudiobone_t*>(reinterpret_cast<char*>(observedPlayerHeader)+observedPlayerHeader->boneindex);
    const auto* wb=reinterpret_cast<mstudiobone_t*>(reinterpret_cast<char*>(h)+h->boneindex);
    for(int i=0;i<h->numbones;++i) {
        for(int a=0;a<3;++a)heldPose[i][a]=(*matrices)[i][a][3]-entity->origin[a];
        for(int j=0;j<observedPlayerHeader->numbones;++j)if(!_strnicmp(wb[i].name,pb[j].name,32)) {
            ++heldMatches;
            for(int a=0;a<3;++a)for(int b=0;b<4;++b) {
                const float delta=std::abs((*matrices)[i][a][b]-observedPlayerBones[j][a][b]);
                if(delta>heldError){heldError=delta;heldErrorBone=i;}
            }
            break;
        }
    }
}
static void ObservePlayerPose()
{
    if(!animationProbe || !currentStudioHeader || !currentStudioModel)return;
    auto* entity=studio.GetCurrentEntity();
    if(!entity || !entity->player || entity->index!=view.playernum+1 ||
        !std::string_view(currentStudioModel->name,strnlen(currentStudioModel->name,sizeof(currentStudioModel->name))).starts_with("models/player/csol") ||
        studio.Mod_Extradata(currentStudioModel)!=currentStudioHeader)return;
    auto* h=static_cast<studiohdr_t*>(currentStudioHeader);
    const auto* bones=reinterpret_cast<mstudiobone_t*>(static_cast<char*>(currentStudioHeader)+h->boneindex);
    const char* names[]{"Bip01 L Foot","Bip01 R Foot","Bip01 R Hand"};
    const auto* matrices=reinterpret_cast<const Bones*>(studio.StudioGetBoneTransform());
    for(int i=0;i<h->numbones && i<MAXSTUDIOBONES;++i)
        for(int j=0;j<3;++j)if(!std::strncmp(bones[i].name,names[j],sizeof(bones[i].name)))
            for(int a=0;a<3;++a)playerPose[j][a]=(*matrices)[i][a][3]-entity->origin[a];
}
static int StudioDrawPlayer(int flags,entity_state_t* player)
{
    auto* entity=studio.GetCurrentEntity();
    if(!entity || !player || !entity->player || player->number<1 || player->number>32)
        return originalDrawPlayer(flags,player);
    auto* model=studio.SetupPlayerModel(player->number-1);
    const auto* minmodels=engine.pfnGetCvarPointer("cl_minmodels");
    if(!model || (minmodels && minmodels->value!=0) ||
        !std::string_view(model->name,strnlen(model->name,sizeof(model->name))).starts_with("models/player/csol"))
        return originalDrawPlayer(flags,player);
    ++playerDraws;playerSequence=entity->curstate.sequence;playerGait=player->gaitsequence;
    if(!studioBoneCache)studioBoneCache=engine.pfnGetCvarPointer("r_studio_bone_caches");
    PlayerBoneCacheScope boneCache(studioBoneCache);
    PlayerSequenceScope sequence(static_cast<studiohdr_t*>(studio.Mod_Extradata(model)),entity,player);
    if(sequence.Mapped())++playerAliases;
    return originalDrawPlayer(flags,player);
}
void PlayerAnimationDiagnostics(std::ostream& out)
{
    out<<",\"playerAnimation\":{\"draws\":"<<playerDraws<<",\"mapped\":"<<playerAliases
        <<",\"sequence\":"<<playerSequence<<",\"gait\":"<<playerGait<<",\"pose\":[";
    for(int i=0;i<3;++i){if(i)out<<',';out<<'['<<playerPose[i][0]<<','<<playerPose[i][1]<<','<<playerPose[i][2]<<']';}
    out<<"],\"leftHandInRight\":["<<leftHandInRight[0]<<','<<leftHandInRight[1]<<','<<leftHandInRight[2]
        <<"],\"renderSequence\":"<<renderedPlayerSequence<<",\"renderLabel\":\""<<renderedPlayerLabel
        <<"\",\"model\":\""<<observedPlayerName<<"\",\"held\":\""<<observedHeldName<<"\",\"matches\":"<<heldMatches
        <<",\"mergeError\":"<<heldError<<",\"errorBone\":"<<heldErrorBone<<",\"playerCache\":"<<playerCacheId<<",\"heldCache\":"<<heldCacheId
        <<",\"cacheAvailable\":"<<bool(studioBoneCache)<<",\"cacheValue\":"<<(studioBoneCache?studioBoneCache->value:-1)<<",\"heldPose\":[";
    for(int i=0;i<heldBoneCount;++i){if(i)out<<',';out<<'['<<heldPose[i][0]<<','<<heldPose[i][1]<<','<<heldPose[i][2]<<']';}out<<"]}";
}
void* currentStudioHeader{};
model_t* currentStudioModel{};
static void (*originalSetHeader)(void*);
static void (*originalSetRenderModel)(model_t*);
static void (*originalDrawPoints)();
static void SetStudioHeader(void* header){currentStudioHeader=header;originalSetHeader(header);}
static void SetStudioModel(model_t* model){currentStudioModel=model;originalSetRenderModel(model);}
static void StudioDrawPoints()
{
    ObservePlayerPose();
    ObserveHeldPose();
    if(!DrawWorldWeaponPoints())originalDrawPoints();
}

void* Resolve(const char* name, mh_gamesymbol_kind_t kind)
{
    void* address{};
    const auto status=api->ResolveGameSymbol(api->GetEngineBase(),name,kind,&address);
    if(status!=MH_GAMESYMBOL_OK || !address) throw std::runtime_error(std::string("unavailable matched engine symbol: ")+name);
    return address;
}
void Hook(void* address, void* replacement, void** original)
{
    auto* hook=api->InlineHook(address,replacement,original);
    if(!hook) throw std::runtime_error("cannot install CSOL engine hook");
    hooks.push_back(hook);
}
void RemoveHooks()
{
    for(auto it=hooks.rbegin();it!=hooks.rend();++it) api->UnHook(*it);
    hooks.clear();
}
static int StudioDrawModel(int flags)
{
    auto* entity=studio.GetCurrentEntity();
    try { if(entity && DrawModel(entity,flags,originalDrawModel)) return 1; }
    catch(const std::exception& e){engine.Con_Printf("CSOLSupport: %s\n",e.what());}
    NativeHandFrameGrid(entity);
    return originalDrawModel(flags);
}
static int GetStudioInterface(int version,r_studio_interface_t** result,engine_studio_api_t* functions)
{
    // The client and earlier plugins copy this public table. Track the actual
    // studio context; currententity can be a brush at PostScene time.
    if(functions->StudioSetHeader!=SetStudioHeader){originalSetHeader=functions->StudioSetHeader;functions->StudioSetHeader=SetStudioHeader;}
    if(functions->SetRenderModel!=SetStudioModel){originalSetRenderModel=functions->SetRenderModel;functions->SetRenderModel=SetStudioModel;}
    if(functions->StudioDrawPoints!=StudioDrawPoints){originalDrawPoints=functions->StudioDrawPoints;functions->StudioDrawPoints=StudioDrawPoints;}
    const int accepted=downstream.HUD_GetStudioModelInterface(version,result,functions);
    if(accepted && result && *result)
    {
        studio=*functions; studioInterface=*result;
        if(studioInterface->StudioDrawPlayer!=StudioDrawPlayer) {
            originalDrawPlayer=studioInterface->StudioDrawPlayer;
            studioInterface->StudioDrawPlayer=StudioDrawPlayer;
        }
        if(studioInterface->StudioDrawModel!=StudioDrawModel)
        {
            originalDrawModel=studioInterface->StudioDrawModel;
            studioInterface->StudioDrawModel=StudioDrawModel;
        }
    }
    return accepted;
}
static void Init()
{
    animationProbe=GetEnvironmentVariableA("CSOL_DIAGNOSTIC_PATH",nullptr,0)>0;
    MountResources();
    InitializeDeathNotices();
    downstream.HUD_Init();
    studioBoneCache=engine.pfnGetCvarPointer("r_studio_bone_caches");
    engine.pfnAddCommand("csol_info",Report);
    InitializeScene();
    InitializeWeapons();
    InitializeWeaponInventory();
    InitializeCamera();
    InitializeProjectiles();
    InitializeCharacters();
}
static int VidInit()
{
    ResetCamera();
    ResetProjectiles();
    ClearScene();
    // A video mode change does not re-send the server's sign-on definitions.
    // Keep inventory until SynchronizeSession observes an actual session change.
    WeaponVideoInit();
    return downstream.HUD_VidInit();
}
static void Frame(double time)
{
    SynchronizeFrameSession();
    if(downstream.HUD_Frame)downstream.HUD_Frame(time);
    SceneFrame();
    CharacterFrame();
}
static void CalcRefdef(ref_params_t* params)
{
    downstream.V_CalcRefdef(params);
    CameraRefdef(params);
    view=*params;
}
static void Transparent()
{
    downstream.HUD_DrawTransparentTriangles();
    DrawWorldEffects();
}
static void PlayerMove(playermove_t* move, int server)
{
    // Read only the server's current prediction data, never a local CVAR or
    // cached scoreboard team. Disconnect/map/slot changes therefore cannot
    // retain a previous server's pass-through permission.
    const auto peers = !server && !move->dead && !move->spectator && move->PM_Info_ValueForKey ?
        movement::ReadPassMask(move->PM_Info_ValueForKey(move->physinfo, movement::PassKey)) : 0;
    const movement::PlayerCollisionScope collision(*move, peers);
    downstream.HUD_PlayerMove(move, server);
}
}

void IPluginsV4::Init(metahook_api_t* api,mh_interface_t* interfaces,mh_enginesave_t*)
{
    csol::client::api=api; csol::client::interfaces=interfaces;
}
void IPluginsV4::LoadEngine(cl_enginefunc_t* functions)
{
    using namespace csol::client;
    engine=*functions;
    try { InstallLoaders(); }
    catch(const std::exception& error)
    {
        RemoveHooks();
        api->SysError("CSOLSupport: %s",error.what());
    }
}
void IPluginsV4::LoadClient(cl_exportfuncs_t* functions)
{
    using namespace csol::client;
    downstream=*functions;
    functions->HUD_Init=csol::client::Init;
    functions->HUD_VidInit=VidInit;
    functions->HUD_Frame=Frame;
    functions->HUD_PlayerMove=PlayerMove;
    functions->HUD_AddEntity=ProjectileAddEntity;
    functions->HUD_GetStudioModelInterface=GetStudioInterface;
    functions->HUD_StudioEvent=StudioEvent;
    functions->V_CalcRefdef=CalcRefdef;
    functions->HUD_DrawTransparentTriangles=Transparent;
    functions->HUD_Redraw=WeaponRedraw;
    functions->HUD_PostRunCmd=WeaponPostRunCmd;
    functions->CL_CreateMove=CharacterCreateMove;
    functions->CAM_Think=CameraThink;
    functions->CL_IsThirdPerson=CameraIsThirdPerson;
    functions->CL_CameraOffset=CameraOffsets;
}
void IPluginsV4::Shutdown(){}
void IPluginsV4::ExitGame(int){csol::client::ShutdownCharacters();csol::client::ClearScene();csol::client::RemoveHooks();}
const char* IPluginsV4::GetVersion(){return "CSOLSupport 0.1 / 2026-10-21";}
EXPOSE_SINGLE_INTERFACE(IPluginsV4,IPluginsV4,METAHOOK_PLUGIN_API_VERSION_V4);
