// SPDX-License-Identifier: GPL-3.0-or-later
// Actual complete production TU with recorded SDK calls, compared externally
// to the original client instruction oracle. No game or OpenGL process.
#include "../../native/csol/client/skull3.cpp"
#include "../../native/csol/client/shared_random.cpp"
#include "../../native/csol/client/weapon_module.cpp"
#include "../../native/csol/client/gun_smoke.cpp"
#include <iostream>
#include <iomanip>
#include <map>
#include <sstream>

namespace csol::client {
cl_enginefunc_t engine{};cl_exportfuncs_t downstream{};
metahook_api_t storage{};metahook_api_t* api=&storage;
WeaponDefinition singleDefinition{},dualDefinition{};
int activeId=142;bool allow=true;
const WeaponDefinition* FindWeaponDefinition(int id){return id==142?&singleDefinition:id==143?&dualDefinition:nullptr;}
bool WeaponPresentationAllowed(int id){return allow&&id==activeId;}
}
namespace fixture {
using namespace csol;using namespace csol::client;
struct Sprite {TEMPENTITY temp{};std::string path;float fps{},origin[3]{};};
std::map<std::string,void(*)(event_args_t*)> hooks;
std::map<std::string,xcommand_t> commands;
std::map<int,std::string> models;
std::vector<std::string> paths;
std::vector<std::array<float,3>> rng;
std::vector<char> rngKinds;
std::vector<Sprite> sprites;
event_api_t ev{};efx_api_t fx{};cl_entity_t vm{},local{};TEMPENTITY shell{};
int choice{},hand{},anim=-1,body{},pitch{},channel{},soundFlags{},shellModel{},bounce{},calls{};
float angle[3]{},origin[3]{},velocity[3]{},shellAngle[3]{},life{},volume{},attenuation{};
std::string sound;
template<class A>void Three(const A& a){std::cout<<'['<<a[0]<<','<<a[1]<<','<<a[2]<<']';}
void Setup()
{
 engine.pEventAPI=&ev;engine.pEfxAPI=&fx;
 api->HookCmd=[](const char* n,xcommand_t f)->xcommand_t{const auto old=commands[n];commands[n]=f;return old;};
 engine.pfnAddCommand=[](const char* n,xcommand_t f){commands[n]=f;return 1;};
 engine.GetViewModel=[](){return &vm;};engine.GetLocalPlayer=[](){return &local;};local.index=1;
 engine.pfnGetCvarFloat=[](const char* n){return !std::strcmp(n,"cl_righthand")?float(hand):1.f;};
 engine.pfnRandomLong=[](int a,int b){rngKinds.push_back('L');rng.push_back({float(a),float(b),0});return std::clamp(choice,a,b);};
 engine.pfnRandomFloat=[](float a,float b){rngKinds.push_back('F');rng.push_back({a,b,0});return (a+b)*.5f;};
 engine.pfnAngleVectors=[](const float* a,float* f,float* r,float* u){std::copy_n(a,3,angle);std::fill_n(f,3,0);std::fill_n(r,3,0);std::fill_n(u,3,0);f[0]=1;r[1]=-1;u[2]=1;};
 engine.pfnWeaponAnim=[](int a,int b){anim=a;body=b;};
 engine.pfnHookEvent=[](const char* n,void(*f)(event_args_t*)){hooks[n]=f;};
 ev.EV_IsLocal=[](int i){return int(i==0);};
 ev.EV_LocalPlayerViewheight=[](float* h){h[0]=h[1]=0;h[2]=28;};
 ev.EV_FindModelIndex=[](const char* n){paths.emplace_back(n);const int i=int(paths.size());models[i]=n;return i;};
 ev.EV_PlaySound=[](int,float*,int c,const char* n,float v,float a,int f,int p){channel=c;sound=n;volume=v;attenuation=a;soundFlags=f;pitch=p;++calls;};
 fx.R_DefaultSprite=[](float* o,int id,float fps)->TEMPENTITY*{sprites.emplace_back();auto& s=sprites.back();s.path=models.at(id);s.fps=fps;std::copy_n(o,3,s.origin);return &s.temp;};
 fx.R_TempModel=[](float* o,float* v,float* a,float t,int id,int b)->TEMPENTITY*{std::copy_n(o,3,origin);std::copy_n(v,3,velocity);std::copy_n(a,3,shellAngle);life=t;shellModel=id;bounce=b;shell={};shell.entity.index=123;for(int i=0;i<3;++i)shell.entity.baseline.angles[i]=float(i+2);return &shell;};
 InitializeSkull3();sprites.reserve(8);
 for(int a=0;a<4;++a)for(int i=0;i<3;++i)vm.attachment[a][i]=float(10*(i+1)+100*a);
}
void Output()
{
 std::cout<<std::setprecision(9)<<"{\"angles\":";Three(angle);
 std::cout<<",\"animation\":";if(anim<0)std::cout<<"null";else std::cout<<'['<<anim<<','<<body<<']';
 std::cout<<",\"muzzleFlash\":"<<int((vm.curstate.effects&EF_MUZZLEFLASH)!=0)<<",\"models\":[";
 for(size_t i=0;i<paths.size();++i){if(i)std::cout<<',';std::cout<<'\"'<<paths[i]<<'\"';}
 std::cout<<"],\"rng\":[";for(size_t i=0;i<rng.size();++i){if(i)std::cout<<',';std::cout<<"[\""<<rngKinds[i]<<"\","<<rng[i][0]<<','<<rng[i][1]<<']';}
 std::cout<<"],\"shell\":{\"origin\":";Three(origin);std::cout<<",\"velocity\":";Three(velocity);std::cout<<",\"angles\":";Three(shellAngle);
 std::cout<<",\"life\":"<<life<<",\"model\":"<<shellModel<<",\"bounce\":"<<bounce<<",\"extra\":0,\"angularVelocity\":";Three(shell.entity.baseline.angles);
 std::cout<<",\"index\":"<<shell.entity.baseline.body<<"},\"sound\":{\"sample\":\""<<sound<<"\",\"channel\":"<<channel<<",\"flags\":"<<soundFlags<<",\"volume\":"<<volume<<",\"attenuation\":"<<attenuation<<",\"pitch\":"<<pitch<<"},\"sprites\":[";
 for(size_t i=0;i<sprites.size();++i){if(i)std::cout<<',';const auto& s=sprites[i];const auto& c=s.temp.entity.curstate;
  std::cout<<"{\"path\":\""<<s.path<<"\",\"fps\":"<<s.fps<<",\"origin\":";Three(s.origin);
  std::cout<<",\"flags\":"<<s.temp.flags<<",\"scale\":["<<c.scale<<','<<c.scale<<','<<c.scale<<"],\"renderMode\":"<<c.rendermode<<",\"alpha\":"<<c.renderamt<<",\"renderFx\":"<<c.renderfx<<",\"color\":["<<int(c.rendercolor.r)<<','<<int(c.rendercolor.g)<<','<<int(c.rendercolor.b)<<"],\"velocity\":";Three(s.temp.entity.baseline.origin);std::cout<<'}';}
 std::cout<<"]}\n";
}
}
#ifndef SKULL3_CLIENT_EVENTS_NO_MAIN
int main()
{
 using namespace fixture;Setup();int kind,bparam,isLocal,smoke;
 while(std::cin>>kind>>bparam>>isLocal>>hand>>smoke>>choice)
 {
  if(GunSmokeEnabled()!=bool(smoke))commands.at("gunsmoke")();
  rng.clear();rngKinds.clear();sprites.clear();models.clear();paths.clear();anim=-1;vm.curstate.effects=0;
  csol::client::activeId=kind?143:142;event_args_t a{};a.entindex=isLocal?1:2;
  for(int i=0;i<3;++i){a.origin[i]=float(100*(i+1));a.angles[i]=float(10*(i+1));a.velocity[i]=float(4*(i+1));}
  a.iparam1=125;a.iparam2=-250;a.bparam1=bparam;a.fparam1=.02f;a.fparam2=.03f;
  hooks.at(kind==0?"events/skull3.sc":kind==1?"events/skull3d_left.sc":"events/skull3d_right.sc")(&a);Output();
 }
}
#endif
