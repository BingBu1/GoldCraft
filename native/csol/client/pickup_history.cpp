// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "weapon_inventory.hpp"
#include "pickup_history.hpp"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <ostream>

namespace csol::client
{
namespace
{
enum class Kind { Ammo, Weapon };
struct Entry
{
    int id{},secondary{},amount{},sprite{};
    wrect_t rect{};
    float received{},expires{};
    Kind kind{};
    bool loaded{};
};
std::array<Entry,12> entries;
pfnUserMsgHook oldAmmoPickup{};
int draws{},weaponDraws{},nextEntry{},historyGap{};
void Load(Entry& entry,int resolution);
void Add(int id,int secondary,int amount,Kind kind=Kind::Ammo)
{
    if(!FindWeaponDefinition(id) || (kind==Kind::Ammo && amount<=0))return;
    const float now=engine.GetClientTime();
    const int duration=int(std::clamp(engine.pfnGetCvarFloat("hud_drawhistory_time"),0.f,30.f));
    if(kind==Kind::Ammo)for(auto& entry:entries)
        if(entry.kind==kind && entry.id==id && entry.secondary==secondary && now<entry.expires &&
            now>=entry.received && now-entry.received<.1f)
        {entry.amount=std::min(32767,entry.amount+amount);entry.expires=now+duration;return;}
    SCREENINFO screen{};screen.iSize=sizeof(screen);engine.pfnGetScreenInfo(&screen);
    Entry entry{id,secondary,amount,0,{},now,now+duration,kind};
    Load(entry,screen.iWidth<640?320:640);
    // Stock history fills upward, then reuses the first row at its screen/capacity limit.
    if(std::none_of(entries.begin(),entries.end(),[&](const auto& e){return e.id && now>=e.received && now<e.expires;}) ||
        nextEntry>=int(entries.size()) || 2*(historyGap+36)+nextEntry*(historyGap+5)>screen.iHeight-100)nextEntry=0;
    entries[nextEntry++]=entry;
}
int ExtendedPickup(const char*,int size,void* data)
{
    SynchronizeSession();AmmoPickup pickup;
    if(data && size>0 && DecodeAmmoPickup({static_cast<const std::byte*>(data),std::size_t(size)},pickup))
        Add(pickup.id,pickup.secondary,pickup.amount);
    return 1;
}
int LegacyPickup(const char* name,int size,void* data)
{
    if(data && size==2)
    {
        const auto* b=static_cast<unsigned char*>(data);
        // Older servers may still emit the byte-sized message. Keep extended
        // ammo out of stock's history, which cannot resolve its icon and stops
        // aging while HIDEHUD_WEAPONS is set.
        for(int id=1;id<LegacyWeaponLimit;++id)
            if(const auto* d=FindWeaponDefinition(id);d && (d->ammo1==b[0] || d->ammo2==b[0]))
                return oldAmmoPickup?oldAmmoPickup(name,size,data):1;
        for(int id=LegacyWeaponLimit;id<WeaponIdLimit;++id)
            if(const auto* d=FindWeaponDefinition(id);d && (d->ammo1==b[0] || d->ammo2==b[0]))
            {Add(id,d->ammo1==b[0]?0:1,b[1]);return 1;}
    }
    return oldAmmoPickup?oldAmmoPickup(name,size,data):1;
}
void Load(Entry& e,int resolution)
{
    if(e.loaded)return;
    const auto* d=FindWeaponDefinition(e.id);if(!d)return;
    e.loaded=true;
    std::string path="sprites/"+d->name+".txt";int count=0;
    const auto* list=engine.pfnSPR_GetList(path.data(),&count);
    const char* role=e.kind==Kind::Weapon?"weapon":e.secondary?"ammo2":"ammo";
    for(int pass=0;pass<(resolution==640?1:2);++pass)
    {
        for(int i=0;list && i<count;++i)
        {
            if(list[i].iRes!=(pass?640:resolution) || !std::memchr(list[i].szName,0,sizeof(list[i].szName)) ||
                !std::memchr(list[i].szSprite,0,sizeof(list[i].szSprite)))continue;
            const auto& rect=list[i].rc;
            if(rect.left<0 || rect.top<0 || rect.right<=rect.left || rect.bottom<=rect.top)continue;
            if(!std::strcmp(list[i].szName,"weapon_s"))historyGap=std::max(historyGap,rect.bottom-rect.top);
            if(std::strcmp(list[i].szName,role))continue;
            std::string sprite=list[i].szSprite;
            if(sprite.empty() || sprite.front()=='/' || sprite.find("..")!=std::string::npos || sprite.find_first_of(":\\")!=std::string::npos)continue;
            sprite="sprites/"+sprite+".spr";e.sprite=engine.pfnSPR_Load(sprite.c_str());e.rect=rect;
            historyGap=std::max(historyGap,rect.bottom-rect.top);
        }
        if(e.sprite)break;
    }
}
}
void InitializePickupHistory()
{
    engine.pfnRegisterVariable("csol_ammo_protocol","1",FCVAR_USERINFO);
    engine.Cvar_SetValue("csol_ammo_protocol",1);
    engine.pfnHookUserMsg(AmmoPickupMessage,ExtendedPickup);
    oldAmmoPickup=api->HookUserMsg("AmmoPickup",LegacyPickup);
}
void AddWeaponPickup(int id){if(ExtendedWeaponId(id))Add(id,0,0,Kind::Weapon);}
void ResetPickupHistory(){entries={};nextEntry=0;}
void PickupHistoryVideoInit(){historyGap=0;for(auto& e:entries){e.sprite=0;e.loaded=false;}}
void DrawPickupHistory(int hidden,int intermission)
{
    const float now=engine.GetClientTime();
    for(auto& e:entries)if(e.id && (now>=e.expires || now<e.received))e={};
    if((hidden&5) || intermission || view.health<=0 || view.spectator || engine.pfnGetCvarFloat("hud_draw")==0)return;
    SCREENINFO screen{};screen.iSize=sizeof(screen);engine.pfnGetScreenInfo(&screen);
    for(auto& e:entries)if(e.id)Load(e,screen.iWidth<640?320:640);
    for(std::size_t index=0;index<entries.size();++index)
    {
        auto& e=entries[index];if(!e.id || !e.sprite)continue;
        const int width=e.rect.right-e.rect.left,height=e.rect.bottom-e.rect.top;
        const int y=screen.iHeight-2*(historyGap+36)-int(index)*(historyGap+5);
        if(width<=0 || height<=0 || y<40)continue;
        const float fade=std::clamp(int((e.expires-now)*80.f),0,255)/255.f;
        const bool hasAmmo=e.kind==Kind::Ammo || WeaponHasAmmo(e.id);
        const int r=int(255*fade),g=int((hasAmmo?160:16)*fade),b=hasAmmo?0:int(16*fade);
        const int x=e.kind==Kind::Weapon?screen.iWidth-width:screen.iWidth-10-width;
        engine.pfnSPR_Set(e.sprite,r,g,b);engine.pfnSPR_DrawAdditive(0,x,y,&e.rect);
        if(e.kind==Kind::Ammo){
            char text[12];std::snprintf(text,sizeof(text),"%d",e.amount);
            engine.pfnDrawStringReverse(x-10,y+height/2-6,text,r,g,b);
        }else ++weaponDraws;
        ++draws;
    }
}
void PickupHistoryDiagnostics(std::ostream& out)
{
    out<<",\"pickups\":{\"draws\":"<<draws<<",\"id\":"<<entries[0].id<<",\"amount\":"<<entries[0].amount
       <<",\"sprite\":"<<entries[0].sprite<<",\"weaponDraws\":"<<weaponDraws<<",\"entries\":[";
    bool first=true;for(const auto& e:entries)if(e.id){if(!first)out<<',';first=false;
        out<<"{\"id\":"<<e.id<<",\"kind\":\""<<(e.kind==Kind::Weapon?"weapon":"ammo")<<"\",\"amount\":"<<e.amount<<",\"sprite\":"<<e.sprite<<'}';}
    out<<"]}";
}
}
