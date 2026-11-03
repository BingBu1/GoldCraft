// SPDX-License-Identifier: GPL-3.0-or-later
#define main OriginalLifeRegression
#include "action_main.inc"
#undef main
#include <algorithm>

namespace {
struct EmotionFixture {
    CBasePlayer player;
    CBasePlayerWeapon weapon, alternate;
    std::shared_ptr<CCSOLScenarioSession> session=std::make_shared<CCSOLScenarioSession>();
    std::shared_ptr<CSOLScenarioPlayerRuntimeState> state=std::make_shared<CSOLScenarioPlayerRuntimeState>();
    std::shared_ptr<CCSOLScenarioPlayerRuntime> players=std::make_shared<CCSOLScenarioPlayerRuntime>();
    ConfigError error;
    explicit EmotionFixture(int count=6) {
        actionCallback={};actionEvents.clear();actions={};emotions={};gpGlobals->time=10;actionHeader.numseq=400;
        resources=std::make_shared<Resources>();resources->access=session->Map().EntityAccess();resources->session=session;
        auto& row=resources->actions[101];row={101,204,30,0,142,"test"};row.emotionCount=count;row.emotionMenu="MENU_TEST";
        for(int slot=1;slot<=10;++slot)row.emotions[slot]={200+slot,10+slot,float(90+slot)};
        row.emotions[5]={204,30,142};resources->viewmodels={40,41};resources->emotionMenus["MENU_TEST"]="1. Original emotion";
        state->player=EHANDLE(&player);players->state=state;player.m_pActiveItem=&weapon;
    }
    bool Select(int slot){return SelectCharacterEmotion(players,state,session,slot,error);}
    int Count(const char* event)const{return int(std::count(actionEvents.begin(),actionEvents.end(),event));}
    std::shared_ptr<Action> Skill(){return std::make_shared<Action>(players,state,session,state->activeSkill.get());}
};
}
int main(){
    const int legacy=OriginalLifeRegression();
    int checks{},failed{};const auto check=[&](bool value,const char* reason){++checks;if(!value){++failed;std::printf("FAIL %s\n",reason);}};
    for(int count:{4,6})for(int slot=-1;slot<=11;++slot){
        EmotionFixture f(count);const bool valid=slot>=1&&slot<=count;
        check(f.Select(slot),"valid character menu selection request is consumed");
        const auto current=FindAction(f.player);
        check(bool(current)==valid,"menu honors exact inclusive row slot count, including slot5");
        if(valid){
            const auto& row=resources->actions.at(101);const auto expected=row.emotions[slot];
            check(current->playing&&f.player.pev->sequence==expected.sequence&&f.player.pev->weaponanim==expected.weaponAnimation&&
                f.player.m_flFrameRate==expected.frameRate,"selected slot uses original tuple and rate");
            check(emotions[1]==current&&current->owner==nullptr,"J action has retained lifetime without a skill owner");
            check(f.player.pev->viewmodel==40&&f.player.pev->weaponmodel==0&&f.weapon.m_fInReload==0,
                "J action sets shared model and cancels current reload");
            check(PreserveCharacterAction(f.player,0)&&!PreserveCharacterAction(f.player,PLAYER_DIE),"J animation preserves ordinary activity but never death");
        }else check(f.Count("sequence")==0&&f.Count("message-begin")==0&&f.Count("holster")==0,
            "invalid menu slot emits no animation and leaves weapon alone");
    }
    {EmotionFixture f(4);auto skill=f.Skill();bool started{};
     check(skill->Start(5,started,f.error)&&started,"skill-owned action5 remains available on four-item J menu");}
    for(bool skillFirst:{false,true}){
        EmotionFixture f;auto skill=f.Skill();bool started{};
        if(skillFirst)check(skill->Start(5,started,f.error)&&started,"skill begins before J attempt");
        else check(f.Select(1)&&FindAction(f.player),"J begins before skill attempt");
        const auto original=FindAction(f.player);const int sequences=f.Count("sequence");
        if(skillFirst)check(f.Select(2)&&!emotions[1],"J cannot replace an active skill action");
        else check(skill->Start(5,started,f.error)&&!started,"skill cannot replace an active J action");
        check(FindAction(f.player)==original&&f.Count("sequence")==sequences,"mutual exclusion leaves active animation untouched");
        check(CancelCharacterAction(f.player,false),"active action cancels through public entry");
        if(skillFirst)check(f.Select(2)&&emotions[1]&&emotions[1]->playing,"J starts after skill cancellation");
        else check(skill->Start(5,started,f.error)&&started,"skill starts after J cancellation");
    }
    for(int trigger=0;trigger<4;++trigger){
        EmotionFixture f;check(f.Select(1),"J starts for lifecycle completion test");const auto action=FindAction(f.player);
        actionEvents.clear();
        if(trigger==0)f.player.m_fSequenceFinished=1;
        if(trigger==1)f.player.pev->button=1;
        if(trigger==2){f.alternate.m_flReleaseThrow=73;f.player.m_pActiveItem=&f.alternate;}
        if(trigger==3){f.player.pev->health=0;f.player.pev->deadflag=1;}
        check(CharacterActionPostThink(f.player)&&!action->playing,"finish, attack, switch or death retires J action");
        check(f.Count("deploy")==(trigger<2?1:0),"only finish/attack restores active weapon through Deploy");
        if(trigger==2)check(f.alternate.m_flReleaseThrow==73,"switch cancellation does not mutate new weapon");
        if(trigger==3){++f.state->skillLifeSerial;f.player.pev->health=100;f.player.pev->deadflag=0;
            check(f.Select(3)&&emotions[1]->playing,"respawn can start a new J action after death retirement");}
    }
    for(const char* point:{"can-deploy","model","sequence","message-begin","message-byte","message-end","holster","index"}){
        EmotionFixture f;bool fired{};
        actionCallback=[&](const char* name){if(!fired&&std::string(name)==point){fired=true;++f.state->skillLifeSerial;
            f.player.pev->viewmodel=777;f.player.pev->weaponmodel=778;f.player.m_flNextAttack=-123;}};
        // Select performs an extra lookup before Start. Test Action directly;
        // its first index callback belongs to OtherActionPlaying.
        auto action=std::make_shared<Action>(f.players,f.state,f.session,nullptr);bool started{};
        check(!action->Start(1,started,f.error)&&fired&&!started,"new-life callback cancels ownerless J start");
        check(f.player.pev->viewmodel==777&&f.player.pev->weaponmodel==778&&f.player.m_flNextAttack==-123,
            "ownerless J does not overwrite new-life presentation after a callback");
        actionCallback={};
    }
    {EmotionFixture f;f.player.pev->health=0;check(OpenCharacterEmotionMenu(f.player)&&CharacterEmotionMenuOpen(f.player),
        "original cheer menu opens even when dead");check(shownMenuKeys==575&&shownMenuText=="1. Original emotion","original menu key mask and text used");
     check(f.Select(1)&&!FindAction(f.player),"dead player menu cannot start an action");}
    {EmotionFixture f;f.player.m_iFOV=40;check(f.Select(1)&&!FindAction(f.player)&&f.Count("print")==1,"zoom blocks J and prints original notice path");
     auto skill=f.Skill();bool started{};check(skill->Start(5,started,f.error)&&!started&&f.Count("print")==1,"zoom blocks action5 without J notice");}
    actionCallback={};actions={};emotions={};resources.reset();
    std::printf("emotion lifecycle: %d checks, %d failures; previous life regressions exit=%d\n",checks,failed,legacy);
    return failed||legacy?1:0;
}
