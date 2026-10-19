#include "../../native/client/spectator_menu.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
using namespace goldcraft::spectator_menu::detail;
namespace {
Menu menu{};Node nodes[3]{};int items[4]{};
int armed{},opened{},selected{},disarmed{},mode{};
void* resolve(Menu*,std::uint32_t handle){return handle<4?&items[handle]:nullptr;}
void arm(void*){++armed;if(mode==1)menu.head=-1;}
void open(void*){++opened;if(mode==2)menu.head=-1;if(mode==3){nodes[0].item=&items[2];nodes[1].item=&items[0];}}
void select(Menu*,int i){++selected;assert(i==(mode==3?1:0));}
void disarm(void*){++disarmed;}
const Calls calls{resolve,arm,open,select,disarm};
void reset(){menu={};menu.nodes=nodes;menu.capacity=3;menu.head=0;nodes[0]={&items[0],-1,1};nodes[1]={&items[1],0,-1};armed=opened=selected=disarmed=mode=0;}
}
int main(){
    reset();assert(dispatch(&menu,0,true,calls));assert(armed==1&&opened==1&&selected==1);
    assert(dispatch(&menu,0,false,calls));assert(disarmed==1);
    for(bool enter:{false,true}){reset();assert(!dispatch(&menu,2,enter,calls));assert(!armed&&!opened&&!selected&&!disarmed);}
    reset();menu.head=-1;assert(!dispatch(&menu,0,true,calls));assert(!armed);
    reset();mode=1;assert(!dispatch(&menu,0,true,calls));assert(armed==1&&!opened&&!selected);
    reset();mode=2;assert(!dispatch(&menu,0,true,calls));assert(armed==1&&opened==1&&!selected);
    reset();mode=3;assert(dispatch(&menu,0,true,calls));assert(selected==1);
    reset();menu.input_mode=1;assert(dispatch(&menu,99,true,calls));assert(!armed&&!selected);
    reset();nodes[1].next=0;assert(!dispatch(&menu,2,true,calls));assert(!armed);
    reset();nodes[1].next=99;assert(!dispatch(&menu,2,true,calls));assert(!armed);
    reset();assert(!dispatch(&menu,99,true,calls));assert(!armed);
    std::puts("Menu guard: stale/empty/invalid events, enter/exit, keyboard mode and reentrant removal/reorder passed");
}
