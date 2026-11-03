// SPDX-License-Identifier: GPL-3.0-or-later
namespace
{
using namespace bomb_test;
void Basic()
{
    Fixture f;float damage=100,force=350;bool immune=true;
    Check(f.State()->zombieBombProtectionUntil==0&&f.State()->zombieBombProtectionRatio==1&&
        f.State()->zombieBombProtectionPart==0,"original constructor 0/1/0");
    Require(f.Hit(damage,force,immune),"empty parts hit accepted");
    Check(damage==100&&force==350&&!immune&&f.seeds==0&&f.rolls==0,"empty records consume neither random source");
    f.Parts({Fixture::Part(1)});damage=0;Require(f.Hit(damage,force,immune),"zero damage path accepted");
    Check(force==350&&f.seeds==0&&f.rolls==0&&f.State()->zombieBombProtectionUntil==0,"zero damage skips protection without losing force");
    damage=100;Require(f.Hit(damage,force,immune),"part hit accepted");
    Check(damage==75&&force==262.5f&&f.State()->zombieBombProtectionUntil==15&&
        f.State()->zombieBombProtectionRatio==.25&&f.State()->zombieBombProtectionPart==1,"double protection installs and scales damage and impulse");
    damage=100;force=350;f.globals.time=15;Require(f.Hit(damage,force,immune),"deadline equality hit");
    Check(damage==100&&force==350&&f.seeds==1&&f.State()->zombieBombProtectionUntil==15,"equality neither reduces nor rolls again before normal cleanup");
    Require(f.players->UpdatePlayerBuffs(*f.player,15),"normal expiry updater");
    Check(f.State()->zombieBombProtectionUntil==0&&f.State()->zombieBombProtectionRatio==.01&&
        f.State()->zombieBombProtectionPart==1,"zero-duration installer leaves percent .01 and previous ID");
    Require(f.Hit(damage,force,immune),"new hit after expiry may trigger");
    Check(f.seeds==2&&f.State()->zombieBombProtectionUntil==20,"cleanup reopens exact-zero trigger");
    Require(f.players->ResetMonsterSkillState(f.Handle()),"death motion cleanup");
    f.players->PlayerSpawnStarted(f.Handle());
    Check(f.State()->zombieBombProtectionUntil==20&&f.State()->nativeSpawnSerial==1,"death and spawn do not clear protection");
}
void Selection()
{
    {
        Fixture f;f.Parts({Fixture::Part(1,81,9,50),Fixture::Part(2,100,5,25)});
        float d=100,k=100;bool immune{};Require(f.Hit(d,k,immune),"strict probability sample");
        Check(f.State()->zombieBombProtectionPart==2&&d==75,"first MT sample81 equals81: no trigger");
    }
    {
        Fixture f;f.Parts({Fixture::Part(1,100,5,25),Fixture::Part(2,100,5,90),Fixture::Part(1,100,7,50,true)});
        float d=100,k=100;bool immune{};Require(f.Hit(d,k,immune),"active plus passive selection");
        Check(f.State()->zombieBombProtectionPart==1&&f.State()->zombieBombProtectionUntil==17&&d==50,"longest positive duration beats active row and tie retains first");
    }
    {
        Fixture f;f.Parts({Fixture::Part(1,100,5,25),Fixture::Part(2,100,5,90)});
        float d=100,k=100;bool immune{};Require(f.Hit(d,k,immune),"duration tie");Check(d==75,"duration tie retains first row");
    }
    for(double duration:{0.,-1.})
    {Fixture f;f.Parts({Fixture::Part(1,100,duration,50)});float d=100,k=100;bool immune{};
     Require(f.Hit(d,k,immune),"nonpositive duration");Check(d==100&&f.State()->zombieBombProtectionUntil==0&&f.seeds==1,"nonpositive duration never installs but consumes random record");}
    for(double percentage:{-50.,0.,25.,100.,150.})
    {Fixture f;f.Parts({Fixture::Part(1,100,5,percentage)});float d=100,k=350;bool immune{};
     Require(f.Hit(d,k,immune),"unclamped reduction");Check(d==float(100.-percentage)&&k==float(350.-350.*(percentage*.01)),"negative and over100 percentages preserve original subtraction");}
    // Load a legal MT stream state so the first tempered word is zero. This
    // exercises rejection without changing the production integer sampler.
    std::stringstream text;for(int i=0;i<624;++i)text<<(i==398?1u:i==399?0x80000000u:0u)<<' ';
    std::mt19937 random;text>>random;Check(PartPercent(random)==0&&random()==2282758660u,"low-product zero is rejected and next MT word consumed");
}
void Collection()
{
    Fixture f;f.Parts({Fixture::Part(1),Fixture::Part(2,100,6,50),Fixture::Part(1,100,7,25,true)});
    Require(f.weapons->SetParts(*f.guns[1],{2,0}),"second gun has distinct direct part");
    std::vector<CSOLWeaponPartEffect> out;csol::scenario::ConfigError error;
    f.guns[0]->isWeapon=[&]{f.player->m_pActiveItem=f.guns[1];};
    Require(f.weapons->CollectPlayerParts(*f.player,*f.State(),15,f.Guard(),out,error),"active callback may switch weapons");
    Check(out.size()==2&&out[0].id==2&&!out[0].passive&&out[1].id==1&&out[1].passive,"direct collector rereads active then visits carried passive");
    f.player->m_pActiveItem=f.guns[0];f.guns[0]->isWeapon=[&]{f.Block(false,true);};out.clear();
    Require(f.weapons->CollectPlayerParts(*f.player,*f.State(),15,f.Guard(),out,error),"block flag changed during direct IsWeapon");
    Check(out.size()==2&&!out[0].passive&&!out[1].passive,"flags reread only at passive collector entrance");
    f.guns[0]->isWeapon={};f.Block(true,false);out.clear();
    Require(f.weapons->CollectPlayerParts(*f.player,*f.State(),15,f.Guard(),out,error)&&out.empty(),"blocked direct and passive collection");
    f.Block(false,false);f.guns[0]->isWeapon=[&]{f.valid=false;};
    Check(!f.weapons->CollectPlayerParts(*f.player,*f.State(),15,f.Guard(),out,error),"IsWeapon host cancellation propagates");
}
void Addon()
{
    for(int level:{0,1,2})for(int sample:{49,50,54,55})
    {Fixture f;f.Level(level);f.sample=sample;float d=100,k=100;bool immune{};
     Require(f.Hit(d,k,immune),"addon level probability");Check(immune==(level>0&&(50+5*(level-1)>sample))&&f.rolls==(level>0),"strict probability and inactive RNG gate");}
    {
        Fixture f;f.Level(4);tables.addons[13].parameters={{"Percent",-2.1f},{"PercentAdd",.7f}};f.sample=0;
        float d=100,k=100;bool immune{};Require(f.Hit(d,k,immune),"SSE rounding regression");
        Check(!immune,"mulss rounds before addss; x87 single-store would incorrectly grant immunity");
    }
    {
        Fixture f;f.Level(1);f.sample=0;f.onRandom=[&]{f.Level(0);};float d=100,k=100;bool immune{};
        Require(f.Hit(d,k,immune),"RNG callback changes addon level");Check(!immune&&f.rolls==1,"level reread after original RNG callback");
    }
    {
        Fixture f;f.Level(1);f.sample=0;f.Parts({Fixture::Part(1)});float d=100,k=100;bool immune{};
        Require(f.Hit(d,k,immune),"parts before addon");Check(immune&&f.order==std::vector<int>({1,2})&&d==75,"part then addon RNG order retained");
    }
}
void Cancellation()
{
    for(int kind=0;kind<5;++kind)
    {Fixture f;f.Parts({Fixture::Part(1)});f.onSeed=[&]{if(kind==0)f.valid=false;if(kind==1)++f.player->m_iNumSpawns;
       if(kind==2)f.players->PlayerSpawnStarted(f.Handle());if(kind==3)active=false;if(kind==4)++f.edicts[0].serialnumber;};
     float d=100,k=100;bool immune{};const auto state=f.State();Check(!f.Hit(d,k,immune),"entropy callback invalidates owner");
     Check(state->zombieBombProtectionUntil==0&&d==100&&k==100&&f.rolls==0,"cancelled entropy never commits protection or addon roll");}
    for(int kind=0;kind<4;++kind)
    {Fixture f;f.Level(1);f.onRandom=[&]{if(kind==0)f.valid=false;if(kind==1)++f.player->m_iNumSpawns;
       if(kind==2)f.players->PlayerSpawnStarted(f.Handle());if(kind==3)f.players->ResetMonsterSkillState(f.Handle());};
     float d=100,k=100;bool immune{};Check(!f.Hit(d,k,immune)&&!immune,"addon RNG replacement cancels old hit");}
    {Fixture f;f.player->pev->health=0;f.player->pev->deadflag=DEAD_DEAD;float d=100,k=100;bool immune{};
     Require(f.Hit(d,k,immune),"registered corpse remains a valid original damage target");Check(d==100&&k==100,"corpse path retains force");}
}
}
int main()
{
    try{Basic();Selection();Collection();Addon();Cancellation();}
    catch(const std::exception& e){++bomb_test::failures;std::cerr<<e.what()<<'\n';}
    std::cout<<"zombie_bomb_protection checks="<<bomb_test::checks<<" failures="<<bomb_test::failures<<'\n';
    return bomb_test::failures?1:0;
}
