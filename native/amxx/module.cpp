#include <Windows.h>
#include <amxxmodule.h>
#define EXT_FUNC
#include <interface.h>
#include "goldcraft/server_api.hpp"

namespace {
goldcraft::IServerControl* server=nullptr;
int changed_forward=-1;
bool Resolve(){
    if(server)return true;
    auto module=GetModuleHandleA("mp.dll");
    auto factory=module?reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module,"CreateInterface")):nullptr;
    server=factory?static_cast<goldcraft::IServerControl*>(factory(goldcraft::server_control_api_version,nullptr)):nullptr;
    return server!=nullptr;
}
cell AMX_NATIVE_CALL ApiVersion(AMX*,cell*){return Resolve()?1:0;}
cell AMX_NATIVE_CALL GetForm(AMX*,cell* params){return Resolve()?server->GetForm(params[1]):-1;}
cell AMX_NATIVE_CALL IsPaired(AMX*,cell* params){return Resolve()?server->IsPaired(params[1]):0;}
cell AMX_NATIVE_CALL SetForm(AMX*,cell* params){return Resolve()?server->SetForm(params[1],params[2]):-3;}
void FormChanged(int player,int previous,int current){
    if(changed_forward>=0)MF_ExecuteForward(changed_forward,player,previous,current);
}
const AMX_NATIVE_INFO natives[]={
    {"gc_api_version",ApiVersion},{"gc_get_form",GetForm},{"gc_is_paired",IsPaired},{"gc_set_form",SetForm},{nullptr,nullptr}
};
}
void OnAmxxAttach(){MF_AddNatives(natives);}
void OnPluginsLoaded(){
    if(!Resolve()){MF_Log("GoldCraft ReGameDLL server API unavailable");return;}
    changed_forward=MF_RegisterForward("goldcraft_form_changed",ET_IGNORE,FP_CELL,FP_CELL,FP_CELL,FP_DONE);
    server->SetFormListener(FormChanged);
}
void OnPluginsUnloading(){if(server)server->SetFormListener(nullptr);changed_forward=-1;}
void OnAmxxDetach(){OnPluginsUnloading();server=nullptr;}
