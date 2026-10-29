// SPDX-License-Identifier: GPL-3.0-or-later
#include "fixture.hpp"
namespace equipment_test
{
std::shared_ptr<const EquipmentTables> RealEquipment()
{
    difficulty_test::Files files;
    files.contents[BuffPath]=difficulty_test::Read(resourceRoot/"BuffWeapon.cso");
    files.contents[SettingsPath]=difficulty_test::Read(resourceRoot/"ZBS.json");
    EquipmentResources resources;ResourceError error;
    const bool loaded=resources.Load(files.Api(),error);
    if(!loaded)std::cerr<<"RESOURCE "<<error.path<<" "<<error.detail.message<<'\n';
    Require(loaded,"actual encrypted buff and plain JSON load through production resource API");
    return resources.Snapshot();
}
std::shared_ptr<EquipmentTables> EffectFixture()
{
    auto tables=std::make_shared<EquipmentTables>();ConfigError error;
    Require(ReadEquipmentSettings(R"json({"ZBS_ClueItem":{
        "Type":{"NONE":0,"EXP_RATIO":10,"XP_ALIAS":10,"JUMP":20,"ZERO":21,"NEGZERO":22,
            "INITIAL_HEALTH":31,"MAX_HEALTH_LEVEL":32},
        "OperatorType":{"RATIO":1,"MAX":2,"ADD":3},
        "ClueItems":[
            {"itemID":11,"type":"XP_ALIAS","value":0.75,"resourceName":"a","operatorType":"ADD"},
            {"itemID":12,"type":"EXP_RATIO","value":2,"resourceName":"b","operatorType":"RATIO"},
            {"itemID":13,"type":"EXP_RATIO","value":8.2,"resourceName":"c","operatorType":"MAX"},
            {"itemID":14,"type":"XP_ALIAS","value":0.6,"resourceName":"d","operatorType":"ADD"},
            {"itemID":15,"type":"EXP_RATIO","value":2,"resourceName":"e","operatorType":"RATIO"},
            {"itemID":16,"type":"missing_A","value":3,"resourceName":"u","operatorType":"ADD"},
            {"itemID":17,"type":"missing_B","value":2,"resourceName":"v","operatorType":"RATIO"},
            {"itemID":18,"type":"EXP_RATIO","value":99,"resourceName":"noop","operatorType":"missing_op"},
            {"itemID":19,"type":"ZERO","value":0.0,"resourceName":"z","operatorType":"MAX"},
            {"itemID":20,"type":"NEGZERO","value":-0.0,"resourceName":"z","operatorType":"MAX"},
            {"itemID":21,"type":"JUMP","value":0.5,"resourceName":"j","operatorType":"RATIO"},
            {"itemID":31,"type":"INITIAL_HEALTH","value":2,"resourceName":"h","operatorType":"ADD"},
            {"itemID":32,"type":"MAX_HEALTH_LEVEL","value":3,"resourceName":"h","operatorType":"ADD"},
            {"itemID":-1,"type":"JUMP","value":7,"resourceName":"signed","operatorType":"ADD"},
            {"itemID":40,"type":"EXP_RATIO","value":1,"resourceName":"first","operatorType":"ADD"},
            {"itemID":40,"type":"EXP_RATIO","value":2,"resourceName":"second","operatorType":"RATIO"},
            {"itemID":0,"type":"EXP_RATIO","value":1000,"resourceName":"skip","operatorType":"ADD"},
            {"itemID":99,"type":"EXP_RATIO","value":1000,"operatorType":"ADD"}
        ]}})json",*tables,error),"controlled effect records parse through production JSON reader");
    return tables;
}
}
