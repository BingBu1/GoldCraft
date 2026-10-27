// SPDX-License-Identifier: GPL-3.0-or-later
#include "precompiled.h"
void CSOL_ResetWeaponService() {}
void CSOL_UpdateBeamGuardHUD(CBasePlayer*) {}
#include "weapon_slots.hpp"
#include "server/weapon_slots.h"
#include <cstdlib>
#include <iostream>
#include <map>
ItemInfo CBasePlayerItem::m_ItemInfoArray[32]{};
CBasePlayer* observedPlayer{};
std::map<std::string,int> ids;std::vector<std::pair<int,std::vector<std::byte>>> messages;
int checks{},errors{},dest{};
void Check(bool b){++checks;if(!b){std::cerr<<"FAIL server "<<checks<<'\n';std::exit(1);}}
void ALERT(int,const char*,...){++errors;}
int REG_USER_MSG(const char* s,int){const int id=int(ids.size())+64;return ids.emplace(s,id).first->second;}
void MESSAGE_BEGIN(int d,int id,const void*,entvars_t* player){Check(player!=nullptr);dest=d;messages.emplace_back(id,std::vector<std::byte>{});}
void MESSAGE_BEGIN(int d,int id){Check(d==MSG_INIT);dest=d;messages.emplace_back(id,std::vector<std::byte>{});}
void WRITE_BYTE(int b){messages.back().second.push_back(std::byte(b&255));}
void WRITE_SHORT(int b){WRITE_BYTE(b);WRITE_BYTE(b>>8);}
void MESSAGE_END(){}
int main()
{
    using namespace csol;
    CSOL_ResetWeaponRegistry();Check(ids.size()==5);
    ItemInfo info{};info.iId=725;info.iSlot=0;info.pszName="weapon_beamgun";info.pszAmmo1="Beam";
    info.pszAmmo2="Energy";info.iMaxClip=80;info.iMaxAmmo1=200;info.iMaxAmmo2=100;
    Check(CSOL_RegisterItemInfo(info));Check(CSOL_ItemInfo(725)->iMaxClip==80);Check(CSOL_ItemInfo(1024)==nullptr);
    Check(CSOL_WeaponSlot(725) && CSOL_WeaponSlot(725)->slot==PRIMARY_WEAPON_SLOT);
    Check(CSOL_WeaponSlot("WEAPON_BEAMGUN")==CSOL_WeaponSlot(725));
    Check(!CSOL_WeaponSlot("weapon_unknown") && !CSOL_WeaponSlot(-1) && !CSOL_WeaponSlot(1024));
    Check(CSOL_ItemInfo(-1)==nullptr);Check(CSOL_ItemInfo(1)==&CBasePlayerItem::m_ItemInfoArray[1]);
    info.pszName="weapon_wrong";Check(!CSOL_RegisterItemInfo(info));Check(errors==1);
    info.iId=726;info.pszName="weapon_beamgunle";Check(CSOL_RegisterItemInfo(info));
    info.iId=31;Check(!CSOL_RegisterItemInfo(info));Check(errors==2);
    info.iId=99;Check(!CSOL_RegisterItemInfo(info));Check(errors==3);
    Check(CSOL_ExtendedWeaponHudSlot(725)==0&&CSOL_ExtendedWeaponHudSlot(726)==0);
    Check(CSOL_ExtendedWeaponHudSlot(99)==-1&&CSOL_ExtendedWeaponHudSlot(220)==-1);
    Check(CSOL_MaxAmmoCarry("Beam")==200&&CSOL_MaxAmmoCarry("Energy")==100);
    CSOL_WriteWeaponDefinitions();Check(messages.size()==2&&dest==MSG_INIT);
    WeaponDefinition definition;Check(DecodeWeaponDefinition(messages[0].second,definition)&&definition.id==725);
    Check(DecodeWeaponDefinition(messages[1].second,definition)&&definition.id==726);
    CBasePlayer p;CBasePlayerWeapon a,b,ordinary;a.m_iId=725;b.m_iId=726;ordinary.m_iId=28;
    a.m_pNext=&b;b.m_pNext=&ordinary;p.m_rgpPlayerItems[0]=&a;p.m_pActiveItem=&a;p.m_rgAmmo[15]=400;p.m_rgAmmo[16]=100;
    Check(CSOL_HasAnyWeapon(&p));Check(CSOL_HasWeapon(&p,725)&&CSOL_HasWeapon(&p,726)&&CSOL_HasWeapon(&p,28));
    Check(!CSOL_HasWeapon(&p,31)&&!CSOL_HasWeapon(&p,-1));Check(CSOL_CanUseExtendedWeapons(&p));
    messages.clear();CSOL_UpdateWeaponInventory(&p);Check(messages.size()==2);
    WeaponInventoryReceiver client;Check(client.Receive(messages[0].second));
    Check(client.Owned()[725]&&client.Owned()[726]&&client.Owned()[28]);
    WeaponCurrent current;Check(DecodeWeaponCurrent(messages[1].second,current)&&current.id==725&&current.reserve==400);
    p.m_fWeapon=true;messages.clear();CSOL_UpdateWeaponInventory(&p);Check(messages.empty());
    p.m_pActiveItem=&b;CSOL_UpdateWeaponInventory(&p);Check(messages.size()==1);
    Check(DecodeWeaponCurrent(messages[0].second,current)&&current.id==726);
    messages.clear();p.m_rgpPlayerItems[0]=&b;CSOL_UpdateWeaponInventory(&p);Check(messages.size()==1);
    Check(client.Receive(messages[0].second)&&!client.Owned()[725]&&client.Owned()[726]);
    messages.clear();p.alive=false;CSOL_UpdateWeaponInventory(&p);Check(messages.size()==1);
    Check(DecodeWeaponCurrent(messages[0].second,current)&&current.id==0);
    p.m_rgpPlayerItems[0]=nullptr;p.m_pActiveItem=nullptr;messages.clear();CSOL_UpdateWeaponInventory(&p);
    Check(messages.size()==1&&client.Receive(messages[0].second)&&client.Owned()==WeaponOwnership{});
    Check(!CSOL_HasAnyWeapon(&p));
    p.alive=true;p.m_rgpPlayerItems[0]=&a;p.m_pActiveItem=&a;p.entity.userid=2;messages.clear();CSOL_UpdateWeaponInventory(&p);
    Check(messages.size()==2); // same edict index, different connection.
    p.m_fWeapon=false;messages.clear();CSOL_UpdateWeaponInventory(&p);Check(messages.size()==2);
    CBasePlayer observer;observer.index=2;observer.alive=false;observer.vars.iuser1=OBS_IN_EYE;
    observer.m_hObserverTarget=&p;observedPlayer=&p;messages.clear();CSOL_UpdateWeaponInventory(&observer);
    Check(messages.size()==2&&DecodeWeaponCurrent(messages.back().second,current)&&current.id==725&&current.reserve==400);
    Check(client.Receive(messages.front().second)&&client.Owned()==WeaponOwnership{});
    observer.vars.iuser1=0;observer.m_fWeapon=true;messages.clear();CSOL_UpdateWeaponInventory(&observer);
    Check(messages.size()==1&&DecodeWeaponCurrent(messages[0].second,current)&&current.id==0);
    p.entity.version="";messages.clear();CSOL_UpdateWeaponInventory(&p);Check(messages.empty()&&!CSOL_CanUseExtendedWeapons(&p));
    p.entity.version="1";p.vars.flags=FL_FAKECLIENT;Check(!CSOL_CanUseExtendedWeapons(&p));p.vars.flags=0;
    Check(!CSOL_SendWeaponPickup(&p,28));Check(CSOL_SendWeaponPickup(&p,725));Check(messages.back().second.size()==3);
    int price=0,count=0;Check(CSOL_AmmoPurchase(220,price,count)&&price==30&&count==1);Check(!CSOL_AmmoPurchase(28,price,count));
    Check(CSOL_AmmoPurchase(725,price,count)&&price==100&&count==20);
    Check(CSOL_AmmoPurchase(726,price,count)&&price==100&&count==20);
    messages.clear();Check(CSOL_SendAmmoPickup(&p,"Beam",30));AmmoPickup pickup;
    Check(messages.size()==1 && DecodeAmmoPickup(messages[0].second,pickup) && pickup.id==725 && pickup.secondary==0 && pickup.amount==30);
    Check(CSOL_SendAmmoPickup(&p,"Energy",256));Check(DecodeAmmoPickup(messages.back().second,pickup)&&pickup.secondary==1&&pickup.amount==256);
    Check(!CSOL_SendAmmoPickup(&p,"unknown",1));
    CBasePlayerItem::m_ItemInfoArray[1].pszAmmo1="Beam";Check(!CSOL_SendAmmoPickup(&p,"Beam",10));CBasePlayerItem::m_ItemInfoArray[1]={};
    p.entity.version="";Check(!CSOL_SendAmmoPickup(&p,"Beam",10));p.entity.version="1";
    Check(CSOL_LegacyWeaponMask(725)==0&&CSOL_LegacyWeaponMask(726)==0);
    Check(CSOL_PredictionSlot(725,true)==0&&CSOL_PredictionSlot(726,false)==-1&&CSOL_PredictionSlot(28,false)==28);
    CSOL_ResetWeaponRegistry();Check(CSOL_ItemInfo(725)->pszName==nullptr);messages.clear();CSOL_UpdateWeaponInventory(&p);Check(messages.empty());
    std::cout<<"PASS server "<<checks<<" checks\n";
}
