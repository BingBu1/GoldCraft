// SPDX-License-Identifier: GPL-3.0-or-later
// Both the current and pinned upstream map-buy tables are included by runner.
#include "../../native/csol/buy_menu.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include "weapon_ids.inc"
constexpr int CT=2,TERRORIST=1;
bool scenario{};
bool CSOL_UsesScenarioRules(){return scenario;}
bool CSOL_ExtendedWeapon(int id){return id==144||id==220||id==725||id==726;}
#include "actual_team_helper.inc"
#include "actual_team_table.inc"
#include "upstream_team_table.inc"
int main(){try{
    unsigned checks{};
    const int nativeIds[]{1,3,5,7,8,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,26,27,28,30};
    for(int mode=0;mode<2;++mode)for(int team=0;team<4;++team)for(int as=0;as<2;++as)for(int id=0;id<=2002;++id){
        scenario=mode!=0;const bool added=scenario&&team==CT&&std::find(std::begin(nativeIds),std::end(nativeIds),id)!=std::end(nativeIds);
        const bool actual=CanBuyWeaponByMaptype(team,static_cast<WeaponIdType>(id),as!=0);
        const bool expected=added||UpstreamCanBuyWeaponByMaptype(team,static_cast<WeaponIdType>(id),as!=0);
        if(actual!=expected)throw std::runtime_error("mode/team/map/weapon table mismatch");
        if(CSOL_CanBuyScenarioNativeWeapon(team,id)!=added)throw std::runtime_error("scenario helper admits unsupported product");
        checks+=2;
    }
    std::cout<<"buy team tables: "<<checks<<" comparisons passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
