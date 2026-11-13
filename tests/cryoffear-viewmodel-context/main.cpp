// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/weapons/guest_cof_viewmodel.hpp"
#include "../../native/csol/client/weapons/guest_cof_resources.hpp"
#include "../../native/csol/client/resource_refresh.hpp"
#include "../../native/guest/cof/catalog.hpp"
#include <thread>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
namespace csol::client {metahook_api_t* api{};cl_enginefunc_t engine{};}
namespace {
using namespace csol::client;
unsigned checks{},failed{},resolves{};unsigned char knife{},elite{};cvar_t hand{};
cl_entity_t viewmodel{},other{};model_t model{};bool prepared=true,missing{};
void Check(bool value,int line){++checks;if(!value){++failed;std::printf("FAIL %d\n",line);}}
#define CHECK(x) Check(bool(x),__LINE__)
void* Client(){return reinterpret_cast<void*>(0x10000000);}
mh_gamesymbol_status_t Resolve(void* base,const char* name,mh_gamesymbol_kind_t kind,void** out){
 CHECK(base==Client()&&kind==MH_GAMESYMBOL_KIND_GLOBAL);++resolves;
 if(missing){*out=nullptr;return MH_GAMESYMBOL_SYMBOL_NOT_FOUND;}
 *out=std::strstr(name,"Knife")?static_cast<void*>(&knife):static_cast<void*>(&elite);return MH_GAMESYMBOL_OK;
}
cvar_t* Cvar(const char* name){CHECK(!std::strcmp(name,"cl_righthand"));return &hand;}
cl_entity_t* View(){return &viewmodel;}
}
namespace csol::client {GuestCofResourceState GuestCofResourcesState() noexcept {GuestCofResourceState s;s.status=prepared?GuestCofResourceStatus::Prepared:GuestCofResourceStatus::Armed;return s;}}
int main(){
 using namespace csol::client;metahook_api_t table{};api=&table;table.GetClientBase=Client;table.ResolveGameSymbol=Resolve;
 engine.pfnGetCvarPointer=Cvar;engine.GetViewModel=View;viewmodel.model=&model;
 CHECK(InitializeGuestCofViewmodel()&&GuestCofViewmodelReady());
 const auto calls=resolves;
 for(const auto& pistol:guest::cof::Pistols)for(int flags:{0,1,2,3})for(float user:{0.f,1.f,-1.f,2.f,.5f})
  for(unsigned char k:{0,1})for(unsigned char e:{0,1}){
   std::strcpy(model.name,pistol.viewModel.data());hand.value=user;knife=k;elite=e;
   {GuestCofViewmodelScope scope(&viewmodel,flags);
    if(flags&3){CHECK(hand.value==(std::trunc(user)==0?1.f:0.f)&&knife==0&&elite==0);
      {GuestCofViewmodelScope nested(&other,flags);CHECK(knife==0&&elite==0);}
      {GuestCofViewmodelScope nested(&viewmodel,flags);CHECK(hand.value==(std::trunc(user)==0?1.f:0.f)&&knife==0&&elite==0);}}
    else CHECK(hand.value==user&&knife==k&&elite==e);
   }
   CHECK(hand.value==user&&knife==k&&elite==e);
  }
 CHECK(resolves==calls);
 for(int flags:{1,2,3}){
  for(bool allowed:{false,true}){resource_refresh_detail::readsAllowed=allowed;prepared=false;hand.value=1;knife=1;elite=1;
   {GuestCofViewmodelScope scope(&viewmodel,flags);CHECK(hand.value==1&&knife==1&&elite==1);}
   prepared=true;}
 }
 resource_refresh_detail::readsAllowed=true;
 std::strcpy(model.name,"models/v_knife.mdl");hand.value=1;knife=1;elite=1;
 {GuestCofViewmodelScope native(&viewmodel,3);CHECK(hand.value==1&&knife==1&&elite==1);}
 std::strcpy(model.name,guest::cof::Pistols[0].viewModel.data());
 std::thread foreign([]{CHECK(!GuestCofViewmodelReady());GuestCofViewmodelScope scope(&viewmodel,3);CHECK(hand.value==1&&knife==1&&elite==1);});foreign.join();
 hand.value=std::numeric_limits<float>::quiet_NaN();{GuestCofViewmodelScope scope(&viewmodel,3);CHECK(std::isnan(hand.value)&&knife==1&&elite==1);}
 ShutdownGuestCofViewmodel();CHECK(!GuestCofViewmodelReady());missing=true;CHECK(!InitializeGuestCofViewmodel()&&!GuestCofViewmodelReady());
 std::printf("viewmodel context SDK checks=%u failures=%u; actual RAII and cold resolver; live=no performance=no\n",checks,failed);return failed?1:0;
}
