// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace equipment_test
{
void SelectionCases()
{
    {
        EquipmentAbilities abilities(RealEquipment());EquipmentLoadout donor;
        donor.buffItems={809,810,825,891,8033,935,936,8117,8555};
        Require(abilities.Bind(1,donor)&&abilities.Bind(2,{}),"global inventory bonuses activate for one donor and another player");
        for(unsigned stat=0;stat<5;++stat){int value=-1;Require(abilities.Bonus(2,UpgradeStat(stat),value),"read another player's global capability");Check(value==5,"duplicates select strongest bonus rather than stacking");}
        abilities.Remove(1);for(unsigned stat=0;stat<5;++stat){int value=-1;Require(abilities.Bonus(2,UpgradeStat(stat),value),"query after donor removal");Check(value==0,"removing donor recomputes every global capability");}
    }
    auto tables=std::make_shared<EquipmentTables>();
    const auto add=[&](int item,int priority,float amount,bool global){EquipmentBuff buff;buff.item=item;buff.type=4;buff.priority=priority;buff.global=global;buff.values[0]=amount;tables->buffs[item].push_back(buff);};
    add(900,1,9,true);add(1000,1,3,false);add(1100,1,4,true);add(5,2,-3,false);add(2000,3,11,true);
    EquipmentAbilities abilities(tables);EquipmentLoadout global,local,other;
    global.buffItems={900};local.buffItems={1000};other.buffItems={1100};
    Require(abilities.Bind(1,global)&&abilities.Bind(2,local)&&abilities.Bind(3,{})&&abilities.Bind(4,other),"global and local competition fixture");
    const auto bonus=[&](int player,int expected,const char* label){int value=999;Require(abilities.Bonus(player,UpgradeStat::Health,value),"bound player has bonus query");Check(value==expected,label);};
    bonus(3,4,"equal-priority global winner uses higher item ID, not larger bonus value");
    bonus(2,4,"higher global item ID defeats equal-priority local item");
    abilities.Remove(4);bonus(2,3,"higher local item ID defeats remaining equal-priority global");bonus(3,9,"removed global owner restores next winner");
    local.buffItems={5};Require(abilities.Bind(2,local),"rebind different local priority");
    bonus(2,-3,"priority wins before item ID and negative value is retained");bonus(3,9,"local selection does not leak to another player");
    global.buffItems={2000};Require(abilities.Bind(1,global),"replace donor's authoritative loadout");bonus(2,11,"replacement global priority supersedes old local winner");
    abilities.Remove(1);bonus(2,-3,"replacement donor removal preserves independent local bonus");bonus(3,0,"no global donor returns zero");
    int value=321;Check(!abilities.Bonus(0,UpgradeStat::Health,value)&&value==321&&!abilities.Bind(0,{})&&!abilities.Bind(33,{}),"player-slot boundary rejects without output mutation");
    Check(!abilities.Bonus(2,UpgradeStat(5),value)&&value==321,"invalid stat rejected");abilities.Clear();
    Check(!abilities.HasPlayer(2)&&!abilities.Bonus(2,UpgradeStat::Health,value),"clear removes local and global state");
}
}
