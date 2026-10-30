// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
#include "buy_menu.h"
#include "game_rules_gate.h"
#include "weapon_service.h"
#include "weapon_slots.h"
#include "../buy_menu.hpp"
#include <array>
#include <limits>

namespace {
using namespace csol::buy;
struct Client {int userid{};std::uint16_t request{};bool purchased{},busy{};Packet last{};};
std::array<Client,33> clients;
int message{};
bool ready{};
std::vector<std::string> resources;
void Send(const Packet& p, CBasePlayer* player=nullptr)
{
    if(!message)return;
    if(player)MESSAGE_BEGIN(MSG_ONE,message,nullptr,player->pev);else MESSAGE_BEGIN(MSG_INIT,message);
    for(auto b:Encode(p))WRITE_BYTE(b);MESSAGE_END();
}
bool Eligible(CBasePlayer* player)
{
    return ready && player && !player->has_disconnected && !(player->pev->flags&(FL_KILLME|FL_DORMANT|FL_FAKECLIENT)) &&
        player->IsAlive() && player->m_iTeam==CT && CSOL_CanUseExtendedWeapons(player) && CSOL_ScenarioCanBuy();
}
int Price(CBasePlayer* player,const Item& item)
{
    // The stock item consumer discounts a helmet/vest already owned.
    if(item.id==1002)return player->pev->armorvalue>=100?HELMET_PRICE:
        player->m_iKevlar==ARMOR_VESTHELM?KEVLAR_PRICE:ASSAULTSUIT_PRICE;
    if(item.id>=1000 || CSOL_ExtendedWeapon(item.id))return item.extendedPrice;
    auto* info=GetWeaponInfo(item.id);return info?info->cost:0;
}
bool Available(CBasePlayer* player,const Item& item)
{
    if(item.id>=1000)return true;
    if(CSOL_ExtendedWeapon(item.id))return CSOL_CanDeliverScenarioWeapon(player,item.id);
    auto* info=GetWeaponInfo(item.id);
    return info&&info->entityName&&CanBuyWeaponByMaptype(player->m_iTeam,static_cast<WeaponIdType>(item.id),CSGameRules()->m_bMapHasVIPSafetyZone==TRUE);
}
bool Number(const char* text,std::uint16_t& n)
{
    if(!text||!*text)return false;unsigned value=0;
    for(;*text;++text){if(*text<'0'||*text>'9')return false;value=value*10+unsigned(*text-'0');if(value>65535)return false;}
    if(!value)return false;n=std::uint16_t(value);return true;
}
}
void CSOL_ResetBuyMenu(){ready=false;clients={};resources.clear();}
bool CSOL_CanBuyScenarioNativeWeapon(int playerTeam,int weaponId)
{
    // Scenario participants remain CT. The T/CT tabs classify equipment;
    // choosing a T weapon must not temporarily change authoritative team state.
    const auto* item=csol::buy::Find(weaponId);
    return CSOL_UsesScenarioRules()&&playerTeam==CT&&item&&item->id<1000&&!CSOL_ExtendedWeapon(item->id);
}
bool CSOL_PrecacheBuyMenu()
{
    CSOL_ResetBuyMenu();if(!CSOL_UsesScenarioRules())return false;
    int length{};auto* file=LOAD_FILE_FOR_ME("models/csol/buymenu.json",&length);
    if(!file)return false;
    try{resources=csol::buy::Resources(csol::buy::ParsePresentation({reinterpret_cast<const char*>(file),std::size_t(length)}));}
    catch(const std::exception& e){FREE_FILE(file);ALERT(at_error,"CSOL buy menu: %s\n",e.what());return false;}
    FREE_FILE(file);
    for(const auto& path:resources){int size{};auto* bytes=LOAD_FILE_FOR_ME(path.c_str(),&size);if(!bytes||size<=0){if(bytes)FREE_FILE(bytes);ALERT(at_error,"CSOL buy resource missing: %s\n",path.c_str());return false;}FREE_FILE(bytes);}
    for(const auto& path:resources)PRECACHE_GENERIC(const_cast<char*>(path.c_str()));
    if(!message)message=REG_USER_MSG(csol::buy::Message,static_cast<int>(csol::buy::MessageBytes));
    ready=message!=0;if(ready)Send({Kind::Capability});return ready;
}
bool CSOL_BuyMenuCommand(edict_t* entity,const char* command)
{
    if(!command || (Q_strcmp(command,"csol_buy_menu")&&Q_strcmp(command,"csol_buy")))return false;
    CSOLScenarioSdkScope scope;
    if(!scope.Current()||!scope.Scenario()||!entity||entity->free)return true;
    auto* player=dynamic_cast<CBasePlayer*>(CBaseEntity::Instance(entity));if(!player)return true;
    const int index=player->entindex();if(index<1||index>=int(clients.size()))return true;
    const bool catalog=!Q_strcmp(command,"csol_buy_menu");std::uint16_t request{},id{};
    if(CMD_ARGC()!=(catalog?2:3)||!Number(CMD_ARGV(1),request)||(!catalog&&!Number(CMD_ARGV(2),id)))return true;
    const int userid=GETPLAYERUSERID(entity);auto& state=clients[index];if(state.userid!=userid)state={userid};
    if(state.busy)return true;state.busy=true;
    struct Finish {Client& c;int user;~Finish(){if(c.userid==user)c.busy=false;}}finish{state,userid};
    const EHANDLE handle(player);
    const auto current=[&](){return scope.Current()&&state.userid==userid&&handle.Get<CBaseEntity>()==player&&!player->has_disconnected&&!(player->pev->flags&FL_KILLME);};
    if(!Eligible(player)||!player->CanPlayerBuy(false)||!current()){
        if(current())Send({Kind::Result,request,id,0,std::max(0,player->m_iAccount),Result::Unavailable,2},player);return true;
    }
    if(catalog){
        state.request=request;state.purchased=false;
        player->m_iMenu=Menu_OFF;
        Send({Kind::Begin,request,0,0,player->m_iAccount,Result::Available,2},player);
        for(const auto& item:Catalog()){
            if(!current())return true;
            const bool available=Available(player,item);if(!current())return true;
            Send({Kind::Item,request,item.id,Price(player,item),player->m_iAccount,available?Result::Available:Result::Unavailable,2},player);
        }
        if(current())Send({Kind::End,request,0,0,player->m_iAccount,Result::Available,2},player);return true;
    }
    if(state.request!=request)return true;
    if(state.purchased){Send(state.last,player);return true;}
    const auto* item=Find(id);if(!item)return true;
    const int price=Price(player,*item);bool success=false;Result result=Result::Failed;
    if(!Available(player,*item)||!current())result=Result::Unavailable;
    else if(item->id<2000&&player->m_iAccount<price)result=Result::Insufficient;
    else if(item->id>=2000){
        const int slot=item->id==2001?PRIMARY_WEAPON_SLOT:PISTOL_SLOT;
        const auto ammoState=[&](){
            std::array<int,MAX_AMMO_SLOTS+2> ammo{};
            std::copy_n(player->m_rgAmmo,MAX_AMMO_SLOTS,ammo.begin());
            // Clip-only CSOL ammunition and the shield's pistol redirection
            // must also count as progress when reserve ammunition is unchanged.
            for(int i=0;i<2;++i){
                auto* owned=player->m_rgpPlayerItems[i==0?PRIMARY_WEAPON_SLOT:PISTOL_SLOT];
                auto* gun=owned?static_cast<CBasePlayerWeapon*>(owned->GetWeaponPtr()):nullptr;
                ammo[MAX_AMMO_SLOTS+i]=gun?gun->m_iClip:-1;
            }
            return ammo;
        };
        bool first=true;
        while(current()&&Eligible(player)){
            const auto before=ammoState();
            const bool bought=BuyAmmo(player,slot,first);
            if(!current())return true;
            const bool gained=ammoState()!=before;
            success|=gained;
            // Follow primammo/secammo: repeat native purchases until full or
            // unaffordable. A hook returning success without progress must stop.
            if(!bought||!gained)break;
            first=false;
        }
    }
    else if(item->id>=1000){
        const int before=player->m_iAccount;
        constexpr int slots[]{MENU_SLOT_ITEM_VEST,MENU_SLOT_ITEM_VESTHELM,MENU_SLOT_ITEM_HEGREN,MENU_SLOT_ITEM_FLASHGREN,MENU_SLOT_ITEM_SMOKEGREN};
        BuyItem(player,slots[item->id-1001]);if(current())success=player->m_iAccount<before;
    }else if(!CSOL_ExtendedWeapon(item->id)){
        success=BuyWeaponByWeaponID(player,static_cast<WeaponIdType>(item->id))!=nullptr;
    }else{
        // Reserve funds before the hookable delivery so reentrant purchases
        // cannot spend the same balance. A failed delivery refunds this debit.
        const int before=player->m_iAccount;player->AddAccount(-price,RT_PLAYER_BOUGHT_SOMETHING,true);
        if(!current())return true;
        const int debit=before-player->m_iAccount;
        if(debit==price&&Eligible(player))success=CSOL_DeliverScenarioWeapon(player,item->id);
        if(!current())return true;
        if(!success&&debit>0&&player->m_iAccount<=std::numeric_limits<int>::max()-debit)player->AddAccount(debit,RT_NONE,false);
    }
    if(!current())return true;
    if(success)result=Result::Success;
    state.last={Kind::Result,request,id,price,player->m_iAccount,result,2};state.purchased=success;
    Send(state.last,player);return true;
}
