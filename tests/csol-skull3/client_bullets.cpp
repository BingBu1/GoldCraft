// SPDX-License-Identifier: GPL-3.0-or-later
#define SKULL3_CLIENT_EVENTS_NO_MAIN
#include "client_events.cpp"

namespace bullet_fixture {
using namespace csol::client;
struct Trace{float start[3],end[3],length{},fraction{},hit[3];int flags{},ignore{};};
struct Sound{std::string sample;float volume{},attenuation{};int channel{};};
struct Streak{float origin[3],speed{};int color{},count{};};
std::vector<Trace> traces;std::vector<Sound> sounds;std::vector<Streak> streaks;
std::vector<std::string> decals;std::vector<std::array<int,2>> engineCalls;
physent_t entity{};char material{};float hitDistance{};
int hitgroup{};
void Setup()
{
 fixture::Setup();auto& ev=fixture::ev;auto& fx=fixture::fx;
 engine.pfnRandomLong=[](int low,int){return low;};
 engine.pfnRandomFloat=[](float low,float high){return (low+high)*.5f;};
 ev.EV_SetUpPlayerPrediction=[](int p,int q){engineCalls.push_back({0,p});if(q!=1)std::abort();};
 ev.EV_PushPMStates=[](){engineCalls.push_back({1,0});};
 ev.EV_SetSolidPlayers=[](int n){engineCalls.push_back({2,n});};
 ev.EV_SetTraceHull=[](int n){engineCalls.push_back({3,n});};
 ev.EV_PopPMStates=[](){engineCalls.push_back({4,0});};
 ev.EV_GetPhysent=[](int){return &entity;};ev.EV_IndexFromTrace=[](pmtrace_t*){return 0;};
 ev.EV_TraceTexture=[](int,float*,float*){return "+0{EXAMPLE_LONG1234";};
 downstream.HUD_PlayerMoveTexture=[](char* name){if(std::strcmp(name,"EXAMPLE_LONG1234"))std::abort();return material;};
 ev.EV_PlayerTrace=[](float* start,float* end,int flags,int ignore,pmtrace_t* result){
  traces.emplace_back();auto& t=traces.back();std::copy_n(start,3,t.start);std::copy_n(end,3,t.end);
  t.length=end[0]-start[0];t.fraction=std::min(1.f,hitDistance/t.length);t.flags=flags;t.ignore=ignore;
  *result={};result->fraction=t.fraction;result->plane.normal[0]=-1;result->hitgroup=hitgroup;
  for(int i=0;i<3;++i)result->endpos[i]=t.hit[i]=start[i]+(end[i]-start[i])*t.fraction;
 };
 ev.EV_PlaySound=[](int,float*,int channel,const char* sample,float volume,float attenuation,int,int){
  if(channel==6)sounds.push_back({sample,volume,attenuation,channel});};
 fx.R_BulletImpactParticles=[](float*){};
 fx.R_StreakSplash=[](float* p,float*,int color,int count,float speed,int lo,int hi){
  if(lo!=-75||hi!=75)std::abort();streaks.emplace_back();auto& s=streaks.back();std::copy_n(p,3,s.origin);s.color=color;s.count=count;s.speed=speed;};
 fx.Draw_DecalIndexFromName=[](char* n){decals.emplace_back(n);return 4;};
 fx.Draw_DecalIndex=[](int n){return n;};
 fx.R_DecalShoot=[](int,int,int,float*,int f){if(f!=0x800)std::abort();};
}
void MonsterAuthorityBoundary()
{
 for(int solid:{3,6})for(int group:{1,8})
 {
  traces.clear();sounds.clear();streaks.clear();decals.clear();fixture::sprites.clear();entity={};
  entity.solid=solid;entity.studiomodel=reinterpret_cast<model_t*>(1);material='C';hitDistance=100;hitgroup=group;
  event_args_t args{};args.entindex=1;const float forward[]{1,0,0},right[]{0,-1,0},up[]{0,0,1};
  Bullet(args,false,forward,right,up);
  if(traces.size()!=2 || !sounds.empty() || !streaks.empty() || !decals.empty() || !fixture::sprites.empty())std::abort();
 }
 hitgroup=0;
}
void Output()
{
 using fixture::Three;std::cout<<std::setprecision(9)<<"{\"traces\":[";
 for(size_t i=0;i<traces.size();++i){if(i)std::cout<<',';const auto&t=traces[i];std::cout<<"{\"source\":";Three(t.start);std::cout<<",\"end\":";Three(t.end);std::cout<<",\"length\":"<<t.length<<",\"fraction\":"<<t.fraction<<",\"flags\":"<<t.flags<<",\"ignore\":"<<unsigned(t.ignore)<<",\"hit\":";Three(t.hit);std::cout<<'}';}
 std::cout<<"],\"sounds\":[";for(size_t i=0;i<sounds.size();++i){if(i)std::cout<<',';const auto&s=sounds[i];std::cout<<"{\"sample\":\""<<s.sample<<"\",\"volume\":"<<s.volume<<",\"attenuation\":"<<s.attenuation<<",\"channel\":"<<s.channel<<'}';}
 std::cout<<"],\"streaks\":[";for(size_t i=0;i<streaks.size();++i){if(i)std::cout<<',';const auto&s=streaks[i];std::cout<<"{\"origin\":";Three(s.origin);std::cout<<",\"color\":"<<s.color<<",\"count\":"<<s.count<<",\"speed\":"<<s.speed<<'}';}
 std::cout<<"],\"decals\":[";for(size_t i=0;i<decals.size();++i){if(i)std::cout<<',';std::cout<<'\"'<<decals[i]<<'\"';}
 std::cout<<"],\"smoke\":[";for(size_t i=0;i<fixture::sprites.size();++i){if(i)std::cout<<',';const auto&s=fixture::sprites[i];const auto&c=s.temp.entity.curstate;
 std::cout<<"{\"source\":";Three(s.origin);std::cout<<",\"scale\":"<<c.scale<<",\"color\":["<<int(c.rendercolor.r)<<','<<int(c.rendercolor.g)<<','<<int(c.rendercolor.b)<<"]}";}
 std::cout<<"],\"engine\":[";for(size_t i=0;i<engineCalls.size();++i){if(i)std::cout<<',';std::cout<<'['<<engineCalls[i][0]<<','<<engineCalls[i][1]<<']';}std::cout<<"]}\n";
}
}
int main(){using namespace bullet_fixture;Setup();MonsterAuthorityBoundary();while(std::cin>>material>>hitDistance){
 traces.clear();sounds.clear();streaks.clear();decals.clear();engineCalls.clear();fixture::sprites.clear();fixture::models.clear();fixture::paths.clear();entity={};entity.solid=SOLID_BSP;
 event_args_t args{};args.entindex=1;args.origin[0]=100;args.origin[1]=200;args.origin[2]=283;
 const float forward[]{1,0,0},right[]{0,-1,0},up[]{0,0,1};Bullet(args,false,forward,right,up);Output();}}
