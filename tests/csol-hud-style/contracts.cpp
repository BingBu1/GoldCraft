// SPDX-License-Identifier: GPL-3.0-or-later
// The generated includes are exact production function spans, not rewrites.
// Their source identities/ranges are registered by prepare.py. Full weapons
// and beamgun translation units are compiled separately in the same plan.
#include "native/csol/client/runtime.hpp"
#include "native/csol/hud_style.hpp"
#include "native/csol/weapons/beamgun_prediction.hpp"
#include "native/csol/weapons/beamgun_state.cpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <map>
#include <set>
#include <source_location>
#include <stdexcept>

namespace fixture {
unsigned checks{},failures{};
void Check(bool value,const char* text,std::source_location at=std::source_location::current())
{++checks;if(!value){++failures;std::cerr<<"FAIL "<<at.line()<<' '<<text<<'\n';}}
}
namespace native_draw {
using fixture::Check;
struct Sprite {int handle{};wrect_t rect{};};
Sprite nativeDigits[10],currency[3];
std::array<Sprite,5> nativeVitals;
std::array<Sprite,2> nativeAmmo;
#include "generated/native_state.inc"
csol::hud_style::Vitals vitalValues{1700,700,true,true};
SCREENINFO screen{};
bool drawingNative=true,vitals=true,weapon=true,moneyVisible=true,adjustMoney=true,moneyAnchor{};
int currentSprite{},moneyX{},moneyY{},moneyHeight=22,shiftedMoneySprites{};
struct {bool enabled{},drawn{},hasClip{},infinite{};int value{},x{},clipX{},y{},width{},height{},shift{};} wideAmmo;
int nativeDigitSprites[10]{},spriteR{},spriteG{},spriteB{},active{},wideAmmoDraws{};
Sprite ammoIcon;
cl_enginefunc_t engine{};
struct FillCall {int x,y,w,h;};
std::vector<FillCall> fills;
struct SpriteCall {int frame,x,y;wrect_t rect;};
std::vector<SpriteCall> spriteCalls;
bool HudStyleVitalsVisible(){return vitals;}
csol::hud_style::Vitals HudStyleVitalsState(){return vitalValues;}
bool HudStyleWeaponVisible(){return weapon;}
bool HudStyleMoneyVisible(){return moneyVisible;}
float HudStyleScale(){return 1;}
bool AmmoArea(int,int){return false;}
bool DrawCharacterSkillInfiniteAmmo(int,int,int,int,int,int){return false;}
void originalSet(int,int,int,int){}
void originalDraw(int frame,int x,int y,const wrect_t* rect){spriteCalls.push_back({frame,x,y,rect?*rect:wrect_t{}});}
void originalFill(int x,int y,int w,int h,int,int,int,int){fills.push_back({x,y,w,h});}
#include "generated/native_draw.inc"
void Run()
{
    screen.iWidth=1280;screen.iHeight=720;
    for(int d=0;d<10;++d)nativeDigits[d]={1,{d*20,d*20+20,0,22}};
    for(int i=0;i<5;++i)nativeVitals[i]={2,{i*16,i*16+16,0,16}};
    nativeAmmo={Sprite{3,{0,24,0,24}},Sprite{3,{24,48,0,24}}};
    currency[0]={1,{200,220,0,22}};currency[1]={1,{220,240,0,22}};currency[2]={1,{240,260,0,22}};
    const int base=687; // 720 - 22 - floor(22/2), fixed B PE evidence.
    nativeVitalRuns={};PrepareStyledNativeAmmo(true,true,true);currentSprite=1;
    Check(!HideStyledSprite(26,base,&nativeDigits[1].rect),"unanchored shared health digit is not hidden");
    currentSprite=2;Check(HideStyledSprite(8,base,&nativeVitals[0].rect),"health icon establishes left anchor");
    currentSprite=1;Check(HideStyledSprite(26,base,&nativeDigits[1].rect),"health baseline digit hidden after icon");
    currentSprite=2;Check(HideStyledSprite(256,base+2,&nativeVitals[1].rect),"armor icon has distinct baseline");
    auto clipped=nativeVitals[2].rect;clipped.top+=7;
    Check(HideStyledSprite(256,base+9,&clipped),"cropped armor fill recognized without exact full rect");
    currentSprite=1;Check(HideStyledSprite(272,base,&nativeDigits[7].rect),"armor number uses numeric baseline, not icon y");
    Check(!HideStyledSprite(272,base+2,&nativeDigits[7].rect),"same glyph on armor icon y is not assumed numeric");
    Check(!HideStyledSprite(600,base,&nativeDigits[7].rect),"unrelated left half digit outside anchor span retained");
    Check(HideStyledSprite(1096,base,&nativeDigits[9].rect),"ammo digits precede icon and hide at original clip cell");
    Check(HideStyledSprite(1176,base-22-5,&nativeDigits[3].rect),"secondary digits use distinct native baseline and anchor");
    Check(!HideStyledSprite(1160,base-44,&nativeDigits[3].rect),"nearby money or pickup row survives");
    auto other=nativeDigits[3].rect;other.left++;
    Check(!HideStyledSprite(1160,base,&other),"same atlas non-digit crop survives");
    currentSprite=3;Check(HideStyledSprite(1238,base-3,&nativeAmmo[0].rect),"primary ammo icon hidden at original post-reserve position");
    currentSprite=99;Check(!HideStyledSprite(1238,base-3,&nativeAmmo[0].rect),"foreign texture with same crop survives");
    currentSprite=1;drawingNative=false;Check(!HideStyledSprite(1080,base,&nativeDigits[9].rect),"custom draw outside downstream not filtered");drawingNative=true;
    vitals=weapon=false;Check(!HideStyledSprite(26,base,&nativeDigits[1].rect)&&!HideStyledSprite(1080,base,&nativeDigits[1].rect),"fallback leaves original numbers intact");vitals=weapon=true;
    Fill(1166,base,2,22,255,160,0,255);Check(fills.empty(),"only native ammo divider suppressed");
    Fill(1166,base-1,2,22,255,160,0,255);Fill(1166,base,3,22,255,160,0,255);Fill(100,base,2,22,255,160,0,255);
    Check(fills.size()==3,"different row width or x filled normally");
    Fill(1166,base,2,22,255,160,0,255);Check(fills.size()==4,"same-location later fill is not consumed twice");
    weapon=false;Fill(1166,base,2,22,255,160,0,255);Check(fills.size()==5,"disabled replacement retains divider");weapon=true;
    const int field=1280-5*20-16,account=720-3*22,delta=account-22-11;
    currentSprite=1;Check(!NativeMoneySprite(field,account,&nativeDigits[4].rect),"account digit needs its dollar anchor");
    Check(NativeMoneySprite(field-20,account,&currency[0].rect),"native dollar establishes exact account anchor");
    Check(NativeMoneySprite(field,account,&nativeDigits[4].rect),"native account numeric row follows dollar anchor");
    Check(NativeMoneySprite(field,delta,&nativeDigits[4].rect),"income or purchase delta keeps its distinct original row");
    Check(NativeMoneySprite(field-20,delta,&currency[1].rect)&&NativeMoneySprite(field-20,delta,&currency[2].rect),"native plus and minus are classified only at delta symbol anchor");
    Check(!NativeMoneySprite(field+5*20,account,&nativeDigits[4].rect)&&!NativeMoneySprite(field-20,delta+1,&currency[1].rect),"sixth cell and nearby sign are not account sprites");
    Check(!NativeMoneySprite(field,account,nullptr),"null crop cannot claim money ownership");
    Check(MoneySpriteY(field-20,account,&currency[0].rect)==account-22,"legacy beam fallback moves native money by exactly one line");
    Check(MoneySpriteY(field,account,&nativeDigits[4].rect)==account-22,"legacy account number follows one-line displacement");
    Check(MoneySpriteY(field,delta,&nativeDigits[4].rect)==delta-22,"legacy delta retains relative offset after one-line displacement");
    Check(MoneySpriteY(field+1,account,&nativeDigits[4].rect)==account,"unaligned shared glyph retained");
    Check(MoneySpriteY(field,base,&nativeDigits[4].rect)==base,"ammo row not shifted as money");
    currentSprite=99;Check(MoneySpriteY(field,account,&nativeDigits[4].rect)==account,"foreign atlas not shifted");
    // Exercise the exact DrawSprite call site, not just its classifier.
    currentSprite=1;moneyAnchor=false;spriteCalls.clear();
    DrawSprite(0,field,account,&nativeDigits[4].rect);
    Check(spriteCalls.size()==1&&spriteCalls.back().y==account,"unanchored shared glyph reaches downstream unchanged");
    DrawSprite(0,field-20,account,&currency[0].rect);
    for(int digit=0;digit<5;++digit)DrawSprite(0,field+20*digit,account,&nativeDigits[digit].rect);
    DrawSprite(0,field-20,delta,&currency[1].rect);DrawSprite(0,field,delta,&nativeDigits[7].rect);
    Check(spriteCalls.size()==1,"modern account hides exact native dollar amount and expense row");
    DrawSprite(0,field+1,account,&nativeDigits[4].rect);DrawSprite(0,field,account+1,&nativeDigits[4].rect);
    currentSprite=99;DrawSprite(0,field,account,&nativeDigits[4].rect);
    Check(spriteCalls.size()==4,"modern account does not swallow unrelated positions or atlases");
    currentSprite=1;moneyVisible=false;moneyAnchor=false;
    DrawSprite(0,field-20,account,&currency[0].rect);DrawSprite(0,field,delta,&nativeDigits[4].rect);
    Check(spriteCalls.size()==6&&spriteCalls[4].y==account-22&&spriteCalls[5].y==delta-22,"disabled modern money preserves old beam fallback path");
    adjustMoney=false;moneyAnchor=false;DrawSprite(0,field-20,account,&currency[0].rect);
    DrawSprite(0,field,account,&nativeDigits[4].rect);
    Check(spriteCalls.size()==8&&spriteCalls[6].y==account&&spriteCalls[7].y==account,"no modern money or secondary ammo keeps original account coordinates");
    moneyVisible=adjustMoney=true;moneyAnchor=false;drawingNative=false;
    DrawSprite(0,field-20,account,&currency[0].rect);Check(spriteCalls.size()==9&&spriteCalls.back().y==account,"foreign draw outside native redraw retains money-looking art");drawingNative=true;
    for(int width:{320,480,640,1280})for(int rows:{3,5})
    {
        screen.iWidth=width;moneyAnchor=false;const int x=width-5*20-(width>=640?16:12),y=720-rows*22;
        Check(NativeMoneySprite(x-20,y,&currency[0].rect),"both stock money baselines and resolution offsets are recognized");
        Check(NativeMoneySprite(x+80,y,&nativeDigits[9].rect)&&NativeMoneySprite(x,y-33,&nativeDigits[2].rect),"last amount cell and delta remain classified across native resolutions");
    }
    // Wide values must consume the exact digit run past the screen midline.
    // A later timer on the same baseline/atlas must remain drawable.
    weapon=false;vitalValues.health=vitalValues.armor=2147483647;
    for(int width:{320,480})
    {
        screen.iWidth=width;nativeVitalRuns={};
        for(int which:{0,1})
        {
            currentSprite=2;const int iconX=which?width/5:8;
            Check(HideStyledSprite(iconX,base+(which?2:0),&nativeVitals[which].rect),"narrow viewport establishes exact native vital anchor");
            const int firstX=which?iconX+16:26;
            currentSprite=1;int index{};
            for(const char digit:std::string("2147483647"))
                Check(HideStyledSprite(firstX+20*index++,base,&nativeDigits[digit-'0'].rect),"INT_MAX vital consumes all ten native digits past midline");
            Check(!HideStyledSprite(width/2,base,&nativeDigits[1].rect),"central timer sharing atlas and baseline survives completed vital run");
            Check(!HideStyledSprite(firstX+200,base,&nativeDigits[1].rect),"eleventh digit not mistaken for vital");
        }
        weapon=true;PrepareStyledNativeAmmo(true,true,true);currentSprite=1;
        Check(!HideStyledSprite(width/2,base,&nativeDigits[1].rect),"both replacements keep central clock outside exact ammo cells");
        const int clipX=width-184,reserveX=width-102,secondaryX=width-104;
        for(const auto& field:std::array<std::array<int,2>,3>{{{clipX,base},{reserveX,base},{secondaryX,base-27}}})
            for(int cell=0;cell<3;++cell)
            {
                const int x=field[0]+20*cell,y=field[1];
                Check(HideStyledSprite(x,y,&nativeDigits[2].rect),"exact native ammo cell consumed once");
                Check(!HideStyledSprite(x,y,&nativeDigits[2].rect),"later same-cell shared digit retained");
                Check(!HideStyledSprite(x+1,y,&nativeDigits[2].rect),"unaligned shared digit retained");
            }
        currentSprite=3;
        for(const auto& icon:std::array<std::array<int,3>,2>{{{width-42,base-3,0},{width-44,base-30,1}}})
        {
            Check(HideStyledSprite(icon[0],icon[1],&nativeAmmo[icon[2]].rect),"exact native ammo icon consumed");
            Check(!HideStyledSprite(icon[0],icon[1],&nativeAmmo[icon[2]].rect),"subsequent matching icon retained");
        }
        PrepareStyledNativeAmmo(false,true,false);currentSprite=1;
        Check(!HideStyledSprite(clipX,base,&nativeDigits[1].rect),"weapon without magazine has no clip field");
        Check(HideStyledSprite(secondaryX,base,&nativeDigits[1].rect),"total-only native ammunition uses original four-cell offset");
        Check(!HideStyledSprite(secondaryX,base-27,&nativeDigits[1].rect),"absent secondary ammo does not reserve its row");
        weapon=false;
    }
    nativeDigits[0].rect.right=0;Check(!HideStyledSprite(1080,base,&nativeDigits[1].rect),"invalid native dimensions fall back");
}
}
namespace precache {
using fixture::Check;
std::map<std::string,std::vector<unsigned char>> files;
std::set<unsigned char*> allocated;
std::vector<std::string> cached;
unsigned errors{};
enum {at_error};
void ALERT(int,const char*,...){++errors;}
unsigned char* LOAD_FILE_FOR_ME(const char* path,int* length)
{
    const auto it=files.find(path);if(it==files.end()){*length=0;return nullptr;}
    *length=int(it->second.size());auto* bytes=new unsigned char[std::max(1,*length)];
    if(*length)std::memcpy(bytes,it->second.data(),*length);allocated.insert(bytes);return bytes;
}
void FREE_FILE(unsigned char* p){Check(allocated.erase(p)==1,"server frees each loaded file once");delete[] p;}
int PRECACHE_GENERIC(const char* path){cached.emplace_back(path);return int(cached.size());}
#include "generated/server_precache.inc"
void Put(const std::string& path,const std::string& value){files[path]={value.begin(),value.end()};}
void Run()
{
    const std::string json=R"({"version":1,"weapons":[{"name":"weapon_cannon","title":"黑龙炮","weapon":"resource/csol/hud/v1/cannon.tga","ammo":"resource/csol/hud/v1/ammo.tga","ammo2":""}],"portraits":[{"id":1,"path":"resource/csol/hud/v1/p1.tga"}]})";
    PrecacheHudStyle();Check(cached.empty()&&errors==0,"absent optional style does not fail legacy server");
    Put(csol::hud_style::CatalogPath,"{");PrecacheHudStyle();Check(cached.empty()&&errors==1,"bad installed catalog adds no download entries");
    const auto paths=csol::hud_style::ResourcePaths(csol::hud_style::ParseCatalog(json));
    const auto setup=[&]{files.clear();cached.clear();for(const auto& p:paths)Put(p,"data");Put(csol::hud_style::CatalogPath,json);};
    setup();PrecacheHudStyle();Check(cached==paths&&allocated.empty(),"complete closure precached once in deterministic order");
    for(const auto& p:paths)if(p!=csol::hud_style::CatalogPath)
    {
        setup();files.erase(p);const auto before=errors;PrecacheHudStyle();
        Check(cached.empty()&&errors==before+1&&allocated.empty(),"every missing dependency fails before first precache");
        setup();files[p].clear();PrecacheHudStyle();Check(cached.empty()&&allocated.empty(),"empty dependency also keeps original HUD");
    }
}
}
namespace beam {
using fixture::Check;
using namespace csol;
cl_enginefunc_t engine{};ref_params_t view{};SCREENINFO screen{};
WeaponCurrent current{};BeamWeaponState predicted{};bool havePrediction{};
int predictedReserve{},draws{},scopes{},numbers{},sprites{},fills{},active=BeamgunLEId;float hudDraw=1;
int ActiveWeaponHudId(){return active;}
struct Sprite {int handle{};wrect_t rect{};};Sprite digits[10],ammo,energy;
void LoadSprites(){digits[0]={1,{0,20,0,22}};ammo={2,{0,24,0,24}};energy=ammo;}
void DrawBeamScope(int,int,int,float){++scopes;}
void Number(int,int,int,bool=false){++numbers;}
bool CharacterSkillInfiniteAmmoFor(int){return false;}
bool DrawCharacterSkillInfiniteAmmo(int,int,int,int,int,int){return false;}
#include "generated/beam_hud.inc"
void Run()
{
    engine.pfnGetCvarFloat=[](const char*){return hudDraw;};
    engine.pfnFillRGBA=[](int,int,int,int,int,int,int,int){++fills;};
    engine.pfnSPR_Set=[](int,int,int,int){};
    engine.pfnSPR_DrawAdditive=[](int,int,int,const wrect_t*){++sprites;};
    screen.iWidth=1280;screen.iHeight=720;view.health=100;current.id=BeamgunLEId;current.state=1;
    current.clip=60;current.reserve=200;current.secondary=10;
    DrawBeamgunHUD(0,0,false);Check(scopes==1&&!numbers&&!sprites&&!fills&&!draws,"style hides old ammo but preserves charge scope");
    DrawBeamgunHUD(0,0,true);Check(scopes==2&&numbers==3&&sprites==2&&fills==1&&draws==1,"fallback keeps all native beam ammunition");
    active=16;DrawBeamgunHUD(0,0,false);Check(scopes==2,"old beam state cannot draw scope after stock USP selection arrives first");active=BeamgunLEId;
    DrawBeamgunHUD(64,0,false);Check(scopes==2,"scope hide bit still honored");
    for(int mask:{1,4})DrawBeamgunHUD(mask,0,false);
    DrawBeamgunHUD(0,1,false);view.spectator=1;DrawBeamgunHUD(0,0,false);view.spectator=0;
    view.health=0;DrawBeamgunHUD(0,0,false);view.health=100;hudDraw=0;DrawBeamgunHUD(0,0,false);
    Check(scopes==2&&draws==1,"dead spectator intermission hide and hud_draw rules preserved");
}
}
int main(){native_draw::Run();precache::Run();beam::Run();std::cout<<"hud_style_contracts checks="<<fixture::checks<<" failures="<<fixture::failures<<'\n';return fixture::failures?1:0;}
