// SPDX-License-Identifier: GPL-3.0-or-later
// Compile the actual five ReAPI wrappers and forwarding templates in place.
// The VM callback and next-chain are controlled ABI edges, not a Pawn VM.
#include "../../external/ReAPI/reapi/src/hook_callback.cpp"
#include "../../external/ReAPI/reapi/src/amx_hook.cpp"
#include "../../external/ReAPI/reapi/src/reapi_utils.cpp"
#include <array>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
amxxapi_t g_amxxapi{};CHookManager g_hookManager;
// Unused exported wrappers retain these external interfaces at link time.
// NULL bindings cannot silently supply behavior if a probe reaches them.
decltype(g_RehldsFuncs) g_RehldsFuncs{};decltype(g_RecheckerFuncs) g_RecheckerFuncs{};
decltype(g_pMove) g_pMove{};decltype(gpGamedllFuncs) gpGamedllFuncs{};
decltype(g_ReGameApi) g_ReGameApi{};decltype(g_pengfuncsTable) g_pengfuncsTable{};
enginefuncs_t g_engfuncs{};globalvars_t* gpGlobals{};int gmsgSendAudio{};
void AMXX_LogError(AMX*,int,const char*,...){throw std::runtime_error("unexpected AMXX error boundary");}
edict_t* g_pEdicts{};IRehldsServerStatic* g_RehldsSvs{};
hook_t hooklist_engine[1024],hooklist_gamedll[1024],hooklist_animating[1024],hooklist_player[1024],hooklist_gamerules[1024],hooklist_rechecker[1024],hooklist_grenade[1024],hooklist_weaponbox[1024],hooklist_weapon[1024],hooklist_gib[1024],hooklist_cbaseentity[1024],hooklist_botmanager[1024];
namespace {
int checks{},failures{},arg{},expected{},calls{};bool changeToNull{};std::vector<int> observed;
void Check(bool ok){++checks;if(!ok)++failures;}
int Execute(int,...){
 auto* value=reinterpret_cast<int*>(g_hookCtx->args[arg].handle);observed.push_back(*value);Check(*value==(changeToNull&&observed.size()>1?AMX_NULLENT:expected));
 if(changeToNull)*value=AMX_NULLENT;return HC_CONTINUE;
}
template<class Interface,class R,class... A>struct Chain final:Interface {
 std::function<R(A...)> next;
 R callNext(A... args)override{++calls;return next(args...);}
 R callOriginal(A... args)override{return callNext(args...);}
};
void Prepare(int id,int parameter,int wanted){
 arg=parameter;expected=wanted;observed.clear();auto* h=g_hookManager.getHookFast(id);
 static CAmxxHookBase vm(nullptr,"controlled-vm",-1,1);h->pre={&vm};h->post={&vm};
}
}
int main(){
 g_amxxapi.ExecuteForward=Execute;
 std::array<edict_t,4> edicts{};g_pEdicts=edicts.data();
 // Only pointer/pev ABI fields are consumed; no fake GameDLL behavior runs here.
 alignas(CBasePlayer) unsigned char pdata[sizeof(CBasePlayer)]{};
 auto* p=reinterpret_cast<CBasePlayer*>(pdata);p->pev=&edicts[1].v;edicts[1].pvPrivateData=p;
 for(int i=0;i<4;++i)edicts[i].v.pContainingEntity=&edicts[i];
 Chain<IReGameHook_CBasePlayer_TakeDamage,int,CBasePlayer*,entvars_t*,entvars_t*,float&,int> damage;
 damage.next=[](CBasePlayer*,entvars_t*,entvars_t* source,float&,int){Check(source==nullptr);return 1;};
 Chain<IReGameHook_CBasePlayer_Killed,void,CBasePlayer*,entvars_t*,int> killed;
 killed.next=[](CBasePlayer*,entvars_t* source,int){Check(source==nullptr);};
 Chain<IReGameHook_CSGameRules_FPlayerCanTakeDamage,BOOL,CBasePlayer*,CBaseEntity*> permission;
 permission.next=[](CBasePlayer*,CBaseEntity* source){Check(source==nullptr);return TRUE;};
 Chain<IReGameHook_CSGameRules_PlayerKilled,void,CBasePlayer*,entvars_t*,entvars_t*> playerKilled;
 playerKilled.next=[](CBasePlayer*,entvars_t* source,entvars_t*){Check(source==nullptr);};
 Chain<IReGameHook_CSGameRules_DeathNotice,void,CBasePlayer*,entvars_t*,entvars_t*> deathNotice;
 deathNotice.next=[](CBasePlayer*,entvars_t* source,entvars_t*){Check(source==nullptr);};
 float amount=10;
 for(bool mutate:{false,true}){
  changeToNull=mutate;const int wanted=mutate?2:AMX_NULLENT;auto* source=mutate?&edicts[2].v:nullptr;
  // POST observes the mutated -1; no NULL->world conversion is accepted.
  Prepare(RG_CBasePlayer_TakeDamage,2,wanted);CBasePlayer_TakeDamage(&damage,p,&edicts[3].v,source,amount,8);Check(observed==std::vector<int>({wanted,AMX_NULLENT}));
  Prepare(RG_CBasePlayer_Killed,1,wanted);CBasePlayer_Killed(&killed,p,source,0);
  Prepare(RG_CSGameRules_PlayerKilled,1,wanted);CSGameRules_PlayerKilled(&playerKilled,p,source,&edicts[3].v);
  Prepare(RG_CSGameRules_DeathNotice,1,wanted);CSGameRules_DeathNotice(&deathNotice,p,source,&edicts[3].v);
 }
 changeToNull=false;Prepare(RG_CSGameRules_FPlayerCanTakeDamage,1,AMX_NULLENT);CSGameRules_FPlayerCanTakeDamage(&permission,p,nullptr);Check(observed==std::vector<int>({AMX_NULLENT,AMX_NULLENT}));
 Check(PEV(AMX_NULLENT)==nullptr&&getPrivate<CBaseEntity>(AMX_NULLENT)==nullptr);
 Check(indexOfEdictAmx(&edicts[0].v)==0&&indexOfEdictAmx(static_cast<entvars_t*>(nullptr))==AMX_NULLENT);
 std::cout<<"reapi_null_forward checks="<<checks<<" failures="<<failures<<" calls="<<calls<<'\n';return failures?1:0;
}
