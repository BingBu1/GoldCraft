// SPDX-License-Identifier: GPL-3.0-or-later
// SDK entities/pool and model application are real; engine model services are
// controlled. No Origin, behavior, animation activation or live model loading.
#include "../csol-scenario-monster-pool/pool_fixture.hpp"
#include "../../native/csol/server/scenario_monster_models.h"
#include <bit>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include "expected_catalog.hpp"

using namespace csol::scenario;
namespace
{
int checks{},failures{};
void Check(bool value,const char* text)
{++checks;if(!value){++failures;std::cerr<<"FAIL: "<<text<<'\n';}}
bool Equal(const Vector& value,const std::array<float,3>& expected)
{return value.x==expected[0]&&value.y==expected[1]&&value.z==expected[2];}
bool Equal(const MonsterAttributes& a,const MonsterAttributes& b)
{return a.health==b.health&&a.armor==b.armor&&a.attackDamage==b.attackDamage&&a.attackRange==b.attackRange&&a.attackInterval==b.attackInterval&&a.speed==b.speed&&a.bonusMoney==b.bonusMoney;}
MonsterAttributes Sentinel(){return {901,902,903,904,905,906,907};}
MonsterType Type(int id,const char* name)
{MonsterType t;t.id=id;t.appearance=name;t.health=1234;t.armor=55;t.speed=87;t.attackDamage=5;t.attackRange=60;t.attackInterval=.5f;t.bonusMoney=6;return t;}
MonsterTypeTable Table(std::initializer_list<MonsterType> rows)
{MonsterTypeTable t;t.rows=rows;for(std::size_t i=0;i<t.rows.size();++i)t.byId[t.rows[i].Key()]=i;return t;}

struct ModelServices
{
    inline static ModelServices* current{};
    std::vector<std::string> cached;
    std::vector<char> order;
    std::function<void(edict_t*)> onModel,onSize;
    std::function<void()> onPrecache;
    int failPrecacheAt{},modelCalls{},sizeCalls{};
    bool overwriteBounds{true};
    float healthAtModel{},maxHealthAtModel{},armorAtModel{},speedAtModel{};
    Vector boundsAtSize;
    ModelServices()
    {current=this;g_engfuncs.pfnPrecacheModel=Precache;g_engfuncs.pfnSetModel=Model;g_engfuncs.pfnSetSize=Size;}
    ~ModelServices(){current=nullptr;}
    static int Precache(const char* name)
    {auto& s=*current;s.cached.emplace_back(name);if(s.onPrecache)s.onPrecache();return s.failPrecacheAt==static_cast<int>(s.cached.size())?0:100+static_cast<int>(s.cached.size());}
    static void Model(edict_t* ed,const char* name)
    {
        auto& s=*current;++s.modelCalls;s.order.push_back('M');
        s.healthAtModel=ed->v.health;s.maxHealthAtModel=ed->v.max_health;s.armorAtModel=ed->v.armorvalue;s.speedAtModel=ed->v.maxspeed;
        ed->v.model=Engine::AllocString(name);ed->v.modelindex=321;
        if(s.overwriteBounds){ed->v.mins=Vector(-999,-998,-997);ed->v.maxs=Vector(997,998,999);}
        if(s.onModel)s.onModel(ed);
    }
    static void Size(edict_t* ed,const float* mins,const float* maxs)
    {
        auto& s=*current;++s.sizeCalls;s.order.push_back('S');s.boundsAtSize=ed->v.mins;
        ed->v.mins=Vector(mins);ed->v.maxs=Vector(maxs);ed->v.size=ed->v.maxs-ed->v.mins;
        if(s.onSize)s.onSize(ed);
    }
};

void Catalog()
{
    const auto actual=MonsterAppearances();Check(actual.size()==ExpectedCatalog.size(),"complete original appearance count");
    for(const auto& expected:ExpectedCatalog)
    {
        const auto& item=actual[expected.index];
        Check(item.index==expected.index,"original appearance order/index");
        Check(item.name==expected.name,"original appearance name bytes");
        Check(item.model==expected.model,"original model path bytes");
        for(std::size_t j=0;j<3;++j)
        {Check(std::bit_cast<std::uint32_t>(item.mins[j])==expected.bounds[j],"original mins float bits");Check(std::bit_cast<std::uint32_t>(item.maxs[j])==expected.bounds[j+3],"original maxs float bits");}
        Check(item.healthSummary==expected.summary,"original summary flag");
        const auto* first=FindMonsterAppearance(item.name);
        Check(first&&first->name==item.name&&first->index<=item.index,"lookup retains first matching address");
    }
    for(const auto& d:ExpectedDuplicates)
    {Check(actual[d.first].name==d.name&&actual[d.later].name==d.name,"duplicate table entries both retained");Check(FindMonsterAppearance(d.name)==&actual[d.first],"duplicate first entry wins");}
    for(const auto name:{"","1","18","zbs_bossl_big02","ZOMBI_HOST"," zombi_host","zombi_host ","models/player/zombi_host/zombi_host.mdl","missing_model"})
        Check(!FindMonsterAppearance(name),"unknown case numeric or path names have no fallback");
    Check(!FindMonsterAppearance(std::string_view("zombi_host\0junk",15)),"embedded NUL is not silently truncated by view lookup");
}

void CacheLifecycle()
{
    auto table=Table({Type(1,"zbs_bossS_jun00"),Type(2,"zbs_bossS_jun02"),Type(3,"stamper_zombi_host")});
    PoolFixture f;ModelServices engine;CCSOLMonsterModels models;ConfigError error;
    Check(!models.Ready()&&models.ModelIndex(MonsterAppearances()[10])==0,"new map model cache not ready");
    Check(models.Precache(table,error)&&models.Ready()&&error.message.empty(),"valid complete type cache loads");
    Check(engine.cached.size()==2,"same model path is precached once for distinct appearances");
    Check(models.ModelIndex(MonsterAppearances()[10])==models.ModelIndex(MonsterAppearances()[19]),"alias appearances share actual model index");
    Check(models.ModelIndex(MonsterAppearances()[21])>0&&models.ModelIndex(MonsterAppearances()[99])==0,"duplicate appearance name only first index selected");
    auto fake=MonsterAppearances()[10];Check(models.ModelIndex(fake)==0,"foreign copied appearance cannot alias catalog index");
    fake.index=999;Check(models.ModelIndex(fake)==0,"foreign out of range index rejected");
    Check(models.Reset()&&!models.Ready()&&models.ModelIndex(MonsterAppearances()[10])==0,"map reset clears ready and cached indices");
    engine.cached.clear();engine.failPrecacheAt=2;
    Check(!models.Precache(table,error)&&!models.Ready()&&!error.message.empty(),"mid precache engine failure leaves no committed map cache");
    Check(engine.cached.size()==2&&models.ModelIndex(MonsterAppearances()[10])==0,"irreversible engine precache calls remain but lookup cache stays empty");
    engine.failPrecacheAt=0;engine.cached.clear();
    Check(models.Precache(table,error),"cache recovers after failed map load");
    const auto before=engine.cached.size();auto unknown=Table({Type(1,"zombi_host"),Type(2,"unknown")});
    Check(!models.Precache(unknown,error)&&!models.Ready()&&engine.cached.size()==before,"unknown type validates all entries before engine and clears previous cache");
    auto malformed=Table({Type(1,"zombi_host")});malformed.byId[1]=99;
    Check(!models.Precache(malformed,error)&&engine.cached.size()==before,"out of range type row rejected before calls");
    malformed=Table({Type(1,"zombi_host")});malformed.byId.clear();malformed.byId[2]=0;
    Check(!models.Precache(malformed,error)&&engine.cached.size()==before,"wrong effective map key rejected");
    auto shadow=Table({Type(1,"unknown"),Type(1,"zombi_host")});
    Check(models.Precache(shadow,error)&&models.Ready(),"only effective last key participates in model precache");
    engine.cached.clear();bool resetAllowed=true,nestedAllowed=true;
    engine.onPrecache=[&]{resetAllowed=models.Reset();ConfigError nested;nestedAllowed=models.Precache(table,nested);};
    Check(models.Precache(table,error)&&!resetAllowed&&!nestedAllowed&&engine.cached.size()==2,"reentrant reset/precache blocked while outer candidate commits");
    engine.onPrecache={};
    Check(models.Precache(MonsterTypeTable{},error)&&models.Ready()&&models.ModelIndex(MonsterAppearances()[10])==0,"empty effective table is ready but retains no prior model");
}

void ApplyOrder()
{
    auto table=Table({Type(1,"deimos_zombi_host")});
    PoolFixture f;ModelServices engine;CCSOLMonsterModels models;ConfigError error;
    Check(models.Precache(table,error),"apply model cached");
    auto* m=f.pool.Acquire(table.rows[0],7,f.points);Check(m!=nullptr,"apply uses actual borrowed slot");if(!m)return;
    m->pev->origin=Vector(31,32,33);m->pev->angles=Vector(41,42,43);m->pev->sequence=88;m->pev->frame=17;
    m->pev->rendercolor=Vector(1,2,3);m->pev->speed=123;m->pev->nextthink=999;
    const int precaches=static_cast<int>(engine.cached.size());MonsterAttributes output=Sentinel();
    RoundMonsterDeltas delta;delta.health=23;delta.armor=7;delta.speed=7;
    bool freeAllowed=true,acquireAllowed=true,resetAllowed=true,reapplyAllowed=true,shutdownAllowed=true;
    engine.onModel=[&](edict_t*){
        Check(f.pool.Busy(),"pool is busy during model callback");freeAllowed=f.pool.Free(*m);acquireAllowed=f.pool.Acquire(table.rows[0],7,f.points)!=nullptr;
        resetAllowed=models.Reset();shutdownAllowed=f.pool.Shutdown();ConfigError nested;MonsterAttributes sink;
        reapplyAllowed=models.Apply(*m,{},sink,nested);
    };
    engine.onSize=[&](edict_t*){Check(f.pool.Busy()&&!f.pool.Free(*m),"size callback also blocks pool Free");};
    Check(models.Apply(*m,delta,output,error),"original model application succeeds");
    Check(engine.order==std::vector<char>{'M','S'},"model precedes bounds with no extra engine stage");
    Check(engine.healthAtModel==1257&&engine.maxHealthAtModel==1257&&engine.armorAtModel==62&&engine.speedAtModel==94,"scalar entity writes precede SetModel");
    Check(Equal(engine.boundsAtSize,{-999,-998,-997}),"SetModel actually overwrote old bounds in fixture");
    const auto* appearance=FindMonsterAppearance(table.rows[0].appearance);
    Check(Equal(m->pev->mins,appearance->mins)&&Equal(m->pev->maxs,appearance->maxs),"catalog hull restores after engine model bounds");
    Check(Equal(output,{1257,62,5,60,.5f,94,6}),"output attributes commit only after both engine stages");
    Check(!freeAllowed&&!acquireAllowed&&!resetAllowed&&!reapplyAllowed&&!shutdownAllowed,"model callback cannot mutate owners or reapply");
    Check(!f.pool.Busy()&&models.Ready(),"operation busy flags release after success");
    Check(Equal(m->pev->origin,{31,32,33})&&Equal(m->pev->angles,{41,42,43})&&f.engine.setOrigins==0,"position and angles outside this stage");
    Check(m->pev->sequence==88&&m->pev->frame==17&&Equal(m->pev->rendercolor,{1,2,3}),"animation and render color outside this stage");
    Check(m->pev->speed==123&&m->pev->maxspeed==94,"resolved movement attribute writes maxspeed not speed");
    Check(m->pev->takedamage==DAMAGE_NO&&!m->Behavior()&&m->pev->nextthink==999,"application does not enable damage behavior or schedule AI");
    Check(static_cast<int>(engine.cached.size())==precaches,"Apply never precaches at spawn time");
    engine.onModel={};engine.onSize={};Check(f.pool.Free(*m),"slot remains normally returnable after apply");
    output=Sentinel();Check(!models.Apply(*m,delta,output,error)&&Equal(output,Sentinel()),"parked slot refuses model application without output mutation");
}

void Rejections()
{
    BehaviorStats stats;auto table=Table({Type(1,"zombi_host")});
    PoolFixture f;ModelServices engine;CCSOLMonsterModels models;ConfigError error;
    auto* m=f.pool.Acquire(table.rows[0],7,f.points);Check(m!=nullptr,"rejection slot borrowed");if(!m)return;
    MonsterAttributes output=Sentinel();Check(!models.Apply(*m,{},output,error)&&Equal(output,Sentinel()),"unready models reject before writes");
    Check(models.Precache(table,error),"rejection cache prepared");
    m->pev->takedamage=DAMAGE_AIM;Check(!models.Apply(*m,{},output,error),"active damage slot rejected");m->pev->takedamage=DAMAGE_NO;
    m->pev->deadflag=DEAD_DEAD;Check(!models.Apply(*m,{},output,error),"dead slot rejected");m->pev->deadflag=DEAD_NO;
    table.rows[0].appearance="unknown";Check(!models.Apply(*m,{},output,error),"changed unknown appearance rejected");
    table.rows[0].appearance="zombi_origin";Check(!models.Apply(*m,{},output,error),"known but unprecached appearance rejected");table.rows[0].appearance="zombi_host";
    table.rows[0].attackInterval=std::numeric_limits<float>::infinity();Check(!models.Apply(*m,{},output,error),"nonfinite attributes rejected before engine");table.rows[0].attackInterval=.5f;
    Check(engine.order.empty()&&Equal(output,Sentinel()),"precondition failures do not partially write or call engine");
    Check(m->InstallBehavior(std::make_unique<BehaviorProbe>(stats)),"real behavior-ready stage prepared for rejection");
    Check(!models.Apply(*m,{},output,error)&&engine.order.empty(),"installed behavior cannot be overwritten by attribute model stage");
}

enum class Fault{Destroy,Replace,Kill,Recycle,PrivateMissing};
void CallbackFailures()
{
    for(const bool atSize:{false,true})for(const auto fault:{Fault::Destroy,Fault::Replace,Fault::Kill,Fault::Recycle,Fault::PrivateMissing})
    {
        auto table=Table({Type(1,"deimos_zombi_host")});
        PoolFixture f;ModelServices engine;CCSOLMonsterModels models;ConfigError error;
        Check(models.Precache(table,error),"fault cache prepared");
        auto* m=f.pool.Acquire(table.rows[0],7,f.points);Check(m!=nullptr,"fault actual slot acquired");if(!m)continue;
        auto* ed=m->edict();MonsterAttributes output=Sentinel();
        const auto callback=[&](edict_t* target){
            Check(f.pool.Busy(),"pool stays guarded during destructive callback");
            switch(fault){
            case Fault::Destroy:f.engine.DestroyPrivate(target);break;
            case Fault::Replace:f.engine.ReplaceWithBase(target);break;
            case Fault::Kill:target->v.flags|=FL_KILLME;break;
            case Fault::Recycle:++target->serialnumber;break;
            case Fault::PrivateMissing:f.engine.orphanedPrivate.push_back(target->pvPrivateData);target->pvPrivateData=nullptr;break;
            }
        };
        if(atSize)engine.onSize=callback;else engine.onModel=callback;
        Check(!models.Apply(*m,{},output,error)&&!error.message.empty(),"callback invalidation is rejected after engine stage");
        Check(Equal(output,Sentinel()),"failed engine stage leaves output uncommitted");
        Check(!f.pool.Busy()&&models.Ready(),"failure releases operation guards");
        Check(engine.modelCalls==1&&engine.sizeCalls==(atSize?1:0),"model failure prevents size and size failure has no later stage");
        Check(ed->v.health==1234&&ed->v.max_health==1234&&ed->v.armorvalue==55,"prior entity scalar writes remain on engine failure");
        Check(atSize?Equal(ed->v.mins,{-18,-18,-36}):Equal(ed->v.mins,{-999,-998,-997}),"partial engine writes are explicitly not rolled back");
        engine.onModel={};engine.onSize={};
    }
}

void Actual(const char* path)
{
    std::ifstream input(path,std::ios::binary);Check(input.good(),"actual decoded monster type file opens");
    if(!input)return;std::string bytes((std::istreambuf_iterator<char>(input)),{});
    MonsterTypeTable table;ConfigError error;Check(ReadMonsterTypes(bytes,table,error),"actual type table through production reader");
    Check(table.rows.size()==38&&table.byId.size()==38,"actual table has 38 effective types");
    PoolFixture f;ModelServices engine;CCSOLMonsterModels models;Check(models.Precache(table,error),"actual 38-type model cache prepares");
    std::set<std::string> expectedModels;for(const auto& item:ExpectedDeadend)expectedModels.insert(ExpectedCatalog[item.appearance].model);
    Check(expectedModels.size()==13&&engine.cached.size()==13&&std::set<std::string>(engine.cached.begin(),engine.cached.end())==expectedModels,"actual 38 types resolve exactly 13 original models");
    for(const auto& item:ExpectedDeadend)
    {
        const auto* type=table.Find(item.type);Check(type!=nullptr,"every original type ID exists");if(!type)continue;
        const auto* appearance=FindMonsterAppearance(type->appearance);Check(appearance==&MonsterAppearances()[item.appearance],"actual type appearance matches independent original mapping");
        auto* m=f.pool.Acquire(*type,7,f.points);Check(m!=nullptr,"actual type gets real pool entity");if(!m)continue;
        MonsterAttributes output;Check(models.Apply(*m,{},output,error),"actual type applies model and original hull");
        Check(std::string(STRING(m->pev->model))==ExpectedCatalog[item.appearance].model,"actual entity model path matches original");
        Check(Equal(m->pev->mins,MonsterAppearances()[item.appearance].mins)&&Equal(m->pev->maxs,MonsterAppearances()[item.appearance].maxs),"actual entity final bounds match catalog");
        Check(f.pool.Free(*m),"actual mapped entity returns before next type");
    }
    Check(engine.cached.size()==13,"38 applications add no runtime model precaches");
}
}
int main(int argc,char** argv)
{
    try{if(argc==3&&std::string_view(argv[1])=="--actual")Actual(argv[2]);else{Catalog();CacheLifecycle();ApplyOrder();Rejections();CallbackFailures();}}
    catch(const std::exception& e){++failures;std::cerr<<e.what()<<'\n';}
    std::cout<<"scenario_monster_models checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
