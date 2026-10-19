#include "../../native/client/server_hud.hpp"
#include "../../native/client/hud_sprites.hpp"
#include "goldcraft/death_notice.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <iostream>

using namespace goldcraft;
namespace {
unsigned text_calls=0,loads=0,measurements=0,panels=0,sprites=0;
float red=0,green=0,blue=0;
std::vector<std::string> drawn;
std::vector<std::array<float,3>> colors;
std::vector<int> drawn_sprites;
int current_sprite=0;
client_sprite_t assets[3]{};
Bytes menu(unsigned keys,int lifetime,bool more,std::string_view value){
    Writer out;out.u16(static_cast<std::uint16_t>(keys));out.u8(static_cast<std::uint8_t>(lifetime));out.u8(more?1:0);
    out.data.insert(out.data.end(),value.begin(),value.end());out.u8(0);return out.data;
}
Bytes death(unsigned killer,unsigned victim,bool headshot,std::string_view weapon){
    Bytes out{static_cast<std::uint8_t>(killer),static_cast<std::uint8_t>(victim),static_cast<std::uint8_t>(headshot)};
    out.insert(out.end(),weapon.begin(),weapon.end());out.push_back(0);return out;
}
template<class F>void reject(F function){bool rejected=false;try{function();}catch(const ProtocolError&){rejected=true;}assert(rejected);}
void clear_draws(){drawn.clear();colors.clear();drawn_sprites.clear();}
}
int main(){
    FormMenu m;
    m.message(menu(1023,-1,false,"\\yAMXX inventory\n\\rRed \\wWhite\\R\\dDisabled\n\\gGreen"),1);
    assert(m.active(100)&&!m.forms()&&m.lines().size()==3);
    assert(m.lines()[1].size()==3&&m.lines()[1][0].color=='r'&&m.lines()[1][1].color=='w');
    assert(m.lines()[1][2].right&&m.lines()[1][2].color=='d');
    assert(m.selection('x',"slot9",2)==9&&m.selection('x',"slot10",2)==10);
    m.message(menu(513,-1,true,"A long arbitrary title\n"),3);assert(!m.active(3));
    m.message(menu(513,-1,false,"1. Item\n0. Exit"),3.01);assert(m.active(3.01));
    assert(m.selection('2',3.01)==-1&&m.selection('0',3.01)==10);
    assert(FormMenu::slot_key('x',"slot2")=='2'&&m.selection('x',"slot2",3.01)==-1);
    m.message(menu(768,1,false,"9. Next\n0. Back"),4);assert(m.active(4.99)&&!m.active(5));
    m.message(menu(0,-1,false,""),5);assert(!m.active(5));
    m.message(menu(1,-1,true,"old"),6);
    reject([&]{m.message(menu(2,-1,false,"different"),6.1);});assert(!m.active(6.1));
    m.message(menu(1,-1,true,"old"),7);
    reject([&]{m.message(menu(1,-1,false,"late"),10);});assert(!m.active(10));
    auto broken=menu(1,-1,false,"a");broken.insert(broken.end()-1,0);
    reject([&]{m.message(broken,10);});
    for(int lifetime:{0,-1,-128}){m.message(menu(1,lifetime,false,"indefinite"),11);assert(m.active(1000));}
    for(unsigned i=0;i<8;++i)m.message(menu(1,-1,true,std::string(500,'a')),12+i*.01);
    reject([&]{m.message(menu(1,-1,false,std::string(500,'a')),12.1);});
    auto extended=death(1,2,true,"ak47");extended.insert(extended.end(),{1,0,0,0,0});
    assert(read_death_notice(extended).weapon=="ak47"&&read_death_notice(extended).headshot);
    auto invalid=death(1,33,false,"knife");reject([&]{read_death_notice(invalid);});
    invalid=death(1,2,false,"knife");invalid.pop_back();reject([&]{read_death_notice(invalid);});
    reject([&]{read_death_notice(death(1,2,false,std::string(64,'a')));});

    cl_enginefunc_t engine{};
    engine.pfnGetCvarPointer=[](const char*)->cvar_t*{return nullptr;};
    engine.pfnGetPlayerInfo=[](int slot,hud_player_info_t* out){out->name=const_cast<char*>(slot==1?"Killer":"Victim");};
    engine.pfnDrawConsoleStringLen=[](const char* value,int* width,int* height){++measurements;*width=static_cast<int>(std::strlen(value))*8;*height=16;};
    engine.pfnGetScreenInfo=[](SCREENINFO* out){out->iWidth=1280;out->iHeight=720;return 1;};
    const char* names[]={"d_ak47","d_headshot","d_skull"};
    for(int i=0;i<3;++i){std::strcpy(assets[i].szName,names[i]);std::strcpy(assets[i].szSprite,names[i]);assets[i].iRes=640;assets[i].rc={0,32,0,16};}
    engine.pfnSPR_GetList=[](char*,int* size){*size=3;return assets;};
    engine.pfnSPR_Load=[](const char*){return static_cast<int>(++loads);};
    engine.pfnSPR_Set=[](int sprite,int,int,int){current_sprite=sprite;};
    engine.pfnSPR_DrawAdditive=[](int,int,int,const wrect_t*){++sprites;drawn_sprites.push_back(current_sprite);};
    engine.pfnFillRGBA=[](int,int,int,int,int,int,int,int){++panels;};
    engine.pfnDrawSetTextColor=[](float r,float g,float b){red=r;green=g;blue=b;};
    engine.pfnDrawConsoleString=[](int x,int,char* value){++text_calls;drawn.emplace_back(value);colors.push_back({red,green,blue});return x+static_cast<int>(std::strlen(value))*8;};
    server_hud::initialize(&engine);server_hud::reset();
    server_hud::team(Bytes{1,'T','E','R','R','O','R','I','S','T',0});
    server_hud::team(Bytes{2,'C','T',0});
    server_hud::death(extended,20);
    m.message(menu(513,-1,false,"\\y任意 AMXX 菜单\n\\r1. Item \\wReady\\R\\d0. Exit"),20);
    server_hud::draw(20,m,true);
    assert(loads==3&&sprites==2&&panels==1&&drawn[0]=="Killer"&&drawn[1]=="Victim");
    assert(colors[0][0]==1.f&&colors[0][1]<.3f&&colors[1][2]==1.f);
    assert(drawn_sprites[0]==1&&drawn_sprites[1]==2);
    const unsigned old_measure=measurements,old_load=loads;
    for(int i=0;i<100;++i){clear_draws();server_hud::draw(20.01,m,true);}
    assert(measurements==old_measure&&loads==old_load);
    const auto prior_panels=panels;server_hud::draw(21,m,false);assert(panels==prior_panels);
    for(int i=0;i<8;++i)server_hud::death(death(1,2,false,"unknown"),21+i*.1);
    assert(server_hud::notices(22)==4);
    clear_draws();server_hud::draw(22,m,false);assert(drawn_sprites.size()==4&&drawn_sprites[0]==3);
    assert(server_hud::notices(28)==0);clear_draws();server_hud::draw(28,m,false);assert(drawn.empty());
    server_hud::death(death(2,2,false,"world"),29);clear_draws();server_hud::draw(29,m,false);assert(drawn[0].empty()&&drawn[1]=="Victim");
    server_hud::reset();assert(server_hud::notices(29)==0);
    server_hud::video_reset();server_hud::draw(29,m,true);assert(loads==old_load+3);
    // Both the native client and hosted HUD consume this same extended list.
    hud_sprites::bind(engine.pfnSPR_GetList);
    engine.pfnSPR_GetList=hud_sprites::list;
    int count=0;char hud_path[]="sprites/hud.txt", other_path[]="sprites/weapon.txt";
    auto* extended_list=engine.pfnSPR_GetList(hud_path,&count);
    assert(count==5&&extended_list[3].iRes==320&&extended_list[4].iRes==640);
    assert(!std::strcmp(extended_list[4].szName,"d_infection")&&extended_list[4].rc.right==20);
    assert(engine.pfnSPR_GetList(hud_path,&count)==extended_list);
    assert(engine.pfnSPR_GetList(other_path,&count)==assets&&count==3);
    server_hud::initialize(&engine);server_hud::reset();server_hud::video_reset();
    const auto old_infection_load=loads;
    server_hud::death(death(1,2,false,"infection"),30);clear_draws();server_hud::draw(30,m,false);
    assert(loads==old_infection_load+4&&drawn_sprites.size()==1&&drawn_sprites[0]==static_cast<int>(loads));
    server_hud::reset();server_hud::death(death(1,2,false,"ak47"),31);clear_draws();server_hud::draw(31,m,false);
    assert(drawn_sprites.size()==1&&drawn_sprites[0]==static_cast<int>(old_infection_load+1));
    std::strcpy(assets[0].szName,"d_infection");std::strcpy(assets[0].szSprite,"custom_infection");
    hud_sprites::bind([](char*,int* n){*n=3;return assets;});
    extended_list=hud_sprites::list(hud_path,&count);
    assert(count==4&&!std::strcmp(extended_list[0].szSprite,"custom_infection"));
    std::cout<<"PASS: arbitrary/fragmented/paged/colored menus, cancellation/timeout, native slots, real HUD draw routing, team/weapon/headshot/world deaths, bounded expiry and warm layout cache\n";
}
