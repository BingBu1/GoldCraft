// SPDX-License-Identifier: GPL-3.0-or-later
// The build inserts the actual Waypoint::FindLoosedBomb definition below.
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>

struct edict_t { struct { const char* model; } v; int origin; };
std::array<edict_t,3> first{{{{""},0},{{"models/w_backpack.mdl"},12},{{"models/w_ak47.mdl"},0}}};
std::array<edict_t,2> second{{{{""},0},{{"models/w_backpack.mdl"},84}}};
edict_t* entities=first.data();int count=3;int searches=0;
constexpr auto null=nullptr;
const char* STRING(const char* value){return value;}
bool FNullEnt(edict_t* e){return e==nullptr;}
int GetEntityOrigin(edict_t* e){return e->origin;}
edict_t* FIND_ENTITY_BY_CLASSNAME(edict_t* after,const char* name)
{
    if(std::strcmp(name,"weaponbox"))throw std::runtime_error("unexpected class");
    ++searches;int start=0;
    if(after){
        for(;start<count && &entities[start]!=after;++start){}
        if(start==count)throw std::runtime_error("NUM_FOR_EDICT: stale map cursor");
        ++start;
    }
    return start<count?&entities[start]:nullptr;
}
class Waypoint { public:int FindLoosedBomb();int FindNearest(int point){return point;} };
Waypoint waypoint;Waypoint* g_waypoint=&waypoint;
#include "bomb_function.inc"
int main()
{
    int checks=0;
    auto check=[&](bool ok){++checks;if(!ok)throw std::runtime_error("check "+std::to_string(checks));};
    try{
        check(waypoint.FindLoosedBomb()==12);
        // Map changes replace the edict range; old addresses can stay mapped.
        first[1].v.model="models/w_ak47.mdl";entities=second.data();count=2;
        check(waypoint.FindLoosedBomb()==84);
        second[1].origin=93;check(waypoint.FindLoosedBomb()==93);
        second[1].v.model="";check(waypoint.FindLoosedBomb()==-1);
        second[1].v.model="x";check(waypoint.FindLoosedBomb()==-1);
        second[1].v.model="otherdir/backpack.mdl";check(waypoint.FindLoosedBomb()==-1);
        second[1].v.model="models/w_backpack.mdl";check(waypoint.FindLoosedBomb()==93);
        entities=first.data();count=3;check(waypoint.FindLoosedBomb()==-1);
        first[1].v.model="models/w_backpack.mdl";check(waypoint.FindLoosedBomb()==12);
        for(int i=0;i<100;++i){entities=i%2?first.data():second.data();count=i%2?3:2;check(waypoint.FindLoosedBomb()==(i%2?12:93));}
        std::cout<<"PASS "<<checks<<" lifecycle checks, "<<searches<<" engine searches\n";
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
