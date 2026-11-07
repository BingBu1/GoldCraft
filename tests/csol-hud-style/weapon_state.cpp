// SPDX-License-Identifier: GPL-3.0-or-later
// Actual wire codecs and byte-exact production observers/readers. Prediction
// simulation and rendering are separate tests; no fabricated WeaponState is
// used to stand in for the message-to-quantity path here.
#include "native/csol/client/runtime.hpp"
#include "native/csol/hud_style.hpp"
#include "native/csol/weapons/weapon_slots.cpp"
#include "native/csol/weapons/k3.hpp"
#include "native/csol/weapons/cannon.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <set>
#include <source_location>

namespace fixture {
unsigned checks{},failures{},forwarded{},synchronized{},commands{};
void Check(bool value,const char* text,std::source_location at=std::source_location::current())
{++checks;if(!value){++failures;std::cerr<<"FAIL "<<at.line()<<' '<<text<<'\n';}}
int Previous(const char*,int,void*){++forwarded;return 73;}
}
namespace k3_prediction {
using csol::WeaponCurrent;namespace k3=csol::k3;
int predictedClip=-1,predictedReserve=-1,presentedModel{},authoritativeAnimation=-1;
bool presented{},shot{},emptySoundReady=true;
#include "generated/k3_state.inc"
}
namespace cannon_prediction {
using csol::WeaponCurrent;namespace cannon=csol::cannon;
int predictedClip=-1,presentedModel{};bool presented{},shot{};
#include "generated/cannon_state.inc"
}
namespace wire {
using namespace csol;using fixture::Check;
using k3_prediction::ReceiveK3State;
using k3_prediction::K3PredictedAmmo;
using k3_prediction::K3PredictedReserve;
using cannon_prediction::ReceiveCannonState;
using cannon_prediction::CannonPredictedAmmo;
cl_enginefunc_t engine{};
constexpr int KnifeId=ThrowingKnifeId;
bool registered{},available{},currentReceived{},infiniteAmmo{};
int active{},clip{},reserve{},ammoType=-1,predictedClip=-1,predictedTotal=-1,hidden{},selected{},animationSyncWeapon{};
std::uint64_t animationSyncGeneration{};
std::array<WeaponDefinition,WeaponIdLimit> definitions;
std::array<int,WeaponIdLimit> clips;
std::array<int,32768> ammo;
std::set<std::string> commandNames;
WeaponCurrent current;
pfnUserMsgHook oldCurWeapon=fixture::Previous,oldAmmo=fixture::Previous,oldHide{};
float localPrediction{};
void SynchronizeSession(){++fixture::synchronized;}
void NamedWeapon(){Check(false,"state test must not issue a selection command");}
void ReceiveBeamgunState(const WeaponCurrent&){}
WeaponCurrent BeamgunHudState(){return {};}
bool CharacterSkillInfiniteAmmoFor(int){return infiniteAmmo;}
void ReceiveExtendedWeaponDefinition(const WeaponDefinition&);
void ReceiveExtendedWeaponState(const WeaponCurrent&);
const WeaponDefinition* FindWeaponDefinition(int);
bool ExtendedWeaponActive();
void ObserveSelectionAmmo(int,int);
void ObserveSelectionClip(int,int);
int WeaponReserveAmmo(int);
const WeaponCurrent& CurrentExtendedWeapon();
#include "generated/inventory_state.inc"
#include "generated/weapon_state.inc"

void Register(const char* name,int id,int ammoId,int maximum,int capacity,int slot=0)
{
    WeaponDefinition value;value.name=name;value.id=id;value.ammo1=ammoId;value.maxAmmo1=maximum;
    value.maxClip=capacity;value.slot=slot;
    auto packet=EncodeWeaponDefinition(value);Check(!packet.empty(),"test definition encoded by production codec");
    Check(Definition(WeaponDefinitionMessage,int(packet.size()),packet.data())==1&&definitions[id]==value,
        "actual extended definition observer commits complete validated weapon");
}
void Send(int id,int amount,int reserveCount,int state=1)
{
    const WeaponCurrent value{id,state,amount,reserveCount,0};auto packet=EncodeWeaponCurrent(value);
    Check(!packet.empty(),"test current state encoded by production wire codec");
    Check(Current(WeaponCurrentMessage,int(packet.size()),packet.data())==1&&current==value,
        "actual CSOWpnCur decoder and inventory observer commit exact signed-short quantities");
}
int LegacyCurrent(unsigned state,unsigned id,unsigned amount)
{
    unsigned char packet[]{static_cast<unsigned char>(state),static_cast<unsigned char>(id),static_cast<unsigned char>(amount)};
    return CurWeapon("CurWeapon",sizeof(packet),packet);
}
void Expect(int id,hud_style::AmmoKind kind,int amount,int reserveCount,const char* description)
{
    const auto value=CurrentWeaponHudState();
    Check(value.id==id&&value.kind==kind&&value.clip==amount&&value.reserve==reserveCount,description);
}
void Run()
{
    engine.pfnGetCvarFloat=[](const char*){return localPrediction;};
    engine.pfnAddCommand=[](const char*,void(*)()){++fixture::commands;return 1;};
    Register("weapon_k3",k3::Id,12,2000,k3::Clip);
    Check(definitions[k3::Id].maxClip==150,"K3 original 150-round capacity is preserved");
    Register("weapon_cannon",cannon::Id,14,20,cannon::Capacity);
    Register("weapon_tknifeex2",ThrowingKnifeId,15,30,1,1);
    for(const auto value:{std::array<int,2>{150,200},{100,2000},{100,1000},{0,2000}})
    {
        Send(k3::Id,value[0],value[1]);
        Expect(k3::Id,hud_style::AmmoKind::Magazine,value[0],value[1],"K3 HUD reads exact message clip and wide reserve through real consumers");
        Check(clips[k3::Id]==value[0]&&WeaponReserveAmmo(12)==value[1],"selection and HUD share the same decoded K3 quantities");
    }
    Send(k3::Id,0,1000);Send(k3::Id,0,2000);
    Expect(k3::Id,hud_style::AmmoKind::Magazine,0,2000,"buying reserve cannot reload an empty K3 magazine");
    auto before=fixture::forwarded;
    Check(LegacyCurrent(1,0,0)==1&&fixture::forwarded==before,"stock zero selection used to hide extended HUD cannot clear K3 state");
    Expect(k3::Id,hud_style::AmmoKind::Magazine,0,2000,"legacy zero does not replace extended quantities");
    Check(LegacyCurrent(1,k3::Id,150)==1&&fixture::forwarded==before,"invalid extended ID cannot enter stock three-byte CurWeapon");
    unsigned char ammoPacket[]{12,208};
    Check(Ammo("AmmoX",sizeof(ammoPacket),ammoPacket)==73&&fixture::forwarded==before+1,"native byte AmmoX still forwards through original handler");
    Check(WeaponReserveAmmo(12)==208,"actual legacy ammo observer consumes the byte-compatible value");
    Expect(k3::Id,hud_style::AmmoKind::Magazine,0,2000,"matched extended reserve retains wide 2000 despite legacy byte-compatible AmmoX");
    // Real ordinary definition and CurWeapon path: no synthetic HUD state.
    std::vector<unsigned char> stock{'w','e','a','p','o','n','_','m','4','a','1',0,4,90,255,255,0,0,22,0};
    ObserveLegacyWeaponDefinition(int(stock.size()),stock.data());
    Check(FindWeaponDefinition(22)&&definitions[22].name=="weapon_m4a1","actual legacy WeaponList reader registers stock weapon");
    unsigned char stockAmmo[]{4,90};Ammo("AmmoX",sizeof(stockAmmo),stockAmmo);
    before=fixture::forwarded;Check(LegacyCurrent(1,22,30)==73&&fixture::forwarded==before+1,"native CurWeapon chain forwards exactly once");
    Expect(22,hud_style::AmmoKind::Magazine,30,90,"stock selection cannot inherit stale K3 reserve or clip");
    LegacyCurrent(0,22,29);Expect(22,hud_style::AmmoKind::Magazine,30,90,"inactive stock clip update does not replace active clip state");
    Check(clips[22]==29,"inactive clip update is retained for selection inventory");
    Send(22,30,1000);Expect(22,hud_style::AmmoKind::Magazine,30,1000,"ordinary weapon also receives wide reserve through negotiated extension");
    LegacyCurrent(1,22,29);Expect(22,hud_style::AmmoKind::Magazine,29,1000,"fresh ordinary CurWeapon clip wins over older extended snapshot without losing wide reserve");
    Send(k3::Id,100,2000);Expect(k3::Id,hud_style::AmmoKind::Magazine,100,2000,"switching back restores K3 from actual current message");
    for(int length:{-1,0,1,2,3,4,5,6,7,8,9,11})
    {
        auto malformed=EncodeWeaponCurrent({k3::Id,1,0,0,0});malformed.resize(11);
        const auto saved=current;const auto savedClip=clip,savedReserve=reserve,savedActive=active;
        Current(WeaponCurrentMessage,length,malformed.data());
        Check(current==saved&&clip==savedClip&&reserve==savedReserve&&active==savedActive,"bad extended current length leaves all quantity owners unchanged");
    }
    auto unknown=EncodeWeaponCurrent({900,1,50,500,0});Current(WeaponCurrentMessage,int(unknown.size()),unknown.data());
    Expect(k3::Id,hud_style::AmmoKind::Magazine,100,2000,"unregistered extended weapon cannot overwrite current quantities");
    for(int length:{-1,0,1,2,4})
    {
        unsigned char bad[]{1,22,0,0};const auto calls=fixture::forwarded;
        Check(CurWeapon("CurWeapon",length,bad)==73&&fixture::forwarded==calls+1,"malformed native current remains forwarded once");
        Expect(k3::Id,hud_style::AmmoKind::Magazine,100,2000,"malformed native current does not mutate extended quantities");
    }
    localPrediction=1;k3_prediction::predictedClip=99;k3_prediction::predictedReserve=1999;
    Expect(k3::Id,hud_style::AmmoKind::Magazine,99,1999,"valid K3 prediction remains an explicit presentation source");
    Send(k3::Id,98,2000);
    Check(K3PredictedAmmo()==-1&&K3PredictedReserve()==-1,"actual K3 receipt invalidates both previous prediction quantities");
    Expect(k3::Id,hud_style::AmmoKind::Magazine,98,2000,"authoritative correction replaces invalidated K3 prediction");
    localPrediction=0;Send(cannon::Id,20,0);
    auto value=CurrentWeaponHudState();Check(value.kind==hud_style::AmmoKind::Total&&value.specialAmmo&&value.total==20,"cannon total comes from actual extended clip message");
    Send(ThrowingKnifeId,1,29);value=CurrentWeaponHudState();
    Check(value.kind==hud_style::AmmoKind::Total&&value.specialAmmo&&value.total==30,"knife complete total includes ready throw and reserve from actual wire state");
    Send(ThrowingKnifeId,0,29);Check(CurrentWeaponHudState().total==29,"throwing one knife changes authoritative complete total once");
    Send(k3::Id,150,1000);Send(0,-1,0,0);
    Check(CurrentWeaponHudState().id==0&&!ExtendedWeaponActive(),"empty extension selection does not retain any previous weapon count");
    Check(!FindWeaponDefinition(999),"unregistered identity remains unavailable to HUD reader");
}
}
int main(){wire::Run();std::cout<<"hud_style_weapon_state checks="<<fixture::checks<<" failures="<<fixture::failures<<'\n';return fixture::failures?1:0;}
