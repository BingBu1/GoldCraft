// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
#include <cmath>
namespace equipment_test
{
void EffectCases()
{
    auto tables=EffectFixture();EquipmentAbilities abilities(tables);EquipmentLoadout loadout;
    loadout.carriedItems={11,12};loadout.equippedItems[0]=13;loadout.equippedItems[9]=14;loadout.extraItem=15;
    Require(abilities.Bind(1,loadout),"ordered carried then ten equipped slots then signed extra item");
    float value=3;Require(abilities.Effect(1,"EXP_RATIO",value),"type-number alias effect dispatch");
    Check(value==17.6f,"3+.75 ->7.5 ->MAX8.2 ->8.8 ->17.6 preserves exact collection order");
    int integer=3;Require(abilities.IntegerEffect(1,"EXP_RATIO",integer),"integer effect boundary");
    Check(integer==17,"only completed effect chain truncates, not each stage which would give16");
    Check(tables->effects.count(0)==0&&tables->effects.count(99)==0,"zero ID and incomplete clue record skipped");
    loadout={};loadout.carriedItems={40,40};Require(abilities.Bind(1,loadout),"duplicate item occurrences retained");
    value=1;Require(abilities.Effect(1,"XP_ALIAS",value),"same numeric type under a second name");
    Check(value==10&&tables->effects.at(40)[0].resourceName=="first"&&tables->effects.at(40)[1].resourceName=="second",
        "repeated item vectors preserve duplicate records and item occurrence order");
    loadout={};loadout.carriedItems={16,17,18};Require(abilities.Bind(1,loadout),"unknown type and operator fixture");
    const auto names=tables->effectTypes.size();value=4;Require(abilities.Effect(1,"unlisted-request",value),"unknown request resolves original type0");
    Check(value==14&&tables->effectTypes.size()==names,"unknown type aliases share0 without mutating immutable map");
    value=4;Require(abilities.Effect(1,"EXP_RATIO",value),"unknown operator dispatch");Check(value==4,"unknown operator is no-op, not implicit add");
    loadout={};loadout.carriedItems={19,20};Require(abilities.Bind(1,loadout),"MAX signed-zero fixture");
    value=-0.0f;Require(abilities.Effect(1,"ZERO",value),"MAX selects positive second zero");Check(!std::signbit(value),"equal MAXSS operands select second sign");
    value=0.0f;Require(abilities.Effect(1,"NEGZERO",value),"MAX selects negative second zero");Check(std::signbit(value),"negative parameter zero is preserved by MAXSS equality");
    loadout={};loadout.extraItem=-1;Require(abilities.Bind(1,loadout),"signed16 negative extra item");
    value=1;Require(abilities.Effect(1,"JUMP",value),"signed extra item participates");Check(value==8,"extraItem is signed and does not wrap to65535");

    // Physical row19 deliberately has a nonmatching JSON ID and itemID. Empty
    // preceding records retain positions; neither ID field chooses the index.
    std::string json="{\"ZBS_Addon\":{\"Addons\":[";
    for(int i=0;i<19;++i){if(i)json+=',';json+="{}";}
    json+=",{\"ID\":999,\"ItemID\":77,\"Params\":{\"AddPercent\":25,\"AddPercentAdd\":10}}]}}";
    auto addons=std::make_shared<EquipmentTables>();ConfigError error;
    Require(ReadEquipmentSettings(json,*addons,error),"physical addon fixture parse");
    Check(addons->addons.size()==20&&addons->addons[0].item==0&&addons->addons[19].item==77,"missing fields retain physical rows before19");
    EquipmentAbilities money(addons);EquipmentLoadout activation;activation.addonLevels[999]=3;activation.addonLevels[77]=3;
    Require(money.Bind(1,activation),"non-index IDs cannot activate addon19");std::optional<float> percent=99;
    Require(money.MoneyAddPercent(1,percent),"unactivated money query");Check(!percent,"JSON ID and item ID are not activation index");
    activation.addonLevels[19]=3;Require(money.Bind(1,activation)&&money.MoneyAddPercent(1,percent),"physical addon19 active at level3");
    Check(percent&&*percent==45,"AddPercent plus level-minus-one increments");
    activation.addonLevels[19]=0;Require(money.Bind(1,activation)&&money.MoneyAddPercent(1,percent),"inactive level0");Check(!percent,"zero addon level yields absent modifier");
    auto missing=std::make_shared<EquipmentTables>();Require(ReadEquipmentSettings("{\"ZBS_Addon\":{\"Addons\":[{}]}}",*missing,error),"missing physical19 resource");
    EquipmentAbilities missingMoney(missing);activation.addonLevels[19]=1;Require(missingMoney.Bind(1,activation)&&missingMoney.MoneyAddPercent(1,percent),"active absent row query");
    Check(percent&&*percent==0,"active missing addon parameters produce present zero, not absent");
}
}
