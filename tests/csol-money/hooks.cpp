// SPDX-License-Identifier: GPL-3.0-or-later
#include <iostream>
#include <string>
enum class Action {Normal,Cancel,Change,ReplaceRules,Revoke,Recreate,Disconnect,Kill,Reenter,PostRetire};
Action action{};int checks{},failures{},depth{},calls{};bool pinObserved{},postGuard{},direct{};
void Check(bool v,const char* text){++checks;if(!v){++failures;std::cerr<<"FAIL "<<text<<'\n';}}
void Hook(AccountChain* chain,CBasePlayer* player,int amount,RewardType type,bool track)
{
    ++calls;pinObserved=activePins>0;
    if(action==Action::Cancel)return;
    if(action==Action::Change)amount=37;
    if(action==Action::ReplaceRules)g_pGameRules=&otherRulesStorage;
    if(action==Action::Revoke){rulesStorage.access->allowed=false;g_pGameRules=&otherRulesStorage;}
    if(action==Action::Recreate)++player->serial;
    if(action==Action::Disconnect)player->has_disconnected=true;
    if(action==Action::Kill)player->pev->flags|=FL_KILLME;
    if(action==Action::Reenter&&depth++==0){player->AddAccount(20,RT_NONE,true);--depth;}else if(action==Action::Reenter)--depth;
    if(direct)chain->callOriginal(player,amount,type,track);
    else chain->callNext(player,amount,type,track);
    if(action==Action::PostRetire){rulesStorage.access->allowed=false;postGuard=CSOLBreakableScope::Current(nullptr);}
}
int main()
{
    g_ReGameHookchains.m_CBasePlayer_AddAccount.registerHook(Hook,128);
    for(bool directCall:{false,true})for(bool scenario:{false,true})for(Action a:{Action::Normal,Action::Cancel,Action::Change,Action::ReplaceRules,Action::Revoke,Action::Recreate,Action::Disconnect,Action::Kill,Action::Reenter,Action::PostRetire})
    {
        scenarioMode=scenario;rulesStorage.access=std::make_shared<CSOLScenarioEntityAccess>();otherRulesStorage.access=std::make_shared<CSOLScenarioEntityAccess>();g_pGameRules=&rulesStorage;
        action=a;direct=directCall;calls=depth=0;pinObserved=postGuard=false;messages.clear();CBasePlayer player;player.m_iAccount=100;
        player.AddAccount(50,RT_NONE,true);
        const bool invalid=scenario&&(a==Action::ReplaceRules||a==Action::Revoke||a==Action::Recreate||a==Action::Disconnect||a==Action::Kill);
        const int expected=a==Action::Cancel||invalid?100:a==Action::Change?137:a==Action::Reenter?170:150;
        const auto packets=a==Action::Cancel||invalid?0u:a==Action::Reenter?2u:1u;
        Check(player.m_iAccount==expected,(std::string(scenario?"scenario ":"ordinary ")+std::to_string(int(a))+" balance").c_str());
        Check(messages.size()==packets,"matching packet count");Check(pinObserved&&activePins==0,"scope pinned through hook and released");
        Check(CSOLBreakableScope::Current(nullptr),"guard stack restored after call");
        if(scenario&&a==Action::PostRetire)Check(!postGuard,"guard remains active through post hook");
    }
    std::cout<<"account hooks "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
