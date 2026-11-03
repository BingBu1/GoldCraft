// SPDX-License-Identifier: GPL-3.0-or-later
// Inspect static registration in the exact linked DLL after normal CRT loading.
// RVAs and private Registry offsets come from its matching PDB, never fixed game offsets.
#include "../../native/csol/client/weapon_module.hpp"
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace {
using namespace csol::client;int checks{},failures{};
void Check(bool value,const char* label){++checks;if(!value){++failures;std::fprintf(stderr,"FAIL %s\n",label);}}
template<class T>T Read(const unsigned char* p){T value;std::memcpy(&value,p,sizeof(value));return value;}
}
int main(int argc,char** argv){
 if(argc!=8)return 2;
 unsigned value[6]{};for(int i=0;i<6;++i){char* end{};value[i]=std::strtoul(argv[i+2],&end,0);if(!end||*end)return 2;}
 const auto [rva,orderedOffset,countOffset,invalidOffset,registrySize,descriptorSize]=std::array<unsigned,6>{value[0],value[1],value[2],value[3],value[4],value[5]};
 Check(sizeof(void*)==4&&sizeof(WeaponModule)==descriptorSize,"probe ABI matches actual PDB descriptor");
 for(int cycle=0;cycle<2;++cycle){
  HMODULE module=LoadLibraryExA(argv[1],nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
  if(!module){std::fprintf(stderr,"LoadLibrary failed %lu\n",GetLastError());return 2;}
  const auto* image=reinterpret_cast<const unsigned char*>(module);const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
  const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(image+dos->e_lfanew);const auto size=nt->OptionalHeader.SizeOfImage;
  Check(nt->FileHeader.Machine==IMAGE_FILE_MACHINE_I386&&rva<size&&registrySize<=size-rva,"PDB registry extent belongs to exact x86 image");
  if(rva>=size||registrySize>size-rva)return 2;
  const auto* registry=image+rva;Check(Read<unsigned>(registry+countOffset)==9,"full DLL CRT registers nine modules");Check(!Read<bool>(registry+invalidOffset),"full DLL registration has no conflict");
  const WeaponModule* expected[1024]{};
  for(unsigned i=0;i<9;++i){
   const auto* descriptor=Read<const WeaponModule*>(registry+orderedOffset+i*4);
   const auto address=reinterpret_cast<std::uintptr_t>(descriptor),base=reinterpret_cast<std::uintptr_t>(image);
   Check(address>=base&&address+descriptorSize<=base+size,"registered descriptor belongs to linked image");
   if(address<base||address+descriptorSize>base+size)return 2;
   Check(descriptor->order==std::array<int,9>{10,20,30,40,50,60,65,70,80}[i],"full DLL registers original callback order");
   Check(descriptor->initialize&&descriptor->reset&&descriptor->videoInit&&descriptor->receive&&descriptor->predict&&descriptor->diagnostics,"all module callbacks retained");
   const unsigned count=(i==0||i==3)?2:1;Check(descriptor->ids.size()==count,"multi-ID span length retained");
   constexpr int first[]{725,144,197,1022,96,130,312,112,203};
   for(unsigned j=0;j<count;++j){const int id=first[i]+int(j);Check(descriptor->ids[j]==id,"full DLL exact weapon ID");expected[id]=descriptor;}
  }
  for(unsigned id=0;id<1024;++id)Check(Read<const WeaponModule*>(registry+id*4)==expected[id],"full DLL by-ID table exact and unused cells empty");
  Check(FreeLibrary(module)!=0,"full DLL unload succeeds without engine initialization");
 }
 std::printf("DLL registry checks=%d failures=%d\n",checks,failures);return failures?1:0;
}
