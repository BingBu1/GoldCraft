// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace equipment_test
{
namespace
{
std::shared_ptr<CCSOLScenarioEquipment> Service(std::shared_ptr<const EquipmentTables> tables,CCSOLScenarioEquipment::CapEvent event={})
{return std::make_shared<CCSOLScenarioEquipment>(std::move(tables),std::move(event));}
}
void ServiceCases()
{
    {
        Engine engine;auto& player=engine.NewPlayer();auto service=Service(EffectFixture());EquipmentLoadout loadout;
        loadout.carriedItems={31,32,21};loadout.classCaps={{3,{9,9,9,9,9}},{45,{19,24,21,20,21}},{45,{30,30,30,30,30}}};
        Require(service->BindPlayer(player,45,loadout),"concrete service binds class and authoritative caps");
        auto attrs=Attributes();attrs[0].level=25;
        Require(service->Initialize(EHANDLE(&player),45,attrs),"concrete ordered initialization");
        Check(attrs[0].level==27&&attrs[0].cap==22&&attrs[1].cap==24&&attrs[2].cap==21&&attrs[3].cap==20&&attrs[4].cap==21,
            "first matching per-player class caps apply before INITIAL and MAX effects without base clamp");
        Check(!service->Initialize(EHANDLE(&player),3,attrs),"different requested class cannot borrow another binding");
        float gravity=1;Require(service->ModifyJump(EHANDLE(&player),gravity),"concrete JUMP effect service");Check(gravity==0.5f,"named JUMP ability applies actual ordered effect engine");
        loadout.classCaps={{45,{0,0,0,0,0}},{45,{20,20,20,20,20}}};loadout.carriedItems.clear();
        Require(service->BindPlayer(player,45,loadout),"first zero-cap record is authoritative");attrs=Attributes();
        Require(service->Initialize(EHANDLE(&player),45,attrs),"zero record copied by service");
        Check(std::all_of(attrs.begin(),attrs.end(),[](const auto& v){return v.cap==0;}),"all-zero class record is not replaced by later duplicate or defaults");
        UpgradeProgress progress;ConfigError error;
        Check(!progress.Initialize(RealTables(),45,attrs,0,error),"gameplay rejects unusable zero caps instead of accepting false success");
    }
    {
        Engine engine;auto& donor=engine.NewPlayer();auto& recipient=engine.NewPlayer();auto service=Service(RealEquipment());
        EquipmentLoadout items;items.buffItems={809,810,825,891,8033};
        Require(service->BindPlayer(donor,0,items)&&service->BindPlayer(recipient,0,{}),"real global buff donors bind through SDK service");
        int value=-1;Require(service->ReadBonus(EHANDLE(&recipient),UpgradeStat::Health,value),"recipient reads real donor bonus");Check(value==5,"concrete service consumes real buff resource");
        engine.Destroy(donor);Require(service->ReadBonus(EHANDLE(&recipient),UpgradeStat::Health,value),"find prunes freed donor");Check(value==0,"raw-free donor cannot leave a global bonus");
        service->Invalidate();value=123;float gravity=1;int xp=7;std::optional<float> money=99;
        Check(!service->ReadBonus(EHANDLE(&recipient),UpgradeStat::Health,value)&&!service->ModifyJump(EHANDLE(&recipient),gravity)&&
            !service->ModifyExperience(EHANDLE(&recipient),xp)&&!service->MoneyAddPercent(EHANDLE(&recipient),money)&&
            !service->BindPlayer(recipient,0,{}),"invalidated service rejects every later player consumer and binding");
        Check(value==123&&gravity==1&&xp==7&&money==99,"invalidated service leaves caller values untouched");
    }
    {
        Engine engine;auto& player=engine.NewPlayer();auto tables=RealEquipment();auto service=Service(tables);
        Require(service->BindPlayer(player,0,{}),"concrete money absence binding");CCSOLScenarioMessages messages;Require(messages.Register(),"concrete service message registry");
        auto state=CCSOLScenarioPlayerUpgrades::Create(player,RealTables(),service,messages,1,1);
        Require(state&&state->Configure(0,0,false),"upgrade adapter accepts concrete equipment service");engine.movement=state;
        int money=16777217;Require(state->ModifyMoney(money),"concrete inactive money addon");Check(money==16777217,"inactive addon keeps full original integer without float roundtrip");
        EquipmentLoadout active;active.addonLevels[19]=1;Require(service->BindPlayer(player,0,active),"activate real money addon");
        money=123;Require(state->ModifyMoney(money),"concrete active money modifier");Check(money==246,"actual AddPercent100 doubles money only when activated");
        Check(!service->ReachedEffectiveCap(EHANDLE(&player))&&!service->ReachedEffectiveCap(EHANDLE(&player)),"missing event0x26 sink explicitly fails repeatedly");
    }
    {
        Engine engine;auto& player=engine.NewPlayer();auto tables=EffectFixture();EquipmentLoadout loadout;
        loadout.carriedItems={11,12};loadout.equippedItems[0]=13;loadout.equippedItems[9]=14;loadout.extraItem=15;
        auto service=Service(tables);Require(service->BindPlayer(player,0,loadout),"concrete effect collector binding");
        CCSOLScenarioMessages messages;Require(messages.Register(),"concrete XP messages");
        auto state=CCSOLScenarioPlayerUpgrades::Create(player,RealTables(),service,messages,1.5f,1);
        Require(state&&state->Configure(0,0,false)&&state->AddExperience(3),"complete concrete EXP effect plus adapter difficulty");
        Check(state->State()->experience==25,"ordered effect chain truncates17 before separate difficulty1.5 truncates25");
    }
    for(int action=0;action<3;++action)
    {
        Engine engine;auto& player=engine.NewPlayer();auto service=Service(RealEquipment());bool callback=false;
        g_engfuncs.pfnIndexOfEdict=[](const edict_t* ed){Engine::active->Step("entindex");return Engine::Index(ed);};
        engine.boundary=[&](std::string_view label){if(label=="entindex"&&!callback){callback=true;
            if(action==0)service->RemovePlayer(EHANDLE(&player));
            if(action==1)service->Invalidate();
            if(action==2)Check(!service->BindPlayer(player,45,{}),"nested ENTINDEX Bind is rejected while outer bind is active");}};
        const bool bound=service->BindPlayer(player,0,{});Check(callback&&bound==(action==2),"ENTINDEX remove/invalidate cancels first old binding without resurrection");
        engine.boundary={};int value=123;Check(service->ReadBonus(EHANDLE(&player),UpgradeStat::Health,value)==bound,"only surviving binding can answer capability query");
    }
    for(int behavior=0;behavior<3;++behavior)
    {
        Engine engine;auto& player=engine.NewPlayer();auto tables=RealEquipment();int calls=0;
        std::shared_ptr<CCSOLScenarioEquipment> service;
        service=Service(tables,[&](const EHANDLE& handle){++calls;
            Check(!service->ReachedEffectiveCap(handle),"cap event cannot recursively dispatch");
            Check(!service->BindPlayer(player,0,{}),"cap dispatch blocks replacement binding");
            if(behavior==2)service->RemovePlayer(handle);
            return behavior!=1;});
        Require(service->BindPlayer(player,0,{}),"full-event lifecycle fixture");
        Check(service->ReachedEffectiveCap(EHANDLE(&player))==(behavior==0),"cap event result and in-callback removal propagate");
        if(behavior<2){Require(service->BindPlayer(player,45,{}),"equipment/character rebind keeps full-event session state");
            Check(service->ReachedEffectiveCap(EHANDLE(&player))==(behavior==0)&&calls==1,"successful and failed cap event each dispatch at most once across rebind");}
        else Check(!service->ReachedEffectiveCap(EHANDLE(&player))&&calls==1,"removed player cannot accept stale full-event success");
    }
}
}
