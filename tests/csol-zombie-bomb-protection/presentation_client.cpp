// SPDX-License-Identifier: GPL-3.0-or-later
#define main PreviousMonsterClientMain
#define ConsumeOberonHint UnusedOberonHintBoundary
#include "../csol-scenario-monster-skills/client.cpp"
#undef ConsumeOberonHint
#undef main
#include "../../native/csol/scenario_part_effects.hpp"
#include "../../native/csol/scenario/core/scenario_healing.hpp"
#include <bit>
#include <functional>
#include <limits>
namespace csol::client {bool ConsumeOberonHint(int,const void*){return false;}}
namespace attachment
{
using namespace fixture;
std::array<TEMPENTITY,600> pool{};
std::array<cl_entity_t,33> owners{};
model_t model{};
int allocated{},reuse=-1,missingOwner{};
bool noModel{};
std::function<void()> onLoad,onAlloc,onOwner,onTime;
std::vector<unsigned char> Packet(int owner,int subtype,float z,float duration)
{
    std::vector<unsigned char> bytes{199,static_cast<unsigned char>(owner),static_cast<unsigned char>(owner>>8),static_cast<unsigned char>(subtype)};
    for(float value:{z,duration}){const auto raw=std::bit_cast<std::uint32_t>(value);for(int i=0;i<4;++i)bytes.push_back(static_cast<unsigned char>(raw>>(i*8)));}
    return bytes;
}
void SendEffect(int subtype,float duration=4,int owner=1,float z=36.125f)
{Check(Send("MPToCL",Packet(owner,subtype,z,duration))==1);}
void Reset()
{
    csol::client::ResetScenarioEffects();allocated=0;reuse=-1;missingOwner=0;noModel=false;pool={};
    onLoad={};onAlloc={};onOwner={};onTime={};calls.clear();model={};model.type=mod_sprite;model.numframes=7;now=10;
    for(int i=1;i<=32;++i){owners[i]={};owners[i].model=&model;owners[i].origin[0]=float(i);owners[i].origin[1]=2;owners[i].origin[2]=3;}
}
void Install()
{
    fixture::Install();Reset();
    csol::client::engine.GetClientTime=[](){auto cb=onTime;if(cb)cb();return now;};
    csol::client::engine.GetEntityByIndex=[](int index)->cl_entity_t*
    {auto cb=onOwner;if(cb)cb();return index==missingOwner||index<1||index>32?nullptr:&owners[index];};
    csol::client::engine.CL_LoadModel=[](const char* path,int* index)->model_t*
    {calls.emplace_back(path);*index=88;auto cb=onLoad;if(cb)cb();return noModel?nullptr:&model;};
    fx.CL_TempEntAllocHigh=[](float* origin,model_t* value)->TEMPENTITY*
    {auto* temp=&pool[reuse>=0?reuse:allocated++];*temp={};temp->entity.model=value;std::copy_n(origin,3,temp->entity.origin);auto cb=onAlloc;if(cb)cb();return temp;};
}
void Metadata()
{
    for(int subtype:{43,46,47,53})for(float duration:{-.5f,0.f,.5f,1.f,4.125f})
    {
        Reset();SendEffect(subtype,duration,32);Check(allocated==1);
        const auto& t=pool[0];Check(calls==std::vector<std::string>{csol::part_effects::Sprites[csol::part_effects::Index(subtype)]});
        // Original CSO bit0x100000 is not this engine's CLIENTCUSTOM bit.
        // Follow replaces its attachment manager; compare the target ABI flags.
        Check(t.flags==(FTENT_PERSIST|FTENT_CLIENTCUSTOM|FTENT_SPRANIMATE|FTENT_SPRANIMATELOOP|FTENT_FADEOUT)&&
            t.clientIndex==32&&t.callback&&t.entity.curstate.iuser4>0);
        Check(t.die==10+(subtype==43?0:std::max(0.f,duration-1))&&t.fadeSpeed==1&&t.frameMax==7);
        Check(t.entity.curstate.frame==0&&t.entity.curstate.framerate==10&&t.entity.curstate.scale==1&&
            t.entity.curstate.rendermode==kRenderTransAdd&&t.entity.curstate.renderfx==14&&
            t.entity.curstate.renderamt==254&&t.entity.baseline.renderamt==254);
        Check(t.entity.origin[0]==32&&t.entity.origin[1]==2&&t.entity.origin[2]==39.125f);
        owners[32].origin[0]=90;owners[32].origin[2]=100;pool[0].callback(&pool[0],0,now);
        Check(pool[0].entity.origin[0]==90&&pool[0].entity.origin[2]==136.125f);
        missingOwner=32;pool[0].callback(&pool[0],0,17);Check(pool[0].die==17&&(pool[0].flags&FTENT_NOMODEL));
    }
}
void Ownership()
{
    Reset();SendEffect(53);SendEffect(53);Check(allocated==2&&pool[0].die==13&&pool[1].die==13);
    SendEffect(46);Check(pool[0].die==13&&pool[1].die==13);SendEffect(53);
    Check(pool[2].die==-1&&(pool[2].flags&FTENT_NOMODEL)&&pool[0].die==13&&pool[1].die==13);
    Reset();SendEffect(47,4,2);SendEffect(53,4,1);Check(pool[0].die==13); // independent owners
    Reset();SendEffect(46);++pool[0].entity.curstate.iuser4;SendEffect(53);Check(pool[0].die==13); // recycled temp token
    Reset();SendEffect(47);pool[0].callback=nullptr;SendEffect(53);Check(pool[0].die==13); // foreign owner
    Reset();SendEffect(53);csol::client::ResetScenarioEffects();Check(pool[0].die==-1&&(pool[0].flags&FTENT_NOMODEL));
    Reset();SendEffect(53);++pool[0].entity.curstate.iuser4;csol::client::ResetScenarioEffects();Check(pool[0].die==13);
    Reset();for(int i=0;i<512;++i)SendEffect(53);Check(allocated==512);SendEffect(53);Check(allocated==512);
    now=14;SendEffect(53);Check(allocated==512);now=14.01f;SendEffect(53);Check(allocated==513);
}
void Invalid()
{
    Reset();for(int owner:{0,-1,32768})SendEffect(53,4,owner);Check(allocated==0);
    for(float value:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
    {SendEffect(53,value);SendEffect(53,4,1,value);}Check(allocated==0);
    auto packet=Packet(1,53,36,4);for(int size:{0,1,3,4,11,13}){packet.resize(size);Send("MPToCL",packet);}Check(allocated==0);
    Reset();noModel=true;SendEffect(53);Check(allocated==0);noModel=false;model.type=mod_studio;SendEffect(53);Check(allocated==0);
    model.type=mod_sprite;model.numframes=0;SendEffect(53);Check(allocated==0);
    Reset();owners[1].model=nullptr;SendEffect(53);Check(allocated==0);
}
void Cancellation()
{
    for(bool heal:{false,true})for(int stage=0;stage<4;++stage)
    {
        Reset();const auto cancel=[](){csol::client::ResetScenarioEffects();};
        if(stage==0)onOwner=cancel;if(stage==1)onLoad=cancel;if(stage==2)onTime=cancel;if(stage==3)onAlloc=cancel;
        if(heal)Check(Send("MPToCL",{4,1,0,9})==1);else SendEffect(53);
        Check(allocated==0||(pool[0].die<=now&&(pool[0].flags&FTENT_NOMODEL)));
    }
    for(bool foreignCallback:{false,true})
    {
        Reset();onAlloc=[&]{csol::client::ResetScenarioEffects();pool[0].die=99;
            if(foreignCallback)pool[0].callback=[](TEMPENTITY*,float,float){};
            else pool[0].entity.curstate.iuser4=12345;};
        SendEffect(53);Check(pool[0].die==99&&!(pool[0].flags&FTENT_NOMODEL));
    }
    Reset();onAlloc=[](){onAlloc={};csol::client::ResetScenarioEffects();reuse=0;SendEffect(53,8);};
    SendEffect(53);Check(pool[0].die==17&&!(pool[0].flags&FTENT_NOMODEL));
    csol::client::ResetScenarioEffects();Check(pool[0].die==-1&&(pool[0].flags&FTENT_NOMODEL));
    Reset();SendEffect(53);const auto callback=pool[0].callback;
    onOwner=[](){onOwner={};csol::client::ResetScenarioEffects();pool[0].entity.curstate.iuser4=123;
        pool[0].callback=[](TEMPENTITY*,float,float){};pool[0].die=99;pool[0].entity.origin[0]=1234;};
    callback(&pool[0],0,now);Check(pool[0].entity.origin[0]==1234&&pool[0].die==99);
}
void Heal()
{
    for(int subtype:{9,13,40,44})
    {
        Reset();Check(Send("MPToCL",{4,1,0,static_cast<unsigned char>(subtype)})==1);Check(allocated==1);
        const auto& t=pool[0];Check(t.die==11&&t.tentOffset[2]==(subtype==40||subtype==44?60:subtype==13?25:40));
        Check(t.entity.curstate.scale==(subtype==40?.5f:1.f)&&t.entity.curstate.renderamt==254&&t.callback);
        const char* path=subtype==40?"sprites/zb_skill_hpbuff.spr":subtype==44?"sprites/zombiarmorheal_head.spr":csol::healing::Sprites[subtype==13?1:0];
        Check(calls==std::vector<std::string>{path});
        csol::client::ResetScenarioEffects();Check(pool[0].die==-1&&(pool[0].flags&FTENT_NOMODEL));
    }
}
void Reentry()
{
    for(bool heal:{false,true})for(int variant=0;variant<3;++variant)
    {
        Reset();const auto receive=[&]{if(heal)Check(Send("MPToCL",{4,1,0,9})==1);else SendEffect(53);};
        if(variant==2)for(int i=0;i<511;++i)receive();
        onAlloc=[&](){onAlloc={};if(variant==1)reuse=allocated-1;receive();};receive();
        Check(allocated<=(variant==2?513:2));csol::client::ResetScenarioEffects();
        for(int i=0;i<allocated;++i)Check(pool[i].die<=now&&(pool[i].flags&FTENT_NOMODEL));
    }
}
}
int main(int argc,char** argv)
{
    try{attachment::Install();if(argc>1&&std::string_view(argv[1])=="--cancel")attachment::Cancellation();
        else if(argc>1&&std::string_view(argv[1])=="--reentry")attachment::Reentry();
        else{attachment::Metadata();attachment::Ownership();attachment::Invalid();attachment::Heal();}
        std::cout<<"bomb attachment checks="<<fixture::checks<<" failures=0\n";return 0;}
    catch(const std::exception& e){std::cerr<<e.what()<<" checks="<<fixture::checks<<'\n';return 1;}
}
