// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/weapon_resources.hpp"
#include "../../native/csol/weapon_hud_paths.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <set>

int wmain(int argc,wchar_t**argv) {
    if(argc!=2)return 2;
    const auto root=std::filesystem::path(argv[1]);
    unsigned checks{},failures{};
    const auto check=[&](bool ok,const std::string& why) {++checks;if(!ok){++failures;std::cerr<<why<<'\n';}};
    std::set<std::string_view> all;
    for(auto* name:csol::M249EXResources)all.insert(name);
    for(auto* name:csol::Balrog11Resources)all.insert(name);
    for(auto* name:csol::BeamgunResources)all.insert(name);
    for(auto* name:csol::BeamgunLEResources)all.insert(name);
    for(auto name:all) {
        const auto source=csol::WeaponResourceSource(name);
        const auto path=root/source;
        if(csol::GeneratedWeaponResource(name).empty()) {
            check(std::filesystem::is_regular_file(path),std::string(source));
            if(std::filesystem::is_regular_file(path))check(std::filesystem::file_size(path)>0,std::string(source)+" empty");
            if(name.starts_with("sprites/csol/"))check(source==(name=="sprites/csol/640hud7_v2.spr"?"sprites/640hud7.spr":
                name=="sprites/csol/640hud3_v2.spr"?"sprites/640hud3.spr":
                name=="sprites/csol/640hud266_v2.spr"?"sprites/640hud266.spr":"sprites/"+std::string(name.substr(13))),std::string(name)+" atlas namespace");
        }
        std::cout<<"resource\t"<<name<<'\t'<<source<<'\n';
    }
    for(auto gun:{"m249ex","balrog11","beamgun","beamgunle"}) {
        const auto name=csol::WeaponHudList("weapon_"+std::string(gun));
        std::ifstream original(root/("sprites/weapon_"+std::string(gun)+".txt"));
        std::istringstream generated{std::string(csol::GeneratedWeaponResource(name))};
        int count{},actualCount{};original>>count;generated>>actualCount;
        check(count>=4 && count<=5 && actualCount==count,std::string(name)+" row count");
        for(int i=0;i<count;++i) {
            std::string token,atlas,actualToken,actualAtlas;int resolution{},x{},y{},w{},h{},ar{},ax{},ay{},aw{},ah{};
            original>>token>>resolution>>atlas>>x>>y>>w>>h;
            generated>>actualToken>>ar>>actualAtlas>>ax>>ay>>aw>>ah;
            check(original && generated,std::string(name)+" row parse");
            check(token==actualToken && actualAtlas=="csol/"+atlas+(atlas=="640hud7"||atlas=="640hud3"||atlas=="640hud266"?"_v2":"") && resolution==ar && x==ax && y==ay && w==aw && h==ah,std::string(name)+" original selection/ammo rectangle");
            check(all.contains("sprites/"+actualAtlas+".spr"),std::string(name)+" atlas listed");
        }
        std::string extra;check(!(generated>>extra),std::string(name)+" no invented rows");
    }
    check(csol::GeneratedSkullBalrogResource("sprites/weapon_ak47.txt").empty(),"other weapons retained");
    std::cout<<"resources "<<checks<<" checks, "<<failures<<" failures\n";return failures?1:0;
}
