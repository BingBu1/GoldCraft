// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include <cstring>

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
static std::string level;
static double clientTime=-1;

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
    return originalDrawModel(flags);
}
static int GetStudioInterface(int version,r_studio_interface_t** result,engine_studio_api_t* functions)
{
    const int accepted=downstream.HUD_GetStudioModelInterface(version,result,functions);
    if(accepted && result && *result)
    {
        studio=*functions; studioInterface=*result;
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
    MountResources();
    downstream.HUD_Init();
    engine.pfnAddCommand("csol_info",Report);
    InitializeScene();
}
static int VidInit()
{
    ClearScene();
    level.clear();clientTime=-1;
    return downstream.HUD_VidInit();
}
static void Frame(double time)
{
    const char* current=engine.pfnGetLevelName();if(!current)current="";
    const double now=engine.GetClientTime();
    if(level!=current || (clientTime>=0 && now<clientTime))
    {ClearScene();level=current;}
    clientTime=now;
    if(downstream.HUD_Frame)downstream.HUD_Frame(time);
    SceneFrame();
}
static void CalcRefdef(ref_params_t* params)
{
    downstream.V_CalcRefdef(params);
    view=*params;
}
static void Transparent()
{
    downstream.HUD_DrawTransparentTriangles();
    DrawWorldEffects();
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
    functions->HUD_GetStudioModelInterface=GetStudioInterface;
    functions->HUD_StudioEvent=StudioEvent;
    functions->V_CalcRefdef=CalcRefdef;
    functions->HUD_DrawTransparentTriangles=Transparent;
}
void IPluginsV4::Shutdown(){}
void IPluginsV4::ExitGame(int){csol::client::ClearScene();csol::client::RemoveHooks();}
const char* IPluginsV4::GetVersion(){return "CSOLSupport 0.1 / 2026-10-21";}
EXPOSE_SINGLE_INTERFACE(IPluginsV4,IPluginsV4,METAHOOK_PLUGIN_API_VERSION_V4);
