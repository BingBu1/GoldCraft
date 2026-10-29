// SPDX-License-Identifier: GPL-3.0-or-later
#include "client/weapons.hpp"
#include "client/weapon_inventory.hpp"
#include "client/pickup_history.hpp"
#include "client/beamgun.hpp"
#include "client/cannon.hpp"
#include "cannon.hpp"
#include <event_api.h>
#include "beamgun_prediction.hpp"
#include "beamgun_effects.hpp"
#include <in_buttons.h>
#include <r_efx.h>
#include <pmtrace.h>
#include <triangleapi.h>
#include <deque>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <array>
#include <source_location>
#include <algorithm>
#include <iomanip>
#include <fstream>
namespace csol::client
{
cl_enginefunc_t engine{};cl_exportfuncs_t downstream{};ref_params_t view{};metahook_api_t* api{};
void ClearScene(){}
void ForgetSceneEntity(const cl_entity_t*){}
void ResetCamera(){}
void ResetCharacters(){}
void ResetStatusIcons(){}
int testThirdPerson{};
int CameraIsThirdPerson(){return testThirdPerson;}
void ResetProjectiles(){}
void ResetBeamScope(){}
void DrawBeamScope(int,int,int,float){}
hook_t* Hook(void* address,void* replacement,void** original)
{
    *original=address;
    if(address==reinterpret_cast<void*>(engine.pfnSPR_Set))engine.pfnSPR_Set=reinterpret_cast<decltype(engine.pfnSPR_Set)>(replacement);
    else if(address==reinterpret_cast<void*>(engine.pfnSPR_DrawAdditive))engine.pfnSPR_DrawAdditive=reinterpret_cast<decltype(engine.pfnSPR_DrawAdditive)>(replacement);
    else if(address==reinterpret_cast<void*>(engine.pfnFillRGBA))engine.pfnFillRGBA=reinterpret_cast<decltype(engine.pfnFillRGBA)>(replacement);
    return nullptr;
}
}
namespace
{
using namespace csol;using namespace csol::client;
std::map<std::string,pfnUserMsgHook> messages;
std::map<std::string,xcommand_t> commands;
int checks{},fallbacks{},predicts{},animations{},nativeMessages{},hudMasks{},lastHudMask{};
float now=10,fast=1;
std::string sent,commandName;
std::string level="maps/cs_assault.bsp";
cvar_t protocol{};
float hud=1,cross=1,translucent=1,dynamic=1,crossType=0;
std::string crossSize="small",crossColor="1 22 233";
std::vector<std::array<int,8>> rectangles;
int blendCalls{},spriteCalls{},historyNumber{};
int screenWidth=1280,screenHeight=720,spriteHandle{},textCalls{};
bool missingSprites{};
bool lowResolutionSprites{};
int spriteLoads{},reverseCalls{};
std::vector<std::array<int,4>> spriteColors;
std::vector<std::array<int,7>> spriteDraws;
std::deque<TEMPENTITY> temps;
std::array<cl_entity_t,33> players;
model_t effectModel;
std::vector<std::string> effectPaths;
std::vector<std::array<int,2>> weaponAnimations;
cl_entity_t weaponView;
std::vector<std::array<float,3>> tracerVertices;
std::vector<float> smokeAlpha;
std::vector<int> smokeFrames;
int cannonSounds{},cannonMuzzleRandom{};
bool cannonMuzzleSpawn{};
void Check(bool value,const std::source_location where=std::source_location::current()){++checks;if(!value){std::cerr<<"FAIL client "<<checks<<" line "<<where.line()<<'\n';std::exit(1);}}
void Fallback(){++fallbacks;}
int Native(const char* name,int size,void* data){
    ++nativeMessages;
    if(!std::strcmp(name,"HideWeapon")){
        Check(size==1);lastHudMask=*static_cast<unsigned char*>(data);++hudMasks;
    }
    return 1;
}
void Send(const char* name,const std::vector<std::byte>& bytes){Check(messages.contains(name));messages[name](name,int(bytes.size()),const_cast<std::byte*>(bytes.data()));}
void Owned(const WeaponOwnership& ids){for(const auto& part:EncodeWeaponInventory(ids,1))Send(WeaponInventoryMessage,part);}
void Legacy()
{
    const char name[]="weapon_ak47";std::vector<std::byte> bytes;
    for(char c:name)bytes.push_back(std::byte(c));
    for(int b:{2,90,255,255,0,1,28,0})bytes.push_back(std::byte(b));
    Send("WeaponList",bytes);
}
void WeaponPickup(int id)
{
    Send(WeaponPickupMessage,{std::byte{WeaponProtocolVersion},std::byte(id&255),std::byte((id>>8)&255)});
}
void PickupChecks(const WeaponDefinition& knife,const WeaponDefinition& beam)
{
    ResetWeapons(true);view.health=100;view.spectator=0;hud=1;now=200;
    auto draw=[](int hidden=0,int intermission=0){spriteDraws.clear();spriteColors.clear();reverseCalls=0;DrawPickupHistory(hidden,intermission);};
    WeaponPickup(knife.id);draw();Check(spriteDraws.empty()); // Definitions are required; never guess a resource path.
    Send(WeaponDefinitionMessage,EncodeWeaponDefinition(knife));
    Send(WeaponDefinitionMessage,EncodeWeaponDefinition(beam));
    draw();Check(spriteDraws.empty()); // No stale pre-registration pickup replay.
    for(auto bytes:std::vector<std::vector<std::byte>>{{},{std::byte{1}},{std::byte{1},std::byte{220}},
        {std::byte{1},std::byte{220},std::byte{0},std::byte{0}},
        {std::byte{2},std::byte{220},std::byte{0}}})Send(WeaponPickupMessage,bytes);
    messages[WeaponPickupMessage](WeaponPickupMessage,3,nullptr);
    for(int id:{0,28,31,99,1024,65535,724})WeaponPickup(id);
    draw();Check(spriteDraws.empty());
    Legacy();Send(WeaponCurrentMessage,EncodeWeaponCurrent({220,1,1,29,0}));
    WeaponPickup(220);draw();
    Check(spriteDraws.size()==1 && spriteDraws[0]==std::array<int,7>{82,1110,558,0,170,0,45});
    Check(reverseCalls==0 && spriteColors.back()==std::array<int,4>{82,255,160,0});
    Send(AmmoPickupMessage,EncodeAmmoPickup({220,0,1}));Send(AmmoPickupMessage,EncodeAmmoPickup({220,0,29}));
    draw();Check(spriteDraws.size()==2 && historyNumber==30 && reverseCalls==1);
    Check(spriteDraws[1][2]==508 && spriteDraws[1][0]==77);
    now+=.01f;WeaponPickup(220);draw();Check(spriteDraws.size()==3 && reverseCalls==1);
    Check(spriteDraws[2][2]==458); // Weapon events do not merge into ammo or one another.
    Send(WeaponCurrentMessage,EncodeWeaponCurrent({28,1,30,90,0}));draw();Check(spriteDraws.size()==3);
    now+=6;draw();Check(spriteDraws.empty());
    ResetPickupHistory();WeaponPickup(725);draw();Check(spriteColors.back()==std::array<int,4>{82,255,16,16});
    ObserveSelectionClip(725,1);draw();Check(spriteColors.back()==std::array<int,4>{82,255,160,0});
    now+=4;draw();Check(spriteColors.back()==std::array<int,4>{82,80,50,0});
    for(int gate=0;gate<5;++gate){view.health=gate==0?0:100;view.spectator=gate==1;hud=gate==2?0:1;
        draw(gate==3?1:0,gate==4);Check(spriteDraws.empty());}
    view.health=100;view.spectator=0;hud=1;draw();Check(spriteDraws.size()==1);
    now+=2;draw(1);draw();Check(spriteDraws.empty());
    WeaponPickup(220);now-=1;draw();Check(spriteDraws.empty()); // Time rollback cannot leave old history.
    screenWidth=480;screenHeight=360;WeaponVideoInit();ResetPickupHistory();WeaponPickup(220);draw();
    Check(spriteDraws.size()==1 && spriteDraws[0][0]==82 && spriteDraws[0][1]==310); // CSO640 fallback.
    lowResolutionSprites=true;WeaponVideoInit();draw();Check(spriteDraws[0][0]==84 && spriteDraws[0][1]==395);
    lowResolutionSprites=false;screenWidth=1280;screenHeight=720;WeaponVideoInit();draw();Check(spriteDraws[0][0]==82);
    missingSprites=true;WeaponVideoInit();const int loads=spriteLoads;draw();draw();Check(spriteDraws.empty()&&spriteLoads==loads);
    missingSprites=false;WeaponVideoInit();draw();Check(spriteDraws.size()==1);
    ResetWeapons(false);draw();Check(spriteDraws.empty());WeaponPickup(220);draw();Check(spriteDraws.size()==1);
    ResetWeapons(true);draw();Check(spriteDraws.empty());WeaponPickup(220);draw();Check(spriteDraws.empty());
    Send(WeaponDefinitionMessage,EncodeWeaponDefinition(knife));
    for(int i=0;i<40;++i)WeaponPickup(220);draw();Check(spriteDraws.size()<=12 && !spriteDraws.empty());
    const int nativeBefore=nativeMessages;
    Check(!messages.contains("WeapPickup")); // Stock weapon messages stay registered to the stock HUD.
    unsigned char ordinary[]{2,1};messages["AmmoPickup"]("AmmoPickup",2,ordinary);Check(nativeMessages==nativeBefore+1);
    ResetWeapons(true);
}
void Predict(int id,int record,int buttons,int run=1)
{
    local_state_t from{},to{};usercmd_t cmd{};cmd.msec=10;cmd.buttons=buttons;
    from.client.m_iId=id;from.client.viewmodel=37;from.client.health=100;from.client.iuser3=1;
    from.client.vuser4[1]=29;from.weapondata[0].m_iId=record;from.weapondata[0].m_iClip=1;
    WeaponPostRunCmd(&from,&to,&cmd,run,10,17);
    Check(from.client.m_iId==id);Check(to.client.m_iId==id);Check(to.client.viewmodel==37);
    Check(to.client.iuser3==from.client.iuser3);
    if((id==220||id==725||id==726)&&record==id){Check(to.weapondata[0].m_iClip==0);Check(to.client.weaponanim==1);}
    else if(id>=32){Check(to.weapondata[0].m_iId==record);Check(to.weapondata[0].m_iClip==1);}
}
void PredictionPermissions()
{
    local_state_t from{};usercmd_t cmd{};cmd.msec=10;
    from.client.m_iId=220;from.client.health=100;from.client.iuser3=1;
    from.client.vuser4[1]=29;from.weapondata[0].m_iId=220;
    from.weapondata[0].m_iClip=1;from.weapondata[0].m_flTimeWeaponIdle=20;
    auto step=[&](int buttons,int run=1){
        // Movement supplies health; the weapon callback owns iuser3 propagation.
        local_state_t to=from;to.client.iuser3=0;cmd.buttons=buttons;
        WeaponPostRunCmd(&from,&to,&cmd,run,10,17);
        Check(to.client.iuser3==from.client.iuser3);
        from=to;
    };
    step(0);step(0);const int before=animations;
    step(IN_ATTACK);Check(animations==before+1&&from.client.weaponanim==1);
    Check(from.weapondata[0].m_iClip==0);
    step(IN_ATTACK);Check(animations==before+1); // Cooldown, no duplicate shot.
    from.weapondata[0].m_flNextSecondaryAttack=0;
    step(IN_ATTACK2);Check(animations==before+2&&from.client.weaponanim==3);
    for(int permission:{0,2,3}){
        from.client.iuser3=permission;from.client.weaponanim=0;
        from.weapondata[0].m_iClip=1;
        from.weapondata[0].m_flNextPrimaryAttack=from.weapondata[0].m_flNextSecondaryAttack=0;
        step(IN_ATTACK);step(IN_ATTACK2);
        Check(animations==before+2&&from.weapondata[0].m_iClip==1&&from.client.weaponanim==0);
    }
    from.client.iuser3=1;
    step(IN_ATTACK,0);Check(animations==before+2&&from.client.weaponanim==1);
    Check(from.weapondata[0].m_iClip==0); // Replay predicts state without replaying the effect.
}
void CannonChecks()
{
    ResetWeapons(true);WeaponVideoInit();view.health=100;view.spectator=0;
    WeaponDefinition definition{144,0,1,17,-1,20,-1,20,0,"weapon_cannon"};
    Send(WeaponDefinitionMessage,EncodeWeaponDefinition(definition));
    WeaponOwnership owned{};owned[144]=true;Owned(owned);
    Send(WeaponCurrentMessage,EncodeWeaponCurrent({144,1,20,0,0}));
    local_state_t state{};state.client.m_iId=144;state.client.health=100;state.client.iuser3=1;
    state.client.viewmodel=77;state.client.weaponanim=2;state.client.m_flNextAttack=.75f;
    state.weapondata[0].m_iId=144;state.weapondata[0].m_iClip=20;state.weapondata[0].m_flTimeWeaponIdle=1.5f;
    usercmd_t cmd{};cmd.msec=10;
    auto step=[&](int buttons,int run=1){local_state_t to=state;cmd.buttons=buttons;
        WeaponPostRunCmd(&state,&to,&cmd,run,now,17);Check(state.client.m_iId==144);state=to;};
    step(IN_ATTACK);Check(state.weapondata[0].m_iClip==20 && state.client.weaponanim==2);
    for(int n=0;n<76;++n)step(0);
    step(IN_ATTACK);Check(state.weapondata[0].m_iClip==19 && state.client.weaponanim==1);
    Check(std::abs(state.weapondata[0].m_flNextPrimaryAttack-3.19f)<.0001f);
    const int before=animations;step(IN_ATTACK);Check(state.weapondata[0].m_iClip==19 && animations==before);
    for(int n=0;n<321;++n)step(0);
    step(IN_ATTACK|IN_ATTACK2);Check(state.weapondata[0].m_iClip==19);
    step(IN_ATTACK2);step(IN_RELOAD);Check(state.weapondata[0].m_iClip==19 && !state.weapondata[0].m_fInReload);
    const int replayAnimations=animations;step(IN_ATTACK,0);
    Check(state.weapondata[0].m_iClip==18 && animations==replayAnimations);
    ReceiveCannonState({28,1,30,90,0}); // Returning to the weapon must retain the authoritative cooldown.
    step(IN_ATTACK);Check(state.weapondata[0].m_iClip==18 && state.weapondata[0].m_flNextPrimaryAttack>3);
    for(int permission:{0,2,3}){state.client.iuser3=permission;state.weapondata[0].m_flNextPrimaryAttack=0;
        step(IN_ATTACK);Check(state.weapondata[0].m_iClip==18);}
    state.client.iuser3=1;state.weapondata[0].m_iClip=0;state.weapondata[0].m_flNextPrimaryAttack=0;
    step(IN_ATTACK);Check(std::abs(state.weapondata[0].m_flNextPrimaryAttack-.39f)<.0001f);
    state.weapondata[0].m_iClip=18;state.weapondata[0].m_flNextPrimaryAttack=0;state.client.deadflag=1;
    step(IN_ATTACK);Check(state.weapondata[0].m_iClip==18);
    Send(WeaponCurrentMessage,EncodeWeaponCurrent({144,1,20,0,0}));
    spriteDraws.clear();spriteColors.clear();WeaponRedraw(now,0);
    Check(std::any_of(spriteDraws.begin(),spriteDraws.end(),[](auto d){return d[3]==73 && d[4]==97 && d[5]==231 && d[6]==255;}));
    Check(spriteColors.back()==std::array<int,4>{77,114,197,255});

    efx_api_t fx{};event_api_t ev{};engine.pEfxAPI=&fx;engine.pEventAPI=&ev;
    engine.pfnRandomLong=[](int a,int b)->int{return a==0&&b==5?(cannonMuzzleSpawn?0:1):a;};
    engine.pfnRandomFloat=[](float a,float)->float{return a;};
    engine.pfnAngleVectors=[](const float*,float* forward,float* right,float* up){
        forward[0]=1;forward[1]=forward[2]=0;right[0]=right[2]=0;right[1]=-1;up[0]=up[1]=0;up[2]=1;};
    ev.EV_FindModelIndex=[](const char* path)->int{effectPaths.push_back(path);return 10;};
    engine.CL_LoadModel=[](const char* path,int* index)->model_t*{effectPaths.emplace_back(path);*index=1;effectModel.numframes=30;return &effectModel;};
    triangleapi_t triangles{};engine.pTriAPI=&triangles;
    triangles.RenderMode=[](int){};triangles.CullFace=[](TRICULLSTYLE){};
    triangles.Color4f=[](float,float,float,float alpha){smokeAlpha.push_back(alpha);};
    triangles.Begin=[](int){};triangles.End=[](){};triangles.TexCoord2f=[](float,float){};
    triangles.Vertex3f=[](float x,float y,float z){tracerVertices.push_back({x,y,z});};
    triangles.SpriteTexture=[](model_t*,int frame)->int{smokeFrames.push_back(frame);return 1;};
    fx.R_DefaultSprite=[](float* origin,int,float rate)->TEMPENTITY*{
        temps.emplace_back();auto& temp=temps.back();std::copy(origin,origin+3,temp.entity.origin);temp.entity.curstate.framerate=rate;return &temp;};
    ev.EV_PlaySound=[](int,float*,int,const char* path,float volume,float attenuation,int,int pitch){
        Check(!std::strcmp(path,"weapons/cannon-1.wav") && volume==1 && attenuation==.52f && pitch==94);++cannonSounds;};
    const auto savedLocal=engine.GetLocalPlayer;engine.GetLocalPlayer=nullptr;
    temps.clear();std::vector<std::byte> effect(cannon::EffectBytes);effect[0]=std::byte{2};effect[1]=std::byte{2};
    Send(cannon::EffectMessage,effect);Check(temps.size()==12);
    auto drawSmoke=[](){tracerVertices.clear();smokeAlpha.clear();smokeFrames.clear();DrawCannonSmoke();};
    drawSmoke();Check(tracerVertices.size()==4 && smokeFrames==std::vector<int>{0});
    Check(std::abs(tracerVertices[0][1]-tracerVertices[3][1]-22)<.0001f);
    Check(std::abs(smokeAlpha[0]-250.f/255)<.00001f);
    now+=.75f;drawSmoke();Check(smokeFrames==std::vector<int>{22});
    Check(std::abs(tracerVertices[0][1]-tracerVertices[3][1]-67)<.0001f);
    Check(std::abs(smokeAlpha[0]-125.f/255)<.00001f && tracerVertices[0][0]==45);
    ReceiveCannonState({28,1,30,90,0});ResetCannon();drawSmoke();Check(tracerVertices.size()==4);
    now+=.3f;drawSmoke();Check(smokeFrames==std::vector<int>{1});
    now+=.45f;drawSmoke();Check(tracerVertices.empty());
    for(auto& temp:temps){Check(temp.entity.curstate.rendermode==kRenderTransAdd && temp.entity.curstate.renderamt==150);
        Check(temp.entity.origin[0]==40 && temp.entity.origin[2]==-9 && temp.clientIndex==2);
        Check((temp.flags&(FTENT_COLLIDEALL|FTENT_ROTATE|FTENT_CLIENTCUSTOM))==(FTENT_COLLIDEALL|FTENT_ROTATE|FTENT_CLIENTCUSTOM));
        const float scale=temp.entity.curstate.scale,velocity=temp.entity.baseline.origin[0];
        temp.callback(&temp,.01f,now);Check(std::abs(temp.entity.curstate.scale-scale-.03f)<.00001f);
        temp.hitcallback(&temp,nullptr);Check(temp.entity.curstate.framerate==60 && std::abs(temp.entity.baseline.origin[0]-velocity*.01f)<.00001f);}
    effect[2]=std::byte{1};Send(cannon::EffectMessage,effect);Check(temps.size()==24 && temps.back().entity.origin[2]==-3);
    for(int index:{0,1,2}){auto bad=effect;bad[index]=std::byte{255};Send(cannon::EffectMessage,bad);Check(temps.size()==24);}
    effect.pop_back();Send(cannon::EffectMessage,effect);Check(temps.size()==24);
    drawSmoke();Check(tracerVertices.size()==4);WeaponVideoInit();drawSmoke();Check(tracerVertices.empty());
    effect.push_back(std::byte{0});Send(cannon::EffectMessage,effect);now-=1;
    drawSmoke();Check(tracerVertices.empty()); // Rewound demo/session cannot retain the old particle.
    Send(cannon::EffectMessage,effect);effect[3]=std::byte{0x20};effect[4]=std::byte{0x03};
    Send(cannon::EffectMessage,effect);drawSmoke();
    Check(tracerVertices.size()==8 && tracerVertices[0][0]==100 && tracerVertices[4][0]==0);
    ResetWeapons(true);drawSmoke();Check(tracerVertices.empty());
    Send(WeaponDefinitionMessage,EncodeWeaponDefinition(definition));
    players[1].index=1;players[2].index=2;
    engine.GetLocalPlayer=[]()->cl_entity_t*{return &players[1];};
    auto ready=[&](){state={};state.client.m_iId=144;state.client.health=100;state.client.iuser3=1;
        state.client.viewmodel=77;state.client.view_ofs[2]=17;state.client.velocity[0]=10;
        state.weapondata[0].m_iId=144;state.weapondata[0].m_iClip=20;state.weapondata[0].m_flTimeWeaponIdle=20;};
    auto fire=[&](unsigned seed,int run=1){local_state_t to=state;cmd.buttons=IN_ATTACK;
        WeaponPostRunCmd(&state,&to,&cmd,run,now,seed);state=to;};
    auto confirm=[&](int owner,unsigned seed){std::vector<std::byte> message(cannon::EffectBytes);message[0]=std::byte{2};message[1]=std::byte(owner);
        for(int i=0;i<4;++i)message[21+i]=std::byte((seed>>(i*8))&255);Send(cannon::EffectMessage,message);};
    auto clearEffects=[&](){ResetCannonEffects();temps.clear();cannonSounds=0;};
    clearEffects();ready();fire(0xfedcba98u);Check(temps.size()==12 && cannonSounds==1 && state.weapondata[0].m_iClip==19);
    Check(temps.front().entity.origin[0]==40 && temps.front().entity.origin[2]==8);
    ready();fire(0xfedcba98u,0);Check(temps.size()==12 && cannonSounds==1 && state.weapondata[0].m_iClip==19);
    confirm(1,0xfedcba98u);Check(temps.size()==12 && cannonSounds==1);
    drawSmoke();Check(tracerVertices.size()==4); // Server TempEmit survives local event deduplication.
    clearEffects();ready();fire(0u);confirm(2,0u);Check(temps.size()==24 && cannonSounds==2);
    confirm(1,0u);Check(temps.size()==24 && cannonSounds==2); // Same seed is scoped to the owner, including zero.
    clearEffects();ready();fire(17);confirm(1,18);Check(temps.size()==24 && cannonSounds==2);
    confirm(1,17);Check(temps.size()==24 && cannonSounds==2);
    clearEffects();ready();fire(19);ReceiveCannonState({28,1,30,90,0});ResetCannon();confirm(1,19);
    Check(temps.size()==12 && cannonSounds==1); // Holstering must preserve the in-flight acknowledgement.
    clearEffects();ready();fire(20);ResetCannonEffects();confirm(1,20);Check(temps.size()==24 && cannonSounds==2);
    clearEffects();ready();engine.pEfxAPI=nullptr;fire(21);Check(temps.empty() && cannonSounds==0);
    engine.pEfxAPI=&fx;confirm(1,21);Check(temps.size()==12 && cannonSounds==1);
    clearEffects();ready();const auto savedSprite=fx.R_DefaultSprite;const auto savedSound=ev.EV_PlaySound;
    fx.R_DefaultSprite=nullptr;ev.EV_PlaySound=nullptr;fire(22);Check(temps.empty() && cannonSounds==0);
    fx.R_DefaultSprite=savedSprite;ev.EV_PlaySound=savedSound;confirm(1,22);Check(temps.size()==12 && cannonSounds==1);
    clearEffects();ready();const auto savedFind=ev.EV_FindModelIndex;ev.EV_FindModelIndex=nullptr;fire(23);Check(temps.empty() && cannonSounds==0);
    ev.EV_FindModelIndex=savedFind;confirm(1,23);Check(temps.size()==12 && cannonSounds==1);
    clearEffects();ready();ev.EV_FindModelIndex=[](const char*)->int{return 0;};fire(24);Check(temps.empty() && cannonSounds==0);
    ev.EV_FindModelIndex=savedFind;confirm(1,24);Check(temps.size()==12 && cannonSounds==1);
    clearEffects();ready();for(int i=0;i<9;++i){ready();fire(100+i);}Check(temps.size()==108 && cannonSounds==9);
    confirm(1,100);Check(temps.size()==120 && cannonSounds==10); // The bounded pending ring only replaces its oldest entry.
    for(unsigned seed=101;seed<109;++seed)confirm(1,seed);Check(temps.size()==120 && cannonSounds==10);
    clearEffects();for(int reason=0;reason<5;++reason){ready();if(reason==0)state.weapondata[0].m_iClip=0;
        if(reason==1)state.client.deadflag=1;if(reason==2)state.client.health=0;if(reason==3)state.client.iuser3=0;
        if(reason==4)state.client.m_flNextAttack=.5f;fire(200+reason);Check(temps.empty() && cannonSounds==0);}
    ready();cmd.buttons=IN_ATTACK|IN_ATTACK2;{local_state_t to=state;WeaponPostRunCmd(&state,&to,&cmd,1,now,210);Check(to.weapondata[0].m_iClip==20);}
    Check(temps.empty() && cannonSounds==0);
    // The muzzle cloud is local-only and uses the original 1-in-6 branch.
    clearEffects();cannonMuzzleSpawn=false;confirm(1,300);Check(temps.size()==12);
    clearEffects();cannonMuzzleSpawn=true;confirm(2,301);Check(temps.size()==12);
    clearEffects();effectPaths.clear();confirm(1,302);Check(temps.size()==13);
    const auto& muzzle=temps.front();Check(muzzle.entity.curstate.framerate==35 && muzzle.entity.curstate.scale==.26f);
    Check(muzzle.entity.curstate.rendermode==kRenderTransAdd && muzzle.entity.curstate.rendercolor.r==15 && muzzle.entity.curstate.renderamt==150);
    Check((muzzle.flags&(FTENT_CLIENTCUSTOM|FTENT_PERSIST|FTENT_COLLIDEWORLD))==(FTENT_CLIENTCUSTOM|FTENT_PERSIST|FTENT_COLLIDEWORLD));
    Check(std::find(effectPaths.begin(),effectPaths.end(),"sprites/pistol_smoke2.spr")!=effectPaths.end());
    model_t muzzleModel{};std::strcpy(muzzleModel.name,"models/v_cannon.mdl");weaponView.model=&muzzleModel;
    weaponView.attachment[0][0]=100;weaponView.attachment[0][1]=200;weaponView.attachment[0][2]=300;
    const auto savedView=engine.GetViewModel;engine.GetViewModel=[]()->cl_entity_t*{return &weaponView;};
    clearEffects();confirm(1,303);Check(temps.front().entity.origin[0]==100 && temps.front().entity.origin[2]==300);
    Check(temps[1].entity.origin[0]==107 && temps[1].entity.origin[2]==300);
    testThirdPerson=1;clearEffects();confirm(1,304);Check(temps.front().entity.origin[0]==40 && temps.front().entity.origin[2]==-9);
    testThirdPerson=0;engine.GetViewModel=savedView;weaponView.model=nullptr;cannonMuzzleSpawn=false;
    engine.GetLocalPlayer=savedLocal;engine.pEfxAPI=nullptr;engine.pEventAPI=nullptr;engine.pTriAPI=nullptr;
    ResetWeapons(true);Check(CannonPredictedAmmo()==-1);
}
efx_api_t cannonFlashFx{};
model_t cannonFlashModel{},cannonViewModel{};
int cannonFlashRandomCalls{},cannonFlashRandomFrame{};
void ConfigureCannonFlash()
{
    cannonFlashFx={};engine.pEfxAPI=&cannonFlashFx;engine.GetViewModel=[]()->cl_entity_t*{return &weaponView;};
    cannonFlashModel={};cannonFlashModel.type=mod_sprite;cannonFlashModel.numframes=5;
    cannonViewModel={};std::strcpy(cannonViewModel.name,"models/v_cannon.mdl");weaponView={};weaponView.model=&cannonViewModel;
    weaponView.attachment[0][0]=10;weaponView.attachment[0][1]=20;weaponView.attachment[0][2]=30;
    engine.CL_LoadModel=[](const char* path,int* index)->model_t*{Check(!std::strcmp(path,"sprites/muzzleflash12.spr"));*index=11;return &cannonFlashModel;};
    engine.pfnRandomLong=[](int minimum,int maximum)->int{Check(minimum==0&&maximum==4);++cannonFlashRandomCalls;return cannonFlashRandomFrame;};
    cannonFlashFx.CL_TempEntAlloc=[](float* origin,model_t* model)->TEMPENTITY*{
        Check(model==&cannonFlashModel);temps.emplace_back();auto& temp=temps.back();std::copy_n(origin,3,temp.entity.origin);temp.entity.model=model;
        temp.entity.baseline.renderamt=42;return &temp;};
    temps.clear();cannonFlashRandomCalls=0;
}
void CannonFlashChecks()
{
    ConfigureCannonFlash();now=10;mstudioevent_t event{};event.event=5001;std::strcpy(event.options,"103112");
    Check(CannonStudioEvent(&event,&weaponView));Check(temps.size()==1&&cannonFlashRandomCalls==0);
    auto temp=temps.back();Check(temp.flags==0x12180&&temp.die==10.5f&&temp.frameMax==5&&temp.fadeSpeed==.5f&&temp.bounceFactor==1);
    Check(!temp.callback&&!temp.hitcallback&&temp.entity.curstate.frame==0&&temp.entity.curstate.framerate==20);
    Check(temp.entity.curstate.rendermode==kRenderTransAdd&&temp.entity.curstate.renderamt==255&&temp.entity.baseline.renderamt==0);
    Check(temp.entity.curstate.scale==.1f&&temp.entity.curstate.renderfx==kRenderFxNone&&temp.entity.curstate.rendercolor.r==255);
    Check(temp.entity.origin[0]==10&&temp.entity.origin[1]==20&&temp.entity.origin[2]==30);
    weaponView.attachment[0][0]=200;Check(temp.entity.origin[0]==10); // A flash snapshots the attachment; no owner-follow callback.
    for(int axis=0;axis<3;++axis)Check(temp.entity.angles[axis]==0&&temp.entity.curstate.angles[axis]==0&&temp.entity.baseline.origin[axis]==0);
    cl_entity_t remote=weaponView;cannonFlashRandomFrame=4;Check(CannonStudioEvent(&event,&remote));Check(temps.size()==2&&cannonFlashRandomCalls==1);
    const auto& other=temps.back();Check(other.flags==0&&other.die==float(10.01)&&other.entity.curstate.frame==4&&other.entity.curstate.framerate==0);
    Check(other.entity.baseline.renderamt==0&&other.entity.curstate.renderamt==255&&other.entity.origin[0]==200);
    Check(!CannonStudioEvent(nullptr,&weaponView)&&!CannonStudioEvent(&event,nullptr));
    event.event=5002;Check(!CannonStudioEvent(&event,&weaponView));event.event=5001;
    std::strcpy(event.options,"103113");Check(!CannonStudioEvent(&event,&weaponView));std::memset(event.options,'1',sizeof(event.options));
    Check(!CannonStudioEvent(&event,&weaponView));std::strcpy(event.options,"103112");std::strcpy(cannonViewModel.name,"models/v_ak47.mdl");
    Check(!CannonStudioEvent(&event,&weaponView));std::strcpy(cannonViewModel.name,"models/v_cannon.mdl");
    weaponView.model=nullptr;Check(!CannonStudioEvent(&event,&weaponView));weaponView.model=&cannonViewModel;
    const auto size=temps.size();engine.pEfxAPI=nullptr;Check(CannonStudioEvent(&event,&weaponView));engine.pEfxAPI=&cannonFlashFx;
    auto allocate=cannonFlashFx.CL_TempEntAlloc;cannonFlashFx.CL_TempEntAlloc=nullptr;Check(CannonStudioEvent(&event,&weaponView));cannonFlashFx.CL_TempEntAlloc=allocate;
    auto loader=engine.CL_LoadModel;engine.CL_LoadModel=nullptr;Check(CannonStudioEvent(&event,&weaponView));engine.CL_LoadModel=loader;
    auto clock=engine.GetClientTime;engine.GetClientTime=nullptr;Check(CannonStudioEvent(&event,&weaponView));engine.GetClientTime=clock;
    auto random=engine.pfnRandomLong;engine.pfnRandomLong=nullptr;Check(CannonStudioEvent(&event,&remote));engine.pfnRandomLong=random;
    cannonFlashModel.type=mod_studio;Check(CannonStudioEvent(&event,&weaponView));cannonFlashModel.type=mod_sprite;
    cannonFlashModel.numframes=0;Check(CannonStudioEvent(&event,&weaponView));cannonFlashModel.numframes=5;
    cannonFlashFx.CL_TempEntAlloc=[](float*,model_t*)->TEMPENTITY*{return nullptr;};Check(CannonStudioEvent(&event,&weaponView));
    Check(temps.size()==size);engine.pEfxAPI=nullptr;engine.GetViewModel=nullptr;engine.CL_LoadModel=nullptr;weaponView.model=nullptr;
}
void CannonFlashOracle()
{
    ConfigureCannonFlash();mstudioevent_t event{};event.event=5001;std::strcpy(event.options,"103112");cl_entity_t remote=weaponView;
    int local;std::cout<<std::setprecision(9);
    while(std::cin>>local>>now>>cannonFlashRandomFrame){temps.clear();cannonFlashRandomCalls=0;Check(CannonStudioEvent(&event,local?&weaponView:&remote));Check(temps.size()==1);
        const auto& t=temps.back();std::cout<<"{\"flags\":"<<t.flags<<",\"die\":"<<t.die<<",\"frameMax\":"<<t.frameMax<<",\"fadeSpeed\":"<<t.fadeSpeed
        <<",\"callback\":"<<(t.callback?1:0)<<",\"hitcallback\":"<<(t.hitcallback?1:0)<<",\"velocity\":["<<t.entity.baseline.origin[0]<<','<<t.entity.baseline.origin[1]<<','<<t.entity.baseline.origin[2]
        <<"],\"baselineRenderAmt\":"<<t.entity.baseline.renderamt<<",\"frame\":"<<t.entity.curstate.frame<<",\"scale\":["<<t.entity.curstate.scale<<','<<t.entity.curstate.scale<<','<<t.entity.curstate.scale
        <<"],\"renderMode\":"<<t.entity.curstate.rendermode<<",\"renderAmt\":"<<t.entity.curstate.renderamt<<",\"color\":["<<int(t.entity.curstate.rendercolor.r)<<','<<int(t.entity.curstate.rendercolor.g)<<','<<int(t.entity.curstate.rendercolor.b)
        <<"],\"renderFx\":"<<t.entity.curstate.renderfx<<",\"frameRate\":"<<t.entity.curstate.framerate<<",\"origin\":["<<t.entity.origin[0]<<','<<t.entity.origin[1]<<','<<t.entity.origin[2]
        <<"],\"angles\":["<<t.entity.angles[0]<<','<<t.entity.angles[1]<<','<<t.entity.angles[2]<<"],\"resourceIndex\":11,\"randomCalls\":"<<cannonFlashRandomCalls<<",\"renderSubmissions\":1}\n";
    }
}
void CannonMuzzleOracle()
{
    ResetWeapons(true);Send(WeaponDefinitionMessage,EncodeWeaponDefinition({144,0,1,17,-1,20,-1,20,0,"weapon_cannon"}));
    efx_api_t fx{};event_api_t ev{};engine.pEfxAPI=&fx;engine.pEventAPI=&ev;
    engine.pfnRandomLong=[](int,int)->int{return cannonMuzzleRandom;};
    engine.pfnRandomFloat=[](float a,float)->float{return a;};
    engine.pfnAngleVectors=[](const float*,float* f,float* r,float* u){f[0]=1;f[1]=f[2]=0;r[0]=r[2]=0;r[1]=-1;u[0]=u[1]=0;u[2]=1;};
    ev.EV_FindModelIndex=[](const char*)->int{return 10;};
    ev.EV_PlaySound=[](int,float*,int,const char*,float,float,int,int){};
    fx.R_DefaultSprite=[](float*,int,float)->TEMPENTITY*{temps.emplace_back();return &temps.back();};
    players[1].index=1;engine.GetLocalPlayer=[]()->cl_entity_t*{return &players[1];};
    std::vector<std::byte> effect(cannon::EffectBytes);effect[0]=std::byte{2};effect[1]=std::byte{1};
    cannonMuzzleRandom=0;Send(cannon::EffectMessage,effect);Check(temps.size()==13);
    TEMPENTITY temp=temps.front();std::cout<<std::setprecision(9);char operation;
    while(std::cin>>operation){float vx,vy,vz;std::cin>>vx>>vy>>vz;temp.entity.baseline.origin[0]=vx;temp.entity.baseline.origin[1]=vy;temp.entity.baseline.origin[2]=vz;
        if(operation=='u'){int reset;float frame;std::cin>>reset>>frame>>cannonMuzzleRandom;if(reset)ResetCannonEffects();
            temp.entity.curstate.frame=frame;temp.callback(&temp,.01f,now);}
        else if(operation=='h'){pmtrace_t trace{};std::cin>>trace.plane.normal[0]>>trace.plane.normal[1]>>trace.plane.normal[2];temp.hitcallback(&temp,&trace);}
        else std::exit(2);
        std::cout<<'['<<temp.entity.baseline.origin[0]<<','<<temp.entity.baseline.origin[1]<<','<<temp.entity.baseline.origin[2]<<"]\n";
    }
}
void BeamPredictionChecks()
{
    for(const int id:{725,726})
    {
        local_state_t from{};usercmd_t cmd{};cmd.msec=10;
        from.client.m_iId=id;from.client.health=100;from.client.iuser3=1;
        from.client.vuser4[1]=200;from.weapondata[0].m_iId=id;
        from.weapondata[0].m_iClip=80;from.weapondata[0].iuser1=100;
        from.weapondata[0].m_flTimeWeaponIdle=20;
        auto step=[&](int buttons,int run=1){
            local_state_t to=from;cmd.buttons=buttons;
            WeaponPostRunCmd(&from,&to,&cmd,run,10,17);from=to;
        };
        int before=animations;
        step(IN_ATTACK);Check(from.weapondata[0].m_iClip==79&&from.client.weaponanim==BeamShoot&&animations==before+1);
        for(int i=0;i<29;++i)step(IN_ATTACK);
        Check(from.weapondata[0].m_iClip==79);step(IN_ATTACK);step(IN_ATTACK);Check(from.weapondata[0].m_iClip==78);
        for(int i=0;i<31;++i)step(0);
        before=animations;step(IN_ATTACK,0);Check(from.weapondata[0].m_iClip==77&&animations==before);
        for(int i=0;i<31;++i)step(0);
        step(IN_RELOAD);Check(from.weapondata[0].m_fInReload&&from.client.weaponanim==BeamReload);
        for(int i=0;i<230;++i)step(0);
        step(0);Check(from.weapondata[0].m_iClip==80&&int(from.client.vuser4[1])==197&&!from.weapondata[0].m_fInReload);
        from.weapondata[0].m_flNextPrimaryAttack=from.weapondata[0].m_flNextSecondaryAttack=0;
        step(IN_ATTACK2);Check(from.client.weaponanim==BeamShootB1&&ReadBeamPrediction(from.weapondata[0]).pending==BeamPending::First);
        for(int i=0;i<101;++i)step(0);
        step(IN_ATTACK2);Check(from.client.weaponanim==BeamShootB2);
        for(int i=0;i<32;++i)step(0);
        Check(ReadBeamPrediction(from.weapondata[0]).charge==BeamCharge::Alpha);
        for(int i=0;i<51;++i)step(0);
        const int ammoBefore=from.weapondata[0].m_iClip;
        step(IN_ATTACK);Check(from.client.weaponanim==BeamChargeShoot&&from.weapondata[0].m_iClip==ammoBefore);
        for(int i=0;i<250;++i)step(0);
        Check(ReadBeamPrediction(from.weapondata[0]).charge==BeamCharge::None);
        step(IN_ATTACK|IN_ATTACK2);auto state=ReadBeamPrediction(from.weapondata[0]);
        Check(state.charge==BeamCharge::Beta&&state.energy==0&&from.client.weaponanim==BeamChargeEnter);
        for(int i=0;i<31;++i)step(0);
        step(IN_ATTACK2);Check(ReadBeamPrediction(from.weapondata[0]).dive==BeamDive::Start);
        for(int i=0;i<41;++i)step(0);
        from.client.flags=FL_ONGROUND;
        step(0);Check(ReadBeamPrediction(from.weapondata[0]).dive==BeamDive::Landed);
        step(0);Check(ReadBeamPrediction(from.weapondata[0]).dive==BeamDive::None&&from.client.weaponanim==BeamDiveEnd);
        for(int permission:{0,2,3})
        {
            from.client.iuser3=permission;from.weapondata[0]={};from.weapondata[0].m_iId=id;
            from.weapondata[0].m_iClip=80;from.weapondata[0].iuser1=100;
            before=animations;step(0);step(IN_ATTACK|IN_ATTACK2);
            Check(animations==before&&from.weapondata[0].m_iClip==80&&from.weapondata[0].iuser1==100);
        }
        from.client.iuser3=1;from.client.health=0;before=animations;
        step(IN_ATTACK);Check(animations==before&&from.weapondata[0].m_iClip==80);
    }
}
void BeamSwitchChecks()
{
    for(int id:{725,726})for(int input:{IN_ATTACK,IN_ATTACK2})for(int run:{0,1})
    {
        ResetBeamgun();
        local_state_t from{};usercmd_t cmd{};cmd.msec=10;cmd.buttons=input;
        from.client.m_iId=id;from.client.health=100;from.client.iuser3=1;
        from.client.viewmodel=id;from.client.weaponanim=BeamChargeDraw;
        from.client.m_flNextAttack=.1f;
        BeamWeaponState state;state.clip=80;state.charge=BeamCharge::Alpha;
        state.chargeTime=.15f;state.nextPrimary=state.nextSecondary=.95f;state.idleTime=1.37f;
        from.weapondata[0].m_iId=id;WriteBeamPrediction(state,from.weapondata[0]);
        for(int frame=0;frame<95;++frame)
        {
            local_state_t to=from;Check(PredictBeamgun(&from,&to,&cmd,run,now,17));from=to;
            state=ReadBeamPrediction(from.weapondata[0]);
            Check(state.clip==80&&state.dive==BeamDive::None&&state.pending==BeamPending::None);
            Check(std::abs(state.nextPrimary-(.95f-(frame+1)*.01f))<.0001f);
        }
        Check(!state.Charged());
        for(int frame=0;frame<2;++frame){local_state_t to=from;PredictBeamgun(&from,&to,&cmd,run,now,17);from=to;}
        state=ReadBeamPrediction(from.weapondata[0]);
        Check(input==IN_ATTACK?state.clip==79:state.pending==BeamPending::First);
    }
    engine.GetViewModel=[]()->cl_entity_t*{return &weaponView;};
    model_t model{};weaponView={};weaponView.model=&model;weaponView.curstate.body=1;
    for(int id:{725,726})
    {
        std::strcpy(model.name,id==726?"models/v_beamgunle.mdl":"models/v_beamgun.mdl");
        for(const int sequence:{BeamDraw,BeamChargeDraw})
        {
            ResetBeamgun();ReceiveBeamgunState({id,1,80,200,100});weaponAnimations.clear();
            local_state_t from{};usercmd_t cmd{};
            from.client.m_iId=id;from.client.viewmodel=id;from.client.health=100;
            from.client.iuser3=1;from.client.weaponanim=sequence;from.client.m_flNextAttack=.1f;
            from.weapondata[0].m_iId=id;from.weapondata[0].m_iClip=80;
            from.weapondata[0].m_flTimeWeaponIdle=1.37f;
            auto step=[&](int run=1){local_state_t to=from;
                Check(PredictBeamgun(&from,&to,&cmd,run,now,17));return to;};
            step(0);Check(weaponAnimations.empty()); // Replay cannot consume the switch.
            step();Check(weaponAnimations.size()==1&&weaponAnimations.back()==std::array<int,2>{sequence,1});
            step();ReceiveBeamgunState({id,1,79,200,99});step();Check(weaponAnimations.size()==1);
            weaponView.curstate.sequence=BeamReload;step();Check(weaponAnimations.size()==2&&weaponView.curstate.sequence==sequence);
            local_state_t other{},otherTo{};other.client.m_iId=28;
            Check(!PredictBeamgun(&other,&otherTo,&cmd,1,now,17));
            step();Check(weaponAnimations.size()==3); // Same sequence must restart after switching away.
            from.client.health=0;step();Check(weaponAnimations.size()==3);
            from.client.health=100;step();Check(weaponAnimations.size()==4);
            ReceiveBeamgunState({id,1,79,200,100});Check(BeamgunViewSkin(&weaponView)==1&&weaponView.curstate.body==1);
            ReceiveBeamgunState({id,1,79,200,99});Check(BeamgunViewSkin(&weaponView)==0);
            from.client.m_flNextAttack=0;from.weapondata[0].iuser1=100;cmd.buttons=IN_ATTACK;
            step();Check(weaponAnimations.back()==std::array<int,2>{BeamShoot,1}&&BeamgunViewSkin(&weaponView)==1);
            ReceiveBeamgunState({28,1,30,90,0});Check(BeamgunViewSkin(&weaponView)==-1);
            ReceiveBeamgunState({id,1,79,200,100});
            model.name[0]='x';Check(BeamgunViewSkin(&weaponView)==-1);model.name[0]='m';
        }
    }
    engine.GetViewModel=nullptr;ResetBeamgun();
}
void BeamEffectChecks()
{
    efx_api_t api{};
    triangleapi_t triangles{};
    engine.pTriAPI=&triangles;
    triangles.RenderMode=[](int){};triangles.CullFace=[](TRICULLSTYLE){};
    triangles.Color4f=[](float,float,float,float){};triangles.Begin=[](int){};triangles.End=[](){};
    triangles.TexCoord2f=[](float,float){};
    triangles.Vertex3f=[](float x,float y,float z){tracerVertices.push_back({x,y,z});};
    triangles.SpriteTexture=[](model_t*,int)->int{return 1;};
    triangles.WorldToScreen=[](float* point,float* screen)->int{std::copy_n(point,3,screen);return 0;};
    view.up[2]=1;view.right[1]=1;
    engine.pEfxAPI=&api;
    engine.GetEntityByIndex=[](int index)->cl_entity_t*{return index>0&&index<33?&players[index]:nullptr;};
    engine.CL_LoadModel=[](const char* path,int* index)->model_t*{effectPaths.emplace_back(path);*index=1;return &effectModel;};
    api.R_TempModel=[](float* origin,float*,float*,float life,int,int)->TEMPENTITY*{
        temps.emplace_back();auto& e=temps.back();e.entity.model=&effectModel;std::copy_n(origin,3,e.entity.origin);e.die=now+life;return &e;};
    api.R_DefaultSprite=[](float*,int,float)->TEMPENTITY*{temps.emplace_back();return &temps.back();};
    api.R_TempSprite=[](float* origin,float* velocity,float scale,int,int,int,float,float life,int flags)->TEMPENTITY*{
        temps.emplace_back();auto& e=temps.back();std::copy_n(origin,3,e.entity.origin);
        std::copy_n(velocity,3,e.entity.baseline.origin);e.entity.curstate.scale=scale;e.die=now+life;e.flags=flags;return &e;};
    for(int i=1;i<33;++i){players[i]={};players[i].model=&effectModel;players[i].curstate.weaponmodel=10;players[i].curstate.solid=3;}
    auto send=[](BeamEffect effect){const auto bytes=EncodeBeamEffect(effect);return messages[BeamEffectMessage](BeamEffectMessage,int(bytes.size()),const_cast<std::byte*>(bytes.data()));};
    for(int id:{725,726})
    {
        ResetBeamgun();temps.clear();effectPaths.clear();
        BeamEffect effect{id,1,4,1,0,{}, {100,200,300},{150,200,300}};
        Check(send(effect)==1&&temps.size()==1);
        auto* first=&temps.front();Check(first->callback&&(first->flags&FTENT_PERSIST));
        Check(BeamEffectIdentity(&first->entity)!=0&&BeamEffectIdentity(&players[1])==0);
        players[1].origin[0]=125;first->callback(first,.01f,now);Check(first->entity.origin[0]==125);
        effect.phase=0;send(effect);first->callback(first,.01f,now);Check(first->die==now&&(first->flags&FTENT_NOMODEL));
        Check(!BeamEffectIdentity(&first->entity));
        effect.phase=1;send(effect);auto* second=&temps.back();ResetBeamgun();
        second->callback(second,.01f,now);Check(second->die==now);
        send(effect);auto* third=&temps.back();players[1].curstate.weaponmodel=0;
        third->callback(third,.01f,now);Check(third->die==now);players[1].curstate.weaponmodel=10;
        effect.mode=0;effect.phase=1;send(effect);Check(temps.back().entity.curstate.scale==1.2f&&temps.back().die==now+.83f);
        Check(effectPaths.back()==(id==726?"sprites/ef_beamgunle_shootA_hit.spr":"sprites/ef_beamgun_shootA_hit.spr"));
        effect.mode=2;effect.phase=0;send(effect);Check(temps.back().entity.origin[0]==100);
        for(int mode:{0,1,2})
        {
            const auto before=temps.size();effect.phase=mode;send(effect);
            Check(temps.size()==before+(mode==1?3:1));
            if(mode==1)
            {
                const auto& model=temps[before+1];const auto& sprite=temps.back();
                Check(model.entity.origin[0]==150&&model.entity.curstate.scale==.8f);
                Check(model.die==now+.4f&&sprite.die==now+.4f&&sprite.entity.curstate.scale==.8f&&sprite.entity.curstate.rendermode==kRenderTransAlpha);
                Check(effectPaths.back()==(id==726?"sprites/ef_beamgunle_shootB2_exp2.spr":"sprites/ef_beamgun_shootB2_exp2.spr"));
            }
        }
        effect.phase=0;
        players[2].origin[0]=250;players[2].origin[1]=200;players[2].origin[2]=300;
        effect.mode=5;effect.count=4;effect.targets={2,2,2,2};const auto before=temps.size();send(effect);
        tracerVertices.clear();DrawBeamgunTracers();
        Check(temps.size()==before&&tracerVertices.size()==16);
        // Averaging opposite-width corners recovers the trimmed path origin.
        const auto center=[&](int vertex,int axis){return (tracerVertices[vertex][axis]+tracerVertices[vertex+3][axis])*.5f;};
        Check(std::abs(center(0,1)-209)<.01f&&std::abs(center(4,1)-191)<.01f);
        Check(std::abs(center(0,0)-115)<.01f&&std::abs(center(0,2)-336)<.01f);
        now+=1;tracerVertices.clear();DrawBeamgunTracers();Check(tracerVertices.empty());
        ResetBeamgun();effect.count=1;effect.targets={2,0,0,0};send(effect);
        tracerVertices.clear();DrawBeamgunTracers();Check(tracerVertices.size()==4&&std::abs(center(0,1)-209)<.01f);
        now+=1;DrawBeamgunTracers();send(effect);tracerVertices.clear();DrawBeamgunTracers();
        Check(tracerVertices.size()==4&&std::abs(center(0,1)-191)<.01f); // Alternation crosses event boundaries.
        ResetBeamgun();tracerVertices.clear();DrawBeamgunTracers();Check(tracerVertices.empty());
        // Replay the DEM's20 far and18 near visual requests; no fourfold near duplication.
        int total=0;
        for(int event=0;event<38;++event){effect.phase=event>=20;effect.count=event>=20?2:1;effect.targets={2,effect.count==2?2:0,0,0};
            now+=1;send(effect);tracerVertices.clear();DrawBeamgunTracers();total+=int(tracerVertices.size()/4);}
        Check(total==56);
        const auto count=temps.size();auto bytes=EncodeBeamEffect(effect);bytes[0]=std::byte{2};
        Check(!messages[BeamEffectMessage](BeamEffectMessage,int(bytes.size()),bytes.data())&&temps.size()==count);
    }
    ResetBeamgun();engine.pEfxAPI=nullptr;engine.pTriAPI=nullptr;
}
#include "beam_render.hpp"
void HudChecks(const WeaponDefinition& knife)
{
    Send(WeaponDefinitionMessage,EncodeWeaponDefinition(knife));
    Legacy();
    Send(WeaponCurrentMessage,EncodeWeaponCurrent({220,1,1,29,0}));
    view={};view.health=100;WeaponVideoInit();rectangles.clear();
    WeaponRedraw(now,0);Check(rectangles.size()==4);
    Check(rectangles[0]==std::array<int,8>{628,360,6,1,1,22,233,255});
    Check(rectangles[1][0]==647 && rectangles[2][1]==347);
    translucent=0;rectangles.clear();WeaponRedraw(now,0);Check(blendCalls==4);
    crossColor="300 -7 80";crossSize="large";rectangles.clear();WeaponRedraw(now,0);
    Check(rectangles[0][4]==255&&rectangles[0][5]==0&&rectangles[0][6]==80&&rectangles[0][2]==10);
    for(unsigned char mask:{1,4,64}){
        messages["HideWeapon"]("HideWeapon",1,&mask);rectangles.clear();WeaponRedraw(now,0);Check(rectangles.empty());
    }
    unsigned char zero=0;messages["HideWeapon"]("HideWeapon",1,&zero);
    for(int state=0;state<6;++state){
        view.health=state==0?0:100;view.spectator=state==1;hud=state==2?0:1;cross=state==3?0:1;
        unsigned char fov=state==4?45:90;messages["SetFOV"]("SetFOV",1,&fov);
        rectangles.clear();WeaponRedraw(now,state==5);Check(state==3?rectangles.size()==4:rectangles.empty());
    }
    view.spectator=0;hud=cross=1;messages["SetFOV"]("SetFOV",1,&zero);
    rectangles.clear();WeaponRedraw(now,0);const auto dynamicCrosshair=rectangles;
    cross=0;rectangles.clear();WeaponRedraw(now,0);Check(rectangles==dynamicCrosshair&&!rectangles.empty());
    cross=1;rectangles.clear();WeaponRedraw(now,0);Check(rectangles==dynamicCrosshair);
    Send(WeaponCurrentMessage,EncodeWeaponCurrent({28,1,30,90,0}));rectangles.clear();WeaponRedraw(now,0);Check(rectangles.empty());
    Send(WeaponCurrentMessage,EncodeWeaponCurrent({220,1,1,29,0}));WeaponRedraw(now,0);Check(rectangles.size()==4);
    const int before=nativeMessages;
    unsigned char legacyAmmo[]{14,29};messages["AmmoPickup"]("AmmoPickup",2,legacyAmmo);
    Check(nativeMessages==before);ResetPickupHistory();
    Send(AmmoPickupMessage,EncodeAmmoPickup({220,0,29}));Send(AmmoPickupMessage,EncodeAmmoPickup({220,0,1}));
    historyNumber=0;DrawPickupHistory(0,0);Check(historyNumber==30);
    std::ostringstream state;PickupHistoryDiagnostics(state);Check(state.str().find("\"sprite\":77")!=std::string::npos);
    Send(WeaponCurrentMessage,EncodeWeaponCurrent({28,1,30,90,0}));historyNumber=0;DrawPickupHistory(0,0);Check(historyNumber==30);
    now+=6;historyNumber=0;DrawPickupHistory(0,0);Check(historyNumber==0);
    Send(AmmoPickupMessage,EncodeAmmoPickup({220,0,1}));DrawPickupHistory(0,0);Check(historyNumber==1);
    now+=6;DrawPickupHistory(1,0);historyNumber=0;DrawPickupHistory(0,0);Check(historyNumber==0);
    Send(AmmoPickupMessage,EncodeAmmoPickup({220,0,4}));ResetWeapons(false);DrawPickupHistory(0,0);Check(historyNumber==0);
    legacyAmmo[0]=2;messages["AmmoPickup"]("AmmoPickup",2,legacyAmmo);Check(nativeMessages>before);
}
void SelectionChecks(const WeaponDefinition& knife,const WeaponDefinition& beam,const WeaponDefinition& le)
{
    ResetWeapons(true);view={};view.health=100;hud=1;fast=0;
    for(auto def:{knife,beam,le})Send(WeaponDefinitionMessage,EncodeWeaponDefinition(def));Legacy();
    WeaponOwnership own{};own[28]=own[220]=own[725]=own[726]=true;Owned(own);
    Send(WeaponCurrentMessage,EncodeWeaponCurrent({220,1,1,29,0}));
    auto draw=[](){spriteDraws.clear();rectangles.clear();DrawWeaponSelection(0,0);};
    auto confirm=[](){usercmd_t cmd{};cmd.buttons=IN_ATTACK;sent.clear();WeaponInventoryCreateMove(.01f,&cmd,1);return cmd.buttons;};
    commands["slot2"]();draw();
    Check(spriteDraws.size()==7); // Five headers, selected weapon, overlay.
    Check(spriteDraws.front()[1]==138&&spriteDraws.front()[2]==10);
    Check(std::count_if(spriteDraws.begin(),spriteDraws.end(),[](auto d){return d[0]==80;})==5);
    Check(std::any_of(spriteDraws.begin(),spriteDraws.end(),[](auto d){return d[0]==81&&d[1]==163&&d[2]==30;}));
    Check(std::any_of(rectangles.begin(),rectangles.end(),[](auto r){return r[0]==173&&r[1]==33&&r[2]==20&&r[4]==0&&r[5]==160;}));
    Check(!(confirm()&IN_ATTACK)&&sent=="weapon_tknifeex2");
    commands["slot1"]();Check(!(confirm()&IN_ATTACK)&&sent=="weapon_ak47");
    commands["slot1"]();commands["slot1"]();Check(!(confirm()&IN_ATTACK)&&sent=="weapon_beamgun");
    commands["invnext"]();Check(!(confirm()&IN_ATTACK)&&sent=="weapon_ak47");
    commands["invprev"]();Check(!(confirm()&IN_ATTACK)&&sent=="weapon_beamgunle");
    commands["slot2"]();commands["cancelselect"]();Check((confirm()&IN_ATTACK)&&sent.empty());
    const int previous=fallbacks;commands["cancelselect"]();Check(fallbacks==previous+1);
    commands["slot2"]();now+=6;Check((confirm()&IN_ATTACK)&&sent.empty());
    commands["slot2"]();own[220]=false;Owned(own);Check((confirm()&IN_ATTACK)&&sent.empty());own[220]=true;Owned(own);
    commands["slot2"]();Send(WeaponCurrentMessage,EncodeWeaponCurrent({28,1,30,90,0}));Check((confirm()&IN_ATTACK)&&sent.empty());
    for(int gate=0;gate<4;++gate){commands["slot2"]();view.health=gate==0?0:100;view.spectator=gate==1;
        DrawWeaponSelection(gate==2?1:0,gate==3);Check((confirm()&IN_ATTACK)&&sent.empty());
        view.health=100;view.spectator=0;DrawWeaponSelection(0,0);}
    commands["slot2"]();unsigned char menu[]{3,0,1,1,'a',0};messages["ShowMenu"]("ShowMenu",6,menu);
    int before=fallbacks;commands["slot1"]();Check(fallbacks==before+1);Check((confirm()&IN_ATTACK)&&sent.empty());
    menu[3]=0;messages["ShowMenu"]("ShowMenu",6,menu);now+=2;commands["slot2"]();Check(!(confirm()&IN_ATTACK));
    // AMXX's infinite menus close locally, without a ShowMenu(0) message.
    // Every valid key, including page/exit keys 6..0, must release weapon slots.
    for(int key=1;key<=10;++key){
        unsigned char infinite[]{0xff,3,255,0,'a',0};
        messages["ShowMenu"]("ShowMenu",6,infinite);
        const int nativeBefore=fallbacks;
        const auto command="slot"+std::to_string(key);commands[command]();
        Check(fallbacks==nativeBefore+1);
        commands["slot2"]();Check(!(confirm()&IN_ATTACK)&&sent=="weapon_tknifeex2");
        commands["slot1"]();Check(!(confirm()&IN_ATTACK)&&sent=="weapon_ak47");
    }
    unsigned char limited[]{2,0,255,0,'a',0};
    messages["ShowMenu"]("ShowMenu",6,limited);before=fallbacks;
    commands["invnext"]();commands["invprev"]();Check(fallbacks==before+2);before=fallbacks;
    commands["slot1"]();commands["slot2"]();Check(fallbacks==before+2);
    // A server response may open a submenu immediately after a valid key.
    messages["ShowMenu"]("ShowMenu",6,limited);before=fallbacks;
    commands["slot2"]();Check(fallbacks==before+1);
    commands["slot2"]();Check(!(confirm()&IN_ATTACK)&&sent=="weapon_tknifeex2");
    fast=1;sent.clear();commands["slot2"]();Check(sent=="weapon_tknifeex2");
    sent.clear();commands["slot1"]();Check(sent.empty());Check(!(confirm()&IN_ATTACK)&&sent=="weapon_ak47");
    commands["invnext"]();draw();Check(!spriteDraws.empty());commands["cancelselect"]();
    screenWidth=480;screenHeight=360;WeaponVideoInit();fast=0;commands["slot2"]();draw();
    Check(std::any_of(spriteDraws.begin(),spriteDraws.end(),[](auto d){return d[0]==80&&d[1]==70;}));
    Check(std::any_of(spriteDraws.begin(),spriteDraws.end(),[](auto d){return d[0]==81&&d[1]==95;}));
    missingSprites=true;WeaponVideoInit();commands["slot2"]();textCalls=0;draw();Check(spriteDraws.empty()&&textCalls==6);
    missingSprites=false;screenWidth=1280;screenHeight=720;WeaponVideoInit();draw();Check(spriteDraws.empty());
    commands["slot2"]();ResetWeapons(false);draw();Check(spriteDraws.empty());
    own={};own[28]=true;Owned(own);before=fallbacks;commands["slot1"]();Check(fallbacks==before+1);
    ResetWeapons(true);
}
bool moneySuit=true;
int NativeMoneyFrame(float,int)
{
    const int w=screenWidth>=640?20:12,h=screenWidth>=640?25:16;
    const int x=screenWidth-5*w-(screenWidth>=640?16:12),y=screenHeight-(moneySuit?3:5)*h;
    const wrect_t dollar{192,210,25,25+h},minus{210,223,25,48},digit{0,w,0,h},other{0,w,1,h+1};
    auto emit=[&](int handle,int xx,int yy,const wrect_t& rect){engine.pfnSPR_Set(handle,150,91,17);engine.pfnSPR_DrawAdditive(0,xx,yy,&rect);};
    const int atlas=screenWidth>=640?90:91;
    emit(atlas,x+2*w,y,digit); // A shared digit before the account anchor is not money.
    emit(92,x-18,y,dollar); // Same rectangle on another atlas.
    emit(atlas,x-18,y,dollar);
    emit(atlas,x+2*w,y,digit);
    emit(atlas,x-13,y-h-h/2,minus);
    emit(atlas,x+2*w,y-h-h/2,digit);
    emit(atlas,10,screenHeight-h,digit); // Health/armor side.
    emit(atlas,x+2*w,y+1,digit); // Not the account row.
    emit(atlas,x-1,y,digit); // Not a digit cell.
    emit(atlas,x+2*w,y,other); // Same atlas, different rectangle.
    return 1;
}
void BeamHudChecks()
{
    ResetWeapons(true);view.health=100;view.spectator=0;hud=1;
    const auto native=downstream.HUD_Redraw;downstream.HUD_Redraw=NativeMoneyFrame;
    for(int id:{725,726,28})for(int size:{1280,640,480})for(bool suit:{false,true})for(int energy:{0,1,100})
    {
        screenWidth=size;screenHeight=size==480?360:720;moneySuit=suit;WeaponVideoInit();
        ReceiveExtendedWeaponState({id,1,80,140,energy});
        spriteDraws.clear();spriteColors.clear();rectangles.clear();WeaponRedraw(now,0);
        Check(spriteDraws.size()>=10);
        const int h=size>=640?25:16,y=screenHeight-(suit?3:5)*h;
        const int shift=id!=28 && energy>0?h:0;
        Check(spriteDraws[0][2]==y && spriteDraws[1][2]==y);
        Check(spriteDraws[2][2]==y-shift && spriteDraws[3][2]==y-shift);
        Check(spriteDraws[4][2]==y-h-h/2-shift && spriteDraws[5][2]==y-h-h/2-shift);
        Check(spriteDraws[6][2]==screenHeight-h && spriteDraws[7][2]==y+1 && spriteDraws[8][2]==y && spriteDraws[9][2]==y);
        Check(spriteColors[3]==std::array<int,4>{size>=640?90:91,150,91,17});
        const auto blue=std::count_if(spriteColors.begin(),spriteColors.end(),[](auto c){return c[1]==114 && c[2]==197 && c[3]==255;});
        Check((blue>0)==(id!=28 && energy>0));
    }
    screenWidth=1280;screenHeight=720;moneySuit=true;WeaponVideoInit();
    ReceiveExtendedWeaponState({726,1,80,140,100});
    for(int gate=0;gate<8;++gate)
    {
        unsigned char mask=gate==0?1:gate==1?4:0;
        messages["HideWeapon"]("HideWeapon",1,&mask);
        view.health=gate==2?0:100;view.spectator=gate==3;hud=gate==4?0:1;
        if(gate==6)ResetWeapons(false);
        if(gate==7)ReceiveExtendedWeaponState({726,0,80,140,100});
        spriteDraws.clear();spriteColors.clear();WeaponRedraw(now,gate==5);
        Check(spriteDraws.size()==10 && spriteDraws[2][2]==645 && spriteDraws[4][2]==608);
    }
    view.health=100;view.spectator=0;hud=1;ResetWeapons(false);downstream.HUD_Redraw=native;
}
#include "wide_ammo.hpp"
}
int main(int argc,char** argv)
{
    using namespace csol;using namespace csol::client;
    metahook_api_t hooks{};api=&hooks;
    hooks.HookUserMsg=[](const char* name,pfnUserMsgHook fn)->pfnUserMsgHook{messages[name]=fn;return Native;};
    hooks.HookCmd=[](const char* name,xcommand_t fn)->xcommand_t{commands[name]=fn;return Fallback;};
    engine.pfnHookUserMsg=[](const char* name,pfnUserMsgHook fn)->int{messages[name]=fn;return 1;};
    engine.pfnAddCommand=[](const char* name,xcommand_t fn)->int{commands[name]=fn;return 1;};
    engine.pfnRegisterVariable=[](const char*,const char*,int)->cvar_t*{return &protocol;};
    engine.Cvar_SetValue=[](const char*,float value){protocol.value=value;};
    engine.GetClientTime=[]()->float{return now;};
    engine.pfnGetLevelName=[]()->const char*{return level.c_str();};
    engine.Cmd_Argv=[](int)->char*{return commandName.data();};
    engine.pfnGetCvarFloat=[](const char* name)->float{
        if(!std::strcmp(name,"hud_fastswitch"))return fast;
        if(!std::strcmp(name,"hud_draw"))return hud;
        if(!std::strcmp(name,"crosshair"))return cross;
        if(!std::strcmp(name,"cl_dynamiccrosshair"))return dynamic;
        if(!std::strcmp(name,"cl_crosshair_translucent"))return translucent;
        if(!std::strcmp(name,"cl_crosshair_type"))return crossType;
        if(!std::strcmp(name,"hud_drawhistory_time"))return 5;return 1;};
    engine.pfnGetCvarString=[](const char* name)->char*{return !std::strcmp(name,"cl_crosshair_size")?crossSize.data():crossColor.data();};
    engine.pfnServerCmd=[](const char* command)->int{sent=command;return 1;};
    engine.pfnWeaponAnim=[](int sequence,int body){++animations;weaponAnimations.push_back({sequence,body});weaponView.curstate.sequence=sequence;weaponView.curstate.body=body;};
    engine.pfnGetScreenInfo=[](SCREENINFO* screen)->int{screen->iWidth=screenWidth;screen->iHeight=screenHeight;return 1;};
    engine.pfnSPR_GetList=[](char* path,int* count)->client_sprite_t*{
        static std::vector<client_sprite_t> icons;icons.clear();*count=0;if(missingSprites)return nullptr;
        auto add=[&](const char* name,const char* sprite,int res,wrect_t rect){client_sprite_t i{};
            std::strcpy(i.szName,name);std::strcpy(i.szSprite,sprite);i.iRes=res;i.rc=rect;icons.push_back(i);};
        if(!std::strcmp(path,"sprites/hud.txt"))for(int res:{320,640}){
            for(int i=0;i<5;++i){char name[]{'b','u','c','k','e','t',char('1'+i),0};add(name,"test_buckets",res,{i*20,(i+1)*20,0,20});}
            add("selection","test_selection",res,{0,170,180,225});
            const int w=res==640?20:12,h=res==640?25:16;
            const char* atlas=res==640?"test_digits":"test_small_digits";
            constexpr int left[]{0,23,47,70,95,119,144,169,192,216};
            for(int n=0;n<10;++n){char name[24];std::snprintf(name,sizeof(name),"number_%d",n);const int x=res==640?left[n]:n*w;add(name,atlas,res,{x,x+w,0,h});}
            add("dollar",atlas,res,{192,210,25,25+h});add("minus",atlas,res,{210,223,25,48});add("plus",atlas,res,{223,240,25,48});}
        else {
            add("weapon","test_weapon",640,{0,170,0,45});add("weapon_s","test_active",640,{0,170,45,90});
            if(lowResolutionSprites){add("weapon","test_low_weapon",320,{0,85,0,23});add("weapon_s","test_low_active",320,{0,85,23,46});}
            if(!std::strcmp(path,"sprites/weapon_tknifeex2.txt"))add("ammo","csol/640hud12",640,{49,73,231,255});
            if(!std::strcmp(path,"sprites/weapon_ak47.txt"))for(int res:{320,640})add("ammo","test_ammo",res,{0,24,231,255});}
        *count=int(icons.size());return icons.data();};
    engine.pfnSPR_Load=[](const char* path)->int{++spriteLoads;if(std::strstr(path,"buckets"))return 80;if(std::strstr(path,"selection"))return 81;
        if(std::strstr(path,"test_small_digits"))return 91;if(std::strstr(path,"test_digits"))return 90;
        if(std::strstr(path,"test_low_weapon"))return 84;
        if(std::strstr(path,"test_active"))return 83;if(std::strstr(path,"test_weapon"))return 82;return 77;};
    engine.pfnSPR_Set=[](int handle,int r,int g,int b){spriteHandle=handle;spriteColors.push_back({handle,r,g,b});};
    engine.pfnSPR_DrawAdditive=[](int,int x,int y,const wrect_t* rect){++spriteCalls;spriteDraws.push_back({spriteHandle,x,y,rect->left,rect->right,rect->top,rect->bottom});};
    engine.pfnFillRGBA=[](int x,int y,int w,int h,int r,int g,int b,int a){rectangles.push_back({x,y,w,h,r,g,b,a});};
    engine.pfnFillRGBABlend=[](int x,int y,int w,int h,int r,int g,int b,int a){++blendCalls;rectangles.push_back({x,y,w,h,r,g,b,a});};
    engine.pfnDrawStringReverse=[](int,int,const char* text,int,int,int)->int{++reverseCalls;historyNumber=std::atoi(text);return 0;};
    engine.pfnDrawString=[](int,int,const char*,int,int,int)->int{++textCalls;return 0;};
    downstream.HUD_Redraw=[](float,int)->int{return 1;};
    downstream.CL_CreateMove=[](float,usercmd_t*,int){};
    downstream.HUD_PostRunCmd=[](local_state_t* from,local_state_t* to,usercmd_t*,int,double,unsigned){
        ++predicts;Check(from->client.m_iId>=0&&from->client.m_iId<32);
        if(from->client.m_iId!=0)*to=*from;}; // Actual stock ID 0 exits without copying weapon state.
    view.health=100;InitializeWeapons();InitializeWeaponInventory();Check(protocol.value==1);
    if(argc==3 && !std::strcmp(argv[1],"--wide-ammo")){WideAmmoChecks(argv[2]);return 0;}
    SynchronizeFrameSession();
    commands["slot1"]();Check(fallbacks==1);
    WeaponDefinition knife{220,1,6,14,-1,29,-1,1,24,"weapon_tknifeex2"};
    WeaponDefinition beam{725,0,8,15,16,200,100,80,0,"weapon_beamgun"};
    WeaponDefinition le=beam;le.id=726;le.name="weapon_beamgunle";le.position=9;
    if(argc>1 && !std::strcmp(argv[1],"--cannon-only")){CannonChecks();CannonFlashChecks();std::cout<<"PASS cannon "<<checks<<" checks\n";return 0;}
    if(argc>1 && !std::strcmp(argv[1],"--cannon-flash")){CannonFlashOracle();return 0;}
    if(argc>1 && !std::strcmp(argv[1],"--cannon-muzzle")){CannonMuzzleOracle();return 0;}
    if(argc>1 && !std::strcmp(argv[1],"--cannon-smoke")){
        float age;int frames;std::cout<<std::setprecision(9);
        while(std::cin>>age>>frames){const auto sample=cannon::SampleSmoke(age,frames);
            std::cout<<'['<<sample.width<<','<<sample.alpha<<','<<sample.frame<<"]\n";}return 0;}
    if(argc>1 && !std::strcmp(argv[1],"--beam-render")){BeamRenderOracle(argv);return 0;}
    if(argc>1 && !std::strcmp(argv[1],"--beam-render-only")){BeamRenderChecks();std::cout<<"PASS beam render "<<checks<<" checks\n";return 0;}
    if(argc>1 && !std::strcmp(argv[1],"--beam-layout")){
        screenWidth=std::atoi(argv[2]);screenHeight=std::atoi(argv[3]);moneySuit=std::atoi(argv[6])!=0;WeaponVideoInit();
        ReceiveExtendedWeaponState({std::atoi(argv[4]),1,80,140,std::atoi(argv[5])});
        downstream.HUD_Redraw=NativeMoneyFrame;spriteDraws.clear();rectangles.clear();WeaponRedraw(now,0);
        std::cout<<"{\"sprites\":[";
        for(std::size_t i=0;i<spriteDraws.size();++i){if(i)std::cout<<',';std::cout<<'[';for(int n=0;n<7;++n){if(n)std::cout<<',';std::cout<<spriteDraws[i][n];}std::cout<<']';}
        std::cout<<"],\"rectangles\":[";
        for(std::size_t i=0;i<rectangles.size();++i){if(i)std::cout<<',';std::cout<<'[';for(int n=0;n<8;++n){if(n)std::cout<<',';std::cout<<rectangles[i][n];}std::cout<<']';}
        std::cout<<"]}";return 0;
    }
    if(argc>1 && !std::strcmp(argv[1],"--pickup-oracle")){
        screenWidth=std::atoi(argv[2]);screenHeight=std::atoi(argv[3]);now=10;
        Send(WeaponDefinitionMessage,EncodeWeaponDefinition(beam));ObserveSelectionClip(725,std::atoi(argv[4]));
        for(int i=0;i<std::atoi(argv[6]);++i)WeaponPickup(725);
        now=10+std::atof(argv[5]);spriteDraws.clear();spriteColors.clear();DrawPickupHistory(0,0);
        std::cout<<"[";for(std::size_t i=0;i<spriteDraws.size();++i){if(i)std::cout<<',';std::cout<<'[';
            for(int n=0;n<7;++n){if(n)std::cout<<',';std::cout<<spriteDraws[i][n];}
            for(int n=1;n<4;++n)std::cout<<','<<spriteColors[i][n];std::cout<<']';}
        std::cout<<"]\n";return 0;
    }
    if(argc>1 && !std::strcmp(argv[1],"--pickup-only")){PickupChecks(knife,beam);std::cout<<"PASS pickup "<<checks<<" checks\n";return 0;}
    if(argc>1 && !std::strcmp(argv[1],"--selection-oracle")){
        screenWidth=std::atoi(argv[2]);fast=0;
        Send(WeaponDefinitionMessage,EncodeWeaponDefinition(knife));Legacy();
        WeaponOwnership own{};own[28]=own[220]=true;Owned(own);
        Send(WeaponCurrentMessage,EncodeWeaponCurrent({28,1,30,90,0}));
        Send(WeaponCurrentMessage,EncodeWeaponCurrent({220,1,1,std::atoi(argv[4]),0}));
        commands[std::string("slot")+argv[3]]();spriteDraws.clear();rectangles.clear();DrawWeaponSelection(0,0);
        std::cout<<"{\"sprites\":[";bool first=true;
        for(auto d:spriteDraws){if(!first)std::cout<<',';first=false;std::cout<<'[';
            for(int i=0;i<7;++i){if(i)std::cout<<',';std::cout<<d[i];}std::cout<<']';}
        std::cout<<"],\"rectangles\":[";first=true;
        for(auto d:rectangles){if(!first)std::cout<<',';first=false;std::cout<<'[';
            for(int i=0;i<8;++i){if(i)std::cout<<',';std::cout<<d[i];}std::cout<<']';}
        std::cout<<"]}\n";return 0;
    }
    if(argc>1 && !std::strcmp(argv[1],"--selection-only")){SelectionChecks(knife,beam,le);std::cout<<"PASS selection "<<checks<<" checks\n";return 0;}
    for(auto def:{knife,beam,le})Send(WeaponDefinitionMessage,EncodeWeaponDefinition(def));Legacy();
    Check(FindWeaponDefinition(220)&&FindWeaponDefinition(28));
    WeaponOwnership owned{};owned[28]=owned[220]=owned[725]=owned[726]=true;Owned(owned);
    Send(WeaponCurrentMessage,EncodeWeaponCurrent({725,1,80,200,100}));Check(ExtendedWeaponActive());
    Check(lastHudMask==1);
    unsigned char spectatorWeapon[]{1,0,0};messages["CurWeapon"]("CurWeapon",3,spectatorWeapon);
    Check(ExtendedWeaponActive());
    unsigned char serverHide=2;messages["HideWeapon"]("HideWeapon",1,&serverHide);Check(lastHudMask==3);
    serverHide=0;messages["HideWeapon"]("HideWeapon",1,&serverHide);Check(lastHudMask==1);
    commands["slot1"]();Check(sent.empty());commands["cancelselect"]();
    commands["slot2"]();Check(sent=="weapon_tknifeex2");
    commandName="weapon_beamgun";commands[commandName]();Check(sent==commandName);
    owned[725]=false;Owned(owned);sent.clear();commands[commandName]();Check(sent.empty());
    owned[725]=true;Owned(owned);
    unsigned char menu[]{3,0,255,0,0};messages["ShowMenu"]("ShowMenu",5,menu);
    commands["slot1"]();Check(fallbacks==2&&sent.empty());
    menu[0]=0;messages["ShowMenu"]("ShowMenu",5,menu);
    fast=0;commands["slot1"]();Check(sent.empty());
    usercmd_t cmd{};cmd.buttons=IN_ATTACK;WeaponInventoryCreateMove(.01f,&cmd,1);
    Check(sent=="weapon_ak47"&&!(cmd.buttons&IN_ATTACK));
    sent.clear();commands["slot2"]();now+=6;cmd.buttons=IN_ATTACK;WeaponInventoryCreateMove(.01f,&cmd,1);
    Check(sent.empty()&&(cmd.buttons&IN_ATTACK));
    Predict(220,220,IN_ATTACK);Check(animations==1);
    Predict(220,220,IN_ATTACK,0);Check(animations==1);
    Predict(725,725,IN_ATTACK);Predict(726,726,IN_ATTACK);Predict(220,725,IN_ATTACK);Predict(28,0,0);
    Check(animations==3&&predicts==6);
    PredictionPermissions();
    BeamPredictionChecks();
    BeamSwitchChecks();
    BeamEffectChecks();
    BeamRenderChecks();
    Send(WeaponCurrentMessage,EncodeWeaponCurrent({220,1,1,29,0}));
    std::ostringstream state;WeaponDiagnostics(state);Check(state.str().find("\"active\":220")!=std::string::npos);
    Send(WeaponCurrentMessage,EncodeWeaponCurrent({0,0,-1,0,0}));Check(!ExtendedWeaponActive());Check(lastHudMask==0);
    ResetWeapons(false);Check(FindWeaponDefinition(220));commands["slot1"]();Check(fallbacks==3);
    ResetWeapons(true);Check(!FindWeaponDefinition(220)&&!FindWeaponDefinition(28));
    Check(lastHudMask==0&&hudMasks>=5&&nativeMessages>=8);

    // Exercise the actual production session code around real message handlers.
    // Packet time may be ahead of the next interpolated frame without a reconnect.
    level="";SynchronizeFrameSession();
    level="maps/cs_assault.bsp";now=100;
    Send(WeaponDefinitionMessage,EncodeWeaponDefinition(knife));Legacy();
    now=99.9f;SynchronizeFrameSession();
    Check(FindWeaponDefinition(220)&&FindWeaponDefinition(28));
    for(int i=0;i<4;++i){
        now=100+i;
        Owned(owned);Send(WeaponCurrentMessage,EncodeWeaponCurrent({220,1,1,29,0}));
        now-=.05f;SynchronizeFrameSession();
        Check(ExtendedWeaponActive()&&FindWeaponDefinition(220)&&FindWeaponDefinition(28));
    }
    WeaponVideoInit();Check(ExtendedWeaponActive()&&FindWeaponDefinition(220));
    // Ordinary network clock corrections between HUD frames are not reconnects.
    now-=.01f;SynchronizeFrameSession();
    Check(ExtendedWeaponActive()&&FindWeaponDefinition(220)&&FindWeaponDefinition(28));
    messages["ResetHUD"]("ResetHUD",0,nullptr);
    Check(!ExtendedWeaponActive()&&FindWeaponDefinition(220));
    Owned(owned);Send(WeaponCurrentMessage,EncodeWeaponCurrent({220,1,1,29,0}));
    Check(ExtendedWeaponActive());
    now=1;SynchronizeFrameSession();
    Check(!ExtendedWeaponActive()&&!FindWeaponDefinition(220));
    level="";SynchronizeFrameSession();
    level="maps/cs_assault.bsp";now=50;
    Send(WeaponDefinitionMessage,EncodeWeaponDefinition(knife));
    now=49.9f;SynchronizeFrameSession();Check(FindWeaponDefinition(220));
    level="maps/de_dust2.bsp";now=0;
    Send(WeaponDefinitionMessage,EncodeWeaponDefinition(beam));
    SynchronizeFrameSession();Check(FindWeaponDefinition(725)&&!FindWeaponDefinition(220));
    HudChecks(knife);
    SelectionChecks(knife,beam,le);
    PickupChecks(knife,beam);
    BeamHudChecks();
    // Guard HUD follows reliable owner state even after holstering the beamgun.
    ResetWeapons(true);now=200;view.health=100;view.spectator=0;hud=1;
    unsigned char guard[]{1,0xe8,3};messages["CSOGuard"]("CSOGuard",3,guard);
    spriteDraws.clear();DrawWeaponGuard(0,0);Check(spriteDraws.size()==1);
    Check(spriteDraws[0]==std::array<int,7>{77,608,605,66,130,1,65});
    now+=.5f;spriteDraws.clear();DrawWeaponGuard(0,0);Check(spriteDraws.size()==1);
    for(int gate=0;gate<5;++gate){
        view.health=gate==0?0:100;view.spectator=gate==1;hud=gate==2?0:1;
        spriteDraws.clear();DrawWeaponGuard(gate==3?4:0,gate==4);Check(spriteDraws.empty());
    }
    view.health=100;view.spectator=0;hud=1;
    now=201.01f;spriteDraws.clear();DrawWeaponGuard(0,0);Check(spriteDraws.empty());
    guard[1]=guard[2]=255;messages["CSOGuard"]("CSOGuard",3,guard);DrawWeaponGuard(0,0);Check(spriteDraws.empty());
    guard[1]=0xe8;guard[2]=3;messages["CSOGuard"]("CSOGuard",3,guard);
    guard[1]=guard[2]=0;messages["CSOGuard"]("CSOGuard",3,guard);DrawWeaponGuard(0,0);Check(spriteDraws.empty());
    guard[1]=0xe8;guard[2]=3;messages["CSOGuard"]("CSOGuard",3,guard);ResetWeapons(false);
    DrawWeaponGuard(0,0);Check(spriteDraws.empty());
    std::cout<<"PASS client "<<checks<<" checks\n";
}
