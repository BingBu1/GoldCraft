// SPDX-License-Identifier: GPL-3.0-or-later
// Exercise real message decoders, inventory commits and title selection.
#include "../../native/csol/client/weapons/weapon_inventory.cpp"
#include "../../native/csol/weapons/weapon_slots.cpp"
#include <iostream>
namespace csol::client {
cl_enginefunc_t engine{};cl_exportfuncs_t downstream{};ref_params_t view{};metahook_api_t* api{};
void SynchronizeSession(){}
void ReceiveExtendedWeaponDefinition(const WeaponDefinition&){}
void ReceiveExtendedWeaponState(const WeaponCurrent&){}
void AddWeaponPickup(int){}
bool TryActivateScenarioAbility(){return false;}
bool TryScenarioHealingSlot(int){return false;}
void ClearNativeWeaponSelection(){}
}
namespace {
using namespace csol;using namespace csol::client;
int checks{},failures{};std::uint16_t generation{};
void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<label<<'\n';}}
void Define(int id,int slot,int position,const char* name)
{
 auto bytes=EncodeWeaponDefinition({id,slot,position,24,-1,200,-1,35,0,name});
 Definition(WeaponDefinitionMessage,int(bytes.size()),bytes.data());
}
void Owned(std::initializer_list<int> ids)
{
 WeaponOwnership owned{};for(int id:ids)owned[id]=true;
 for(auto& packet:EncodeWeaponInventory(owned,++generation))Inventory(WeaponInventoryMessage,int(packet.size()),packet.data());
}
int Title(int id){const auto* result=FindWeaponTitleDefinition(id);return result?result->id:0;}
void Run()
{
 engine.pfnAddCommand=[](const char*,xcommand_t){return 1;};ResetWeaponInventory(true);
 Define(142,0,1,"weapon_skull3");Define(143,0,2,"weapon_skull3d");Define(197,0,0,"weapon_k3");
 Owned({142,143});Check(Title(143)==142,"D follows actual owned A first in same slot");
 Check(Title(142)==142&&Title(197)==197&&!Title(0)&&!Title(777),"other and absent weapons retain identity");
 Owned({143});Check(Title(143)==143,"D alone cannot invent A fallback");
 Owned({142,143});Check(Title(143)==142,"same equipped id observes new ownership");
 Owned({197,142,143});Check(Title(143)==143,"unpaired first item blocks fallback despite owned A");
 Owned({142,143});Check(Title(143)==142,"removing unrelated first item refreshes cache");
 Define(143,0,0,"weapon_skull3d");Check(Title(143)==143,"D itself first does not recurse");
 Define(143,0,2,"weapon_skull3d");Check(Title(143)==142,"position update invalidates head cache");
 Define(142,1,1,"weapon_skull3");Check(Title(143)==143,"A in another slot cannot supply title");
 Define(142,0,1,"weapon_skull3");Check(Title(143)==142,"return to original slot updates title");
 // The receiver must not expose an incomplete ownership transaction.
 WeaponOwnership many{};for(int id=1;id<WeaponIdLimit;++id)if(ValidWeaponId(id))many[id]=true;
 auto packets=EncodeWeaponInventory(many,++generation);Check(packets.size()>1,"multipart fixture");
 Inventory(WeaponInventoryMessage,int(packets.front().size()),packets.front().data());
 Check(Title(143)==142,"partial inventory does not replace committed slot head");
 for(std::size_t i=1;i<packets.size();++i)Inventory(WeaponInventoryMessage,int(packets[i].size()),packets[i].data());
 Check(Title(143)==143,"completed inventory updates actual first item");
 Owned({142,143});Check(Title(143)==142,"subsequent complete inventory restores paired head");
 ResetWeaponInventory(false);Check(Title(143)==143,"round reset clears ownership but preserves own definition");
 Owned({142,143});Check(Title(143)==142,"round inventory rebuild restores fallback");
 ResetWeaponInventory(true);Check(!Title(143),"map reset forgets title definitions");
 Define(143,0,2,"weapon_skull3d");Owned({142,143});Check(Title(143)==143,"owned undefined A cannot supply title");
 Define(142,0,1,"weapon_skull3");Check(Title(143)==142,"late A definition invalidates cached head");
}
}
int main(){Run();std::cout<<"title_inventory checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;}
