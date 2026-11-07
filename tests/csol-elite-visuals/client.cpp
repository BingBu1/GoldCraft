// SPDX-License-Identifier: GPL-3.0-or-later
// Production message consumer/update/draw, with SDK and triangle API boundaries.
#include "../../native/csol/client/runtime.hpp"
#include "../../native/csol/client/scenario/monsters/elite_visuals.hpp"
#include "../../native/csol/scenario/monsters/elite_visual_protocol.hpp"
#include <triangleapi.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <iostream>
#include <map>
#include <source_location>
#include <sstream>

namespace fixture
{
std::map<std::string,pfnUserMsgHook> hooks;
std::map<int,cl_entity_t> owners;
std::array<model_t,3> models;
model_t ownerModel;
studiohdr_t header;
float now=10;
int checks{},forwards{};
std::vector<std::pair<float,float>> randoms;
std::vector<int> selected;
std::vector<std::array<float,3>> vertices;
std::vector<float> alphas;
bool missing{};
void Check(bool value,std::source_location at=std::source_location::current())
{++checks;if(!value)throw std::runtime_error("elite visual line "+std::to_string(at.line()));}
bool Close(float a,float b){return std::abs(a-b)<.0001f;}
int Previous(const char*,int,void*){++forwards;return 73;}
void Owner(int index)
{auto&e=owners[index];e={};e.index=index;e.model=&ownerModel;e.curstate.modelindex=30;e.curstate.messagenum=7;
 e.origin[0]=10;e.origin[1]=20;e.origin[2]=30;e.attachment[0][0]=100;e.attachment[0][1]=200;e.attachment[0][2]=300;}
void Reset()
{
 csol::client::ResetEliteVisuals();now=10;owners.clear();Owner(1);Owner(300);Owner(301);
 ownerModel={};ownerModel.type=mod_studio;header={};missing=false;randoms.clear();selected.clear();vertices.clear();alphas.clear();
 for(auto&m:models){m={};m.type=mod_sprite;m.numframes=1;}
}
std::vector<std::uint8_t> Firewall(int sprite=7)
{
 const auto path="sprites/burn"+std::to_string(sprite)+".spr";
 std::vector<std::uint8_t>b(path.begin(),path.end());b.push_back(0);
 const auto word=[&](int n){b.push_back(n&255);b.push_back((n>>8)&255);};
 const auto real=[&](float n){const auto bits=std::bit_cast<std::uint32_t>(n);word(bits);word(bits>>16);};
 word(300);word(8);word(16);word(24);word(0);word(0);word(80);word(-1);real(.04f);real(5);
 word(0x830);word(0);for(int n:{0,0,15,5,150,255,255,255})b.push_back(n);
 for(float n:{100.f,90.f,0.f,5.f,3.f,0.f,0.f})real(n);return b;
}
int Send(const char*name,std::vector<std::uint8_t>b){return hooks.at(name)(name,int(b.size()),b.data());}
void Draw(){selected.clear();vertices.clear();alphas.clear();csol::client::DrawEliteVisuals();}
std::array<float,3> Center()
{std::array<float,3>v{};for(const auto&p:vertices)for(int i=0;i<3;++i)v[i]+=p[i]/4;return v;}
std::string Diagnostic(){std::ostringstream s;csol::client::EliteVisualsDiagnostics(s);return s.str();}
void ClockCorrections()
{
 using namespace csol::client;
 Reset();Send("TempEmit",Firewall());now=10.125f;EliteVisualsFrame();
 owners[300].curstate.modelindex=31;now=10.1875f;EliteVisualsFrame();Draw();
 const auto before=Center();const auto alpha=alphas.front();
 now=10.15625f;EliteVisualsFrame();Draw();Check(Center()==before&&alphas.front()==alpha);
 now=10.1875f;EliteVisualsFrame();Draw();Check(Center()==before&&alphas.front()==alpha);
 now=10.203125f;EliteVisualsFrame();Draw();Check(Center()[2]-before[2]==.15625f);
 Check(alphas.front()<=alpha);

 // Multi-frame sprites make a correction before birth observable at the
 // actual SpriteTexture boundary, including drawing while time catches up.
 auto*const spriteTexture=engine.pTriAPI->SpriteTexture;
 engine.pTriAPI->SpriteTexture=[](model_t*m,int frame)
 {Check(frame>=0&&frame<m->numframes);selected.push_back(int(m-models.data()));return 1;};
 Reset();for(auto&m:models)m.numframes=7;
 Send("TempEmit",Firewall());now=10.25f;EliteVisualsFrame();Draw();
 const auto birthCenter=Center();const auto birthAlpha=alphas.front();
 owners[300].curstate.modelindex=31;
 for(float corrected:{10.f,10.125f,10.25f})
 {now=corrected;EliteVisualsFrame();Draw();Check(vertices.size()==4);Check(Center()==birthCenter&&alphas.front()==birthAlpha);}
 now=10.265625f;EliteVisualsFrame();Draw();Check(vertices.size()==4);
 Check(alphas.front()<=birthAlpha);
 engine.pTriAPI->SpriteTexture=spriteTexture;
 Reset();now=1;Send("TempEmit",Firewall());now=1.125f;EliteVisualsFrame();Draw();Check(vertices.size()==4);
}
}
namespace csol::client
{
metahook_api_t*api{};cl_enginefunc_t engine{};engine_studio_api_t studio{};
void SynchronizeSession(){}
}
int main()
{
 using namespace fixture;using namespace csol::client;
 try
 {
  static metahook_api_t host;api=&host;
  host.HookUserMsg=[](const char*n,pfnUserMsgHook f)->pfnUserMsgHook{auto old=hooks.contains(n)?hooks[n]:Previous;hooks[n]=f;return old;};
  engine.pfnHookUserMsg=[](const char*n,pfnUserMsgHook f){hooks[n]=f;return 1;};
  engine.GetClientTime=[](){return now;};
  engine.GetEntityByIndex=[](int i)->cl_entity_t*{auto p=owners.find(i);return p==owners.end()?nullptr:&p->second;};
  engine.GetLocalPlayer=[]()->cl_entity_t*{return &owners[1];};
  engine.CL_LoadModel=[](const char*p,int*i)->model_t*{*i=42;return missing?nullptr:&models[std::string(p)[12]-'5'];};
  engine.pfnRandomFloat=[](float lo,float hi){randoms.emplace_back(lo,hi);return (lo+hi)/2;};
  engine.pfnRandomLong=[](int lo,int hi){randoms.emplace_back(float(lo),float(hi));return 1;};
  studio.Mod_Extradata=[](model_t*)->void*{return &header;};
  engine.pfnAngleVectors=[](const float*a,float*f,float*r,float*u)
  {Check(a[0]==0&&a[1]==0&&a[2]==1);std::fill_n(f,3,0);std::fill_n(r,3,0);std::fill_n(u,3,0);r[1]=-1;u[2]=1;};
  static triangleapi_t tri;engine.pTriAPI=&tri;
  tri.SpriteTexture=[](model_t*m,int frame){Check(frame==0);selected.push_back(int(m-models.data()));return 1;};
  tri.RenderMode=[](int){};tri.CullFace=[](TRICULLSTYLE){};tri.Begin=[](int kind){Check(kind==TRI_QUADS);};tri.End=[](){};
  tri.Color4f=[](float,float,float,float a){alphas.push_back(a);};tri.TexCoord2f=[](float,float){};
  tri.Vertex3f=[](float x,float y,float z){vertices.push_back({x,y,z});};
  InitializeEliteVisuals();Reset();auto packet=Firewall();
  csol::elitefx::Emitter parsed;Check(csol::elitefx::DecodeFirewall(packet,parsed));
  Check(parsed.owner==300&&parsed.sprite==2&&parsed.offset==std::array<float,3>{1,2,3});
  for(std::size_t n=0;n<packet.size();++n){auto b=packet;b.resize(n);Check(!csol::elitefx::DecodeFirewall(b,parsed));}
  auto extra=packet;extra.push_back(0);Check(!csol::elitefx::DecodeFirewall(extra,parsed));
  for(std::size_t at:{17u,32u,33u,34u,35u,36u,37u,42u,43u,44u,45u,46u,47u,48u,49u,50u,51u,52u,53u})
  {auto b=packet;b[at]^=0x80;Check(!csol::elitefx::DecodeFirewall(b,parsed));}
  Check(Send("TempEmit",packet)==1);EliteVisualsFrame();Draw();Check(vertices.empty());
  now=10.01f;EliteVisualsFrame();Draw();Check(selected==std::vector<int>{2});Check(vertices.size()==4);
  Check(Center()==std::array<float,3>{11,22,33});Check(Close(alphas.front(),150.f/255));
  Check(randoms==std::vector<std::pair<float,float>>{{-1,1},{-1,1},{-1,1},{1,1.5f}});
  owners[300].curstate.messagenum=6;now=10.06f;EliteVisualsFrame();Draw();
  Check(selected.size()==1);Check(Close(Center()[2],33.5f));Check(Diagnostic().find("emitters=0")!=std::string::npos);
  Check(Close(std::abs(vertices[2][1]-vertices[1][1]),94.5f));Check(Close(alphas.front(),142.5f/255));
  now=11.1f;EliteVisualsFrame();now=11.11f;EliteVisualsFrame();Draw();Check(vertices.empty());
  Reset();header.numattachments=1;Send("TempEmit",Firewall(5));now=10.1f;EliteVisualsFrame();Draw();
  Check(Center()==std::array<float,3>{101,202,303});Check(selected==std::vector<int>{0});
  Reset();Send("TempEmit",Firewall(6));now=10.1f;EliteVisualsFrame();now=10.9f;EliteVisualsFrame();Draw();
  Check(selected==std::vector<int>({1,1})); // no catch-up after eight tenths.
  owners[300].curstate.modelindex=31;now=11;EliteVisualsFrame();Check(Diagnostic().find("emitters=0")!=std::string::npos);
  Reset();Check(Send("ZombieBoss",{20,4,44,1,0,0})==1);now=10.01f;EliteVisualsFrame();Draw();
  Check(selected==std::vector<int>{1});
  // Averaging four rounded triangle vertices does not recover the original
  // center bit-for-bit when the CRT-selected size is fractional.
  const auto fireballCenter=Center();
  std::cout<<std::hexfloat<<"fireball center "<<fireballCenter[0]<<','<<fireballCenter[1]<<','<<fireballCenter[2]<<std::defaultfloat<<'\n';
  Check(Close(fireballCenter[0],10)&&Close(fireballCenter[1],20)&&Close(fireballCenter[2],37.5f));
  Check(randoms==std::vector<std::pair<float,float>>{{0,3},{0,2},{5,10},{-10,10},{-10,10},{-1,1},{-1,1},{-1,1},{1,1.5f}});
  now=10.1f;EliteVisualsFrame();Draw();Check(alphas.front()>0&&alphas.front()<250.f/255);
  // A backward render-clock correction keeps live particles (not a session).
  now=10.05f;EliteVisualsFrame();Draw();Check(vertices.size()==4);
  // Packet-time message behind the last frame must not clear live trails.
  Reset();Send("TempEmit",packet);now=10.1f;EliteVisualsFrame();
  now=10.09f;Check(Send("ZombieBoss",{20,4,44,1,0,0})==1);
  Check(Diagnostic().find("emitters=2")!=std::string::npos);Draw();Check(vertices.size()==4);
  ClockCorrections();
  Reset();Send("TempEmit",packet);now=10.1f;EliteVisualsFrame();Check(hooks["ResetHUD"]("ResetHUD",0,nullptr)==73);Draw();Check(vertices.empty());
  Reset();missing=true;Send("TempEmit",packet);now=10.1f;EliteVisualsFrame();Draw();Check(vertices.empty());
  Reset();for(int i=0;i<129;++i)Send("TempEmit",packet);Check(Diagnostic().find("emitters=128")!=std::string::npos);
  now=10.1f;EliteVisualsFrame();Draw();Check(selected.size()==128);ShutdownEliteVisuals();Draw();Check(vertices.empty());
  Check(Send("ZombieBoss",{20,3,44,1,0,0})==73);Check(Send("TempEmit",{0})==73);
  std::cout<<"elite visual client: "<<checks<<" checks passed\n";return 0;
 }
 catch(const std::exception&e){std::cerr<<e.what()<<" after "<<checks<<" checks\n";return 1;}
}
