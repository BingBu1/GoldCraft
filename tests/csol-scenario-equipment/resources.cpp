// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace equipment_test
{
void ResourceCases()
{
    auto tables=RealEquipment();std::size_t buffs{},upgrades{};
    for(const auto& [id,rows]:tables->buffs)for(const auto& row:rows){++buffs;if(row.type==3||row.type==4||row.type==6||row.type==8||row.type==13)++upgrades;}
    Check(tables->buffSource.records.size()==681&&buffs==680&&upgrades==9,"actual680 buff rows include exactly9 five-stat upgrade records");
    const int items[]{809,810,825,891,8033};const int types[]{3,4,6,8,13};
    for(int i=0;i<5;++i)
    {
        const auto found=tables->buffs.find(items[i]);Require(found!=tables->buffs.end(),"original upgrade item exists");
        const auto row=std::find_if(found->second.begin(),found->second.end(),[&](const auto& r){return r.type==types[i];});
        Require(row!=found->second.end(),"original upgrade ability type decoded from correct column");
        Check(row->global&&!row->wornOnly&&!row->cumulative&&row->priority==0&&row->values[0]==5,
            "actual upgrade records are global inventory bonuses of five levels");
    }
    Check(tables->effects.empty()&&tables->effectTypes.size()==1&&tables->EffectType("NONE")==0,"current JSON has no clue effects and only NONE type");
    Check(tables->addons.size()==28&&tables->addons[19].item==3638&&tables->addons[19].Parameter("AddPercent")==100&&
        tables->addons[19].Parameter("AddPercentAdd")==0,"actual addon19 physical record and missing increment");
    EquipmentAbilities abilities(tables);EquipmentLoadout loadout;
    Require(abilities.Bind(1,loadout),"real addon unactivated player");std::optional<float> percent=99;
    Require(abilities.MoneyAddPercent(1,percent),"real addon inactive query");Check(!percent,"item table presence alone never activates free money ability");
    loadout.addonLevels[19]=9;Require(abilities.Bind(1,loadout)&&abilities.MoneyAddPercent(1,percent),"activate real addon level9");
    Check(percent&&*percent==100,"missing AddPercentAdd remains zero across positive levels");
    difficulty_test::Files files;files.contents[BuffPath]=difficulty_test::Read(resourceRoot/"BuffWeapon.cso");
    files.contents[SettingsPath]=difficulty_test::Read(resourceRoot/"ZBS.json");EquipmentResources resources;ResourceError error;
    Require(resources.Load(files.Api(),error),"equipment atomic snapshot");auto retained=resources.Snapshot();
    Check(files.order==std::vector<std::string>{std::string("load:")+BuffPath,std::string("free:")+BuffPath,
        std::string("load:")+SettingsPath,std::string("free:")+SettingsPath},"real equipment paths and owned engine releases in order");
    files.contents.erase(SettingsPath);Check(!resources.Load(files.Api(),error)&&!resources.Snapshot(),"failed next JSON load unpublishes previous equipment snapshot");
    Check(retained->addons.size()==28,"borrowed old equipment snapshot remains owned independently");
}
}
