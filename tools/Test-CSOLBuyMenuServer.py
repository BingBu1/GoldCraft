"""Compile the unchanged shop controller against small SDK boundary stubs."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parent.parent
PRELUDE=r'''
#include "BUY_PROTOCOL_HEADER"
#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
using std::max;
constexpr int FL_KILLME=1,FL_DORMANT=2,FL_FAKECLIENT=4,CT=2,TRUE=1,Menu_OFF=0;
constexpr int HELMET_PRICE=350,KEVLAR_PRICE=650,ASSAULTSUIT_PRICE=1000,ARMOR_VESTHELM=2;
constexpr int MSG_ONE=1,MSG_INIT=3,at_error=1,PRIMARY_WEAPON_SLOT=1,PISTOL_SLOT=2;
constexpr int MAX_AMMO_SLOTS=32;
enum WeaponIdType {WeaponNone};
enum {MENU_SLOT_ITEM_VEST=1,MENU_SLOT_ITEM_VESTHELM,MENU_SLOT_ITEM_FLASHGREN,MENU_SLOT_ITEM_HEGREN,MENU_SLOT_ITEM_SMOKEGREN};
enum {RT_NONE,RT_PLAYER_BOUGHT_SOMETHING};
struct CBasePlayer;
struct entvars_t {int flags{};float armorvalue{};};
struct edict_t {bool free{};int userid=3,serial=1;CBasePlayer* object{};};
using edict_s=edict_t;
std::uint64_t generation=1;
bool scenario=true,canBuy=true,alive=true,canDeliver=true,delivery=true;
int deliveries{},charges{},ammoCalls{},itemCalls{},weaponCalls{},checks{};
std::function<void()> accountCallback,deliveryCallback;
std::function<void()> ammoCallback;
int ammoLimit=90,ammoPack=30,ammoCost=20,ammoSlot{},ammoPurchaseCount{};
bool ammoNoProgress{},clipAmmo{};
std::vector<int> debits,trackFlags;
std::vector<std::string> args;
std::vector<std::vector<std::uint8_t>> packets;
struct CBaseEntity {virtual ~CBaseEntity()=default;static CBaseEntity* Instance(edict_t*);};
struct CBasePlayerItem {virtual ~CBasePlayerItem()=default;virtual CBasePlayerItem* GetWeaponPtr(){return nullptr;}};
struct CBasePlayerWeapon:CBasePlayerItem {int m_iClip{};CBasePlayerItem* GetWeaponPtr()override{return this;}};
struct CBasePlayer: CBaseEntity {
    entvars_t vars;entvars_t* pev=&vars;edict_t entity;bool has_disconnected{};int m_iTeam=CT,m_iAccount=5000,m_iMenu=1,m_iKevlar{};
    int m_rgAmmo[MAX_AMMO_SLOTS]{};CBasePlayerItem* m_rgpPlayerItems[6]{};
    CBasePlayer(){entity.object=this;}int entindex(){return 1;}bool IsAlive(){return alive;}bool CanPlayerBuy(bool){return canBuy;}
    void AddAccount(int amount,int,bool track){++charges;debits.push_back(amount);trackFlags.push_back(track);m_iAccount=std::clamp(m_iAccount+amount,0,16000);if(accountCallback){auto f=std::move(accountCallback);f();}}
};
CBaseEntity* CBaseEntity::Instance(edict_t* e){return e->object;}
struct EHANDLE {CBasePlayer* p;int serial;EHANDLE(CBasePlayer* q):p(q),serial(q->entity.serial){}template<class T>T* Get()const{return p->entity.serial==serial?dynamic_cast<T*>(p):nullptr;}};
struct CSOLScenarioSdkScope {std::uint64_t saved=generation;bool Current(){return saved==generation;}bool Scenario(){return scenario;}};
struct Info {int cost=2500;const char* entityName="weapon";}info;
struct Rules {bool m_bMapHasVIPSafetyZone{};}rules;
Rules* CSGameRules(){return &rules;}
Info* GetWeaponInfo(int){return &info;}
bool CSOL_UsesScenarioRules(){return scenario;}
bool CSOL_ScenarioCanBuy(){return scenario;}
bool CSOL_CanUseExtendedWeapons(CBasePlayer*){return true;}
bool CSOL_ExtendedWeapon(int id){return id>=32&&id<1000;}
bool CSOL_CanDeliverScenarioWeapon(CBasePlayer*,int){return canDeliver;}
bool CanBuyWeaponByMaptype(int,WeaponIdType,bool){return true;}
bool CSOL_DeliverScenarioWeapon(CBasePlayer*,int){++deliveries;if(deliveryCallback){auto f=std::move(deliveryCallback);f();}return delivery;}
bool BuyAmmo(CBasePlayer* p,int slot,bool){
    ++ammoCalls;ammoSlot=slot;
    if(ammoNoProgress)return true;
    auto* gun=static_cast<CBasePlayerWeapon*>(p->m_rgpPlayerItems[slot]);
    int& count=clipAmmo?gun->m_iClip:p->m_rgAmmo[slot];
    if(count>=ammoLimit||p->m_iAccount<ammoCost)return false;
    count=std::min(ammoLimit,count+ammoPack);p->AddAccount(-ammoCost,0,true);++ammoPurchaseCount;
    if(ammoCallback){auto f=std::move(ammoCallback);f();}
    return true;
}
void BuyItem(CBasePlayer* p,int slot){++itemCalls;p->AddAccount(-(slot==2?350:650),0,true);}
CBaseEntity* BuyWeaponByWeaponID(CBasePlayer* p,WeaponIdType){++weaponCalls;p->AddAccount(-2500,0,true);return p;}
int CMD_ARGC(){return int(args.size());}const char* CMD_ARGV(int i){return args[i].c_str();}
int GETPLAYERUSERID(edict_t* e){return e->userid;}
int Q_strcmp(const char* a,const char* b){return std::strcmp(a,b);}
void Begin(int,int,const float* = nullptr,entvars_t* = nullptr){packets.emplace_back();}
void Byte(int b){packets.back().push_back(std::uint8_t(b));}void End(){}
unsigned char* Load(const char*,int*){return nullptr;}void Free(void*){}void Alert(int,const char*,...){}int Precache(char*){return 1;}int Register(const char*,int){return 1;}
#define MESSAGE_BEGIN Begin
#define WRITE_BYTE Byte
#define MESSAGE_END End
#define LOAD_FILE_FOR_ME Load
#define FREE_FILE Free
#define ALERT Alert
#define PRECACHE_GENERIC Precache
#define REG_USER_MSG Register
'''
TESTS=r'''
void Check(bool v){++checks;if(!v)throw std::runtime_error("shop controller check failed");}
void Reset(CBasePlayer& p){CSOL_ResetBuyMenu();ready=true;message=1;scenario=true;canBuy=true;alive=true;canDeliver=true;delivery=true;deliveries=charges=ammoCalls=itemCalls=weaponCalls=0;debits.clear();trackFlags.clear();packets.clear();accountCallback={};deliveryCallback={};ammoCallback={};ammoLimit=90;ammoPack=30;ammoCost=20;ammoPurchaseCount=0;ammoNoProgress=clipAmmo=false;std::fill_n(p.m_rgAmmo,MAX_AMMO_SLOTS,0);std::fill_n(p.m_rgpPlayerItems,6,nullptr);p.m_iAccount=5000;p.m_iTeam=CT;p.has_disconnected=false;p.vars={};}
void Command(CBasePlayer& p,std::initializer_list<const char*> words){args.clear();for(auto w:words)args.emplace_back(w);Check(CSOL_BuyMenuCommand(&p.entity,args[0].c_str()));}
csol::buy::Packet Last(){csol::buy::Packet p;Check(!packets.empty()&&csol::buy::Decode(packets.back(),p));return p;}
int main(){try{
    using namespace csol::buy;CBasePlayer p;
    Reset(p);Command(p,{"csol_buy_menu","1"});Check(Last().kind==Kind::End&&p.m_iMenu==0);
    deliveryCallback=[&]{auto saved=args;Command(p,{"csol_buy","1","144"});args=saved;};
    Command(p,{"csol_buy","1","144"});Check(p.m_iAccount==900&&deliveries==1&&charges==1&&Last().result==Result::Success);
    Command(p,{"csol_buy","1","144"});Check(p.m_iAccount==900&&deliveries==1&&charges==1);
    Reset(p);p.m_iAccount=4000;Command(p,{"csol_buy_menu","2"});Command(p,{"csol_buy","2","144"});Check(charges==0&&deliveries==0&&Last().result==Result::Insufficient);
    Reset(p);delivery=false;Command(p,{"csol_buy_menu","3"});Command(p,{"csol_buy","3","144"});Check(p.m_iAccount==5000&&deliveries==1&&debits==std::vector<int>({-4100,4100})&&trackFlags==std::vector<int>({1,0}));
    Reset(p);Command(p,{"csol_buy_menu","4"});accountCallback=[&]{++generation;};Command(p,{"csol_buy","4","144"});Check(deliveries==0&&charges==1);
    Reset(p);Command(p,{"csol_buy_menu","5"});accountCallback=[&]{++p.entity.serial;p.m_iAccount=12000;};Command(p,{"csol_buy","5","144"});Check(deliveries==0&&p.m_iAccount==12000&&charges==1);
    Reset(p);Command(p,{"csol_buy_menu","6"});Command(p,{"csol_buy","7","144"});Check(charges==0&&deliveries==0);
    Reset(p);canBuy=false;Command(p,{"csol_buy_menu","8"});Check(Last().result==Result::Unavailable&&charges==0);
    Reset(p);p.m_iTeam=1;Command(p,{"csol_buy_menu","9"});Check(Last().result==Result::Unavailable);
    Reset(p);scenario=false;Command(p,{"csol_buy_menu","10"});Check(packets.empty()&&charges==0);
    Reset(p);Command(p,{"csol_buy_menu","11"});Command(p,{"csol_buy","11","65535"});Check(charges==0&&deliveries==0);
    Reset(p);Command(p,{"csol_buy_menu","12"});Command(p,{"csol_buy","12","2001"});Check(p.m_rgAmmo[1]==90&&p.m_iAccount==4940&&ammoPurchaseCount==3&&ammoSlot==1&&Last().result==Result::Success);
    Command(p,{"csol_buy","12","2001"});Check(ammoPurchaseCount==3&&p.m_iAccount==4940);
    Reset(p);p.m_iAccount=45;Command(p,{"csol_buy_menu","21"});Command(p,{"csol_buy","21","2002"});Check(p.m_rgAmmo[2]==60&&p.m_iAccount==5&&ammoPurchaseCount==2&&ammoSlot==2&&Last().result==Result::Success);
    Reset(p);p.m_rgAmmo[1]=90;Command(p,{"csol_buy_menu","22"});Command(p,{"csol_buy","22","2001"});Check(charges==0&&ammoCalls==1&&Last().result==Result::Failed);
    Reset(p);p.m_iAccount=19;Command(p,{"csol_buy_menu","23"});Command(p,{"csol_buy","23","2001"});Check(p.m_rgAmmo[1]==0&&charges==0&&Last().result==Result::Failed);
    Reset(p);CBasePlayerWeapon cannon;clipAmmo=true;ammoPack=1;ammoLimit=50;ammoCost=100;cannon.m_iClip=47;p.m_rgpPlayerItems[1]=&cannon;Command(p,{"csol_buy_menu","24"});Command(p,{"csol_buy","24","2001"});Check(cannon.m_iClip==50&&p.m_iAccount==4700&&Last().result==Result::Success);
    Reset(p);ammoCost=0;Command(p,{"csol_buy_menu","25"});Command(p,{"csol_buy","25","2001"});Check(p.m_rgAmmo[1]==90&&p.m_iAccount==5000&&ammoPurchaseCount==3&&Last().result==Result::Success);
    Reset(p);ammoNoProgress=true;Command(p,{"csol_buy_menu","26"});Command(p,{"csol_buy","26","2001"});Check(ammoCalls==1&&charges==0&&Last().result==Result::Failed);
    Reset(p);Command(p,{"csol_buy_menu","27"});auto packetCount=packets.size();ammoCallback=[](){++generation;};Command(p,{"csol_buy","27","2001"});Check(ammoCalls==1&&packets.size()==packetCount);
    Reset(p);Command(p,{"csol_buy_menu","28"});packetCount=packets.size();ammoCallback=[&](){++p.entity.serial;};Command(p,{"csol_buy","28","2001"});Check(ammoCalls==1&&packets.size()==packetCount);
    Reset(p);Command(p,{"csol_buy_menu","29"});ammoCallback=[](){alive=false;};Command(p,{"csol_buy","29","2001"});Check(ammoCalls==1&&ammoPurchaseCount==1);
    Reset(p);p.m_iAccount=400;p.pev->armorvalue=100;Command(p,{"csol_buy_menu","13"});Command(p,{"csol_buy","13","1002"});Check(itemCalls==1&&p.m_iAccount==50&&Last().result==Result::Success);
    Reset(p);Command(p,{"csol_buy_menu","14"});Command(p,{"csol_buy","14","22"});Check(weaponCalls==1&&p.m_iAccount==2500&&Last().result==Result::Success);
    Reset(p);Command(p,{"csol_buy_menu","65536"});Check(packets.empty());Command(p,{"csol_buy_menu","-1"});Check(packets.empty());
    std::cout<<"buy menu server: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<" after "<<checks<<" checks\n";return 1;}}
'''

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--clang',required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();out=a.output.resolve()
    if not out.is_relative_to(ROOT/'build'):raise ValueError('Output must stay in workspace build')
    out.mkdir(parents=True,exist_ok=True);source=ROOT/'native/csol/server/buy_menu.cpp';body=source.read_text(encoding='utf8');body=re.sub(r'^#include[^\n]*\n','',body,flags=re.M)
    prelude=PRELUDE.replace('BUY_PROTOCOL_HEADER',(ROOT/'native/csol/buy_menu.hpp').as_posix())
    cpp=out/'server-fixture.cpp';cpp.write_text(prelude+body+TESTS,encoding='utf8')
    flags=['--target=i686-pc-windows-msvc','/nologo','/std:c++20','/EHsc','/MT','/utf-8','/O2','/clang:-O3','/clang:-flto=thin','/arch:AVX2','/fp:precise']
    commands=[[a.clang,*flags,'/c',str(cpp),'/Fo'+str(out/'server-fixture.obj')],
              [a.clang,'--target=i686-pc-windows-msvc','-fuse-ld=lld','/MT',str(out/'server-fixture.obj'),str(out/'shared.obj'),'/Fe'+str(out/'server-fixture.exe'),'/link','/OPT:REF','/OPT:LLDLTO=3','/threads:1']]
    for i,command in enumerate(commands):
        r=subprocess.run(command,capture_output=True,text=True);(out/f'server-fixture-build-{i}.log').write_text(r.stdout+r.stderr)
        if r.returncode:raise RuntimeError(f'Server fixture build {i} failed')
    r=subprocess.run([str(out/'server-fixture.exe')],capture_output=True,text=True);(out/'server-fixture-results.log').write_text(r.stdout+r.stderr);print(r.stdout+r.stderr)
    if r.returncode:raise RuntimeError('Server fixture checks failed')
    (out/'server-fixture.json').write_text(json.dumps(dict(sourceSha256=hashlib.sha256(source.read_bytes()).hexdigest(),sourceBodyUnchanged=True,sdkBoundariesStubbed=True,liveGameplay=False,commands=commands),indent=2))
if __name__=='__main__':main()
