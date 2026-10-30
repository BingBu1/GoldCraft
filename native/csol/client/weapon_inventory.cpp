// SPDX-License-Identifier: GPL-3.0-or-later
#include "runtime.hpp"
#include "weapon_inventory.hpp"
#include "pickup_history.hpp"
#include <algorithm>
#include <cstring>
#include <in_buttons.h>
#include <set>
#include <cstdio>
#include <ostream>

namespace csol::client
{
namespace
{
std::array<WeaponDefinition,WeaponIdLimit> definitions;
std::set<std::string> commandNames;
struct SelectionIcon { int handle{};wrect_t rect{}; };
struct SelectionSprites { SelectionIcon normal,selected; };
std::array<SelectionSprites,WeaponIdLimit> sprites;
std::vector<int> selectionIds;
SCREENINFO selectionScreen{};
std::array<SelectionIcon,5> buckets;
SelectionIcon selectionFrame;
int selectionDraws{};
std::array<int,WeaponIdLimit> clips;
std::array<int,32768> ammo;
int selectionHidden{},selectionIntermission{};
WeaponInventoryReceiver inventory;
WeaponCurrent current;
bool available{}, textMenu{};
unsigned menuSlots{};
double menuUntil{}, selectionUntil{};
int selected{};
double guardUntil{};
int guardSprite{},guardDraws{};
std::array<xcommand_t,10> originalSlots{};
xcommand_t originalNext{}, originalPrevious{}, originalCancel{};
pfnUserMsgHook oldShowMenu{};

std::span<const std::byte> Bytes(int size,void* data)
{
    return size>0 && data?std::span(static_cast<const std::byte*>(data),std::size_t(size)):std::span<const std::byte>{};
}
bool ManageSelection()
{
    if(!available || view.health<=0 || view.spectator || (selectionHidden&5) || selectionIntermission)return false;
    for(int id=LegacyWeaponLimit;id<WeaponIdLimit;++id)
        if(inventory.Owned()[id] && definitions[id].id)return true;
    return false;
}
void Select(int id)
{
    if(ValidWeaponId(id) && inventory.Owned()[id] && definitions[id].id)
        engine.pfnServerCmd(definitions[id].name.c_str());
    selected=0;
}
void NamedWeapon()
{
    const char* name=engine.Cmd_Argv(0);
    for(int id=LegacyWeaponLimit;name && id<WeaponIdLimit;++id)
        if(definitions[id].id && definitions[id].name==name){Select(id);return;}
}
std::vector<int> Choices(int slot)
{
    std::vector<int> ids;
    for(int id=1;id<WeaponIdLimit;++id)
        if(inventory.Owned()[id] && definitions[id].id && (slot<0 || definitions[id].slot==slot))ids.push_back(id);
    std::sort(ids.begin(),ids.end(),[](int a,int b){const auto& x=definitions[a];const auto& y=definitions[b];
        return x.slot!=y.slot?x.slot<y.slot:x.position!=y.position?x.position<y.position:a<b;});
    return ids;
}
void PrepareSelection()
{
    selectionIds=Choices(-1);
    selectionScreen.iSize=sizeof(selectionScreen);engine.pfnGetScreenInfo(&selectionScreen);
    const int resolution=selectionScreen.iWidth<640?320:640;
    if(!buckets[0].handle)
    {
        char path[]="sprites/hud.txt";int count=0;
        const auto* list=engine.pfnSPR_GetList(path,&count);
        for(int i=0;list && i<count;++i)
        {
            if(list[i].iRes!=resolution || !std::memchr(list[i].szName,0,sizeof(list[i].szName)) ||
                !std::memchr(list[i].szSprite,0,sizeof(list[i].szSprite)))continue;
            SelectionIcon* target=nullptr;
            for(int slot=0;slot<5;++slot){char name[12];std::snprintf(name,sizeof(name),"bucket%d",slot+1);
                if(!std::strcmp(name,list[i].szName))target=&buckets[slot];}
            if(!std::strcmp(list[i].szName,"selection"))target=&selectionFrame;
            if(target){std::string sprite="sprites/"+std::string(list[i].szSprite)+".spr";
                target->handle=engine.pfnSPR_Load(sprite.c_str());target->rect=list[i].rc;}
        }
    }
    for(const int id:selectionIds)
    {
        auto& image=sprites[id];if(image.normal.handle && image.selected.handle)continue;
        std::string path="sprites/"+definitions[id].name+".txt";int count=0;
        const auto* list=engine.pfnSPR_GetList(path.data(),&count);
        // CSO weapon lists can contain only 640 sprites; retain that fallback.
        for(int pass=0;pass<2;++pass)for(int i=0;list && i<count;++i)
        {
            if(list[i].iRes!=(pass?resolution:640) || !std::memchr(list[i].szName,0,sizeof(list[i].szName)) ||
                !std::memchr(list[i].szSprite,0,sizeof(list[i].szSprite)))continue;
            SelectionIcon* icon=!std::strcmp(list[i].szName,"weapon")?&image.normal:
                !std::strcmp(list[i].szName,"weapon_s")?&image.selected:nullptr;
            if(!icon)continue;
            std::string sprite=list[i].szSprite;
            if(sprite.empty() || sprite.find("..")!=std::string::npos || sprite.front()=='/' ||
                sprite.find_first_of(":\\")!=std::string::npos)continue;
            sprite="sprites/"+sprite+".spr";
            icon->handle=engine.pfnSPR_Load(sprite.c_str());icon->rect=list[i].rc;
        }
    }
}
void Cycle(int slot,int direction,xcommand_t original)
{
    if(textMenu && engine.GetClientTime()>menuUntil)textMenu=false;
    if(textMenu)
    {
        // CHudMenu closes locally on a valid number key. AMXX need not send
        // another ShowMenu, especially for its usual unlimited (-1) timeout.
        // Clear before forwarding: a submenu may arrive from the server later.
        if(slot>=0 && slot<10 && (menuSlots&(1u<<slot)))textMenu=false;
        if(original)original();
        return;
    }
    if(slot>=5 || !ManageSelection()){if(original)original();return;}
    auto ids=Choices(slot);if(ids.empty())return;
    if(engine.GetClientTime()>selectionUntil)selected=0;
    // Number keys start at the first position; wheel cycling starts at the
    // current weapon. Repeated presses advance the pending selection.
    const int from=selected?selected:slot<0?current.id:0;
    auto found=std::find(ids.begin(),ids.end(),from);
    int index=found==ids.end()?(direction>0?-1:0):int(found-ids.begin());
    if(!selected)ClearNativeWeaponSelection();
    selected=ids[(index+direction+int(ids.size()))%ids.size()];
    if(engine.pfnGetCvarFloat("hud_fastswitch")!=0 && slot>=0 && ids.size()==1)Select(selected);
    else {selectionUntil=engine.GetClientTime()+5;PrepareSelection();}
}
template<int Slot>void SlotCommand(){Cycle(Slot,1,originalSlots[Slot]);}
void Next(){Cycle(-1,1,originalNext);}void Previous(){Cycle(-1,-1,originalPrevious);}
void Cancel()
{
    const bool weaponSelection=selected!=0;
    selected=0;
    if(textMenu)
    {
        // Native cancelselect only opens Escape; it does not clear CHudMenu.
        // Close both the native menu and our mirror before forwarding. A new
        // menu delivered by the callback can then acquire its own number keys.
        textMenu=false;menuSlots=0;menuUntil=0;
        if(oldShowMenu){unsigned char clear[5]{};oldShowMenu("ShowMenu",sizeof(clear),clear);}
        if(originalCancel)originalCancel();
    }
    else if(!weaponSelection && originalCancel)originalCancel();
}
int Definition(const char*,int size,void* data)
{
    SynchronizeSession();WeaponDefinition def;
    if(!DecodeWeaponDefinition(Bytes(size,data),def) || !ExtendedWeaponId(def.id))return 1;
    if(definitions[def.id].id && definitions[def.id].name!=def.name)return 1;
    definitions[def.id]=def;available=true;
    const auto [command,first]=commandNames.insert(def.name);
    if(first)engine.pfnAddCommand(command->c_str(),NamedWeapon);
    ReceiveExtendedWeaponDefinition(def);return 1;
}
int Inventory(const char*,int size,void* data)
{
    SynchronizeSession();if(available && inventory.Receive(Bytes(size,data)))
    {
        if(selected && !inventory.Owned()[selected])selected=0;
        else if(selected)PrepareSelection();
    }
    return 1;
}
int Current(const char*,int size,void* data)
{
    SynchronizeSession();WeaponCurrent value;
    if(!available || !DecodeWeaponCurrent(Bytes(size,data),value) || (value.id && !definitions[value.id].id))return 1;
    if(current.id!=value.id)selected=0;
    if(value.id){clips[value.id]=value.clip;const auto& def=definitions[value.id];
        if(def.ammo1>=0)ammo[def.ammo1]=value.reserve;if(def.ammo2>=0)ammo[def.ammo2]=value.secondary;}
    current=value;ReceiveExtendedWeaponState(value);return 1;
}
int Pickup(const char*,int size,void* data)
{
    SynchronizeSession();
    if(!data || size!=3)return 1;
    const auto* bytes=static_cast<const unsigned char*>(data);
    const int id=bytes[1]|(bytes[2]<<8);
    if(bytes[0]==WeaponProtocolVersion && ExtendedWeaponId(id) && FindWeaponDefinition(id))
        AddWeaponPickup(id);
    return 1;
}
int ShowMenu(const char* name,int size,void* data)
{
    if(data && size>=4)
    {
        const auto* b=static_cast<const unsigned char*>(data);
        menuSlots=b[0]|(b[1]<<8);
        textMenu=menuSlots!=0;
        menuUntil=engine.GetClientTime()+(b[2]==255?3600:b[2]);selected=0;
    }
    return oldShowMenu?oldShowMenu(name,size,data):1;
}
int Guard(const char*,int size,void* data)
{
    SynchronizeSession();
    if(!data || size!=3)return 1;
    const auto* b=static_cast<const unsigned char*>(data);
    const int milliseconds=b[1]|(b[2]<<8);
    if(b[0]!=1 || milliseconds>1000)return 1;
    guardUntil=milliseconds?engine.GetClientTime()+milliseconds/1000.0:0;
    return 1;
}
}
void InitializeWeaponInventory()
{
    engine.pfnRegisterVariable("csol_protocol","1",FCVAR_USERINFO);
    engine.Cvar_SetValue("csol_protocol",WeaponProtocolVersion);
    engine.pfnHookUserMsg(WeaponDefinitionMessage,Definition);
    engine.pfnHookUserMsg(WeaponInventoryMessage,Inventory);
    engine.pfnHookUserMsg(WeaponCurrentMessage,Current);
    engine.pfnHookUserMsg(WeaponPickupMessage,Pickup);
    engine.pfnHookUserMsg("CSOGuard",Guard);
    originalSlots[0]=api->HookCmd("slot1",SlotCommand<0>);originalSlots[1]=api->HookCmd("slot2",SlotCommand<1>);
    originalSlots[2]=api->HookCmd("slot3",SlotCommand<2>);originalSlots[3]=api->HookCmd("slot4",SlotCommand<3>);
    originalSlots[4]=api->HookCmd("slot5",SlotCommand<4>);
    originalSlots[5]=api->HookCmd("slot6",SlotCommand<5>);originalSlots[6]=api->HookCmd("slot7",SlotCommand<6>);
    originalSlots[7]=api->HookCmd("slot8",SlotCommand<7>);originalSlots[8]=api->HookCmd("slot9",SlotCommand<8>);
    originalSlots[9]=api->HookCmd("slot10",SlotCommand<9>);
    originalNext=api->HookCmd("invnext",Next);originalPrevious=api->HookCmd("invprev",Previous);
    originalCancel=api->HookCmd("cancelselect",Cancel);
    oldShowMenu=api->HookUserMsg("ShowMenu",ShowMenu);
}
void ResetWeaponInventory(bool forgetDefinitions)
{
    inventory.Reset();current={};selected=0;textMenu=false;menuSlots=0;
    guardUntil=0;
    clips.fill(-1);ammo={};selectionHidden=selectionIntermission=0;
    selectionIds.clear();
    if(forgetDefinitions){definitions={};sprites={};available=false;}
}
void ObserveLegacyWeaponDefinition(int size,void* data)
{
    if(!data || size<=0)return;
    const auto* begin=static_cast<const unsigned char*>(data);
    const auto* end=static_cast<const unsigned char*>(std::memchr(data,0,size));
    if(!end || end-begin+9!=size || !ValidWeaponId(end[7]) || end[7]>=LegacyWeaponLimit || end[5]>=8)return;
    WeaponDefinition def;def.id=end[7];def.slot=end[5];def.position=end[6];def.flags=end[8];
    def.ammo1=end[1]==255?-1:end[1];def.ammo2=end[3]==255?-1:end[3];
    def.maxAmmo1=end[2]==255?-1:end[2];def.maxAmmo2=end[4]==255?-1:end[4];
    def.name.assign(reinterpret_cast<const char*>(begin),end-begin);
    // Validate even ordinary definitions before using their names as commands.
    if(!EncodeWeaponDefinition(def).empty())definitions[def.id]=std::move(def);
}
void WeaponInventoryCreateMove(float frame,usercmd_t* cmd,int active)
{
    if(downstream.CL_CreateMove)downstream.CL_CreateMove(frame,cmd,active);
    if(!cmd || !active)return;
    if(selected && (engine.GetClientTime()>selectionUntil || !inventory.Owned()[selected] || !ManageSelection()))selected=0;
    if(selected && (cmd->buttons&IN_ATTACK)){Select(selected);cmd->buttons&=~IN_ATTACK;}
    // Extended IDs are selected by their validated classname command. The
    // unchanged engine usercmd ABI only has an eight-bit weaponselect field.
}
void DrawWeaponSelection(int hidden,int intermission)
{
    WeaponSelectionVisibility(hidden,intermission);
    if(hidden&5 || intermission || view.health<=0 || view.spectator){selected=0;return;}
    if(!selected || engine.pfnGetCvarFloat("hud_draw")==0)return;
    if(engine.GetClientTime()>selectionUntil){selected=0;return;}
    const int slot=definitions[selected].slot;
    const int bucketWidth=buckets[0].handle?buckets[0].rect.right-buckets[0].rect.left:20;
    const int bucketHeight=buckets[0].handle?buckets[0].rect.bottom-buckets[0].rect.top:20;
    int selectedWidth=bucketWidth;
    for(int id:selectionIds)if(definitions[id].slot==slot)
        selectedWidth=std::max(selectedWidth,sprites[id].selected.rect.right-sprites[id].selected.rect.left);
    // GoldSrc's bucket menu: numbered slots, expanded active column,
    // compact inventory cells in the other columns and a selection overlay.
    int x=selectionScreen.iWidth<640?70:138;
    for(int column=0;column<5;++column)
    {
        const auto& bucket=buckets[column];
        if(bucket.handle){engine.pfnSPR_Set(bucket.handle,255,160,0);engine.pfnSPR_DrawAdditive(0,x,10,&bucket.rect);}
        else {char number[]{char('1'+column),0};engine.pfnDrawString(x,10,number,255,160,0);}
        int y=10+bucketHeight;
        for(const int id:selectionIds)
        {
            if(!inventory.Owned()[id] || definitions[id].slot!=column)continue;
            const auto& def=definitions[id];
            const bool hasAmmo=WeaponHasAmmo(id);
            if(column!=slot){engine.pfnFillRGBA(x,y,bucketWidth,bucketHeight,255,hasAmmo?160:16,hasAmmo?0:16,hasAmmo?128:96);y+=bucketHeight+5;continue;}
            const auto& icon=id==selected?sprites[id].selected:sprites[id].normal;
            const int height=std::max(bucketHeight,sprites[id].selected.rect.bottom-sprites[id].selected.rect.top);
            if(y+height>selectionScreen.iHeight-60)break;
            const int red=hasAmmo?191:127,green=hasAmmo?120:8,blue=hasAmmo?0:8;
            if(icon.handle){engine.pfnSPR_Set(icon.handle,red,green,blue);
                engine.pfnSPR_DrawAdditive(0,x,y,&icon.rect);}
            else engine.pfnDrawString(x,y,definitions[id].name.c_str()+7,255,160,0);
            if(id==selected && selectionFrame.handle){engine.pfnSPR_Set(selectionFrame.handle,red,green,blue);
                engine.pfnSPR_DrawAdditive(0,x,y,&selectionFrame.rect);}
            if(def.ammo1>=0 && def.maxAmmo1>0 && ammo[def.ammo1]>0){
                const int width=selectionScreen.iWidth<640?10:20,h=selectionScreen.iWidth<640?2:4;
                int barX=x+width/2;
                auto bar=[&](int count,int maximum){
                    const int filled=count>0?std::clamp(int(width*std::clamp(float(count)/maximum,0.f,1.f)),1,width):0;
                    if(filled)engine.pfnFillRGBA(barX,y+3,filled,h,0,160,0,255);
                    engine.pfnFillRGBA(barX+filled,y+3,width-filled,h,255,160,0,128);
                    barX+=width+5;};
                bar(ammo[def.ammo1],def.maxAmmo1);
                if(def.ammo2>=0 && def.maxAmmo2>0)bar(ammo[def.ammo2],def.maxAmmo2);
            }
            y+=height+5;
        }
        x+=(column==slot?selectedWidth:bucketWidth)+5;
    }
    ++selectionDraws;
}
void WeaponSelectionVisibility(int hidden,int intermission)
{
    selectionHidden=hidden;selectionIntermission=intermission;
    if((hidden&5) || intermission)selected=0;
}
void ObserveSelectionAmmo(int type,int count){if(type>=0 && type<int(ammo.size()))ammo[type]=std::max(0,count);}
void ObserveSelectionClip(int id,int count){if(ValidWeaponId(id))clips[id]=count;}
void WeaponInventoryVideoInit(){sprites={};buckets={};selectionFrame={};selectionIds.clear();selected=0;guardSprite=0;}
void DrawWeaponGuard(int hidden,int intermission)
{
    if(engine.GetClientTime()>=guardUntil || (hidden&5) || intermission || view.health<=0 ||
        view.spectator || engine.pfnGetCvarFloat("hud_draw")==0)return;
    if(!guardSprite)guardSprite=engine.pfnSPR_Load("sprites/csol/640hud37.spr");
    if(!guardSprite)return;
    SCREENINFO screen{};screen.iSize=sizeof(screen);engine.pfnGetScreenInfo(&screen);
    // Original zbt_invincibility atlas rectangle and common zombie HUD tint.
    // This CS 1.6 adapter uses the server's weapon-guard lifetime, not ZBT items.
    const wrect_t rect{66,130,1,65};
    engine.pfnSPR_Set(guardSprite,70,178,230);
    engine.pfnSPR_DrawAdditive(0,screen.iWidth/2-32,screen.iHeight-115,&rect);
    ++guardDraws;
}
void WeaponSelectionDiagnostics(std::ostream& out)
{
    out<<",\"selection\":{\"id\":"<<selected<<",\"items\":"<<selectionIds.size()<<",\"draws\":"<<selectionDraws
       <<",\"bucket\":"<<buckets[0].handle<<",\"overlay\":"<<selectionFrame.handle
       <<",\"available\":"<<available<<",\"menu\":"<<textMenu<<",\"menuSlots\":"<<menuSlots<<",\"owned\":[";
    bool first=true;
    for(int id=1;id<WeaponIdLimit;++id)if(inventory.Owned()[id]){
        if(!first)out<<',';first=false;
        out<<"{\"id\":"<<id<<",\"defined\":"<<bool(definitions[id].id)<<",\"slot\":"<<definitions[id].slot<<'}';
    }
    out<<"],\"guardUntil\":"<<guardUntil<<",\"guardDraws\":"<<guardDraws<<'}';
}
const WeaponDefinition* FindWeaponDefinition(int id){return ValidWeaponId(id)&&definitions[id].id?&definitions[id]:nullptr;}
bool WeaponHasAmmo(int id)
{
    const auto* def=FindWeaponDefinition(id);
    return def && (def->ammo1<0 || def->maxAmmo1<0 || clips[id]>0 ||
        (def->ammo1>=0 && ammo[def->ammo1]>0) || (def->ammo2>=0 && ammo[def->ammo2]>0) || (def->flags&1));
}
const WeaponCurrent& CurrentExtendedWeapon(){return current;}
bool ExtendedWeaponActive(){return available&&ExtendedWeaponId(current.id);}
}
