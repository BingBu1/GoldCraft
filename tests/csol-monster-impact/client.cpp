// SPDX-License-Identifier: GPL-3.0-or-later
// Complete production decoder and renderer TU, real SDK ABI, recorded draw APIs.
#include "../../native/csol/client/monster_impact.cpp"
#include <array>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <vector>
static_assert(sizeof(void*) == 4);
namespace fixture {
using namespace csol::client;
int checks{},fails{},bloodCalls{},sparkCalls{},queries{};
int sprayIndex=21,dropIndex=22,color{},spray{},drop{},amount{},low{},high{};float size{};
std::array<float,3> position{};event_api_t events{};efx_api_t effects{};
xcommand_t diagnostic{};
void Check(bool ok,const char* label){++checks;if(!ok){++fails;std::cerr<<label<<'\n';}}
void Log(const char*,...){}
std::vector<unsigned char> Packet(int type,int count,std::array<float,3> origin){
 std::vector<unsigned char> p{(unsigned char)type,(unsigned char)count};
 for(float f:origin){auto n=static_cast<std::uint16_t>(static_cast<std::int16_t>(f*8));p.push_back(n);p.push_back(n>>8);}return p;
}
bool Feed(std::vector<unsigned char> p){return ConsumeMonsterImpact((int)p.size(),p.empty()?nullptr:p.data());}
void Setup(){
 engine.pEventAPI=&events;engine.pEfxAPI=&effects;engine.Con_Printf=Log;
 engine.pfnAddCommand=[](const char* name,xcommand_t cmd){Check(!std::strcmp(name,"csol_monster_impact_info"),"diagnostic name");diagnostic=cmd;return 1;};
 events.EV_FindModelIndex=[](const char* name){++queries;if(!std::strcmp(name,csol::monster_impact::BloodSpray))return sprayIndex;Check(!std::strcmp(name,csol::monster_impact::BloodDrop),"only blood resource paths");return dropIndex;};
 effects.R_BloodSprite=[](float* p,int c,int a,int b,float s){++bloodCalls;std::copy_n(p,3,position.begin());color=c;spray=a;drop=b;size=s;};
 effects.R_SparkEffect=[](float* p,int a,int l,int h){++sparkCalls;std::copy_n(p,3,position.begin());amount=a;low=l;high=h;};
 InitializeMonsterImpact();Check(diagnostic!=nullptr,"initializer registers diagnostics only");
}
void Draw(){
 for(auto p:{std::array<float,3>{12.125f,-34.25f,56.5f},std::array<float,3>{-4096,4095.875f,-.125f}}){
  auto n=bloodCalls;Check(Feed(Packet(4,4,p)),"blood packet consumed");Check(bloodCalls==n+1&&position==p&&color==247&&spray==21&&drop==22&&size==4,"five argument blood ABI exact position, palette, model indices and amount");
  n=sparkCalls;Check(Feed(Packet(5,16,p)),"spark packet consumed");Check(sparkCalls==n+1&&position==p&&amount==16&&low==0&&high==500,"four argument metal ABI exact");
 }
 sprayIndex=121;dropIndex=122;int q=queries;Feed(Packet(4,4,{1,2,3}));Check(spray==121&&drop==122&&queries==q+2,"map change resolves indices anew");
 auto before=bloodCalls;sprayIndex=0;Feed(Packet(4,4,{1,2,3}));sprayIndex=121;dropIndex=-1;Feed(Packet(4,4,{1,2,3}));dropIndex=122;
 auto callback=effects.R_BloodSprite;effects.R_BloodSprite=nullptr;Feed(Packet(4,4,{1,2,3}));effects.R_BloodSprite=callback;
 Check(bloodCalls==before&&missingSprites==3,"missing blood sprite or API never produces sparks");
 engine.pEventAPI=nullptr;Feed(Packet(4,4,{1,2,3}));engine.pEventAPI=&events;Check(bloodCalls==before&&missingSprites==4,"missing model API consumed safely");
 q=queries;Feed(Packet(5,16,{1,2,3}));Check(queries==q,"metal does not query blood sprites");
}
void Invalid(){
 int before=bloodCalls+sparkCalls;
 Check(!ConsumeMonsterImpact(8,nullptr)&&!ConsumeMonsterImpact(0,"x")&&!ConsumeMonsterImpact(-1,"x"),"null and empty unknown");
 for(int type:{0,1,2,3,6,199,255})Check(!Feed(Packet(type,4,{1,2,3})),"old subtypes stay with existing dispatcher");
 for(int type:{4,5}){
  auto p=Packet(type,4,{1,2,3});for(int n=1;n<8;++n)Check(ConsumeMonsterImpact(n,p.data()),"every truncated hit packet consumed safely");p.push_back(0);Check(Feed(p),"trailing bytes rejected");
  for(int count:{0,type==4?16:61,255})Check(Feed(Packet(type,count,{1,2,3})),"invalid amounts rejected");
 }
 Check(bloodCalls+sparkCalls==before,"all malformed packets produce no draw");
 auto api=engine.pEfxAPI;engine.pEfxAPI=nullptr;Check(Feed(Packet(4,4,{1,2,3})),"missing fx safe");engine.pEfxAPI=api;
 auto spark=effects.R_SparkEffect;effects.R_SparkEffect=nullptr;Check(Feed(Packet(5,16,{1,2,3})),"missing spark safe");effects.R_SparkEffect=spark;
 Check(bloodCalls+sparkCalls==before,"missing API no draw");
 std::ostringstream report;MonsterImpactDiagnostics(report);Check(report.str().find("\"authority\":\"server\"")!=std::string::npos,"diagnostic identifies server authority");
}
}
namespace csol::client {metahook_api_t* api{};cl_enginefunc_t engine{};}
int main(){fixture::Setup();fixture::Draw();fixture::Invalid();std::cout<<"monster-impact-client "<<fixture::checks<<" checks "<<fixture::fails<<" failures\n";return fixture::fails?1:0;}
