// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/client/aurora.hpp"
#include <metahook.h>
#include <com_model.h>
#include <triangleapi.h>
#include <array>
#include <bit>
#include <fcntl.h>
#include <io.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <new>
#include <string>
#include <type_traits>
namespace allocation_probe {
unsigned requests{},live{};bool failNext{};std::array<void*,128> tracked{};
void Release(void* p) noexcept {if(!p)return;for(auto& slot:tracked)if(slot==p){slot=nullptr;--live;break;}std::free(p);}
}
// Test-only replaceable allocation boundary. No allocator injection in production.
void* operator new(std::size_t n) {if(auto* p=std::malloc(n?n:1))return p;throw std::bad_alloc{};}
void operator delete(void* p) noexcept {allocation_probe::Release(p);}
void operator delete(void* p,std::size_t) noexcept {allocation_probe::Release(p);}
void* operator new(std::size_t n,const std::nothrow_t&) noexcept {
    using namespace allocation_probe;++requests;if(failNext){failNext=false;return nullptr;}
    void* p=std::malloc(n?n:1);if(!p)return nullptr;
    for(auto& slot:tracked)if(!slot){slot=p;++live;return p;}
    std::free(p);return nullptr;
}
void operator delete(void* p,const std::nothrow_t&) noexcept {allocation_probe::Release(p);}
using namespace guest::cof::client;
static_assert(!std::is_copy_constructible_v<Aurora> && !std::is_copy_assignable_v<Aurora>);
static_assert(!std::is_move_constructible_v<Aurora> && !std::is_move_assignable_v<Aurora>);
namespace {
unsigned checks{},viewCalls{},entityCalls{},randomCalls{};float now{};
cl_entity_t view{},owner{},local{};model_t model{},sprite{};triangleapi_t tri{};
bool hasView=true;
void Check(bool v,int line){++checks;if(!v){std::fprintf(stderr,"retention FAIL %d\n",line);std::abort();}}
#define CHECK(x) Check((x),__LINE__)
float Random(float low,float high){++randomCalls;return low+(high-low)*.5F;}
int Load(const char*){return 7;} const model_t* Sprite(int){return &sprite;}
float Time(){return now;}cl_entity_t* View(){++viewCalls;return hasView?&view:nullptr;}
cl_entity_t* Entity(int index){++entityCalls;return index==0||index==5?&owner:nullptr;}
cl_entity_t* Local(){return &local;}
void Mode(int){}void Begin(int){}void End(){}void Color(float,float,float,float){}
void UV(float,float){}void Vertex(float*){}int Texture(model_t*,int){return 1;}
cl_enginefunc_t Engine(){cl_enginefunc_t e{};e.pfnSPR_Load=Load;e.GetSpritePointer=Sprite;
 e.pfnRandomFloat=Random;e.GetClientTime=Time;e.GetViewModel=View;e.GetEntityByIndex=Entity;e.GetLocalPlayer=Local;
 tri.RenderMode=Mode;tri.Begin=Begin;tri.End=End;tri.Color4f=Color;tri.TexCoord2f=UV;
 tri.Vertex3fv=Vertex;tri.SpriteTexture=Texture;e.pTriAPI=&tri;
 std::strcpy(model.name,"models/test.mdl");view.model=&model;sprite.numframes=1;return e;}
AuroraResult Add(Aurora& a,int owner=5,float limit=1000000){return a.Add(owner,"aurora/gunsmoke.aur",1,"models/test.mdl",0,limit);}
void Frame(Aurora& a,float dt=0,int mode=0){const auto alloc=allocation_probe::requests;
 CHECK(a.Frame(dt,mode,{0,1,0},{0,0,1})==AuroraResult::Applied);CHECK(allocation_probe::requests==alloc);}
unsigned Count(const Aurora& a){unsigned count=0;for(auto* s=a.First();s;s=a.Next(*s)){CHECK(s->used);++count;}return count;}
}
int main(int argc,char** argv){CHECK(argc==2||argc==3);std::ifstream file(argv[1],std::ios::binary);
 const std::string script((std::istreambuf_iterator<char>(file)),{});CHECK(!script.empty());auto e=Engine();
 if(argc==3 && std::strcmp(argv[2],"--oracle")==0){
  _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);
  struct Input{int operation,owner;float dt,time,limit;int body,mode;};static_assert(sizeof(Input)==28);
  Aurora a(&e);CHECK(a.Prepare(script)==AuroraResult::Applied);Input in;
  while(std::fread(&in,sizeof(in),1,stdin)==1){now=in.time;owner.curstate.renderfx=in.body;
   if(in.operation==1)CHECK(Add(a,in.owner,in.limit)==AuroraResult::Applied);
   if(in.operation==2)Frame(a,in.dt,in.mode);
   if(in.operation==3)a.KillLocal(true);if(in.operation==4)a.KillLocal(false);
   if(in.operation==5)a.DeleteOwner(in.owner);if(in.operation==6)a.Clear();
   if(in.operation==7)hasView=false;if(in.operation==8)hasView=true;
   const std::array<unsigned,3> header{Count(a),randomCalls,viewCalls};
   if(std::fwrite(header.data(),sizeof(header),1,stdout)!=1)return 2;
   for(auto* s=a.First();s;s=a.Next(*s)){
    const auto bits=[](float f){return std::bit_cast<unsigned>(f);};
    const std::array<unsigned,11> record{static_cast<unsigned>(s->owner),static_cast<unsigned>(s->state),
     s->kill?1U:0U,static_cast<unsigned>(s->head),static_cast<unsigned>(s->free),static_cast<unsigned>(s->main),
     s->main<0?0U:bits(s->particles[s->main].age),s->main<0?0U:bits(s->particles[s->main].life),
     bits(s->deadline),bits(s->fadeStart),bits(s->fadeDuration)};
    if(std::fwrite(record.data(),sizeof(record),1,stdout)!=1)return 2;
   }
  }return 0;
 }

 {
  Aurora a(&e);CHECK(a.Prepare(script)==AuroraResult::Applied);
  // Original Frame reads entity+0x304 (renderfx), independently of body+0x314.
  for(int fx:{70,184,137}){
   owner.curstate.renderfx=fx;owner.curstate.body=0;CHECK(Add(a)==AuroraResult::Applied);
   const auto vm=viewCalls;Frame(a);CHECK(viewCalls==vm);CHECK(a.First()->main<0);
   owner.curstate.renderfx=0;owner.curstate.body=fx;Frame(a);
   CHECK(viewCalls>vm);CHECK(a.First()->main>=0);a.KillLocal(true);Frame(a);CHECK(!a.First());
  }
  owner.curstate.body=0;owner.curstate.renderfx=0;
 }
 {
  Aurora a(&e);allocation_probe::failNext=true;
  CHECK(a.Prepare(script)==AuroraResult::CapacityExceeded);CHECK(!a.First());CHECK(Add(a)==AuroraResult::NotPrepared);
  CHECK(allocation_probe::live==0);CHECK(a.Prepare(script)==AuroraResult::Applied);CHECK(allocation_probe::live==1);
  const auto preparedRequests=allocation_probe::requests;CHECK(a.Prepare(script)==AuroraResult::Applied);
  CHECK(allocation_probe::requests==preparedRequests);
  std::array<const AuroraSystem*,192> nodes{};
  for(unsigned i=0;i<nodes.size();++i){
   if(i==64){const auto* first=a.First();const auto rng=randomCalls;allocation_probe::failNext=true;
    CHECK(Add(a)==AuroraResult::CapacityExceeded);CHECK(a.First()==first);CHECK(Count(a)==64);CHECK(randomCalls==rng);}
   CHECK(Add(a,i%2?5:0)==AuroraResult::Applied);nodes[i]=a.First();CHECK(nodes[i]->state==0);
  }
  CHECK(allocation_probe::live==3);CHECK(a.Prepare(script)==AuroraResult::InvalidInput);
  auto* cursor=a.First();for(unsigned i=192;i>0;--i){CHECK(cursor==nodes[i-1]);CHECK(cursor->owner==((i-1)%2?5:0));cursor=a.Next(*cursor);}CHECK(!cursor);
  Frame(a);CHECK(Count(a)==192);a.KillLocal(true);CHECK(Count(a)==192);
  for(int body:{70,184,137}){owner.curstate.renderfx=body;const auto vm=viewCalls;Frame(a);CHECK(Count(a)==192);CHECK(viewCalls==vm);}
  owner.curstate.renderfx=0;{const auto vm=viewCalls;Frame(a,0,1);CHECK(Count(a)==192);CHECK(viewCalls==vm);}
  hasView=false;const auto vm=viewCalls;Frame(a);CHECK(!a.First());CHECK(viewCalls==vm+192);hasView=true;
  // Explicit deletion is allowed to recycle full slots without more slab allocation.
  const auto reusedRequests=allocation_probe::requests;for(unsigned i=0;i<192;++i)CHECK(Add(a)==AuroraResult::Applied);
  CHECK(allocation_probe::requests==reusedRequests);a.Clear();CHECK(!a.First());CHECK(allocation_probe::live==1);
  CHECK(Add(a)==AuroraResult::Applied);a.VideoInit();CHECK(!a.First());CHECK(allocation_probe::live==1);
  CHECK(Add(a)==AuroraResult::NotPrepared);CHECK(a.Prepare(script)==AuroraResult::Applied);
 }
 CHECK(allocation_probe::live==0);
 {
  Aurora a(&e);CHECK(a.Prepare(script)==AuroraResult::Applied);
  // Real updates exhaust each shot naturally; no test writes any system or particle.
  for(unsigned shot=0;shot<192;++shot){CHECK(Add(a,shot%2?5:0)==AuroraResult::Applied);const auto* newest=a.First();
   for(unsigned frame=0;frame<32;++frame){now+=.25F;Frame(a,.25F);}
   CHECK(a.First()==newest);CHECK(newest->main>=0);CHECK(newest->head<0);CHECK(Count(a)==shot+1);
  }
  CHECK(allocation_probe::live==3);auto* newest=a.First();auto* second=a.Next(*newest);
  CHECK(newest->owner==5&&second->owner==0);CHECK(a.DeleteOwner(0));CHECK(a.First()==newest);
  CHECK(a.Next(*newest)!=second);CHECK(Count(a)==191);CHECK(a.DeleteOwner(5));CHECK(Count(a)==190);
  a.Clear();CHECK(allocation_probe::live==1);
  // Soft drain leaves a retained main whose life>age still contributes to a future Add.
  now=0;CHECK(Add(a)==AuroraResult::Applied);Frame(a);const auto* retained=a.First();a.KillLocal(false);Frame(a);
  now=4;Frame(a,.01F);CHECK(retained->state==1&&retained->head<0&&retained->main>=0);
  const auto& main=retained->particles[retained->main];CHECK(main.life>=0&&main.life>main.age);
  CHECK(Add(a,5,0)==AuroraResult::Applied);CHECK(a.First()->state==2);CHECK(a.Next(*a.First())==retained);
  owner.curstate.renderfx=70;Frame(a);CHECK(Count(a)==2);owner.curstate.renderfx=0;
  const auto beforeVm=viewCalls;Frame(a);CHECK(a.First()==retained);CHECK(viewCalls==beforeVm+3);CHECK(Count(a)==1);
 }
 CHECK(allocation_probe::live==0);
 std::printf("Aurora retention: %u checks, %u slab requests, no live slab allocations, 192 natural shots, OOM/gates/order/lifecycle passed\n",checks,allocation_probe::requests);
}
