// SPDX-License-Identifier: GPL-3.0-or-later
#include <array>
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <xmmintrin.h>
#include "precompiled.h"
#include "../../native/csol/server/scenario_trigger.h"
#include "../../native/csol/server/scenario_trigger_manager.h"

extern "C" void zbstrigger(entvars_t*);
namespace
{
int checks{},failures{};
void Check(bool ok,const char* text){++checks;if(!ok){++failures;std::cerr<<"FAIL: "<<text<<'\n';}}
struct Bounds{Vector mins,maxs;};
struct Engine
{
    static Engine* active;
    globalvars_t globals{};
    std::array<char,65536> strings{};
    std::size_t nextString{1};
    std::vector<std::unique_ptr<edict_t>> edicts;
    std::map<const edict_t*,int> indices;
    std::map<std::string,Bounds> models;
    int allocations{},setModels{},expectedEffects{};
    int searches{},removes{};
    std::vector<int> removed;
    std::vector<std::string> calls;
    std::function<void(CBaseEntity&)> onRemove;
    std::string expectedModel;
    Engine()
    {
        active=this;gpGlobals=&globals;globals.pStringBase=strings.data();g_engfuncs={};
        g_engfuncs.pfnPvAllocEntPrivateData=Allocate;g_engfuncs.pfnIndexOfEdict=Index;g_engfuncs.pfnSetModel=SetModel;
    }
    ~Engine()
    {
        ClearEdicts();
        active=nullptr;gpGlobals=nullptr;g_engfuncs={};
    }
    void ClearEdicts()
    {
        for(auto& ed:edicts)if(auto* e=static_cast<CBaseEntity*>(ed->pvPrivateData))
        {e->OnDestroy();delete e->m_pEntity;std::free(e);}
        edicts.clear();indices.clear();
    }
    static void* Allocate(edict_t* ed,int32 size)
    {
        Check(ed->pvPrivateData==nullptr&&size>0,"real export requests a new private allocation");++active->allocations;
        return ed->pvPrivateData=std::calloc(1,static_cast<std::size_t>(size));
    }
    static int Index(const edict_t* ed){return active->indices.at(ed);}
    int String(const std::string& value)
    {
        const auto offset=nextString;nextString+=value.size()+1;
        if(nextString>strings.size())throw std::runtime_error("fixture string pool full");
        std::memcpy(strings.data()+offset,value.c_str(),value.size()+1);return static_cast<int>(offset);
    }
    CCSOLScenarioTrigger& Add(int index,const std::string& model)
    {
        auto ed=std::make_unique<edict_t>();ed->v.pContainingEntity=ed.get();ed->serialnumber=1;
        auto* raw=ed.get();indices[raw]=index;edicts.push_back(std::move(ed));
        raw->v.classname=String("zbstrigger");raw->v.model=String(model);zbstrigger(&raw->v);
        auto* entity=static_cast<CCSOLScenarioTrigger*>(raw->pvPrivateData);
        Check(entity&&entity->pev==&raw->v&&entity->m_pEntity&&entity->m_pEntity->BaseEntity()==entity,"real export creates correct entity layout and wrapper");
        const int before=allocations;zbstrigger(&raw->v);Check(allocations==before&&raw->pvPrivateData==entity,"repeated export reuses original private object");
        return *entity;
    }
    CBaseEntity& Fake(int index)
    {
        auto ed=std::make_unique<edict_t>();ed->v.pContainingEntity=ed.get();ed->serialnumber=1;
        auto* raw=ed.get();indices[raw]=index;edicts.push_back(std::move(ed));
        raw->v.classname=String("zbstrigger");
        return *GetClassPtr<CCSEntity>(reinterpret_cast<CBaseEntity*>(&raw->v));
    }
    static void SetModel(edict_t* ed,const char* model)
    {
        auto& engine=*active;++engine.setModels;
        Check(ed->v.solid==SOLID_TRIGGER&&ed->v.movetype==MOVETYPE_NONE,"solid and movement set before engine SetModel");
        Check(ed->v.effects==engine.expectedEffects,"NODRAW is added after engine SetModel, preexisting effects preserved at callback");
        Check(model&&std::string(model)==engine.expectedModel,"SetModel uses exact entity model string");
        const auto& bounds=engine.models.at(model);ed->v.mins=bounds.mins;ed->v.maxs=bounds.maxs;
        // Deliberately unrelated bounds: Collect must not use or derive from these.
        ed->v.absmin=Vector(-9000,-8000,-7000);ed->v.absmax=Vector(9000,8000,7000);
    }
    void Spawn(CCSOLScenarioTrigger& entity)
    {expectedEffects=entity.pev->effects;expectedModel=STRING(entity.pev->model);entity.Spawn();}
};
Engine* Engine::active{};
void Key(CCSOLScenarioTrigger& entity,const char* name,const char* value,bool handled=true)
{
    KeyValueData data{nullptr,const_cast<char*>(name),const_cast<char*>(value),777};entity.KeyValue(&data);
    Check(data.fHandled==(handled?TRUE:FALSE),"trigger KeyValue handled result exact");
}
void Unit()
{
    using namespace csol::scenario;
    Engine engine;engine.models["*42"]={Vector(3,-7,11),Vector(33,47,121)};
    auto& entity=engine.Add(71,"*42");Check(entity.m_iId==0,"default full id0");
    for(const auto& [text,value]:std::vector<std::pair<const char*,int>>{{"0",0},{"-1",-1},{"65535",65535},{"65536",65536},{"2147483647",2147483647},{"-2147483648",(-2147483647-1)},{"  -10abc",-10},{"5.5",5},{"+7",7},{"",0},{"none",0}})
    {Key(entity,"id",text);Check(entity.m_iId==value,"full int32 atoi compatible ID retained");}
    Key(entity,"id","196607");
    for(const char* key:{"ID","Id","team2","maxspawn","model","origin","angles","unknown"})Key(entity,key,"99",false);
    Check(entity.m_iId==196607,"unknown and differently cased keys do not alter id");
    entity.pev->origin=Vector(1000,-500,255);entity.pev->angles=Vector(90,45,-20);
    entity.pev->effects=17;entity.pev->flags=789;entity.pev->solid=99;entity.pev->movetype=83;
    const auto origin=entity.pev->origin,angles=entity.pev->angles;
    const auto touch=entity.m_pfnTouch;const auto use=entity.m_pfnUse;
    engine.Spawn(entity);
    Check(entity.pev->solid==SOLID_TRIGGER&&entity.pev->movetype==MOVETYPE_NONE&&entity.pev->effects==(17|EF_NODRAW),"Spawn assigns original brush mode and ORs NODRAW");
    Check(entity.pev->origin==origin&&entity.pev->angles==angles&&entity.pev->flags==789,"Spawn leaves origin angles and flags unchanged");
    Check(entity.pev->mins==Vector(3,-7,11)&&entity.pev->maxs==Vector(33,47,121)&&engine.setModels==1,"Spawn keeps exact engine model bounds without applying a second expansion");
    Check(entity.m_pfnTouch==touch&&entity.m_pfnUse==use,"Spawn installs no Touch or Use callback");
    entity.Activate();entity.Touch(&entity);entity.Use(&entity,&entity,USE_TOGGLE,1);
    Check(engine.setModels==1&&entity.pev->origin==origin,"inherited Activate Touch and Use have no dedicated trigger behavior");
    TriggerVolumes volumes;entity.Collect(volumes);const auto* first=volumes.Find(-1);
    Check(volumes.Volumes().size()==1&&first&&first->id==196607&&first->entityIndex==71,"Register narrows key but retains full id and engine identity");
    Check(first&&first->mins==std::array<float,3>{3,-7,11}&&first->maxs==std::array<float,3>{33,47,121},"Collect copies model mins/maxs with no origin translation rotation or abs bounds substitution");
    auto& last=engine.Add(14,"*42");last.m_iId=-1;last.pev->mins=Vector(-3,-4,-5);last.pev->maxs=Vector(6,7,8);last.Collect(volumes);
    const auto* final=volumes.Find(65535);
    Check(volumes.Volumes().size()==1&&final&&final->id==-1&&final->entityIndex==14&&final->mins==std::array<float,3>{-3,-4,-5},"same low16 key final item replaces whole box and identity");
    entity.pev->mins=Vector(99,99,99);last.pev->mins=Vector(88,88,88);
    Check(final&&final->mins==std::array<float,3>{-3,-4,-5},"registered volume is a value snapshot");
    for(int id:{0,1,32767,32768,65535,65536,65537,-32768,-65536,2147483647,(-2147483647-1)})
    {
        const auto bits=static_cast<unsigned>(id)&65535u;const int expected=bits>=32768u?static_cast<int>(bits)-65536:static_cast<int>(bits);
        Check(static_cast<int>(TriggerKey(id))==expected,"signed16 key matches independent masked integer arithmetic");
    }
    Check(volumes.Find(1)==nullptr,"missing volume query does not create a group");volumes.Clear();volumes.Clear();
    Check(volumes.Volumes().empty()&&!volumes.Find(-1),"Clear repeatably removes stored geometry");
}
void Manager()
{
    using namespace csol::scenario;
    Engine engine;CCSOLTriggerManager manager;
    engine.models["*1"]={Vector(-10,-20,-30),Vector(10,20,30)};
    auto& a=engine.Add(81,"*1");a.m_iId=5;engine.Spawn(a);
    auto& b=engine.Add(23,"*1");b.m_iId=65541;engine.Spawn(b);b.pev->mins=Vector(1,2,3);b.pev->maxs=Vector(10,20,30);
    auto& c=engine.Add(18,"*1");c.m_iId=7;engine.Spawn(c);
    auto& dead=engine.Add(19,"*1");dead.m_iId=8;engine.Spawn(dead);dead.pev->flags|=FL_KILLME;
    auto& free=engine.Add(20,"*1");free.m_iId=9;engine.Spawn(free);free.edict()->free=TRUE;
    engine.Fake(21);
    engine.onRemove=[&](CBaseEntity& e)
    {
        if(e.entindex()==81){b.pev->mins=Vector(999,999,999);++c.edict()->serialnumber;}
    };
    Check(manager.Status()==CSOLTriggerCollection::NotCollected&&manager.Volumes().Volumes().empty(),"new trigger manager has no geometry");
    Check(manager.CollectMap()==CSOLTriggerCollection::Ready,"manager collects real valid trigger entities");
    const auto* winner=manager.Volumes().Find(5);
    Check(manager.Volumes().Volumes().size()==2&&winner&&winner->id==65541&&winner->entityIndex==23,"duplicate narrowed id keeps final entire snapshot");
    Check(winner&&winner->mins==std::array<float,3>{1,2,3},"all trigger snapshots captured before removal callback mutation");
    Check(engine.removed==std::vector<int>{81,23},"free killmarked foreign and recycled handles are never removed");
    Check(manager.Volumes().Find(7)&&!manager.Volumes().Find(8)&&!manager.Volumes().Find(9),"later handle invalidation preserves snapshot, preexisting invalid markers excluded");
    const auto end=std::find(engine.calls.begin(),engine.calls.end(),"end");
    const auto removed=std::find(engine.calls.begin(),engine.calls.end(),"remove:81");
    Check(end<removed,"entire classname scan finishes before first trigger removal");
    const auto searches=engine.searches,removes=engine.removes;
    Check(manager.CollectMap()==CSOLTriggerCollection::Ready&&engine.searches==searches&&engine.removes==removes,"ordinary recollection keeps geometry without searching or deleting again");
    engine.ClearEdicts();manager.BeginMap();
    Check(manager.Status()==CSOLTriggerCollection::NotCollected&&manager.Volumes().Volumes().empty()&&engine.removes==removes,"BeginMap clears previous geometry without touching freed map edicts");
    Check(manager.CollectMap()==CSOLTriggerCollection::NeedsMapTriggerFile,"empty new map requests explicit MAP_TRG fallback");
    auto& late=engine.Add(100,"*1");engine.Spawn(late);const auto missingSearches=engine.searches;
    Check(manager.CollectMap()==CSOLTriggerCollection::NeedsMapTriggerFile&&engine.searches==missingSearches,"empty-map state remains stable until BeginMap");
    manager.BeginMap();Check(manager.CollectMap()==CSOLTriggerCollection::Ready&&manager.Volumes().Find(0),"BeginMap permits new map markers and clears previous empty status");
    manager.BeginMap();manager.BeginMap();Check(manager.Volumes().Volumes().empty(),"repeated BeginMap safe");
}
void OpenBounds()
{
    using namespace csol::scenario;
    TriggerVolume volume{{-10,-20,-30},{10,20,30},4,8};
    Check(volume.Contains({0,0,0}),"interior origin included");
    for(std::size_t axis=0;axis<3;++axis)
    {
        std::array<float,3> point{};point[axis]=volume.mins[axis];Check(!volume.Contains(point),"lower face excluded");
        point[axis]=volume.maxs[axis];Check(!volume.Contains(point),"upper face excluded");
        point[axis]=std::nextafter(volume.mins[axis],volume.maxs[axis]);Check(volume.Contains(point),"nearest representable value inside lower face included");
        point[axis]=std::nextafter(volume.maxs[axis],volume.mins[axis]);Check(volume.Contains(point),"nearest representable value inside upper face included");
        point[axis]=volume.mins[axis]-1;Check(!volume.Contains(point),"below lower face excluded regardless of player hull overlap");
        point[axis]=volume.maxs[axis]+1;Check(!volume.Contains(point),"above upper face excluded");
    }
    Check(!volume.Contains(volume.mins)&&!volume.Contains(volume.maxs),"corners excluded");
    volume.maxs[1]=volume.mins[1];Check(!volume.Contains({0,-20,0}),"zero-thickness box contains no point");
    volume.maxs[1]=volume.mins[1]-1;Check(!volume.Contains({0,-20.5f,0}),"inverted axis does not form a containing box");
}
class Reader
{
    std::ifstream file;
public:
    explicit Reader(const char* path):file(path,std::ios::binary){if(!file)throw std::runtime_error("cannot open trigger fixture");}
    std::uint32_t U32()
    {
        unsigned char bytes[4];file.read(reinterpret_cast<char*>(bytes),4);if(!file)throw std::runtime_error("truncated fixture");
        return std::uint32_t(bytes[0])|(std::uint32_t(bytes[1])<<8)|(std::uint32_t(bytes[2])<<16)|(std::uint32_t(bytes[3])<<24);
    }
    int I32(){return std::bit_cast<std::int32_t>(U32());}
    float Float(){return std::bit_cast<float>(U32());}
    std::array<float,3> Vector3(){std::array<float,3> value;for(auto& f:value)f=Float();return value;}
    std::string String(){std::string text(U32(),'\0');file.read(text.data(),text.size());if(!file)throw std::runtime_error("truncated fixture string");return text;}
    bool Done(){return file.peek()==std::char_traits<char>::eof();}
};
Vector Vec(const std::array<float,3>& v){return Vector(v[0],v[1],v[2]);}
void Actual(const char* path)
{
    using namespace csol::scenario;
    Reader input(path);Check(input.U32()==0x56475254u,"independentTRGV signature");const auto count=input.U32();
    Check(count==30,"actual30 trigger markers");Engine engine;CCSOLTriggerManager manager;
    std::map<int,TriggerVolume> expected;std::vector<int> expectedRemovals;
    for(std::uint32_t i=0;i<count;++i)
    {
        TriggerVolume volume;volume.entityIndex=input.U32();volume.id=input.I32();const int brush=input.I32();
        const auto rawMins=input.Vector3(),rawMaxs=input.Vector3();volume.mins=input.Vector3();volume.maxs=input.Vector3();
        const auto origin=input.Vector3(),angles=input.Vector3();const int key=input.I32();const auto keys=input.U32();
        const auto model="*"+std::to_string(brush);engine.models[model]={Vec(volume.mins),Vec(volume.maxs)};
        auto& entity=engine.Add(static_cast<int>(volume.entityIndex),model);
        entity.pev->origin=Vec(origin);entity.pev->angles=Vec(angles);
        for(std::uint32_t j=0;j<keys;++j)
        {
            const auto name=input.String(),value=input.String();Key(entity,name.c_str(),value.c_str(),name=="id");
            if(name=="model")Check(value==model,"actual model key and raw brush index agree");
        }
        engine.Spawn(entity);entity.Activate();
        Check(entity.m_iId==volume.id&&entity.pev->origin==Vec(origin)&&entity.pev->angles==Vec(angles),"actual entity keeps fullid and original transform after Spawn");
        Check(entity.pev->mins==Vec(volume.mins)&&entity.pev->maxs==Vec(volume.maxs),"actual model output consumed exactly without second expansion");
        bool expectedExpansion=true;
        for(std::size_t axis=0;axis<3;++axis)expectedExpansion&=(volume.mins[axis]==rawMins[axis]-1&&volume.maxs[axis]==rawMaxs[axis]+1);
        Check(expectedExpansion,"independent actual engine bounds fixture uses destination source expansion");
        expected[key]=volume;expectedRemovals.push_back(static_cast<int>(volume.entityIndex));
    }
    Check(input.Done(),"actual binary fully consumed");
    Check(manager.CollectMap()==CSOLTriggerCollection::Ready,"actual BSP trigger manager ready");
    Check(manager.Volumes().Volumes().size()==29&&expected.size()==29,"actual30 markers consolidate to29 independent keys");
    for(const auto& [key,volume]:expected)
    {
        const auto found=manager.Volumes().Volumes().find(static_cast<std::int16_t>(key));
        Check(found!=manager.Volumes().Volumes().end(),"actual independently signed key present");
        if(found==manager.Volumes().Volumes().end())continue;
        const auto& actual=found->second;
        Check(actual.id==volume.id&&actual.entityIndex==volume.entityIndex&&actual.mins==volume.mins&&actual.maxs==volume.maxs,"actual complete trigger snapshot and duplicate winner match independent model oracle");
        std::array<float,3> center;for(std::size_t axis=0;axis<3;++axis)center[axis]=(volume.mins[axis]+volume.maxs[axis])*0.5f;
        Check(actual.Contains(center),"actual box interior matches strict player-origin contract");
        for(std::size_t axis=0;axis<3;++axis)
        {
            auto point=center;point[axis]=volume.mins[axis];Check(!actual.Contains(point),"actual lower boundary excluded");
            point[axis]=volume.maxs[axis];Check(!actual.Contains(point),"actual upper boundary excluded");
        }
    }
    const auto* fifty=manager.Volumes().Find(50);
    Check(fifty&&fifty->entityIndex==681&&fifty->mins==std::array<float,3>{1679,-2481,-1}&&fifty->maxs==std::array<float,3>{2177,-1967,193},"actualid50 keeps laterBSP681 box, never merges older MAP_TRG box");
    Check(engine.setModels==30&&engine.removes==30&&engine.removed==expectedRemovals,"actual markers each modeled and removed exactly once in traversal order");
    const int searches=engine.searches;
    Check(manager.CollectMap()==CSOLTriggerCollection::Ready&&engine.searches==searches&&engine.removes==30,"actual repeated collection retains geometry");
    std::cout<<"actual zs_deadend triggers:30 markers,29 keys,id50 winnerBSP681; SetModel is controlled bounds callback, BSP indices are mock identities\n";
}
void BoundsOracle(const char* path)
{
    Reader input(path);Check(input.U32()==0x4f454754u,"originalTGEO oracle signature");const auto count=input.U32();
    Check(count==482,"original482 finite raw and exceptional comparison cases");
    const auto previous=_mm_getcsr();_mm_setcsr(0x1f80);
    for(std::uint32_t i=0;i<count;++i)
    {
        csol::scenario::TriggerVolume volume;volume.mins=input.Vector3();volume.maxs=input.Vector3();const auto origin=input.Vector3();
        const auto expected=input.U32();const bool actual=volume.Contains(origin);
        if(actual!=(expected!=0))std::cerr<<"bounds oracle mismatch index="<<i<<'\n';
        Check(actual==(expected!=0),"production Contains matches original sixCOMISS/JBE decisions");
    }
    Check(input.Done(),"original oracle corpus fully consumed");_mm_setcsr(previous);
    std::cout<<"Contains oracle comparison: "<<count<<" cases; MXCSR1f80 only, no grid/player eligibility/event execution\n";
}
}
// Lookup and removal services are controlled substitutes. Entity and manager
// methods, real EntityHandle and RTTI are compiled without extraction/rewrite.
CBaseEntity* UTIL_FindEntityByClassname(CBaseEntity* previous,const char* classname)
{
    auto& engine=*Engine::active;++engine.searches;bool eligible=previous==nullptr;
    for(auto& ed:engine.edicts)
    {
        if(!eligible){if(ed.get()==previous->edict())eligible=true;continue;}
        if(ed->pvPrivateData&&FClassnameIs(ed.get(),classname))return static_cast<CBaseEntity*>(ed->pvPrivateData);
    }
    engine.calls.emplace_back("end");return nullptr;
}
BOOL UTIL_IsValidEntity(edict_t* ed){return ed&&!ed->free&&!(ed->v.flags&FL_KILLME);}
void UTIL_Remove(CBaseEntity* entity)
{
    if(!entity||entity->IsPlayer()||(entity->pev->flags&FL_KILLME))return;
    auto& engine=*Engine::active;++engine.removes;engine.removed.push_back(entity->entindex());
    engine.calls.emplace_back("remove:"+std::to_string(entity->entindex()));
    if(engine.onRemove)engine.onRemove(*entity);
    entity->pev->solid=SOLID_NOT;entity->pev->flags|=FL_KILLME;entity->pev->targetname=0;
}
int main(int argc,char** argv)
{
    try
    {
        if(argc==1){Unit();Manager();OpenBounds();}
        else if(argc==3&&std::string_view(argv[1])=="--actual")Actual(argv[2]);
        else if(argc==3&&std::string_view(argv[1])=="--bounds-oracle")BoundsOracle(argv[2]);
        else throw std::runtime_error("usage: trigger tests [--actual independent-volumes.bin | --bounds-oracle original-corpus.bin]");
    }
    catch(const std::exception& error){++failures;std::cerr<<"EXCEPTION: "<<error.what()<<'\n';}
    std::cout<<"trigger checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
