// SPDX-License-Identifier: GPL-3.0-or-later
// Normal-load the exact product and inspect only its matching PDB's registry.
#include "../../native/csol/client/weapon_module.hpp"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace {
using namespace csol::client;
int checks{},failures{};
void Check(bool value,const char* label){++checks;if(!value){++failures;std::fprintf(stderr,"FAIL %s\n",label);}}
template<class T>T Read(const unsigned char* p){T value;std::memcpy(&value,p,sizeof(value));return value;}
constexpr int Orders[]{10,20,30,40,50,60,65,66,67,70,80};
constexpr int FirstIds[]{725,144,197,1022,96,130,312,142,143,112,203};
constexpr unsigned ModuleCount=std::size(Orders);
}
int main(int argc,char** argv){
 if(argc!=8)return 2;
 unsigned value[6]{};
 for(int i=0;i<6;++i){char* end{};value[i]=std::strtoul(argv[i+2],&end,0);if(!end||*end)return 2;}
 const auto [rva,orderedOffset,countOffset,invalidOffset,registrySize,descriptorSize]=std::array<unsigned,6>{value[0],value[1],value[2],value[3],value[4],value[5]};
 Check(sizeof(void*)==4&&sizeof(WeaponModule)==descriptorSize,"probe ABI matches exact product PDB");
 for(int cycle=0;cycle<2;++cycle){
  HMODULE module=LoadLibraryExA(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
  if(!module){std::fprintf(stderr,"LoadLibrary failed %lu\n",GetLastError());return 2;}
  const auto* image=reinterpret_cast<const unsigned char*>(module);
  const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
  const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(image+dos->e_lfanew);
  const auto size=nt->OptionalHeader.SizeOfImage;
  Check(nt->FileHeader.Machine==IMAGE_FILE_MACHINE_I386&&rva<size&&registrySize<=size-rva,"PDB extent belongs to x86 image");
  if(rva>=size||registrySize>size-rva)return 2;
  const auto* registry=image+rva;
  Check(Read<unsigned>(registry+countOffset)==ModuleCount,"eleven descriptors survive LTO and CRT initialization");
  Check(!Read<bool>(registry+invalidOffset),"no registration conflicts");
  const WeaponModule* expected[1024]{};
  for(unsigned i=0;i<ModuleCount;++i){
   const auto* descriptor=Read<const WeaponModule*>(registry+orderedOffset+i*4);
   const auto address=reinterpret_cast<std::uintptr_t>(descriptor),base=reinterpret_cast<std::uintptr_t>(image);
   Check(address>=base&&address+descriptorSize<=base+size,"descriptor resides in linked image");
   if(address<base||address+descriptorSize>base+size)return 2;
   Check(descriptor->order==Orders[i],"stable explicit callback order");
   if(i==8){
    Check(!descriptor->initialize&&!descriptor->reset&&!descriptor->videoInit&&!descriptor->receive&&!descriptor->predict&&!descriptor->diagnostics,"SKULL3 D metadata cannot duplicate A/D lifecycle");
   }else{
    Check(descriptor->initialize&&descriptor->reset&&descriptor->videoInit&&descriptor->receive&&descriptor->predict&&descriptor->diagnostics,"module lifecycle retained");
   }
   const unsigned count=(i==0||i==3)?2:1;
   Check(descriptor->ids.size()==count,"ID span retains exact size");
   for(unsigned j=0;j<count;++j){const int id=FirstIds[i]+int(j);Check(descriptor->ids[j]==id,"exact linked weapon ID");expected[id]=descriptor;}
  }
  for(unsigned id=0;id<1024;++id)Check(Read<const WeaponModule*>(registry+id*4)==expected[id],"full ID table and empty cells");
  const auto* a=expected[142];const auto* d=expected[143];
  Check(a&&d&&a!=d,"A/D separate descriptors");
  Check(a->predictedClip==d->predictedClip&&a->predictedReserve==d->predictedReserve&&a->crosshairShot==d->crosshairShot,"A/D share predictor readback");
  Check(a->ammoKind==csol::hud_style::AmmoKind::Magazine&&d->ammoKind==csol::hud_style::AmmoKind::Magazine,"A/D magazine HUD");
  Check(a->ammoRect.left==49&&a->ammoRect.right==73&&a->ammoRect.top==231&&a->ammoRect.bottom==255&&!std::memcmp(&a->ammoRect,&d->ammoRect,sizeof(wrect_t)),"original shared ammo rectangle");
  Check(FreeLibrary(module)!=0,"unload succeeds without engine initialization");
 }
 std::printf("DLL registry checks=%d failures=%d\n",checks,failures);
 return failures?1:0;
}
