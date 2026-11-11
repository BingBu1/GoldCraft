// SPDX-License-Identifier: GPL-3.0-or-later
#include "../../native/guest/cof/client/aurora.hpp"
#include <metahook.h>
#include <com_model.h>
#include <triangleapi.h>
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <fcntl.h>
#include <io.h>
using namespace guest::cof::client;
namespace
{
float now;unsigned randomCalls,loads,quads;cl_entity_t view,owner,local;model_t model,sprite;
bool hasView=true;int lastMode=-1;triangleapi_t tri;
float Random(float low,float high){const float unit=static_cast<float>((randomCalls++*37U+11U)%101U)/100.0F;
    return static_cast<float>(static_cast<double>(low)+(static_cast<double>(high)-low)*unit);}
int Load(const char* path){++loads;return std::strcmp(path,"sprites/fog2.spr")==0?7:0;}
const model_t* Sprite(int handle){return handle==7?&sprite:nullptr;}
float Time(){return now;}
cl_entity_t* View(){return hasView?&view:nullptr;}
cl_entity_t* Entity(int index){return index==0 || index==5?&owner:nullptr;}
cl_entity_t* Local(){return &local;}
void Mode(int n){lastMode=n;} void Begin(int){++quads;} void End(){} void Color(float,float,float,float){}
void UV(float,float){} void Vertex(float*){} int Texture(model_t*,int){return 1;}
void Check(bool v,int line){if(!v){std::fprintf(stderr,"FAIL %d\n",line);std::abort();}}
#define CHECK(x) Check((x),__LINE__)
cl_enginefunc_t Engine()
{
    cl_enginefunc_t e{};e.pfnSPR_Load=Load;e.GetSpritePointer=Sprite;e.pfnRandomFloat=Random;
    e.GetClientTime=Time;e.GetViewModel=View;e.GetEntityByIndex=Entity;e.GetLocalPlayer=Local;
    tri.RenderMode=Mode;tri.Begin=Begin;tri.End=End;tri.Color4f=Color;tri.TexCoord2f=UV;
    tri.Vertex3fv=Vertex;tri.SpriteTexture=Texture;e.pTriAPI=&tri;
    std::strcpy(model.name,"models/test.mdl");view.model=&model;sprite.numframes=1;
    return e;
}
struct Input {float dt,time,attachment[3];int body,mode,command;};
struct ParticleOutput {int next,type,owner;float values[32];};
struct Output {int present,state,head,free,main,randoms;float deadline,start,duration;ParticleOutput particles[20];};
static_assert(sizeof(Input)==32 && sizeof(ParticleOutput)==140 && sizeof(Output)==2836);
Output Snapshot(const Aurora& effects)
{
    Output out{};out.randoms=static_cast<int>(randomCalls);const auto* s=effects.First();if(!s)return out;
    out.present=1;out.state=s->state;out.head=s->head;out.free=s->free;out.main=s->main;
    out.deadline=s->deadline;out.start=s->fadeStart;out.duration=s->fadeDuration;
    for(unsigned i=0;i<20;++i)
    {
        const auto& p=s->particles[i];auto& o=out.particles[i];o.next=p.next;o.type=p.type;o.owner=p.owner;
        unsigned n=0;auto add=[&](float f){o.values[n++]=f;};
        for(auto f:p.render.position)add(f);for(auto f:p.velocity)add(f);for(auto f:p.acceleration)add(f);
        for(auto f:p.wind)add(f);for(unsigned k=0;k<3;++k)add(p.render.color[k]);
        for(auto f:p.colorDelta)add(f);add(p.render.color[3]);add(p.alphaDelta);add(p.render.frame);add(p.frameDelta);
        add(p.render.angle);add(p.angleDelta);add(p.render.size);add(p.sizeDelta);add(p.drag);add(p.age);add(p.life);add(p.nextSpray);
        CHECK(n==30);
    }
    return out;
}
}
int main(int argc,char** argv)
{
    CHECK(argc>=2);std::ifstream file(argv[1],std::ios::binary);
    const std::string script((std::istreambuf_iterator<char>(file)),{});CHECK(!script.empty());
    auto engine=Engine();Aurora effects(&engine);CHECK(effects.Prepare(script)==AuroraResult::Applied);
    const int effectOwner=argc>=4?std::atoi(argv[3]):5;
    CHECK(loads==1);CHECK(effects.Add(effectOwner,"aurora/gunsmoke.aur",1,"models/test.mdl",0,5)==AuroraResult::Applied);
    if(argc>=3 && std::strcmp(argv[2],"--oracle")==0)
    {
        _setmode(_fileno(stdin),_O_BINARY);_setmode(_fileno(stdout),_O_BINARY);Input input;
        while(std::fread(&input,sizeof(input),1,stdin)==1)
        {
            now=input.time;std::memcpy(view.attachment[0],input.attachment,12);owner.curstate.body=input.body;
            if(input.command==1)effects.KillLocal(false);if(input.command==2)effects.KillLocal(true);
            if(input.command==3)effects.DeleteOwner(effectOwner);if(input.command==4)effects.Clear();
            if(input.command==5)std::strcpy(model.name,"models/other.mdl");
            hasView=input.command!=6;
            if(input.command==7)CHECK(effects.Add(effectOwner,"aurora/gunsmoke.aur",1,"models/test.mdl",0,5)==AuroraResult::Applied);
            CHECK(effects.Frame(input.dt,input.mode,{0,1,0},{0,0,1})==AuroraResult::Applied);
            const auto out=Snapshot(effects);if(std::fwrite(&out,sizeof(out),1,stdout)!=1)return 2;
        }
        return 0;
    }
    now=0;CHECK(effects.Frame(0,0,{0,1,0},{0,0,1})==AuroraResult::Applied);
    CHECK(effects.First()->main>=0 && effects.First()->particles[0].render.position[0]==0);
    view.attachment[0][0]=10;now=.04F;
    CHECK(effects.Frame(.04F,0,{0,1,0},{0,0,1})==AuroraResult::Applied);
    CHECK(effects.First()->head==1 && effects.First()->particles[1].age==0);
    CHECK(quads==0); // New spark starts size0 and receives no update in this traversal.
    now=.08F;CHECK(effects.Frame(.04F,0,{0,1,0},{0,0,1})==AuroraResult::Applied);CHECK(quads>0);
    CHECK(loads==1);owner.curstate.skin=666;lastMode=5;
    CHECK(effects.Frame(.1F,0,{0,1,0},{0,0,1})==AuroraResult::Unsupported);CHECK(lastMode==0);
    owner.curstate.skin=0;effects.KillLocal(true);owner.curstate.body=70;
    CHECK(effects.Frame(.1F,0,{0,1,0},{0,0,1})==AuroraResult::Applied);CHECK(effects.First());
    owner.curstate.body=0;CHECK(effects.Frame(.1F,0,{0,1,0},{0,0,1})==AuroraResult::Applied);CHECK(!effects.First());
    CHECK(effects.Add(5,"aurora/gunsmoke.aur",0,"models/test.mdl",0,5)==AuroraResult::Unsupported);
    CHECK(effects.Add(-1,"aurora/gunsmoke.aur",1,"models/test.mdl",0,5)==AuroraResult::InvalidInput);
    CHECK(effects.Add(0,"aurora/gunsmoke.aur",1,"models/test.mdl",0,5)==AuroraResult::Applied);
    view.attachment[0][0]=123;now=.12F;
    CHECK(effects.Frame(.04F,0,{0,1,0},{0,0,1})==AuroraResult::Applied);
    const auto* zeroSystem=effects.First();CHECK(zeroSystem && zeroSystem->owner==0 && zeroSystem->main>=0);
    CHECK(zeroSystem->particles[zeroSystem->main].owner==0);
    CHECK(zeroSystem->particles[zeroSystem->main].render.position[0]==0);
    CHECK(effects.DeleteOwner(0));CHECK(!effects.First());
    for(unsigned i=0;i<Aurora::MaxSystems;++i)CHECK(effects.Add(5,"aurora/gunsmoke.aur",1,"models/test.mdl",0,5)==AuroraResult::Applied);
    CHECK(effects.Add(5,"aurora/gunsmoke.aur",1,"models/test.mdl",0,5)==AuroraResult::CapacityExceeded);
    CHECK(effects.DeleteOwner(5));CHECK(effects.First());effects.VideoInit();CHECK(!effects.First());
    CHECK(effects.Frame(.1F,0,{0,1,0},{0,0,1})==AuroraResult::NotPrepared);
    auto unknown=script;unknown.replace(unknown.find("gravity"),7,"mystery");
    CHECK(effects.Prepare(unknown)==AuroraResult::UnknownField);CHECK(loads==1);
    std::puts("Aurora actual script, SDK resource/quad, pool order, no same-frame child update, body guard, hard kill, bounds, clear and unknown field checks passed");
}
