// SPDX-License-Identifier: GPL-3.0-or-later
#include <metahook.h>
#include <event_api.h>
#include <event_args.h>
#include "native/guest/cof/client/revolver_events.hpp"
#include <array>
#include <bit>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <cstdlib>
using namespace guest::cof::client;
static_assert(offsetof(cl_entity_t,origin)==0xb48&&offsetof(cl_entity_t,attachment)==0xb60);
static_assert(offsetof(event_args_t,entindex)==4);
#if defined(COF_REVOLVER_EVENT_BRIDGE)
unsigned RevolverBridgeCases();
#endif
namespace {
using Row=std::array<std::uint32_t,12>;
Row input,output;
cl_enginefunc_t engine;
event_api_t api;
cl_entity_t entity;
event_args_t args;
unsigned checks{},currentCalls{},cancelAt{};
void Check(bool value){++checks;if(!value){std::cerr<<"Revolver event check "<<checks<<" failed\n";std::exit(2);}}
bool Current(void*) noexcept{return !cancelAt||++currentCalls<cancelAt;}
RevolverEventHost host;
void Reset(Row row)
{
 input=row;output={};engine={};api={};entity={};args={};cancelAt=currentCalls=0;
 host={&engine,"guest/cof/weapons/revolver/revolver_shell.wav",nullptr,Current};
 args.entindex=std::bit_cast<int>(row[0]);
 std::memcpy(entity.origin,row.data()+3,12);std::memcpy(args.origin,row.data()+6,12);
 std::memcpy(entity.attachment[0],row.data()+9,12);
 engine.GetEntityByIndex=[](int index)->cl_entity_t* {++output[0];Check(index==args.entindex);return input[1]?&entity:nullptr;};
 engine.pfnRandomLong=[](int low,int high)->int{++output[1];Check(low==0&&high==30);return int(input[2]);};
 api.EV_PlaySound=[](int index,float* origin,int channel,const char* sample,float volume,float attenuation,int flags,int pitch){
   ++output[2];output[3]=std::bit_cast<unsigned>(index);output[4]=channel;std::memcpy(output.data()+5,origin,12);
   output[8]=std::bit_cast<unsigned>(volume);output[9]=std::bit_cast<unsigned>(attenuation);output[10]=flags;output[11]=pitch;
   Check(!std::strcmp(sample,host.shellSample));
 };
 engine.pEventAPI=&api;
}
}
int main(int argc,char** argv)
{
 if(argc!=3)return 1;std::ifstream cases(argv[1]),expected(argv[2]);if(!cases||!expected)return 1;
 std::string line,wanted;unsigned rows{};
 while(std::getline(cases,line)){
  Row row{},want{};std::istringstream values(line);for(auto& v:row)if(!(values>>std::hex>>v))return 3;
  if(values>>std::ws&&values.peek()!=std::char_traits<char>::eof())return 3;
  if(!std::getline(expected,wanted))return 4;std::istringstream results(wanted);for(auto& v:want)if(!(results>>std::hex>>v))return 4;
  if(results>>std::ws&&results.peek()!=std::char_traits<char>::eof())return 4;
  Reset(row);const auto result=RevolverShellEvent(host,args);
  Check(result==(row[1]?RevolverEventResult::Issued:RevolverEventResult::Ignored));
  if(output!=want){std::cerr<<"original event mismatch "<<rows<<'\n';return 5;}++rows;
 }
 if(!rows)return 6;if(std::getline(expected,wanted))return 7;
 const Row row{19,1,15,0x3f800000,0x40000000,0x40400000,0x43000000,0x43010000,0x43020000,0x44800000,0x44810000,0x44820000};
 for(unsigned step=1;step<=4;++step){Reset(row);cancelAt=step;Check(RevolverShellEvent(host,args)==RevolverEventResult::Cancelled);Check(output[0]==(step>1)&&output[1]==(step>2)&&output[2]==(step>3));}
 Reset(row);host.current=nullptr;Check(RevolverShellEvent(host,args)==RevolverEventResult::MissingBackend&&output[0]==0);
 Reset(row);host.engine=nullptr;Check(RevolverShellEvent(host,args)==RevolverEventResult::MissingBackend&&output[0]==0);
 Reset(row);host.shellSample=nullptr;Check(RevolverShellEvent(host,args)==RevolverEventResult::MissingBackend&&output[0]==0);
 Reset(row);engine.GetEntityByIndex=nullptr;Check(RevolverShellEvent(host,args)==RevolverEventResult::MissingBackend&&output[0]==0);
 Reset(row);engine.pEventAPI=nullptr;Check(RevolverShellEvent(host,args)==RevolverEventResult::MissingBackend&&output[0]==1&&output[1]==0);
 Reset(row);api.EV_PlaySound=nullptr;Check(RevolverShellEvent(host,args)==RevolverEventResult::MissingBackend&&output[1]==0);
 Reset(row);engine.pfnRandomLong=nullptr;Check(RevolverShellEvent(host,args)==RevolverEventResult::MissingBackend&&output[1]==0);
#if defined(COF_REVOLVER_EVENT_BRIDGE)
 checks+=RevolverBridgeCases();
#endif
 std::cout<<rows<<" original Revolver shell-sound events; "<<checks<<" SDK checks; zero failures\n";
}
