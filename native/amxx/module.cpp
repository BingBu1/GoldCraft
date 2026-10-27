#include <Windows.h>
#include <amxxmodule.h>
#define EXT_FUNC
#include <interface.h>
#include "goldcraft/server_api.hpp"
#include "goldcraft/csol_weapon_api.hpp"
#include "goldcraft/csol_game_mode_api.hpp"
#include "effect_adapter.hpp"

namespace {
goldcraft::IServerControl* server=nullptr;
goldcraft::IServerGameplay* gameplay=nullptr;
csol::IWeaponService* weapons=nullptr;
csol::IWeaponAmmoService* weaponAmmo=nullptr;
csol::IGameModeService* gameModes=nullptr;
bool ResolveGameModes(){
    if(gameModes)return true;
    auto module=GetModuleHandleA("mp.dll");
    auto factory=module?reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module,"CreateInterface")):nullptr;
    gameModes=factory?static_cast<csol::IGameModeService*>(factory(csol::GameModeServiceVersion,nullptr)):nullptr;
    return gameModes!=nullptr;
}
cell AMX_NATIVE_CALL GameModeVersion(AMX*,cell*){return ResolveGameModes()?1:0;}
cell AMX_NATIVE_CALL GameModeCurrent(AMX*,cell*){return ResolveGameModes()?gameModes->LockedMode():-1;}
cell AMX_NATIVE_CALL GameModeConfirm(AMX*,cell* p){
    return p[0]==sizeof(cell)&&ResolveGameModes()&&gameModes->ConfirmPluginIsolation(p[1]);
}
bool ResolveWeapons(){
    if(weapons)return true;
    auto module=GetModuleHandleA("mp.dll");
    auto factory=module?reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module,"CreateInterface")):nullptr;
    weapons=factory?static_cast<csol::IWeaponService*>(factory(csol::WeaponServiceVersion,nullptr)):nullptr;
    return weapons!=nullptr;
}
cell AMX_NATIVE_CALL WeaponVersion(AMX*,cell*){return ResolveWeapons()?1:0;}
cell AMX_NATIVE_CALL WeaponCanBuy(AMX*,cell* p){return ResolveWeapons()?weapons->CanBuy(p[1],p[2]):0;}
cell AMX_NATIVE_CALL WeaponBuy(AMX*,cell* p){return ResolveWeapons()?weapons->Buy(p[1],p[2]):0;}
cell AMX_NATIVE_CALL WeaponZombieMode(AMX*,cell* p){if(!ResolveWeapons())return 0;weapons->SetZombieMode(p[1]!=0);return 1;}
cell AMX_NATIVE_CALL WeaponPlayerRole(AMX*,cell* p){if(!ResolveWeapons())return 0;weapons->SetPlayerRole(p[1],p[2]);return 1;}
cell AMX_NATIVE_CALL WeaponRefill(AMX*,cell* p){
    if(!weaponAmmo){
        auto module=GetModuleHandleA("mp.dll");
        auto factory=module?reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module,"CreateInterface")):nullptr;
        weaponAmmo=factory?static_cast<csol::IWeaponAmmoService*>(factory(csol::WeaponAmmoServiceVersion,nullptr)):nullptr;
    }
    return weaponAmmo?weaponAmmo->Refill(p[1]):0;
}
int changed_forward=-1;
int guard_forward=-1,attack_forward=-1;
bool Resolve(){
    if(server)return true;
    auto module=GetModuleHandleA("mp.dll");
    auto factory=module?reinterpret_cast<CreateInterfaceFn>(GetProcAddress(module,"CreateInterface")):nullptr;
    server=factory?static_cast<goldcraft::IServerControl*>(factory(goldcraft::server_control_api_version,nullptr)):nullptr;
    gameplay=factory?static_cast<goldcraft::IServerGameplay*>(factory(goldcraft::server_gameplay_api_version,nullptr)):nullptr;
    return server!=nullptr;
}
cell AMX_NATIVE_CALL ApiVersion(AMX*,cell*){return Resolve()?1:0;}
cell AMX_NATIVE_CALL GetForm(AMX*,cell* params){return Resolve()?server->GetForm(params[1]):-1;}
cell AMX_NATIVE_CALL IsPaired(AMX*,cell* params){return Resolve()?server->IsPaired(params[1]):0;}
cell AMX_NATIVE_CALL SetForm(AMX*,cell* params){return Resolve()?server->SetForm(params[1],params[2]):-3;}
cell AMX_NATIVE_CALL GameplayVersion(AMX*,cell*){Resolve();return gameplay?1:0;}
cell AMX_NATIVE_CALL PlayerFlags(AMX*,cell* p){Resolve();return gameplay?gameplay->GetPlayerFlags(p[1]):-2;}
cell AMX_NATIVE_CALL PlayerAttack(AMX*,cell* p){Resolve();return gameplay?gameplay->GetPlayerAttack(p[1],p[2]):0;}
int FormAllowed(int player,int requested){
    return guard_forward>=0&&MF_ExecuteForward(guard_forward,player,requested)>=1; // Pawn PLUGIN_HANDLED
}
void PlayerAttackApplied(int victim,int attacker,int kind,float health,float armor){
    if(attack_forward>=0)MF_ExecuteForward(attack_forward,victim,attacker,kind,health,armor);
}
void FormChanged(int player,int previous,int current){
    if(changed_forward>=0)MF_ExecuteForward(changed_forward,player,previous,current);
}
const AMX_NATIVE_INFO natives[]={
    {"csol_game_mode_api_version",GameModeVersion},{"csol_game_mode_current",GameModeCurrent},
    {"csol_game_mode_confirm",GameModeConfirm},
    {"csol_weapon_api_version",WeaponVersion},{"csol_weapon_can_buy",WeaponCanBuy},{"csol_weapon_buy",WeaponBuy},
    {"csol_weapon_zombie_mode",WeaponZombieMode},{"csol_weapon_player_role",WeaponPlayerRole},
    {"csol_weapon_refill_ammo",WeaponRefill},
    {"gc_api_version",ApiVersion},{"gc_get_form",GetForm},{"gc_is_paired",IsPaired},{"gc_set_form",SetForm},
    {"gc_gameplay_api_version",GameplayVersion},{"gc_player_flags",PlayerFlags},{"gc_player_attack",PlayerAttack},{nullptr,nullptr}
};
}
void OnAmxxAttach(){MF_AddNatives(natives);goldcraft::amxx_effects::OnAttach();}
void OnPluginsLoaded(){
    if(ResolveGameModes())gameModes->BeginMap();
    goldcraft::amxx_effects::OnPluginsLoaded();
    if(!Resolve()){MF_Log("GoldCraft ReGameDLL server API unavailable");return;}
    changed_forward=MF_RegisterForward("goldcraft_form_changed",ET_IGNORE,FP_CELL,FP_CELL,FP_CELL,FP_DONE);
    server->SetFormListener(FormChanged);
    if(gameplay){
        guard_forward=MF_RegisterForward("goldcraft_form_request",ET_CONTINUE,FP_CELL,FP_CELL,FP_DONE);
        attack_forward=MF_RegisterForward("goldcraft_player_attack_post",ET_IGNORE,FP_CELL,FP_CELL,FP_CELL,FP_FLOAT,FP_FLOAT,FP_DONE);
        gameplay->SetFormGuard(FormAllowed);gameplay->SetPlayerAttackListener(PlayerAttackApplied);
    }
}
void OnPluginsUnloading(){
    if(weapons)weapons->SetZombieMode(false);
    goldcraft::amxx_effects::OnPluginsUnloading();if(server)server->SetFormListener(nullptr);
    if(gameplay){gameplay->SetFormGuard(nullptr);gameplay->SetPlayerAttackListener(nullptr);}
    changed_forward=guard_forward=attack_forward=-1;
    if(gameModes)gameModes->EndMap();
}
void OnAmxxDetach(){OnPluginsUnloading();server=nullptr;gameplay=nullptr;weapons=nullptr;weaponAmmo=nullptr;gameModes=nullptr;}
