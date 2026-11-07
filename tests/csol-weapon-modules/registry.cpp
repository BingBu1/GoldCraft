// SPDX-License-Identifier: GPL-3.0-or-later
// Link this fixture only with the actual weapon_module.cpp, never fake registry code.
#include "../../native/csol/client/weapons/weapon_module.hpp"
#include <climits>
#include <cstdlib>
#include <iostream>
#include <new>
#include <string_view>
namespace {
using namespace csol; using namespace csol::client;
int checks{}, failures{}, count{}; int calls[64]{}; bool watch{}; std::size_t allocations{};
void Check(bool ok,const char* label){++checks;if(!ok){++failures;std::cerr<<label<<'\n';}}
void A(){calls[count++]=1;} void B(){calls[count++]=2;} void C(){calls[count++]=3;}
void AA(){calls[count++]=4;} void BB(){calls[count++]=5;} void CC(){calls[count++]=6;}
void Sequence(std::initializer_list<int> expected){Check(count==int(expected.size()),"callback count");int i{};for(int value:expected)Check(calls[i++]==value,"callback order");count=0;}
}
void* operator new(std::size_t n){if(watch)++allocations;if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
extern "C" void __cyg_profile_func_enter(void*,void*){}
extern "C" void __cyg_profile_func_exit(void*,void*){}
int main(int argc,char** argv){
 if(argc!=2)return 2;const std::string_view mode=argv[1];
 const int aid[]{70,71},bid[]{80},cid[]{90};
 const WeaponModule a{.ids=aid,.order=10,.initialize=A,.reset=A,.afterReset=AA,.videoInit=A};
 const WeaponModule b{.ids=bid,.order=20,.initialize=B,.reset=B,.afterReset=BB,.videoInit=B};
 const WeaponModule c{.ids=cid,.order=30,.initialize=C,.reset=C,.afterReset=CC,.videoInit=C};
 watch=true;WeaponModuleRegistration rc(c),ra(a),rb(b);watch=false;
 Check(allocations==0,"registration allocates no heap");
 Check(WeaponModules().size()==3,"three modules once");
 Check(WeaponModules()[0]==&a&&WeaponModules()[1]==&b&&WeaponModules()[2]==&c,"construction order sorted");
 Check(FindWeaponModule(70)==&a&&FindWeaponModule(71)==&a,"dual IDs select same descriptor");
 for(int id:{INT_MIN,-1,0,1,16,31,32,69,72,99,1023,1024,INT_MAX})Check(!FindWeaponModule(id),"unmapped and invalid lookup is null");
 if(mode=="valid"){
  watch=true;
  InitializeWeaponModules();Sequence({1,2,3});InitializeWeaponModules();Sequence({1,2,3});
  ResetWeaponModules();Sequence({1,2,3,4,5,6});WeaponModulesVideoInit();Sequence({1,2,3});
  for(int i=0;i<100000;++i){if(FindWeaponModule(71)!=&a)++failures;for(auto* module:WeaponModules())if(!module)++failures;}
  watch=false;Check(allocations==0,"hot lookup/enumeration/lifecycle allocate no heap");Check(WeaponModules().size()==3,"reinitialize cannot register twice");
 }else{
  int bad=mode=="negative"?-1:mode=="zero"?0:mode=="suit"?31:mode=="stock"?16:mode=="shield"?99:mode=="limit"?1024:mode=="max"?INT_MAX:70;
  int ids[]{72,bad};WeaponModule invalid{.ids=ids,.order=40};
  if(mode=="empty")invalid.ids={};
  if(mode=="same-id")ids[1]=72;
  if(mode=="same-order"){ids[1]=73;invalid.order=20;}
  WeaponModuleRegistration rejected(invalid);
  Check(WeaponModules().size()==3,"reject does not publish ordered entry");
  Check(!FindWeaponModule(72)&&!FindWeaponModule(73),"reject publishes no partial IDs");
  Check(FindWeaponModule(70)==&a&&FindWeaponModule(71)==&a,"rejected duplicate preserves owner");
  bool threw{};try{InitializeWeaponModules();}catch(const std::runtime_error&){threw=true;}
  Check(threw&&count==0,"invalid catalog rejects initialization before callbacks");
 }
 std::cout<<"registry "<<mode<<" checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
